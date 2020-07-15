//
// Created by emb on 11/30/19.
//


//-----------------------
//-- debugging
#include <iostream>
#include <iomanip>
#include <chrono>
//--------------

#include <sndfile.hh>
#include <array>
#include <cmath>
#include <utility>

#include "BufDiskWorker.h"

using namespace crone;

std::unique_ptr<std::thread> BufDiskWorker::worker = nullptr;
std::queue<BufDiskWorker::Job> BufDiskWorker::jobQ;
std::mutex BufDiskWorker::qMut;

std::array<BufDiskWorker::BufDesc, BufDiskWorker::maxBufs> BufDiskWorker::bufs;
int BufDiskWorker::numBufs = 0;
bool BufDiskWorker::shouldQuit = false;
int BufDiskWorker::sampleRate = 48000;

// clamp unsigned int to upper bound, inclusive
static inline void clamp(size_t &x, const size_t a) {
    if (x > a) { x = a; }
}

int BufDiskWorker::registerBuffer(float *data, size_t frames) {
    int n = numBufs++;
    bufs[n].data = data;
    bufs[n].frames = frames;
    return n;
}

void BufDiskWorker::requestJob(BufDiskWorker::Job &job) {
    qMut.lock();
    jobQ.push(job);
    qMut.unlock();
    // FIXME: use condvar to signal worker
}

void BufDiskWorker::requestClear(size_t idx, float start, float dur) {
    BufDiskWorker::Job job{BufDiskWorker::JobType::Clear, {idx, 0}, "", 0, start, dur, 0};
    requestJob(job);
}

void BufDiskWorker::requestCopy(size_t srcIdx, size_t dstIdx,
                                float srcStart, float dstStart, float dur,
                                float fadeTime, float preserve, bool reverse) {
    BufDiskWorker::Job job{BufDiskWorker::JobType::Copy, {srcIdx, dstIdx}, "", srcStart, dstStart, dur, 0, fadeTime, preserve, reverse};
    requestJob(job);
}

void
BufDiskWorker::requestReadMono(size_t idx, std::string path, float startSrc, float startDst, float dur, int chanSrc) {
    BufDiskWorker::Job job{BufDiskWorker::JobType::ReadMono, {idx, 0}, std::move(path), startSrc, startDst, dur,
                           chanSrc};
    requestJob(job);
}

void BufDiskWorker::requestReadStereo(size_t idx0, size_t idx1, std::string path,
                                      float startSrc, float startDst, float dur) {
    BufDiskWorker::Job job{BufDiskWorker::JobType::ReadStereo, {idx0, idx1}, std::move(path), startSrc, startDst, dur,
                           0};
    requestJob(job);
}

void BufDiskWorker::requestWriteMono(size_t idx, std::string path, float start, float dur) {
    BufDiskWorker::Job job{BufDiskWorker::JobType::WriteMono, {idx, 0}, std::move(path), start, start, dur, 0};
    requestJob(job);
}

void BufDiskWorker::requestWriteStereo(size_t idx0, size_t idx1, std::string path,
                                       float start, float dur) {
    BufDiskWorker::Job job{BufDiskWorker::JobType::WriteStereo, {idx0, idx1}, std::move(path), start, start, dur, 0};
    requestJob(job);
}

void BufDiskWorker::requestRender(size_t idx, float start, float dur, int samples, RenderCallback callback) {
    BufDiskWorker::Job job{BufDiskWorker::JobType::Render, {idx, 0}, "", start, start, dur, 0, 0.f, 0.f, false, samples, callback};
    requestJob(job);
}

