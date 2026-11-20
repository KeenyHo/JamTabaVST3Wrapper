#pragma once

#include "vst2_abi.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace jtw {

class Vst2EditorView;

class JamTabaWrapperEffect final : public Steinberg::Vst::SingleComponentEffect {
public:
    JamTabaWrapperEffect();
    ~JamTabaWrapperEffect() override;

    static Steinberg::FUnknown* createInstance(void* /*context*/) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new JamTabaWrapperEffect());
    }

    // IPluginBase / SingleComponentEffect
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;

    // IComponent / IAudioProcessor
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs,
        Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs,
        Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE;
    Steinberg::uint32 PLUGIN_API getTailSamples() SMTG_OVERRIDE;

    // State persistence (VST2 chunk <-> VST3 IBStream)
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

    // IEditController
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;

    // Helpers for Vst2EditorView
    bool hasVst2() const { return effect_ != nullptr; }
    std::wstring loadErrorMessage() const { return loadErrorMessage_; }
    bool openVst2Editor(void* hwnd);
    void closeVst2Editor();
    void pumpVst2EditorIdle();
    bool queryVst2EditorRect(int& width, int& height);
    void onEditorDestroyed(Vst2EditorView* view);

private:
    static intptr_t JTW_VST2_CALL audioMasterCallback(
        vst2::AEffect* effect,
        int32_t opcode,
        int32_t index,
        intptr_t value,
        void* ptr,
        float opt);

    intptr_t handleAudioMaster(
        vst2::AEffect* effect,
        int32_t opcode,
        int32_t index,
        intptr_t value,
        void* ptr,
        float opt);

    intptr_t dispatchVst2(int32_t opcode, int32_t index = 0, intptr_t value = 0, void* ptr = nullptr, float opt = 0.0f);

    bool loadVst2Plugin();
    void unloadVst2Plugin();
    std::wstring resolveJamTabaDllPath(std::vector<std::wstring>& searchedPaths) const;

    void updateTimeInfoFromContext(const Steinberg::Vst::ProcessContext* ctx);
    void forwardMidiEvents(Steinberg::Vst::IEventList* inputEvents);
    void processAudioSliceFloat(
        float* const* hostIn,
        int hostInCh,
        float* const* hostOut,
        int hostOutCh,
        int32_t numSamples);
    void processAudioSliceDouble(
        double* const* hostIn,
        int hostInCh,
        double* const* hostOut,
        int hostOutCh,
        int32_t numSamples);

#if defined(_WIN32)
    HMODULE vst2Module_ = nullptr;
#endif
    vst2::AEffect* effect_ = nullptr;
    std::wstring loadedDllPath_;
    std::wstring loadErrorMessage_;

    double sampleRate_ = 44100.0;
    int32_t maxBlockSize_ = 512;
    int32_t vstInputChannels_ = 2;
    int32_t vstOutputChannels_ = 2;
    std::atomic<bool> isBypassed_{false};
    std::atomic<bool> inAudioProcess_{false};
    bool isActive_ = false;
    bool lastHostPlaying_ = false;
    std::string hostProductName_ = "VST3 Host";

    vst2::VstTimeInfo timeInfo_{};

    // Pre-allocated real-time audio scratch buffers (zero allocations in process())
    std::vector<std::vector<float>> scratchIn_;
    std::vector<std::vector<float>> scratchOut_;
    std::vector<float*> inPtrs_;
    std::vector<float*> outPtrs_;

    // Pre-allocated MIDI event buffer for effProcessEvents
    static constexpr size_t kMaxMidiEventsPerBlock = 256;
    struct MidiEventsStorage {
        int32_t numEvents = 0;
        intptr_t reserved = 0;
        vst2::VstEvent* events[kMaxMidiEventsPerBlock]{};
    };
    MidiEventsStorage vstEventsStorage_{};
    std::vector<vst2::VstMidiEvent> midiEventsPool_;

    std::mutex viewMutex_;
    Vst2EditorView* activeView_ = nullptr;
};

} // namespace jtw
