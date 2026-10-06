#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

struct AEffect;
typedef intptr_t (*audioMasterCallback)(struct AEffect *, int32_t, int32_t, intptr_t, void *, float);
typedef struct AEffect {
    int32_t magic;
    intptr_t (*dispatcher)(struct AEffect *, int32_t, int32_t, intptr_t, void *, float);
    void (*process)(struct AEffect *, float **, float **, int32_t);
    void (*setParameter)(struct AEffect *, int32_t, float);
    float (*getParameter)(struct AEffect *, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    void (*processReplacing)(struct AEffect *, float **, float **, int32_t);
    void (*processDoubleReplacing)(struct AEffect *, double **, double **, int32_t);
    char future[56];
} AEffect;

typedef struct { int32_t type, byteSize, deltaFrames, flags; char data[16]; } VstEvent;
typedef struct {
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    unsigned char midiData[4];
    char detune, noteOffVelocity, reserved1, reserved2;
} VstMidiEvent;
typedef struct { int32_t numEvents; intptr_t reserved; VstEvent *events[2]; } VstEvents;

enum {
    effOpen = 0, effClose = 1, effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8,
    effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12, effGetChunk = 23,
    effSetChunk = 24, effProcessEvents = 25, effCanBeAutomated = 26, effGetPlugCategory = 35,
    effGetEffectName = 45, effGetVendorString = 47, effGetProductString = 48,
    effGetVendorVersion = 49, effCanDo = 51, effGetVstVersion = 58,
};

extern "C" AEffect *VSTPluginMain(audioMasterCallback audioMaster);
static intptr_t mockMaster(AEffect *e, int32_t op, int32_t idx, intptr_t val, void *ptr, float opt) {
    return 0;
}

static void sendMidi(AEffect* fx, unsigned char status, unsigned char note, unsigned char vel) {
    VstMidiEvent me = {};
    me.type = 1;
    me.byteSize = sizeof(VstMidiEvent);
    me.midiData[0] = status;
    me.midiData[1] = note;
    me.midiData[2] = vel;
    VstEvents evs = {};
    evs.numEvents = 1;
    evs.events[0] = (VstEvent*)&me;
    fx->dispatcher(fx, effProcessEvents, 0, 0, &evs, 0.0f);
}

static void setParamByName(AEffect* fx, const char* name, float val) {
    for (int p = 0; p < fx->numParams; p++) {
        char pname[64] = {0};
        fx->dispatcher(fx, effGetParamName, p, 0, pname, 0.0f);
        if (strcasecmp(pname, name) == 0) {
            fx->setParameter(fx, p, val);
            return;
        }
    }
}

static float getParamByName(AEffect* fx, const char* name) {
    for (int p = 0; p < fx->numParams; p++) {
        char pname[64] = {0};
        fx->dispatcher(fx, effGetParamName, p, 0, pname, 0.0f);
        if (strcasecmp(pname, name) == 0) {
            return fx->getParameter(fx, p);
        }
    }
    return -1.0f;
}

int main() {
    AEffect *fx = VSTPluginMain(mockMaster);
    if (!fx) { printf("Failed to create VST\n"); return 1; }
    fx->dispatcher(fx, effOpen, 0, 0, NULL, 0.0f);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, NULL, 44100.0f);
    fx->dispatcher(fx, effSetBlockSize, 0, 128, NULL, 0.0f);
    fx->dispatcher(fx, effMainsChanged, 0, 1, NULL, 0.0f);

    float in_l[128] = {0}, in_r[128] = {0};
    float out_l[128] = {0}, out_r[128] = {0};
    float *inputs[2] = { in_l, in_r };
    float *outputs[2] = { out_l, out_r };

    // Warmup
    for (int i = 0; i < 10; ++i) fx->processReplacing(fx, inputs, outputs, 128);

    // Check default parameters
    printf("polyphony param: %f\n", getParamByName(fx, "polyphony"));
    printf("legato param: %f\n", getParamByName(fx, "legato"));
    printf("amp_attack param: %f\n", getParamByName(fx, "Amp Attack"));
    printf("amp_decay param: %f\n", getParamByName(fx, "Amp Decay"));
    printf("amp_sustain param: %f\n", getParamByName(fx, "Amp Sustain"));
    printf("amp_release param: %f\n", getParamByName(fx, "Amp Release"));

    // Set 2.0s attack so envelope rise is clearly visible across blocks
    setParamByName(fx, "Amp Attack", 0.5f); // attack ~ 0.5..2s
    setParamByName(fx, "Amp Sustain", 1.0f);

    printf("\n--- Step 1: Note 60 ON (C4) ---\n");
    sendMidi(fx, 0x90, 60, 100);

    for (int block = 0; block < 10; ++block) {
        fx->processReplacing(fx, inputs, outputs, 128);
        float rms = 0.0f;
        for (int i = 0; i < 128; ++i) rms += out_l[i] * out_l[i];
        rms = sqrtf(rms / 128.0f);
        printf("  Block %02d: out_l RMS = %f, sample[0] = %f\n", block, rms, out_l[0]);
    }

    printf("\n--- Step 2: Note 64 ON (E4) while Note 60 is held ---\n");
    sendMidi(fx, 0x90, 64, 100);

    for (int block = 10; block < 20; ++block) {
        fx->processReplacing(fx, inputs, outputs, 128);
        float rms = 0.0f;
        for (int i = 0; i < 128; ++i) rms += out_l[i] * out_l[i];
        rms = sqrtf(rms / 128.0f);
        printf("  Block %02d: out_l RMS = %f, sample[0] = %f\n", block, rms, out_l[0]);
    }

    printf("\n--- Step 3: Note 64 OFF while Note 60 is still held ---\n");
    sendMidi(fx, 0x80, 64, 0);

    for (int block = 20; block < 30; ++block) {
        fx->processReplacing(fx, inputs, outputs, 128);
        float rms = 0.0f;
        for (int i = 0; i < 128; ++i) rms += out_l[i] * out_l[i];
        rms = sqrtf(rms / 128.0f);
        printf("  Block %02d: out_l RMS = %f, sample[0] = %f\n", block, rms, out_l[0]);
    }

    printf("\n--- Step 4: Note 60 OFF ---\n");
    sendMidi(fx, 0x80, 60, 0);

    for (int block = 30; block < 35; ++block) {
        fx->processReplacing(fx, inputs, outputs, 128);
        float rms = 0.0f;
        for (int i = 0; i < 128; ++i) rms += out_l[i] * out_l[i];
        rms = sqrtf(rms / 128.0f);
        printf("  Block %02d: out_l RMS = %f, sample[0] = %f\n", block, rms, out_l[0]);
    }

    fx->dispatcher(fx, effClose, 0, 0, NULL, 0.0f);
    return 0;
}