void BufDiskWorker::workLoop() {
    while (!shouldQuit) {
        // FIXME: use condvar to wait here instead of sleeping...
        qMut.lock();
        if (!jobQ.empty()) {
            Job job = jobQ.front();
            jobQ.pop();
            qMut.unlock();

            switch (job.type) {
                case JobType::Clear:
                    clearBuffer(bufs[job.bufIdx[0]], job.startDst, job.dur);
                    break;
                case JobType::Copy:
                    copyBuffer(bufs[job.bufIdx[0]], bufs[job.bufIdx[1]], job.startSrc, job.startDst, job.dur, job.fadeTime, job.preserve, job.reverse);
                    break;
                case JobType::ReadMono:
                    readBufferMono(job.path, bufs[job.bufIdx[0]], job.startSrc, job.startDst, job.dur, job.chan);
                    break;
                case JobType::ReadStereo:
                    readBufferStereo(job.path, bufs[job.bufIdx[0]], bufs[job.bufIdx[1]], job.startSrc, job.startDst,
                                     job.dur);
                    break;
                case JobType::WriteMono:
                    writeBufferMono(job.path, bufs[job.bufIdx[0]], job.startSrc, job.dur);
                    break;
                case JobType::WriteStereo:
                    writeBufferStereo(job.path, bufs[job.bufIdx[0]], bufs[job.bufIdx[1]], job.startSrc, job.dur);
                    break;
                case JobType::Render:
                    render(bufs[job.bufIdx[0]], job.startSrc, job.dur, (size_t)job.samples, job.renderCallback);
                    break;
            }
#if 0 // debug, timing
            auto ms_now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
            auto ms_dur = ms_now - ms_start;
            std::cout << "job finished; elapsed time = " << ms_dur << " ms" << std::endl;
#endif

        } else {
            qMut.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepPeriodMs));
        }
    }
}

void BufDiskWorker::init(int sr) {
    sampleRate = sr;
    if (worker == nullptr) {
        worker = std::make_unique<std::thread>(std::thread(BufDiskWorker::workLoop));
        worker->detach();
    }
}

int BufDiskWorker::secToFrame(float seconds) {
    return static_cast<int>(seconds * (float) sampleRate);
}

float BufDiskWorker::raisedCosFade(float unitphase) {
    return 0.5f * (cosf(M_PI * (1.f + unitphase)) + 1.f);
}

float BufDiskWorker::mixFade(float x, float y, float a, float b) {
    return a * x + b * y;
}

//------------------------
//---- private buffer routines

void BufDiskWorker::clearBuffer(BufDesc &buf, float start, float dur) {
    size_t frA = secToFrame(start);
    clamp(frA, buf.frames - 1);
    size_t frB;
    if (dur < 0) {
        frB = buf.frames;
    } else {
        frB = frA + secToFrame(dur);
    }
    clamp(frB, buf.frames);
    for (size_t i = frA; i < frB; ++i) {
        buf.data[i] = 0.f;
    }
}

