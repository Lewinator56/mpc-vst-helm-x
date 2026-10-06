#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <assert.h>

// VST2 structures
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

static intptr_t mockMaster(AEffect *effect, int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt) {
    (void)effect; (void)index; (void)value; (void)ptr; (void)opt;
    if (opcode == 7) { // audioMasterGetTime
        return 0;
    }
    return 0;
}

int main() {
    printf("[TEST] Calling VSTPluginMain...\n");
    AEffect *fx = VSTPluginMain(mockMaster);
    if (!fx) {
        printf("[TEST FAIL] VSTPluginMain returned NULL\n");
        return 1;
    }
    printf("[TEST] Created plugin instance: numParams=%d, numInputs=%d, numOutputs=%d\n",
           fx->numParams, fx->numInputs, fx->numOutputs);

    printf("[TEST] Calling effOpen...\n");
    fx->dispatcher(fx, effOpen, 0, 0, NULL, 0.0f);

    printf("[TEST] Calling effSetSampleRate (44100)...\n");
    fx->dispatcher(fx, effSetSampleRate, 0, 0, NULL, 44100.0f);

    printf("[TEST] Calling effSetBlockSize (128)...\n");
    fx->dispatcher(fx, effSetBlockSize, 0, 128, NULL, 0.0f);

    printf("[TEST] Calling effMainsChanged (1)...\n");
    fx->dispatcher(fx, effMainsChanged, 0, 1, NULL, 0.0f);

    // Audio buffers
    float in_l[128] = {0}, in_r[128] = {0};
    float out_l[128] = {0}, out_r[128] = {0};
    float *inputs[2] = { in_l, in_r };
    float *outputs[2] = { out_l, out_r };

    printf("[TEST] Processing 10 silent blocks...\n");
    for (int i = 0; i < 10; i++) {
        fx->processReplacing(fx, inputs, outputs, 128);
    }

    // Set full sustain for polyphony test
    for (int p = 0; p < fx->numParams; p++) {
        char name[64] = {0};
        fx->dispatcher(fx, effGetParamName, p, 0, name, 0.0f);
        if (strcasecmp(name, "Amp Sustain") == 0 || strcmp(name, "amp_sustain") == 0) {
            fx->setParameter(fx, p, 1.0f);
        }
    }

    printf("[TEST] Sending MIDI Note On (Note 60)...\n");
    VstMidiEvent noteOn = {};
    noteOn.type = 1; // kVstMidiType
    noteOn.byteSize = sizeof(VstMidiEvent);
    noteOn.midiData[0] = 0x90;
    noteOn.midiData[1] = 60;
    noteOn.midiData[2] = 100;

    VstMidiEvent noteOff = {};
    noteOff.type = 1;
    noteOff.byteSize = sizeof(VstMidiEvent);
    noteOff.midiData[0] = 0x80;
    noteOff.midiData[1] = 60;
    noteOff.midiData[2] = 0;
    
    VstEvents events = {};
    events.numEvents = 1;
    events.events[0] = (VstEvent*)&noteOn;
    fx->dispatcher(fx, effProcessEvents, 0, 0, &events, 0.0f);

    printf("[TEST] Processing 10 blocks with Note 60 playing...\n");
    for (int i = 0; i < 10; i++) {
        fx->processReplacing(fx, inputs, outputs, 128);
        printf("  [Note 60 block %d] out_l[0]=%f\n", i, out_l[0]);
    }

    printf("[TEST] Sending Second MIDI Note On (Note 64) while Note 60 is still held...\n");
    VstMidiEvent noteOn2 = {};
    noteOn2.type = 1;
    noteOn2.byteSize = sizeof(VstMidiEvent);
    noteOn2.midiData[0] = 0x90;
    noteOn2.midiData[1] = 64;
    noteOn2.midiData[2] = 100;
    events.events[0] = (VstEvent*)&noteOn2;
    fx->dispatcher(fx, effProcessEvents, 0, 0, &events, 0.0f);

    printf("[TEST] Processing 10 blocks with BOTH Note 60 and Note 64 playing...\n");
    for (int i = 0; i < 10; i++) {
        fx->processReplacing(fx, inputs, outputs, 128);
        printf("  [Poly block %d] out_l[0]=%f (samples: %f, %f, %f)\n", i, out_l[0], out_l[1], out_l[2], out_l[3]);
    }

    printf("[TEST] Sending Note Off for Note 64...\n");
    VstMidiEvent noteOff2 = {};
    noteOff2.type = 1;
    noteOff2.byteSize = sizeof(VstMidiEvent);
    noteOff2.midiData[0] = 0x80;
    noteOff2.midiData[1] = 64;
    noteOff2.midiData[2] = 0;
    events.events[0] = (VstEvent*)&noteOff2;
    fx->dispatcher(fx, effProcessEvents, 0, 0, &events, 0.0f);

    printf("[TEST] Processing 10 blocks after Note 64 released (Note 60 still held)...\n");
    for (int i = 0; i < 10; i++) {
        fx->processReplacing(fx, inputs, outputs, 128);
        printf("  [Post Note 64 release block %d] out_l[0]=%f\n", i, out_l[0]);
    }

    printf("[TEST] Testing Oscillator 1 Pan Hard Left (with Osc 2 muted)...\n");
    // Mute Osc 2, Sub, Noise; Max Osc 1; Pan Osc 1 Left
    for (int p = 0; p < fx->numParams; p++) {
        char name[64] = {0};
        fx->dispatcher(fx, effGetParamName, p, 0, name, 0.0f);
        if (strcasecmp(name, "Osc 2 Volume") == 0 || strcmp(name, "osc_2_volume") == 0) {
            printf("  [Param] Setting %s (p=%d) to 0.0f\n", name, p);
            fx->setParameter(fx, p, 0.0f);
        }
        if (strcasecmp(name, "Osc 1 Volume") == 0 || strcmp(name, "osc_1_volume") == 0) {
            printf("  [Param] Setting %s (p=%d) to 1.0f\n", name, p);
            fx->setParameter(fx, p, 1.0f);
        }
        if (strcasecmp(name, "Sub Volume") == 0 || strcasecmp(name, "Noise Volume") == 0) {
            printf("  [Param] Setting %s (p=%d) to 0.0f\n", name, p);
            fx->setParameter(fx, p, 0.0f);
        }
        if (strcasecmp(name, "Reverb On") == 0 || strcmp(name, "reverb_on") == 0 ||
            strcasecmp(name, "Delay On") == 0 || strcmp(name, "delay_on") == 0 ||
            strcasecmp(name, "Distortion On") == 0 || strcmp(name, "distortion_on") == 0) {
            printf("  [Param] Disabling FX %s (p=%d)\n", name, p);
            fx->setParameter(fx, p, 0.0f);
        }
        if (strcasecmp(name, "Osc 1 Pan") == 0 || strcmp(name, "osc_1_pan") == 0) {
            printf("  [Param] Setting %s (p=%d) to 0.0f\n", name, p);
            fx->setParameter(fx, p, 0.0f); // hard left (-1.0 normalized to 0.0)
        }
    }
    // Retrigger note with new parameters
    events.events[0] = (VstEvent*)&noteOff;
    fx->dispatcher(fx, effProcessEvents, 0, 0, &events, 0.0f);
    for (int i = 0; i < 50; i++) fx->processReplacing(fx, inputs, outputs, 128);

    events.events[0] = (VstEvent*)&noteOn;
    fx->dispatcher(fx, effProcessEvents, 0, 0, &events, 0.0f);

    float pan_l = 0.0f, pan_r = 0.0f;
    for (int i = 0; i < 20; i++) {
        fx->processReplacing(fx, inputs, outputs, 128);
        float blk_l = 0.0f, blk_r = 0.0f;
        for (int s = 0; s < 128; s++) {
            if (fabs(out_l[s]) > pan_l) pan_l = fabs(out_l[s]);
            if (fabs(out_r[s]) > pan_r) pan_r = fabs(out_r[s]);
            if (fabs(out_l[s]) > blk_l) blk_l = fabs(out_l[s]);
            if (fabs(out_r[s]) > blk_r) blk_r = fabs(out_r[s]);
        }
        if (i < 5 || i == 19) printf("  [Block %d] L=%f, R=%f\n", i, blk_l, blk_r);
    }
    printf("[TEST] Pan Left output: Left=%f, Right=%f (expect strong Left isolation)\n", pan_l, pan_r);
    assert(pan_l > 0.05f && pan_l > 4.0f * pan_r);

    printf("[TEST] Testing Oscillator 1 Pan Hard Right...\n");
    for (int p = 0; p < fx->numParams; p++) {
        char name[64] = {0};
        fx->dispatcher(fx, effGetParamName, p, 0, name, 0.0f);
        if (strcasecmp(name, "Osc 1 Pan") == 0 || strcmp(name, "osc_1_pan") == 0) {
            printf("  [Param] Setting %s (p=%d) to 1.0f\n", name, p);
            fx->setParameter(fx, p, 1.0f); // hard right (+1.0 normalized to 1.0)
        }
    }

    events.events[0] = (VstEvent*)&noteOff;
    fx->dispatcher(fx, effProcessEvents, 0, 0, &events, 0.0f);
    for (int i = 0; i < 50; i++) fx->processReplacing(fx, inputs, outputs, 128);

    events.events[0] = (VstEvent*)&noteOn;
    fx->dispatcher(fx, effProcessEvents, 0, 0, &events, 0.0f);

    pan_l = 0.0f; pan_r = 0.0f;
    for (int i = 0; i < 20; i++) {
        fx->processReplacing(fx, inputs, outputs, 128);
        for (int s = 0; s < 128; s++) {
            if (fabs(out_l[s]) > pan_l) pan_l = fabs(out_l[s]);
            if (fabs(out_r[s]) > pan_r) pan_r = fabs(out_r[s]);
        }
    }
    printf("[TEST] Pan Right output: Left=%f, Right=%f (expect strong Right isolation)\n", pan_l, pan_r);
    assert(pan_r > 0.05f && pan_r > 4.0f * pan_l);

    printf("[TEST] Testing parameters set & get...\n");
    for (int p = 0; p < fx->numParams; p++) {
        char name[64] = {0};
        char display[64] = {0};
        char label[64] = {0};
        fx->dispatcher(fx, effGetParamName, p, 0, name, 0.0f);
        fx->dispatcher(fx, effGetParamDisplay, p, 0, display, 0.0f);
        fx->dispatcher(fx, effGetParamLabel, p, 0, label, 0.0f);
        float val = fx->getParameter(fx, p);
        fx->setParameter(fx, p, 0.5f);
        float val2 = fx->getParameter(fx, p);
        (void)val; (void)val2;
    }

    printf("[TEST] Sending MIDI Note Off...\n");
    events.events[0] = (VstEvent*)&noteOff;
    fx->dispatcher(fx, effProcessEvents, 0, 0, &events, 0.0f);

    printf("[TEST] Processing 20 blocks with note off...\n");
    for (int i = 0; i < 20; i++) {
        fx->processReplacing(fx, inputs, outputs, 128);
    }

    printf("[TEST] Testing getChunk / setChunk...\n");
    void *chunk_ptr = NULL;
    intptr_t chunk_len = fx->dispatcher(fx, effGetChunk, 0, 0, &chunk_ptr, 0.0f);
    printf("[TEST] Chunk length = %ld bytes\n", (long)chunk_len);
    if (chunk_len > 0 && chunk_ptr) {
        fx->dispatcher(fx, effSetChunk, 0, chunk_len, chunk_ptr, 0.0f);
    }

    printf("[TEST] Calling effClose...\n");
    fx->dispatcher(fx, effClose, 0, 0, NULL, 0.0f);

    printf("[TEST SUCCESS] All tests passed!\n");
    return 0;
}
