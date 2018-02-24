#include "SC_PlugIn.h"

#include "VariHeadLogic.h"

struct VariHead : public Unit {
    float prevTrig;
    float m_fbufnum;
    float m_failedBufNum;
    SndBuf *m_buf;
    VariHeadLogic variHead; // NB: constructor is never called on this field!
};

static InterfaceTable *ft;

static inline bool checkBuffer(Unit *unit, const float *bufData, uint32 bufChannels,
                               uint32 expectedChannels, int inNumSamples) {
    if (!bufData)
        goto handle_failure;

    if (expectedChannels > bufChannels) {
        if (unit->mWorld->mVerbosity > -1 && !unit->mDone)
            Print("Buffer UGen channel mismatch: expected %i, yet buffer has %i channels\n",
                  expectedChannels, bufChannels);
        goto handle_failure;
    }
    return true;
    handle_failure:
    unit->mDone = true;
    ClearUnitOutputs(unit, inNumSamples);
    return false;
}

static void VariHead_next(VariHead *unit, int inNumSamples);

static void VariHead_Ctor(VariHead *unit);

void VariHead_Ctor(VariHead *unit) {
    Print("VariHead_Ctor() : samplerate %f \n", SAMPLERATE);
    unit->variHead.init();
    unit->variHead.setSampleRate(SAMPLERATE);
    unit->m_fbufnum = -1e9f;
    unit->m_failedBufNum = -1e9f;
    unit->prevTrig = 0.f;
    SETCALC(VariHead_next);
}

// FIXME: d-tor?

void VariHead_next(VariHead *unit, int inNumSamples) {
    GET_BUF;
    uint32 numOutputs = unit->mNumOutputs;
    uint32 numInputChannels = unit->mNumInputs - 4;

    if (!checkBuffer(unit, bufData, bufChannels, numInputChannels, inNumSamples))
        return;

    unit->variHead.setBuffer(bufData, bufFrames);


    const float *in = IN(1);
    float rate = IN0(2);
    float start = IN0(3);
    float end = IN0(4);

    unit->variHead.setRate(rate);
    unit->variHead.setLoopStartSeconds(start);
    unit->variHead.setLoopEndSeconds(end);

#if 0
    float phi = unit->variHead.processBlock(in, inNumSamples);
#else
    // single-sample is still audibly glitched... suggesting an indexing error?
    for (int i = 0; i < inNumSamples; ++i) {
        float phi = unit->variHead.processBlock(in+i, 1);
    }
#endif
}

PluginLoad(VariHead) {
    ft = inTable;
    DefineSimpleUnit(VariHead);
    Print("PluginLoad(VariHead)\n");
}