void BufDiskWorker::copyBuffer(BufDesc &buf0, BufDesc &buf1,
                               float srcStart, float dstStart, float dur,
                               float fadeTime, float preserve, bool reverse) {
    size_t frSrcStart = secToFrame(srcStart);
    clamp(frSrcStart, buf0.frames - 1);
    size_t frDstStart = secToFrame(dstStart);
    clamp(frDstStart, buf1.frames - 1);

    size_t frDur;
    if (dur < 0) {
        frDur = buf0.frames - frSrcStart;
    } else {
        frDur = secToFrame(dur);
    }
    clamp(frDur, buf1.frames - frDstStart);

    if (preserve > 1.f) { preserve = 1.f; }
    if (preserve < 0.f) { preserve = 0.f; }

    float x;
    float phi;
    size_t frFadeTime = secToFrame(fadeTime);
    if (frFadeTime > 0) {
        x = 0.f;
        phi = 1.f / frFadeTime;
        clamp(frFadeTime, frDur);
    } else {
        frFadeTime = 0;
        x = 1.f;
        phi = 0.f;
    }

    size_t i;
    float lambda;
    if (reverse) {
        for (i = 0; i < frFadeTime; i++) {
            lambda = raisedCosFade(x);
            buf1.data[frDstStart + i] = mixFade(buf1.data[frDstStart + i], buf0.data[frSrcStart + frDur - i],
                                                1.f - lambda * (1.f - preserve), lambda);
            x += phi;
        }
        for ( ; i < frDur - frFadeTime; i++) {
            buf1.data[frDstStart + i] = mixFade(buf1.data[frDstStart + i], buf0.data[frSrcStart + frDur - i],
                                                preserve, 1.f);
        }
        for ( ; i < frDur; i++) {
            lambda = raisedCosFade(x);
            buf1.data[frDstStart + i] = mixFade(buf1.data[frDstStart + i], lambda * buf0.data[frSrcStart + frDur - i],
                                                1.f - lambda * (1.f - preserve), lambda);
            x -= phi;
        }
    } else {
        for (i = 0; i < frFadeTime; i++) {
            lambda = raisedCosFade(x);
            buf1.data[frDstStart + i] = mixFade(buf1.data[frDstStart + i], buf0.data[frSrcStart + i],
                                                1.f - lambda * (1.f - preserve), lambda);
            x += phi;
        }
        for ( ; i < frDur - frFadeTime; i++) {
            buf1.data[frDstStart + i] = mixFade(buf1.data[frDstStart + i], buf0.data[frSrcStart + i],
                                                preserve, 1.f);

        }
        for ( ; i < frDur; i++) {
            lambda = raisedCosFade(x);
            buf1.data[frDstStart + i] = mixFade(buf1.data[frDstStart + i], buf0.data[frSrcStart + i],
                                                1.f - lambda * (1.f - preserve), lambda);
            x -= phi;
        }
    }
}

void BufDiskWorker::readBufferMono(const std::string &path, BufDesc &buf,
                                   float startSrc, float startDst, float dur, int chanSrc)
noexcept {
    SndfileHandle file(path);

    if (file.frames() < 1) {
        std::cerr << "readBufferMono(): empty / missing file: " << path << std::endl;
        return;
    }

    size_t bufFrames = buf.frames;

    size_t frSrc = secToFrame(startSrc);
    clamp(frSrc, bufFrames - 1);

    size_t frDst = secToFrame(startDst);
    clamp(frDst, bufFrames - 1);

    size_t frDur;
    if (dur < 0.f) {
        auto maxDurSrc = file.frames() - frSrc;
        auto maxDurDst = bufFrames - frDst;
        frDur = maxDurSrc > maxDurDst ? maxDurDst : maxDurSrc;
    } else {
        frDur = secToFrame(dur);
    }

    auto numSrcChan = file.channels();
    chanSrc = std::min(numSrcChan - 1, std::max(0, chanSrc));

    auto *ioBuf = new float[numSrcChan * ioBufFrames];
    size_t numBlocks = frDur / ioBufFrames;
    size_t rem = frDur - (numBlocks * ioBufFrames);
    std::cout << "file contains " << file.frames() << " frames" << std::endl;
    std::cout << "reading " << numBlocks << " blocks and " << rem << " remainder frames..." << std::endl;
    for (size_t block = 0; block < numBlocks; ++block) {
        int res = file.seek(frSrc, SF_SEEK_SET);
        if (res == -1) {
            std::cerr << "error seeking to frame: " << frSrc << "; aborting read" << std::endl;
            goto cleanup;
        }
        file.readf(ioBuf, ioBufFrames);
        for (int fr = 0; fr < ioBufFrames; ++fr) {
            buf.data[frDst] = ioBuf[fr * numSrcChan + chanSrc];
            frDst++;
        }
        frSrc += ioBufFrames;
    }
    for (size_t i = 0; i < rem; ++i) {
        int res = file.seek(frSrc, SF_SEEK_SET);

        if (res == -1) {
            std::cerr << "error seeking to frame: " << frSrc << "; aborting read" << std::endl;
            goto cleanup;
        }
        file.read(ioBuf, numSrcChan);
        buf.data[frDst] = ioBuf[chanSrc];
        frDst++;
        frSrc++;
    }
    cleanup:
    delete[] ioBuf;
}

