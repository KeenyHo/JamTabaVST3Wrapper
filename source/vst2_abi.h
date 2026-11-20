#pragma once

#include <cstdint>

// Clean-room minimal VST 2.4 C ABI definitions for 64-bit Windows hosting.
// Does not depend on Steinberg's discontinued proprietary VST 2.x SDK.

#if defined(_WIN32)
#define JTW_VST2_CALL __cdecl
#else
#define JTW_VST2_CALL
#endif

#pragma pack(push, 8)

namespace jtw::vst2 {

constexpr int32_t kEffectMagic = 0x56737450; // 'VstP'

struct AEffect;

using AudioMasterCallback = intptr_t (JTW_VST2_CALL*)(
    AEffect* effect,
    int32_t opcode,
    int32_t index,
    intptr_t value,
    void* ptr,
    float opt);

using AEffectDispatcherProc = intptr_t (JTW_VST2_CALL*)(
    AEffect* effect,
    int32_t opcode,
    int32_t index,
    intptr_t value,
    void* ptr,
    float opt);

using AEffectProcessProc = void (JTW_VST2_CALL*)(
    AEffect* effect,
    float** inputs,
    float** outputs,
    int32_t sampleFrames);

using AEffectProcessDoubleProc = void (JTW_VST2_CALL*)(
    AEffect* effect,
    double** inputs,
    double** outputs,
    int32_t sampleFrames);

using AEffectSetParameterProc = void (JTW_VST2_CALL*)(
    AEffect* effect,
    int32_t index,
    float parameter);

using AEffectGetParameterProc = float (JTW_VST2_CALL*)(
    AEffect* effect,
    int32_t index);

using PluginEntryProc = AEffect* (JTW_VST2_CALL*)(AudioMasterCallback audioMaster);

// AEffect::flags
enum AEffectFlags : int32_t {
    effFlagsHasEditor          = 1 << 0,
    effFlagsCanReplacing       = 1 << 4,
    effFlagsProgramChunks      = 1 << 5,
    effFlagsIsSynth            = 1 << 8,
    effFlagsNoSoundInStop      = 1 << 9,
    effFlagsCanDoubleReplacing = 1 << 12
};

// Host -> Plugin dispatcher opcodes
enum AEffectOpcodes : int32_t {
    effOpen                    = 0,
    effClose                   = 1,
    effSetProgram              = 2,
    effGetProgram              = 3,
    effSetProgramName          = 4,
    effGetProgramName          = 5,
    effGetParamLabel           = 6,
    effGetParamDisplay         = 7,
    effGetParamName            = 8,
    effSetSampleRate           = 10,
    effSetBlockSize            = 11,
    effMainsChanged            = 12,
    effEditGetRect             = 13,
    effEditOpen                = 14,
    effEditClose               = 15,
    effEditIdle                = 19,
    effGetChunk                = 23,
    effSetChunk                = 24,
    effProcessEvents           = 25,
    effCanBeAutomated          = 26,
    effGetPlugCategory         = 35,
    effGetEffectName           = 45,
    effGetVendorString         = 47,
    effGetProductString        = 48,
    effGetVendorVersion        = 49,
    effCanDo                   = 51,
    effGetTailSize             = 52,
    effIdle                    = 53,
    effGetVstVersion           = 58,
    effStartProcess            = 71,
    effStopProcess             = 72,
    effSetProcessPrecision     = 77
};

// Plugin -> Host audioMaster opcodes
enum AudioMasterOpcodes : int32_t {
    audioMasterAutomate                 = 0,
    audioMasterVersion                  = 1,
    audioMasterCurrentId                = 2,
    audioMasterIdle                     = 3,
    audioMasterGetTime                  = 7,
    audioMasterProcessEvents            = 8,
    audioMasterIOChanged                = 13,
    audioMasterSizeWindow               = 15,
    audioMasterGetSampleRate            = 16,
    audioMasterGetBlockSize             = 17,
    audioMasterGetInputLatency          = 18,
    audioMasterGetOutputLatency         = 19,
    audioMasterGetCurrentProcessLevel   = 23,
    audioMasterGetAutomationState       = 24,
    audioMasterGetVendorString          = 32,
    audioMasterGetProductString         = 33,
    audioMasterGetVendorVersion         = 34,
    audioMasterCanDo                    = 37,
    audioMasterGetLanguage              = 38,
    audioMasterUpdateDisplay            = 42,
    audioMasterBeginEdit                = 43,
    audioMasterEndEdit                  = 44
};

enum VstProcessLevels : int32_t {
    kVstProcessLevelUnknown   = 0,
    kVstProcessLevelUser      = 1,
    kVstProcessLevelRealtime  = 2,
    kVstProcessLevelPrefetch  = 3,
    kVstProcessLevelOffline   = 4
};

enum VstTimeInfoFlags : int32_t {
    kVstTransportChanged     = 1 << 0,
    kVstTransportPlaying     = 1 << 1,
    kVstTransportCycleActive = 1 << 2,
    kVstTransportRecording   = 1 << 3,
    kVstNanosValid           = 1 << 8,
    kVstPpqPosValid          = 1 << 9,
    kVstTempoValid           = 1 << 10,
    kVstBarsValid            = 1 << 11,
    kVstCyclePosValid        = 1 << 12,
    kVstTimeSigValid         = 1 << 13,
    kVstSmpteValid           = 1 << 14,
    kVstClockValid           = 1 << 15
};

enum VstEventTypes : int32_t {
    kVstMidiType = 1
};

struct ERect {
    int16_t top;
    int16_t left;
    int16_t bottom;
    int16_t right;
};

struct VstTimeInfo {
    double samplePos;
    double sampleRate;
    double nanoSeconds;
    double ppqPos;
    double tempo;
    double barStartPos;
    double cycleStartPos;
    double cycleEndPos;
    int32_t timeSigNumerator;
    int32_t timeSigDenominator;
    int32_t smpteOffset;
    int32_t smpteFrameRate;
    int32_t samplesToNextClock;
    int32_t flags;
};

struct VstEvent {
    int32_t type;
    int32_t byteSize;
    int32_t deltaFrames;
    int32_t flags;
    char data[16];
};

struct VstMidiEvent {
    int32_t type;
    int32_t byteSize;
    int32_t deltaFrames;
    int32_t flags;
    int32_t noteLength;
    int32_t noteOffset;
    char midiData[4];
    char detune;
    char noteOffVelocity;
    char reserved1;
    char reserved2;
};

struct VstEvents {
    int32_t numEvents;
    intptr_t reserved;
    VstEvent* events[2];
};

struct AEffect {
    int32_t magic;
    AEffectDispatcherProc dispatcher;
    AEffectProcessProc process;
    AEffectSetParameterProc setParameter;
    AEffectGetParameterProc getParameter;
    int32_t numPrograms;
    int32_t numParams;
    int32_t numInputs;
    int32_t numOutputs;
    int32_t flags;
    void* resvd1;
    void* resvd2;
    int32_t initialDelay;
    int32_t realQualities;
    int32_t offQualities;
    float ioRatio;
    void* object;
    void* user;
    int32_t uniqueID;
    int32_t version;
    AEffectProcessProc processReplacing;
    AEffectProcessDoubleProc processDoubleReplacing;
    char future[56];
};

#if defined(_WIN64) || defined(__x86_64__)
static_assert(sizeof(AEffect) == 192, "AEffect x64 size mismatch with VST 2.4 ABI");
static_assert(sizeof(VstTimeInfo) == 88, "VstTimeInfo size mismatch with VST 2.4 ABI");
static_assert(sizeof(ERect) == 8, "ERect size mismatch with VST 2.4 ABI");
static_assert(sizeof(VstMidiEvent) == 32, "VstMidiEvent size mismatch with VST 2.4 ABI");
#endif

} // namespace jtw::vst2

#pragma pack(pop)
