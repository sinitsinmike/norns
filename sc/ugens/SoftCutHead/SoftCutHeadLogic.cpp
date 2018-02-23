//
// Created by ezra on 12/6/17.
//

// TODO: looping/1shot behavior flag

#include <cmath>
#include <limits>

#include "SoftCutHeadLogic.h"

#include "interp.h"

/// wtf...
#ifndef nullptr
#define nullptr ((void*)0)
#endif

static int wrap(int val, int bound) {
    if(val >= bound) { return val - bound; }
    if(val < 0) { return val + bound; }
    return val;
}

SoftCutHeadLogic::SoftCutHeadLogic() {
    this->init();
}

void SoftCutHeadLogic::init() {
    sr = 44100.f;
    start = 0.f;
    end = 0.f;
    phase[0] = 0.f;
    phase[1] = 0.f;
    fade[0] = 0.f;
    fade[1] = 0.f;
    state[0] = INACTIVE;
    state[1] = INACTIVE;
    active = 0;
    phaseInc = 0.f;
    setFadeTime(0.1f);
    buf = (float*) nullptr;
    bufFrames = 0;
    trig[0] = 0.f;
    trig[1] = 0.f;
    // fadeMode = FADE_LIN;
    fadeMode = FADE_EQ;
    recRun = false;
    wrPhase[0] = 0.f;
    wrPhase[1] = 0.f;
    wrVal[0] = 0.f;
    wrVal[1] = 0.f;
    wrIdx[0] = 0;
    wrIdx[1] = 0;
}

void SoftCutHeadLogic::nextSample(float in, float *outPhase, float *outTrig, float *outAudio) {

    if(buf == nullptr) {
        return;
    }

    updatePhase(0);
    updatePhase(1);
    updateFade(0);
    updateFade(1);

    if(outPhase != nullptr) { *outPhase = static_cast<float>(phase[active]); }

    *outAudio = mixFade(peek(phase[0]), peek(phase[1]), fade[0], fade[1]);
    *outTrig = trig[0] + trig[1];

    if(recRun) {
      poke(0, in);
      poke(1, in);
    }
}


void SoftCutHeadLogic::setRate(float x)
{
    phaseInc = x;
}

void SoftCutHeadLogic::setLoopStartSeconds(float x)
{
    start = x * sr;
}

void SoftCutHeadLogic::setLoopEndSeconds(float x)
{
    end = x * sr;
}

void SoftCutHeadLogic::updatePhase(int id)
{
    double p;
    trig[id] = 0.f;
    switch(state[id]) {
        case FADEIN:
        case FADEOUT:
        case ACTIVE:
            p = phase[id] + phaseInc;
            if(id == active) {
                if (phaseInc > 0.f) {
                    if (p > end || p < start) {
                        if (loopFlag) {
			  // FIXME: not sure whether or not to preserve phase overshoot
                            // cutToPos(start + (p-end));
                            cutToPhase(start);
                            trig[id] = 1.f;
                        } else {
                            state[id] = FADEOUT;
                        }
                    }
                } else { // negative rate
                    if (p > end || p < start) {
                        if(loopFlag) {
                            // cutToPos(end + (p - start));
                            cutToPhase(end);
                            trig[id] = 1.f;
                        } else {
                            state[id] = FADEOUT;
                        }
                    }
                } // rate sign check
            } // /active check
            phase[id] = p;
            break;
        case INACTIVE:
        default:
            ;; // nothing to do
    }
}

void SoftCutHeadLogic::cutToPhase(float pos) {
    if(state[active] == FADEIN || state[active] == FADEOUT) { return; }
    int newActive = active == 0 ? 1 : 0;
    if(state[active] != INACTIVE) {
        state[active] = FADEOUT;
    }
    state[newActive] = FADEIN;
    phase[newActive] = pos;
    active = newActive;
}

void SoftCutHeadLogic::updateFade(int id) {
    switch(state[id]) {
        case FADEIN:
            fade[id] += fadeInc;
            if (fade[id] > 1.f) {
                fade[id] = 1.f;
                doneFadeIn(id);
            }
            break;
        case FADEOUT:
            fade[id] -= fadeInc;
            if (fade[id] < 0.f) {
                fade[id] = 0.f;
                doneFadeOut(id);
            }
            break;
        case ACTIVE:
        case INACTIVE:
        default:;; // nothing to do
    }
}

void SoftCutHeadLogic::setFadeTime(float secs) {
    fadeInc = (float) 1.0 / (secs * sr);
}

void SoftCutHeadLogic::doneFadeIn(int id) {
    state[id] = ACTIVE;
}

void SoftCutHeadLogic::doneFadeOut(int id) {
    state[id] = INACTIVE;
}

float SoftCutHeadLogic::peek(double phase) {
    return peek4(phase);
}

float SoftCutHeadLogic::peek4(double phase) {
    int phase1 = static_cast<int>(phase);
    int phase0 = phase1 - 1;
    int phase2 = phase1 + 1;
    int phase3 = phase1 + 2;

    double y0 = buf[wrap(phase0, bufFrames)];
    double y1 = buf[wrap(phase1, bufFrames)];
    double y2 = buf[wrap(phase2, bufFrames)];
    double y3 = buf[wrap(phase3, bufFrames)];

    double x = phase - (double)phase1;
    return static_cast<float>(cubicinterp(x, y0, y1, y2, y3));
}