void BufDiskWorker::readBufferStereo(const std::string &path, BufDesc &buf0, BufDesc &buf1,
                                     float startTimeSrc, float startTimeDst, float dur)
noexcept {
    SndfileHandle file(path);

    if (file.frames() < 1) {
        std::cerr << "SoftCutClient::readBufferStereo(): empty / missing file: " << path << std::endl;
        return;
    }

    size_t bufFrames = buf0.frames < buf1.frames ? buf0.frames : buf1.frames;

    size_t frSrc = secToFrame(startTimeSrc);
    clamp(frSrc, bufFrames - 1);

    size_t frDst = secToFrame(startTimeDst);
    clamp(frDst, bufFrames - 1);

    size_t frDur;
    if (dur < 0.f) {
        auto maxDurSrc = file.frames() - frSrc;
        auto maxDurDst = bufFrames - frDst;
        frDur = maxDurSrc > maxDurDst ? maxDurDst : maxDurSrc;
    } else {
        frDur = secToFrame(dur);
    }

    auto numSrcChan = file.channels();
    if (numSrcChan < 2) {
        std::cerr << "SoftCutClient::readBufferStereo(): not enough channels in source; aborting" << std::endl;
        return;
    }

    auto *ioBuf = new float[numSrcChan * ioBufFrames];

    size_t numBlocks = frDur / ioBufFrames;
    size_t rem = frDur - (numBlocks * ioBufFrames);

    for (size_t block = 0; block < numBlocks; ++block) {
        int res = file.seek(frSrc, SF_SEEK_SET);
        if (res == -1) {
            std::cerr << "error seeking to frame: " << frSrc << "; aborting read" << std::endl;
            goto cleanup;
        }
        file.readf(ioBuf, ioBufFrames);
        for (int fr = 0; fr < ioBufFrames; ++fr) {
            buf0.data[frDst] = ioBuf[fr * numSrcChan];
            buf1.data[frDst] = ioBuf[fr * numSrcChan + 1];
            frDst++;
        }
        frSrc += ioBufFrames;
    }
    for (size_t i = 0; i < rem; ++i) {
        int res = file.seek(frSrc, SF_SEEK_SET);
        if (res == -1) {
            std::cerr << "error seeking to frame: " << frSrc << "; aborting read" << std::endl;
            goto cleanup;
        }
        file.read(ioBuf, numSrcChan);
        buf0.data[frDst] = ioBuf[0];
        buf1.data[frDst] = ioBuf[1];
        frDst++;
        frSrc++;
    }
    cleanup:
    delete[] ioBuf;
}

void BufDiskWorker::writeBufferMono(const std::string &path, BufDesc &buf, float start, float dur) noexcept {
    const int sr = 48000;
    const int channels = 1;
    const int format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;

    SndfileHandle file(path, SFM_WRITE, format, channels, sr);

    if (not file) {
        std::cerr << "BufDiskWorker::writeBufferMono(): cannot open sndfile" << path << " for writing" << std::endl;
        return;
    }

    file.command(SFC_SET_CLIPPING, NULL, SF_TRUE);

    size_t frSrc = secToFrame(start);
    size_t bufFrames = buf.frames;
    clamp(frSrc, bufFrames - 1);

    size_t frDur;
    if (dur < 0.f) {
        // FIXME: should check available disk space?
        frDur = bufFrames - frSrc;
    } else {
        frDur = secToFrame(dur);
    }

    size_t numBlocks = frDur / ioBufFrames;
    size_t rem = frDur - (numBlocks * ioBufFrames);
    size_t nf = 0;
    float *pbuf = buf.data + frSrc;
    for (size_t block = 0; block < numBlocks; ++block) {
        size_t n = file.writef(pbuf, ioBufFrames);
        pbuf += ioBufFrames;
        nf += n;
        if (n != ioBufFrames) {
            std::cerr << "BufDiskWorker::writeBufferMono(): write aborted (disk space?) after " << nf << " frames"
                      << std::endl;
            return;
        }
    }

    for (size_t i = 0; i < rem; ++i) {
        if (file.writef(pbuf++, 1) != 1) {
            std::cerr << "BufDiskWorker::writeBufferMono(): write aborted (disk space?) after " << nf << " frames"
                      << std::endl;
            return;
        }
        ++nf;
    }
}

