#include "jamtaba_wrapper.h"
#include "plugids.h"
#include "vst2_editor_view.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace jtw {

namespace {

constexpr uint32_t kStateMagic = 0x3157544A; // 'JTW1'
constexpr int32_t kMaxSupportedChannels = 16;

thread_local JamTabaWrapperEffect* tlsCreatingInstance = nullptr;

#if defined(_WIN32)
std::wstring getSelfModuleDirectory() {
    HMODULE hMod = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&getSelfModuleDirectory),
            &hMod)) {
        return {};
    }

    wchar_t buf[MAX_PATH * 4]{};
    DWORD len = GetModuleFileNameW(hMod, buf, static_cast<DWORD>(std::size(buf)));
    if (len == 0 || len >= std::size(buf)) {
        return {};
    }

    std::wstring path(buf, len);
    const size_t pos = path.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? path.substr(0, pos) : std::wstring();
}

std::wstring parentDir(const std::wstring& dir) {
    if (dir.empty()) {
        return {};
    }
    size_t end = dir.size();
    while (end > 0 && (dir[end - 1] == L'\\' || dir[end - 1] == L'/')) {
        --end;
    }
    const size_t pos = dir.find_last_of(L"\\/", end > 0 ? end - 1 : 0);
    return (pos != std::wstring::npos) ? dir.substr(0, pos) : std::wstring();
}

bool fileExistsW(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES) && ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), len);
    return out;
}

std::wstring readConfiguredPathFile(const std::wstring& configFilePath) {
    if (!fileExistsW(configFilePath)) {
        return {};
    }
    std::ifstream ifs(std::filesystem::path(configFilePath), std::ios::binary);
    if (!ifs) {
        return {};
    }
    std::string line;
    while (std::getline(ifs, line)) {
        // Strip UTF-8 BOM if present
        if (line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }
        // Trim whitespace and optional surrounding quotes
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
            ++start;
        }
        line.erase(0, start);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line.size() >= 2 && line.front() == '"' && line.back() == '"') {
            line = line.substr(1, line.size() - 2);
        }
        return utf8ToWide(line);
    }
    return {};
}
#endif

} // namespace

JamTabaWrapperEffect::JamTabaWrapperEffect() {
    midiEventsPool_.resize(kMaxMidiEventsPerBlock);

    timeInfo_.sampleRate = sampleRate_;
    timeInfo_.tempo = 120.0;
    timeInfo_.timeSigNumerator = 4;
    timeInfo_.timeSigDenominator = 4;
    timeInfo_.flags = vst2::kVstTempoValid | vst2::kVstTimeSigValid;

    processContextRequirements.needSystemTime();
    processContextRequirements.needContinousTimeSamples();
    processContextRequirements.needProjectTimeMusic();
    processContextRequirements.needBarPositionMusic();
    processContextRequirements.needCycleMusic();
    processContextRequirements.needTempo();
    processContextRequirements.needTimeSignature();
    processContextRequirements.needTransportState();
}

JamTabaWrapperEffect::~JamTabaWrapperEffect() {
    unloadVst2Plugin();
}