// void SoftCutHeadLogic::poke(int channeL) float* xhist, float x, double phase, float fade) {
//     double p = phase + recPhaseOffset;
//     poke2(xhist, x, p, fade);
// }

// void SoftCutHeadLogic::poke0(float x, double phase, float fade) {
//     if (fade < std::numeric_limits<float>::epsilon()) { return; }
//     if (rec < std::numeric_limits<float>::epsilon()) { return; }

//     int phase0 = wrap((int) phase, bufFrames);

//     float fadeInv = 1.f - fade;

//     float preFade = pre * (1.f - fadePre) + fadePre * std::fmax(pre, (pre * fadeInv));
//     float recFade = rec * (1.f - fadeRec) + fadeRec * (rec * fade);

//     buf[phase0] *= preFade;
//     buf[phase0] += x * recFade;
// }

void SoftCutHeadLogic::poke2(float x, double phase, float fade) {

    // bail if record/fade level is ~=0, so we don't introduce noise
    if (fade < std::numeric_limits<float>::epsilon()) { return; }
    if (rec < std::numeric_limits<float>::epsilon()) { return; }

    int phase0 = wrap(static_cast<int>(phase), bufFrames);
    int phase1 = wrap(phase0 + 1, bufFrames);

    float fadeInv = 1.f - fade;

    float preFade = pre * (1.f - fadePre) + fadePre * std::fmax(pre, (pre * fadeInv));
    float recFade = rec * (1.f - fadeRec) + fadeRec * (rec * fade);

    float fr = phase - floor(phase);
    float frInv = 1.0 - fr;
    
    // linear-interpolated write values
    //    float x1 = fr*x + frInv * xhist[0];
    //    float x0 = frInv*x + fr * xhist[0];

    float x1 = fr*x;
    float x0 = frInv*x;

    // mix old signal with interpolation
    float preFade1 = fr*preFade + (1.f * frInv);
    float preFade0 = frInv*preFade + fr;

    //// seems like this shoulld be correct...
    //    buf[phase1] = (buf[phase1] * preFade * fr) + (buf[phase1] * frInv);
    // buf[phase0] = (buf[phase0] * preFade * frInv) + (buf[phase0] * fr);
    
    // add new signal with interpolation

    buf[phase0] += x0 * recFade;
    buf[phase1] += x1 * recFade;

    //    xhist[0] = x;

}

void SoftCutHeadLogic::poke(int ch, float val) {
  float newPhase = wrPhase[ch] + phaseInc;
  int n = static_cast<int>(newPhase);

    float valInc = val / static_cast<float>(n);
    while(newPhase > 1.0) {
      wrVal[ch] += valInc;
      //      poke0(wrIdx[ch] + recPhaseOffset, wrVal[ch], fade[ch]);
      // hm...
      poke0(wrIdx[ch] + recPhaseOffset, val, fade[ch]);
      wrIdx[ch]++;
      newPhase -= 1.0;
    }

    wrPhase[ch] = newPhase;
    //      if(n > 0) {
    wrIdx[ch] = static_cast<int>(phase[ch]);
    //      }
    wrVal[ch] = val;
}

void SoftCutHeadLogic::poke0(int phase, float val, float fade) {
  if (fade < std::numeric_limits<float>::epsilon()) { return; }
  if (rec < std::numeric_limits<float>::epsilon()) { return; }
float fadeInv = 1.f - fade;
  float preFade = pre * (1.f - fadePre) + fadePre * std::fmax(pre, (pre * fadeInv));
  float recFade = rec * (1.f - fadeRec) + fadeRec * (rec * fade);
  float* p = &(buf[wrap(phase, bufFrames)]);
  *p *= preFade;
  *p += recFade * val;
}

void SoftCutHeadLogic::setBuffer(float *b, uint32_t bf) {
    buf = b;
    bufFrames = bf;
}

void SoftCutHeadLogic::setLoopFlag(bool val) {
    loopFlag = val;
}

void SoftCutHeadLogic::cutToStart() {
    cutToPhase(start);
}

void SoftCutHeadLogic::setSampleRate(float sr_) {
    sr = sr_;
}

float SoftCutHeadLogic::mixFade(float x, float y, float a, float b) {
    if(fadeMode == FADE_EQ) {
        return x * sinf(a * (float) M_PI_2) + y * sinf(b * (float) M_PI_2);
    } else {
        return (x * a) + (y * b);
    }
}

void SoftCutHeadLogic::setRec(float x) {
    rec = x;
}


void SoftCutHeadLogic::setPre(float x) {
    pre= x;
}

void SoftCutHeadLogic::setFadePre(float x) {
    fadePre = x;

}

void SoftCutHeadLogic::setFadeRec(float x) {
    fadeRec = x;
}

void SoftCutHeadLogic::setRecRun(bool val) {
    recRun = val;
}

void SoftCutHeadLogic::setRecOffset(float x) {
  recPhaseOffset = static_cast<int>(x);
}