void BufDiskWorker::writeBufferStereo(const std::string &path, BufDesc &buf0, BufDesc &buf1, float start, float dur)
noexcept {
    const int sr = 48000;
    const int channels = 2;
    const int format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
    SndfileHandle file(path, SFM_WRITE, format, channels, sr);

    if (not file) {
        std::cerr << "ERROR: cannot open sndfile" << path << " for writing" << std::endl;
        return;
    }

    file.command(SFC_SET_CLIPPING, NULL, SF_TRUE);

    size_t frSrc = secToFrame(start);
    size_t bufFrames = buf0.frames < buf1.frames ? buf0.frames : buf1.frames;
    clamp(frSrc, bufFrames - 1);

    size_t frDur;
    if (dur < 0.f) {
        // FIXME: should check available disk space?
        frDur = bufFrames - frSrc;
    } else {
        frDur = secToFrame(dur);
    }

    size_t numBlocks = frDur / ioBufFrames;
    size_t rem = frDur - (numBlocks * ioBufFrames);
    size_t nf = 0;

    auto *ioBuf = new float[ioBufFrames * 2];
    float *pbuf0 = buf0.data + frSrc;
    float *pbuf1 = buf1.data + frSrc;
    for (size_t block = 0; block < numBlocks; ++block) {
        float *pio = ioBuf;
        for (size_t fr = 0; fr < ioBufFrames; ++fr) {
            *pio++ = *pbuf0++;
            *pio++ = *pbuf1++;
        }
        size_t n = file.writef(ioBuf, ioBufFrames);
        nf += n;
        if (n != ioBufFrames) {
            std::cerr << "BufDiskWorker::writeBufferStereo(): write aborted (disk space?) after " << nf << " frames"
                      << std::endl;
            goto cleanup;
        }
        frSrc += ioBufFrames;
    }

    for (size_t i = 0; i < rem; ++i) {
        ioBuf[0] = *(buf0.data + frSrc);
        ioBuf[1] = *(buf1.data + frSrc);
        if (file.writef(ioBuf, 1) != 1) {
            std::cerr << "BufDiskWorker::writeBufferStereo(): write aborted (disk space?) after " << nf << " frames"
                      << std::endl;
            goto cleanup;
        }
        ++frSrc;
        ++nf;
    }
    cleanup:
    delete[] ioBuf;
}

void BufDiskWorker::render(BufDesc &buf, float start, float dur, size_t samples, RenderCallback callback) {
    size_t frStart = secToFrame(start);

    size_t frDur;
    if (dur < 0) {
        frDur = buf.frames - frStart;
        dur = frDur / (float)sampleRate;
    } else {
        frDur = secToFrame(dur);
    }
    if (frDur < 1) { return; }
    clamp(samples, frDur);
    float window = dur / samples;

    auto *sampleBuf = new float[samples];

    size_t m;
    if (frDur <= samples) {
        // no peak finding
        for (m = 0; m < samples; m++) {
            sampleBuf[m] = buf.data[frStart + m];
        }
    } else {
        size_t w, wStart, wEnd;
        float peak;

        // FIXME -- sloppy heuristic for how many frames to skip when peak finding
        int stride = (int)std::log2f(dur / 4);
        if (stride < 1) { stride = 1; }
        for (m = 1; m <= samples; m++) {
            wStart = secToFrame(start + (m - 1) * window);
            wEnd = secToFrame(start + m * window);
            peak = 0.f;
            for (w = wStart; w <= wEnd; w += stride) {
                if (std::fabs(buf.data[w]) > std::fabs(peak)) {
                    peak = buf.data[w];
                }
            }
            sampleBuf[m - 1] = peak;
        }
    }

    callback(window, start, samples, sampleBuf);
    delete[] sampleBuf;
}