Steinberg::tresult PLUGIN_API JamTabaWrapperEffect::initialize(Steinberg::FUnknown* context) {
    Steinberg::tresult result = SingleComponentEffect::initialize(context);
    if (result != Steinberg::kResultOk) {
        return result;
    }

    if (context) {
        Steinberg::FUnknownPtr<Steinberg::Vst::IHostApplication> hostApp(context);
        if (hostApp) {
            Steinberg::Vst::String128 hostName{};
            if (hostApp->getName(hostName) == Steinberg::kResultOk) {
                char ascii[128]{};
                Steinberg::UString(hostName, 128).toAscii(ascii, sizeof(ascii));
                if (ascii[0] != '\0') {
                    hostProductName_ = ascii;
                }
            }
        }
    }

    loadVst2Plugin();

    // Configure audio buses (JamTaba VST2 is stereo in / stereo out; support auxiliary bus if >2 channels)
    addAudioInput(STR16("Stereo In"), Steinberg::Vst::SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);

    if (vstInputChannels_ > 2) {
        addAudioInput(STR16("Aux In"), Steinberg::Vst::SpeakerArr::kStereo, Steinberg::Vst::kAux, 0);
    }
    if (vstOutputChannels_ > 2) {
        addAudioOutput(STR16("Aux Out"), Steinberg::Vst::SpeakerArr::kStereo, Steinberg::Vst::kAux, 0);
    }

    // Event input bus for DAW MIDI forwarding
    addEventInput(STR16("Event In"), 16);

    // Expose a standard Bypass parameter (keeps strict VST3 hosts/validators happy)
    parameters.addParameter(
        STR16("Bypass"),
        nullptr,
        1,
        0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsBypass,
        kBypassParamId);

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API JamTabaWrapperEffect::terminate() {
    {
        std::lock_guard<std::mutex> lock(viewMutex_);
        if (activeView_) {
            activeView_->detachOwner();
            activeView_ = nullptr;
        }
    }

    unloadVst2Plugin();
    return SingleComponentEffect::terminate();
}

Steinberg::tresult PLUGIN_API JamTabaWrapperEffect::setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs,
    Steinberg::int32 numIns,
    Steinberg::Vst::SpeakerArrangement* outputs,
    Steinberg::int32 numOuts) {
    if (numIns >= 1 && numOuts >= 1) {
        const auto inCh = Steinberg::Vst::SpeakerArr::getChannelCount(inputs[0]);
        const auto outCh = Steinberg::Vst::SpeakerArr::getChannelCount(outputs[0]);
        if ((inCh == 1 || inCh == 2) && (outCh == 1 || outCh == 2)) {
            return SingleComponentEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
        }
    }
    return Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API JamTabaWrapperEffect::canProcessSampleSize(Steinberg::int32 symbolicSampleSize) {
    if (symbolicSampleSize == Steinberg::Vst::kSample32 ||
        symbolicSampleSize == Steinberg::Vst::kSample64) {
        return Steinberg::kResultTrue;
    }
    return Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API JamTabaWrapperEffect::setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) {
    sampleRate_ = (newSetup.sampleRate > 0.0) ? newSetup.sampleRate : 44100.0;
    maxBlockSize_ = (newSetup.maxSamplesPerBlock > 0) ? newSetup.maxSamplesPerBlock : 512;

    const int32_t allocInCh = std::clamp(vstInputChannels_, 2, kMaxSupportedChannels);
    const int32_t allocOutCh = std::clamp(vstOutputChannels_, 2, kMaxSupportedChannels);

    scratchIn_.assign(static_cast<size_t>(allocInCh), std::vector<float>(static_cast<size_t>(maxBlockSize_), 0.0f));
    scratchOut_.assign(static_cast<size_t>(allocOutCh), std::vector<float>(static_cast<size_t>(maxBlockSize_), 0.0f));
    inPtrs_.resize(static_cast<size_t>(allocInCh), nullptr);
    outPtrs_.resize(static_cast<size_t>(allocOutCh), nullptr);
    for (int32_t ch = 0; ch < allocInCh; ++ch) {
        inPtrs_[static_cast<size_t>(ch)] = scratchIn_[static_cast<size_t>(ch)].data();
    }
    for (int32_t ch = 0; ch < allocOutCh; ++ch) {
        outPtrs_[static_cast<size_t>(ch)] = scratchOut_[static_cast<size_t>(ch)].data();
    }

    if (effect_) {
        dispatchVst2(vst2::effSetSampleRate, 0, 0, nullptr, static_cast<float>(sampleRate_));
        dispatchVst2(vst2::effSetBlockSize, 0, maxBlockSize_, nullptr, 0.0f);
    }

    return SingleComponentEffect::setupProcessing(newSetup);
}

Steinberg::tresult PLUGIN_API JamTabaWrapperEffect::setActive(Steinberg::TBool state) {
    const bool active = (state != 0);
    if (effect_ && active != isActive_) {
        if (active) {
            dispatchVst2(vst2::effSetSampleRate, 0, 0, nullptr, static_cast<float>(sampleRate_));
            dispatchVst2(vst2::effSetBlockSize, 0, maxBlockSize_, nullptr, 0.0f);
            dispatchVst2(vst2::effMainsChanged, 0, 1);
            dispatchVst2(vst2::effStartProcess);
        } else {
            dispatchVst2(vst2::effStopProcess);
            dispatchVst2(vst2::effMainsChanged, 0, 0);
        }
    }
    isActive_ = active;
    return SingleComponentEffect::setActive(state);
}

Steinberg::uint32 PLUGIN_API JamTabaWrapperEffect::getLatencySamples() {
    if (effect_ && effect_->initialDelay > 0) {
        return static_cast<Steinberg::uint32>(effect_->initialDelay);
    }
    return 0;
}

Steinberg::uint32 PLUGIN_API JamTabaWrapperEffect::getTailSamples() {
    // JamTaba streams remote audio from NINJAM servers even when local input is silent.
    return Steinberg::Vst::kInfiniteTail;
}

void JamTabaWrapperEffect::updateTimeInfoFromContext(const Steinberg::Vst::ProcessContext* ctx) {
    timeInfo_.sampleRate = sampleRate_;
    if (!ctx) {
        return;
    }

    int32_t flags = 0;
    timeInfo_.samplePos = static_cast<double>(ctx->projectTimeSamples);

    const bool isPlaying = (ctx->state & Steinberg::Vst::ProcessContext::kPlaying) != 0;
    if (isPlaying) {
        flags |= vst2::kVstTransportPlaying;
    }
    if (isPlaying != lastHostPlaying_) {
        flags |= vst2::kVstTransportChanged;
        lastHostPlaying_ = isPlaying;
    }
    if (ctx->state & Steinberg::Vst::ProcessContext::kRecording) {
        flags |= vst2::kVstTransportRecording;
    }
    if (ctx->state & Steinberg::Vst::ProcessContext::kCycleActive) {
        flags |= vst2::kVstTransportCycleActive;
    }
    if (ctx->state & Steinberg::Vst::ProcessContext::kSystemTimeValid) {
        timeInfo_.nanoSeconds = static_cast<double>(ctx->systemTime);
        flags |= vst2::kVstNanosValid;
    }
    if (ctx->state & Steinberg::Vst::ProcessContext::kProjectTimeMusicValid) {
        timeInfo_.ppqPos = ctx->projectTimeMusic;
        flags |= vst2::kVstPpqPosValid;
    }
    if (ctx->state & Steinberg::Vst::ProcessContext::kTempoValid) {
        timeInfo_.tempo = (ctx->tempo > 0.0) ? ctx->tempo : 120.0;
        flags |= vst2::kVstTempoValid;
    }
    if (ctx->state & Steinberg::Vst::ProcessContext::kBarPositionValid) {
        timeInfo_.barStartPos = ctx->barPositionMusic;
        flags |= vst2::kVstBarsValid;
    }
    if (ctx->state & Steinberg::Vst::ProcessContext::kCycleValid) {
        timeInfo_.cycleStartPos = ctx->cycleStartMusic;
        timeInfo_.cycleEndPos = ctx->cycleEndMusic;
        flags |= vst2::kVstCyclePosValid;
    }
    if (ctx->state & Steinberg::Vst::ProcessContext::kTimeSigValid) {
        timeInfo_.timeSigNumerator = (ctx->timeSigNumerator > 0) ? ctx->timeSigNumerator : 4;
        timeInfo_.timeSigDenominator = (ctx->timeSigDenominator > 0) ? ctx->timeSigDenominator : 4;
        flags |= vst2::kVstTimeSigValid;
    }
    if (ctx->state & Steinberg::Vst::ProcessContext::kClockValid) {
        timeInfo_.samplesToNextClock = ctx->samplesToNextClock;
        flags |= vst2::kVstClockValid;
    }

    timeInfo_.flags = flags;
}

void JamTabaWrapperEffect::forwardMidiEvents(Steinberg::Vst::IEventList* inputEvents) {
    if (!effect_ || !inputEvents) {
        return;
    }

    const Steinberg::int32 count = inputEvents->getEventCount();
    if (count <= 0) {
        return;
    }

    int32_t outIdx = 0;
    for (Steinberg::int32 i = 0; i < count && outIdx < static_cast<int32_t>(kMaxMidiEventsPerBlock); ++i) {
        Steinberg::Vst::Event ev{};
        if (inputEvents->getEvent(i, ev) != Steinberg::kResultOk) {
            continue;
        }

        vst2::VstMidiEvent& dst = midiEventsPool_[static_cast<size_t>(outIdx)];
        std::memset(&dst, 0, sizeof(dst));
        dst.type = vst2::kVstMidiType;
        dst.byteSize = sizeof(vst2::VstMidiEvent);
        dst.deltaFrames = ev.sampleOffset;

        bool valid = false;
        switch (ev.type) {
            case Steinberg::Vst::Event::kNoteOnEvent: {
                const uint8_t ch = static_cast<uint8_t>(ev.noteOn.channel & 0x0F);
                const uint8_t pitch = static_cast<uint8_t>(ev.noteOn.pitch & 0x7F);
                const uint8_t vel = static_cast<uint8_t>(
                    std::clamp(static_cast<int>(std::lround(ev.noteOn.velocity * 127.0f)), 0, 127));
                dst.midiData[0] = static_cast<char>(0x90 | ch);
                dst.midiData[1] = static_cast<char>(pitch);
                dst.midiData[2] = static_cast<char>(vel);
                valid = true;
                break;
            }
            case Steinberg::Vst::Event::kNoteOffEvent: {
                const uint8_t ch = static_cast<uint8_t>(ev.noteOff.channel & 0x0F);
                const uint8_t pitch = static_cast<uint8_t>(ev.noteOff.pitch & 0x7F);
                const uint8_t vel = static_cast<uint8_t>(
                    std::clamp(static_cast<int>(std::lround(ev.noteOff.velocity * 127.0f)), 0, 127));
                dst.midiData[0] = static_cast<char>(0x80 | ch);
                dst.midiData[1] = static_cast<char>(pitch);
                dst.midiData[2] = static_cast<char>(vel);
                valid = true;
                break;
            }
            case Steinberg::Vst::Event::kPolyPressureEvent: {
                const uint8_t ch = static_cast<uint8_t>(ev.polyPressure.channel & 0x0F);
                const uint8_t pitch = static_cast<uint8_t>(ev.polyPressure.pitch & 0x7F);
                const uint8_t pressure = static_cast<uint8_t>(
                    std::clamp(static_cast<int>(std::lround(ev.polyPressure.pressure * 127.0f)), 0, 127));
                dst.midiData[0] = static_cast<char>(0xA0 | ch);
                dst.midiData[1] = static_cast<char>(pitch);
                dst.midiData[2] = static_cast<char>(pressure);
                valid = true;
                break;
            }
            case Steinberg::Vst::Event::kLegacyMIDICCOutEvent: {
                const uint8_t ch = static_cast<uint8_t>(ev.midiCCOut.channel & 0x0F);
                const uint8_t cc = static_cast<uint8_t>(ev.midiCCOut.controlNumber & 0x7F);
                const uint8_t val = static_cast<uint8_t>(ev.midiCCOut.value & 0x7F);
                dst.midiData[0] = static_cast<char>(0xB0 | ch);
                dst.midiData[1] = static_cast<char>(cc);
                dst.midiData[2] = static_cast<char>(val);
                valid = true;
                break;
            }
            default:
                break;
        }

        if (valid) {
            vstEventsStorage_.events[outIdx] = reinterpret_cast<vst2::VstEvent*>(&dst);
            ++outIdx;
        }
    }

    if (outIdx > 0) {
        vstEventsStorage_.numEvents = outIdx;
        dispatchVst2(vst2::effProcessEvents, 0, 0, &vstEventsStorage_);
    }
}

void JamTabaWrapperEffect::processAudioSliceFloat(
    float* const* hostIn,
    int hostInCh,
    float* const* hostOut,
    int hostOutCh,
    int32_t numSamples) {
    const int32_t inCh = static_cast<int32_t>(inPtrs_.size());
    const int32_t outCh = static_cast<int32_t>(outPtrs_.size());

    for (int32_t ch = 0; ch < inCh; ++ch) {
        float* dst = scratchIn_[static_cast<size_t>(ch)].data();
        if (hostIn && ch < hostInCh && hostIn[ch]) {
            std::memcpy(dst, hostIn[ch], static_cast<size_t>(numSamples) * sizeof(float));
        } else if (hostIn && hostInCh == 1 && hostIn[0] && ch == 1) {
            // Duplicate mono input to right channel so JamTaba stereo channel receives centered audio.
            std::memcpy(dst, hostIn[0], static_cast<size_t>(numSamples) * sizeof(float));
        } else {
            std::memset(dst, 0, static_cast<size_t>(numSamples) * sizeof(float));
        }
    }

    for (int32_t ch = 0; ch < outCh; ++ch) {
        std::memset(scratchOut_[static_cast<size_t>(ch)].data(), 0, static_cast<size_t>(numSamples) * sizeof(float));
    }

    if (effect_->processReplacing) {
        effect_->processReplacing(effect_, inPtrs_.data(), outPtrs_.data(), numSamples);
    } else if (effect_->process) {
        effect_->process(effect_, inPtrs_.data(), outPtrs_.data(), numSamples);
    }

    for (int ch = 0; ch < hostOutCh; ++ch) {
        if (!hostOut || !hostOut[ch]) {
            continue;
        }
        if (ch < outCh) {
            std::memcpy(hostOut[ch], scratchOut_[static_cast<size_t>(ch)].data(), static_cast<size_t>(numSamples) * sizeof(float));
        } else {
            std::memset(hostOut[ch], 0, static_cast<size_t>(numSamples) * sizeof(float));
        }
    }
}

void JamTabaWrapperEffect::processAudioSliceDouble(
    double* const* hostIn,
    int hostInCh,
    double* const* hostOut,
    int hostOutCh,
    int32_t numSamples) {
    const int32_t inCh = static_cast<int32_t>(inPtrs_.size());
    const int32_t outCh = static_cast<int32_t>(outPtrs_.size());

    for (int32_t ch = 0; ch < inCh; ++ch) {
        float* dst = scratchIn_[static_cast<size_t>(ch)].data();
        if (hostIn && ch < hostInCh && hostIn[ch]) {
            for (int32_t s = 0; s < numSamples; ++s) {
                dst[s] = static_cast<float>(hostIn[ch][s]);
            }
        } else if (hostIn && hostInCh == 1 && hostIn[0] && ch == 1) {
            for (int32_t s = 0; s < numSamples; ++s) {
                dst[s] = static_cast<float>(hostIn[0][s]);
            }
        } else {
            std::memset(dst, 0, static_cast<size_t>(numSamples) * sizeof(float));
        }
    }

    for (int32_t ch = 0; ch < outCh; ++ch) {
        std::memset(scratchOut_[static_cast<size_t>(ch)].data(), 0, static_cast<size_t>(numSamples) * sizeof(float));
    }

    if (effect_->processReplacing) {
        effect_->processReplacing(effect_, inPtrs_.data(), outPtrs_.data(), numSamples);
    } else if (effect_->process) {
        effect_->process(effect_, inPtrs_.data(), outPtrs_.data(), numSamples);
    }

    for (int ch = 0; ch < hostOutCh; ++ch) {
        if (!hostOut || !hostOut[ch]) {
            continue;
        }
        if (ch < outCh) {
            const float* src = scratchOut_[static_cast<size_t>(ch)].data();
            for (int32_t s = 0; s < numSamples; ++s) {
                hostOut[ch][s] = static_cast<double>(src[s]);
            }
        } else {
            std::memset(hostOut[ch], 0, static_cast<size_t>(numSamples) * sizeof(double));
        }
    }
}

Steinberg::tresult PLUGIN_API JamTabaWrapperEffect::process(Steinberg::Vst::ProcessData& data) {
    // 1. Handle parameter changes (Bypass)
    if (data.inputParameterChanges) {
        const Steinberg::int32 numParams = data.inputParameterChanges->getParameterCount();
        for (Steinberg::int32 i = 0; i < numParams; ++i) {
            if (auto* queue = data.inputParameterChanges->getParameterData(i)) {
                if (queue->getParameterId() == kBypassParamId) {
                    const Steinberg::int32 numPoints = queue->getPointCount();
                    if (numPoints > 0) {
                        Steinberg::int32 sampleOffset = 0;
                        Steinberg::Vst::ParamValue value = 0.0;
                        if (queue->getPoint(numPoints - 1, sampleOffset, value) == Steinberg::kResultOk) {
                            isBypassed_.store(value >= 0.5, std::memory_order_relaxed);
                        }
                    }
                }
            }
        }
    }

    if (data.numSamples <= 0 || data.numOutputs == 0) {
        return Steinberg::kResultOk;
    }

    const int hostInCh = (data.numInputs > 0) ? data.inputs[0].numChannels : 0;
    const int hostOutCh = data.outputs[0].numChannels;

    // Pass-through when bypassed or if JamtabaVST2.dll could not be loaded
    if (!effect_ || isBypassed_.load(std::memory_order_relaxed)) {
        if (data.symbolicSampleSize == Steinberg::Vst::kSample32) {
            for (int ch = 0; ch < hostOutCh; ++ch) {
                float* out = data.outputs[0].channelBuffers32[ch];
                if (!out) {
                    continue;
                }
                if (ch < hostInCh && data.inputs[0].channelBuffers32[ch]) {
                    if (out != data.inputs[0].channelBuffers32[ch]) {
                        std::memcpy(out, data.inputs[0].channelBuffers32[ch], static_cast<size_t>(data.numSamples) * sizeof(float));
                    }
                } else {
                    std::memset(out, 0, static_cast<size_t>(data.numSamples) * sizeof(float));
                }
            }
        } else {
            for (int ch = 0; ch < hostOutCh; ++ch) {
                double* out = data.outputs[0].channelBuffers64[ch];
                if (!out) {
                    continue;
                }
                if (ch < hostInCh && data.inputs[0].channelBuffers64[ch]) {
                    if (out != data.inputs[0].channelBuffers64[ch]) {
                        std::memcpy(out, data.inputs[0].channelBuffers64[ch], static_cast<size_t>(data.numSamples) * sizeof(double));
                    }
                } else {
                    std::memset(out, 0, static_cast<size_t>(data.numSamples) * sizeof(double));
                }
            }
        }
        return Steinberg::kResultOk;
    }

    // 2. Sync DAW transport & tempo into VstTimeInfo before processing
    updateTimeInfoFromContext(data.processContext);

    // 3. Forward DAW MIDI events
    forwardMidiEvents(data.inputEvents);

    // 4. Process audio in safe slices (<= maxBlockSize_).
    // IMPORTANT: Never skip processing when input silenceFlags != 0, because JamTaba
    // outputs incoming network audio from remote NINJAM musicians!
    inAudioProcess_.store(true, std::memory_order_relaxed);

    int32_t processed = 0;
    while (processed < data.numSamples) {
        const int32_t slice = std::min(maxBlockSize_, data.numSamples - processed);

        if (data.symbolicSampleSize == Steinberg::Vst::kSample32) {
            float* inSlice[kMaxSupportedChannels]{};
            float* outSlice[kMaxSupportedChannels]{};
            const int inCount = std::min(hostInCh, kMaxSupportedChannels);
            const int outCount = std::min(hostOutCh, kMaxSupportedChannels);
            for (int ch = 0; ch < inCount; ++ch) {
                inSlice[ch] = data.inputs[0].channelBuffers32[ch]
                    ? (data.inputs[0].channelBuffers32[ch] + processed)
                    : nullptr;
            }
            for (int ch = 0; ch < outCount; ++ch) {
                outSlice[ch] = data.outputs[0].channelBuffers32[ch]
                    ? (data.outputs[0].channelBuffers32[ch] + processed)
                    : nullptr;
            }
            processAudioSliceFloat(inSlice, inCount, outSlice, outCount, slice);
        } else {
            double* inSlice[kMaxSupportedChannels]{};
            double* outSlice[kMaxSupportedChannels]{};
            const int inCount = std::min(hostInCh, kMaxSupportedChannels);
            const int outCount = std::min(hostOutCh, kMaxSupportedChannels);
            for (int ch = 0; ch < inCount; ++ch) {
                inSlice[ch] = data.inputs[0].channelBuffers64[ch]
                    ? (data.inputs[0].channelBuffers64[ch] + processed)
                    : nullptr;
            }
            for (int ch = 0; ch < outCount; ++ch) {
                outSlice[ch] = data.outputs[0].channelBuffers64[ch]
                    ? (data.outputs[0].channelBuffers64[ch] + processed)
                    : nullptr;
            }
            processAudioSliceDouble(inSlice, inCount, outSlice, outCount, slice);
        }

        processed += slice;
    }

    inAudioProcess_.store(false, std::memory_order_relaxed);

    // Mark outputs as active (non-silent) so DAW doesn't mute remote NINJAM audio.
    data.outputs[0].silenceFlags = 0;
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API JamTabaWrapperEffect::getState(Steinberg::IBStream* state) {
    if (!state) {
        return Steinberg::kInvalidArgument;
    }

    const uint32_t magic = kStateMagic;
    const uint32_t bypass = isBypassed_.load(std::memory_order_relaxed) ? 1u : 0u;
    void* chunkData = nullptr;
    uint32_t chunkSize = 0;

    if (effect_ && (effect_->flags & vst2::effFlagsProgramChunks)) {
        const intptr_t bytes = dispatchVst2(vst2::effGetChunk, 0, 0, &chunkData);
        if (bytes > 0 && chunkData != nullptr) {
            chunkSize = static_cast<uint32_t>(bytes);
        }
    }

    if (state->write(const_cast<uint32_t*>(&magic), sizeof(magic)) != Steinberg::kResultOk ||
        state->write(const_cast<uint32_t*>(&bypass), sizeof(bypass)) != Steinberg::kResultOk ||
        state->write(&chunkSize, sizeof(chunkSize)) != Steinberg::kResultOk) {
        return Steinberg::kResultFalse;
    }

    if (chunkSize > 0 && chunkData) {
        if (state->write(chunkData, static_cast<Steinberg::int32>(chunkSize)) != Steinberg::kResultOk) {
            return Steinberg::kResultFalse;
        }
    }

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API JamTabaWrapperEffect::setState(Steinberg::IBStream* state) {
    if (!state) {
        return Steinberg::kInvalidArgument;
    }

    uint32_t magic = 0;
    uint32_t bypass = 0;
    uint32_t chunkSize = 0;

    if (state->read(&magic, sizeof(magic)) != Steinberg::kResultOk || magic != kStateMagic) {
        return Steinberg::kResultOk; // Ignore empty or foreign state gracefully
    }
    if (state->read(&bypass, sizeof(bypass)) != Steinberg::kResultOk ||
        state->read(&chunkSize, sizeof(chunkSize)) != Steinberg::kResultOk) {
        return Steinberg::kResultFalse;
    }

    isBypassed_.store(bypass != 0, std::memory_order_relaxed);
    setParamNormalized(kBypassParamId, bypass != 0 ? 1.0 : 0.0);

    if (chunkSize > 0 && chunkSize < (64u * 1024u * 1024u)) {
        std::vector<uint8_t> buffer(chunkSize);
        Steinberg::int32 numRead = 0;
        if (state->read(buffer.data(), static_cast<Steinberg::int32>(chunkSize), &numRead) == Steinberg::kResultOk &&
            numRead == static_cast<Steinberg::int32>(chunkSize) &&
            effect_ && (effect_->flags & vst2::effFlagsProgramChunks)) {
            dispatchVst2(vst2::effSetChunk, 0, static_cast<intptr_t>(chunkSize), buffer.data());
        }
    }

    return Steinberg::kResultOk;
}

Steinberg::IPlugView* PLUGIN_API JamTabaWrapperEffect::createView(Steinberg::FIDString name) {
    if (name && std::strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0) {
        auto* view = new Vst2EditorView(this);
        std::lock_guard<std::mutex> lock(viewMutex_);
        activeView_ = view;
        return view;
    }
    return nullptr;
}

void JamTabaWrapperEffect::onEditorDestroyed(Vst2EditorView* view) {
    std::lock_guard<std::mutex> lock(viewMutex_);
    if (activeView_ == view) {
        activeView_ = nullptr;
    }
}

bool JamTabaWrapperEffect::openVst2Editor(void* hwnd) {
    if (!effect_ || !hwnd) {
        return false;
    }
    dispatchVst2(vst2::effEditOpen, 0, 0, hwnd);
    return true;
}

void JamTabaWrapperEffect::closeVst2Editor() {
    if (effect_) {
        dispatchVst2(vst2::effEditClose);
    }
}

void JamTabaWrapperEffect::pumpVst2EditorIdle() {
    if (effect_) {
        dispatchVst2(vst2::effEditIdle);
        dispatchVst2(vst2::effIdle);
    }
}

bool JamTabaWrapperEffect::queryVst2EditorRect(int& width, int& height) {
    if (!effect_) {
        return false;
    }
    vst2::ERect* rect = nullptr;
    dispatchVst2(vst2::effEditGetRect, 0, 0, &rect);
    if (rect && rect->right > rect->left && rect->bottom > rect->top) {
        width = rect->right - rect->left;
        height = rect->bottom - rect->top;
        return true;
    }
    return false;
}

intptr_t JamTabaWrapperEffect::dispatchVst2(
    int32_t opcode,
    int32_t index,
    intptr_t value,
    void* ptr,
    float opt) {
    if (!effect_ || !effect_->dispatcher) {
        return 0;
    }
    return effect_->dispatcher(effect_, opcode, index, value, ptr, opt);
}

intptr_t JTW_VST2_CALL JamTabaWrapperEffect::audioMasterCallback(
    vst2::AEffect* effect,
    int32_t opcode,
    int32_t index,
    intptr_t value,
    void* ptr,
    float opt) {
    JamTabaWrapperEffect* self = nullptr;
    if (effect && effect->resvd1) {
        self = static_cast<JamTabaWrapperEffect*>(effect->resvd1);
    } else {
        self = tlsCreatingInstance;
    }

    if (self) {
        return self->handleAudioMaster(effect, opcode, index, value, ptr, opt);
    }

    if (opcode == vst2::audioMasterVersion) {
        return 2400;
    }
    return 0;
}

intptr_t JamTabaWrapperEffect::handleAudioMaster(
    vst2::AEffect* /*effect*/,
    int32_t opcode,
    int32_t index,
    intptr_t value,
    void* ptr,
    float /*opt*/) {
    switch (opcode) {
        case vst2::audioMasterVersion:
            return 2400; // VST 2.4 host

        case vst2::audioMasterCurrentId:
            return effect_ ? effect_->uniqueID : 0;

        case vst2::audioMasterGetTime:
            return reinterpret_cast<intptr_t>(&timeInfo_);

        case vst2::audioMasterSizeWindow: {
            std::lock_guard<std::mutex> lock(viewMutex_);
            if (activeView_) {
                return activeView_->requestResizeFromPlugin(index, static_cast<int>(value)) ? 1 : 0;
            }
            return 0;
        }

        case vst2::audioMasterGetSampleRate:
            return static_cast<intptr_t>(std::lround(sampleRate_));

        case vst2::audioMasterGetBlockSize:
            return static_cast<intptr_t>(maxBlockSize_);

        case vst2::audioMasterGetInputLatency:
        case vst2::audioMasterGetOutputLatency:
            return 0;

        case vst2::audioMasterGetCurrentProcessLevel:
            return inAudioProcess_.load(std::memory_order_relaxed)
                ? vst2::kVstProcessLevelRealtime
                : vst2::kVstProcessLevelUser;

        case vst2::audioMasterGetAutomationState:
            return 0;

        case vst2::audioMasterGetVendorString:
            if (ptr) {
                std::strncpy(static_cast<char*>(ptr), "JamTabaWrapper", 63);
                static_cast<char*>(ptr)[63] = '\0';
            }
            return 1;

        case vst2::audioMasterGetProductString:
            if (ptr) {
                std::strncpy(static_cast<char*>(ptr), hostProductName_.c_str(), 63);
                static_cast<char*>(ptr)[63] = '\0';
            }
            return 1;

        case vst2::audioMasterGetVendorVersion:
            return 1000;

        case vst2::audioMasterGetLanguage:
            return 1; // kVstLangEnglish

        case vst2::audioMasterCanDo:
            if (ptr) {
                const char* feature = static_cast<const char*>(ptr);
                if (std::strcmp(feature, "sendVstTimeInfo") == 0 ||
                    std::strcmp(feature, "sizeWindow") == 0 ||
                    std::strcmp(feature, "sendVstEvents") == 0 ||
                    std::strcmp(feature, "sendVstMidiEvent") == 0 ||
                    std::strcmp(feature, "receiveVstEvents") == 0 ||
                    std::strcmp(feature, "receiveVstMidiEvent") == 0 ||
                    std::strcmp(feature, "startStopProcess") == 0 ||
                    std::strcmp(feature, "supplyIdle") == 0) {
                    return 1;
                }
            }
            return 0;

        case vst2::audioMasterIdle:
        case vst2::audioMasterAutomate:
        case vst2::audioMasterBeginEdit:
        case vst2::audioMasterEndEdit:
        case vst2::audioMasterUpdateDisplay:
        case vst2::audioMasterIOChanged:
        case vst2::audioMasterProcessEvents:
            return 0;

        default:
            return 0;
    }
}

std::wstring JamTabaWrapperEffect::resolveJamTabaDllPath(std::vector<std::wstring>& searchedPaths) const {
#if defined(_WIN32)
    const std::wstring selfDir = getSelfModuleDirectory(); // <bundle>/Contents/x86_64-win
    const std::wstring contentsDir = parentDir(selfDir);   // <bundle>/Contents
    const std::wstring bundleDir = parentDir(contentsDir); // JamTabaVST3Wrapper.vst3
    const std::wstring vst3ParentDir = parentDir(bundleDir);

    // 1. Optional explicit config file next to binary or in %LOCALAPPDATA%
    std::vector<std::wstring> configCandidates;
    if (!selfDir.empty()) {
        configCandidates.push_back(selfDir + L"\\jamtaba_vst2_path.txt");
    }
    if (!bundleDir.empty()) {
        configCandidates.push_back(bundleDir + L"\\jamtaba_vst2_path.txt");
    }
    wchar_t localAppData[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        configCandidates.push_back(std::wstring(localAppData) + L"\\JamTabaVST3Wrapper\\jamtaba_vst2_path.txt");
    }

    for (const auto& cfg : configCandidates) {
        const std::wstring configured = readConfiguredPathFile(cfg);
        if (!configured.empty()) {
            searchedPaths.push_back(configured);
            if (fileExistsW(configured)) {
                return configured;
            }
        }
    }

    // 2. Standard candidate locations (relative to .vst3 bundle + common VST2 folders)
    std::vector<std::wstring> candidates;
    if (!selfDir.empty()) {
        candidates.push_back(selfDir + L"\\JamtabaVST2.dll");
        candidates.push_back(selfDir + L"\\Jamtaba 2.dll");
    }
    if (!contentsDir.empty()) {
        candidates.push_back(contentsDir + L"\\Resources\\JamtabaVST2.dll");
    }
    if (!vst3ParentDir.empty()) {
        candidates.push_back(vst3ParentDir + L"\\JamtabaVST2.dll");
    }

    candidates.push_back(L"C:\\Program Files\\VSTPlugins\\JamtabaVST2.dll");
    candidates.push_back(L"C:\\Program Files\\Steinberg\\VstPlugins\\JamtabaVST2.dll");
    candidates.push_back(L"C:\\Program Files\\Steinberg\\VSTPlugins\\JamtabaVST2.dll");
    candidates.push_back(L"C:\\Program Files\\Common Files\\VST2\\JamtabaVST2.dll");
    candidates.push_back(L"C:\\Program Files\\VstPlugins\\Jamtaba 2\\JamtabaVST2.dll");
    candidates.push_back(L"C:\\Program Files\\Jamtaba 2\\JamtabaVST2.dll");

    for (const auto& candidate : candidates) {
        searchedPaths.push_back(candidate);
        if (fileExistsW(candidate)) {
            return candidate;
        }
    }
#else
    (void)searchedPaths;
#endif
    return {};
}

bool JamTabaWrapperEffect::loadVst2Plugin() {
#if defined(_WIN32)
    std::vector<std::wstring> searchedPaths;
    const std::wstring dllPath = resolveJamTabaDllPath(searchedPaths);

    if (dllPath.empty()) {
        std::wstringstream ss;
        ss << L"Could not find 64-bit JamtabaVST2.dll.\r\n\r\n"
           << L"Please copy 64-bit JamtabaVST2.dll next to this .vst3 binary:\r\n"
           << L"  JamTabaVST3Wrapper.vst3\\Contents\\x86_64-win\\JamtabaVST2.dll\r\n"
           << L"OR create jamtaba_vst2_path.txt containing the full path to JamtabaVST2.dll.\r\n\r\n"
           << L"Searched paths:\r\n";
        for (size_t i = 0; i < std::min<size_t>(searchedPaths.size(), 6); ++i) {
            ss << L"  - " << searchedPaths[i] << L"\r\n";
        }
        loadErrorMessage_ = ss.str();
        return false;
    }

    vst2Module_ = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!vst2Module_) {
        const DWORD err = GetLastError();
        std::wstringstream ss;
        ss << L"Found JamtabaVST2.dll at:\r\n  " << dllPath << L"\r\n\r\n"
           << L"Failed to load DLL (Win32 error " << err << L").\r\n"
           << L"Make sure it is the 64-bit version of JamtabaVST2.dll (not 32-bit).";
        loadErrorMessage_ = ss.str();
        return false;
    }

    auto entry = reinterpret_cast<vst2::PluginEntryProc>(GetProcAddress(vst2Module_, "VSTPluginMain"));
    if (!entry) {
        entry = reinterpret_cast<vst2::PluginEntryProc>(GetProcAddress(vst2Module_, "main"));
    }
    if (!entry) {
        loadErrorMessage_ = L"DLL loaded, but neither VSTPluginMain nor main export was found:\r\n  " + dllPath;
        FreeLibrary(vst2Module_);
        vst2Module_ = nullptr;
        return false;
    }

    tlsCreatingInstance = this;
    vst2::AEffect* created = entry(&JamTabaWrapperEffect::audioMasterCallback);
    tlsCreatingInstance = nullptr;

    if (!created || created->magic != vst2::kEffectMagic) {
        loadErrorMessage_ = L"VSTPluginMain did not return a valid VST 2.x AEffect instance:\r\n  " + dllPath;
        FreeLibrary(vst2Module_);
        vst2Module_ = nullptr;
        return false;
    }

    effect_ = created;
    effect_->resvd1 = this;
    loadedDllPath_ = dllPath;

    vstInputChannels_ = std::clamp(effect_->numInputs, 0, kMaxSupportedChannels);
    vstOutputChannels_ = std::clamp(effect_->numOutputs, 0, kMaxSupportedChannels);

    dispatchVst2(vst2::effOpen);
    dispatchVst2(vst2::effSetSampleRate, 0, 0, nullptr, static_cast<float>(sampleRate_));
    dispatchVst2(vst2::effSetBlockSize, 0, maxBlockSize_, nullptr, 0.0f);
    return true;
#else
    loadErrorMessage_ = L"JamTaba VST2 wrapper is only supported on 64-bit Windows.";
    return false;
#endif
}

void JamTabaWrapperEffect::unloadVst2Plugin() {
    if (effect_) {
        if (isActive_) {
            dispatchVst2(vst2::effStopProcess);
            dispatchVst2(vst2::effMainsChanged, 0, 0);
            isActive_ = false;
        }
        dispatchVst2(vst2::effClose);
        effect_ = nullptr;
    }

#if defined(_WIN32)
    if (vst2Module_) {
        FreeLibrary(vst2Module_);
        vst2Module_ = nullptr;
    }
#endif
}

} // namespace jtw
