#include "plugids.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstring>
#include <iostream>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

using GetPluginFactoryFn = IPluginFactory* (PLUGIN_API*)();
using InitDllFn = bool (*)();
using ExitDllFn = bool (*)();

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::cerr << "Usage: vst3_smoke_test <path-to-JamTabaVST3Wrapper.vst3-binary>\n";
        return 2;
    }

    HMODULE mod = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!mod) {
        std::cerr << "Failed to LoadLibraryExW, err=" << GetLastError() << "\n";
        return 1;
    }

    auto initDll = reinterpret_cast<InitDllFn>(GetProcAddress(mod, "InitDll"));
    auto exitDll = reinterpret_cast<ExitDllFn>(GetProcAddress(mod, "ExitDll"));
    auto getFactory = reinterpret_cast<GetPluginFactoryFn>(GetProcAddress(mod, "GetPluginFactory"));
    if (!getFactory) {
        std::cerr << "GetPluginFactory export not found\n";
        return 1;
    }
    if (initDll && !initDll()) {
        std::cerr << "InitDll returned false\n";
        return 1;
    }

    IPluginFactory* factory = getFactory();
    if (!factory) {
        std::cerr << "GetPluginFactory returned nullptr\n";
        return 1;
    }

    const int32 classCount = factory->countClasses();
    std::cout << "Factory class count: " << classCount << "\n";
    if (classCount < 1) {
        std::cerr << "No classes registered in factory\n";
        return 1;
    }

    PClassInfo info{};
    if (factory->getClassInfo(0, &info) != kResultOk) {
        std::cerr << "getClassInfo(0) failed\n";
        return 1;
    }
    std::cout << "Class[0]: name=" << info.name << ", category=" << info.category << "\n";

    IComponent* component = nullptr;
    if (factory->createInstance(info.cid, IComponent::iid, reinterpret_cast<void**>(&component)) != kResultOk || !component) {
        std::cerr << "createInstance(IComponent) failed\n";
        return 1;
    }

    if (component->initialize(nullptr) != kResultOk) {
        std::cerr << "component->initialize failed\n";
        return 1;
    }

    const int32 inBuses = component->getBusCount(kAudio, kInput);
    const int32 outBuses = component->getBusCount(kAudio, kOutput);
    std::cout << "Audio buses: in=" << inBuses << ", out=" << outBuses << "\n";

    IAudioProcessor* processor = nullptr;
    if (component->queryInterface(IAudioProcessor::iid, reinterpret_cast<void**>(&processor)) != kResultOk || !processor) {
        std::cerr << "queryInterface(IAudioProcessor) failed\n";
        return 1;
    }

    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate = 48000.0;

    if (processor->setupProcessing(setup) != kResultOk) {
        std::cerr << "setupProcessing failed\n";
        return 1;
    }

    if (component->setActive(true) != kResultOk) {
        std::cerr << "setActive(true) failed\n";
        return 1;
    }

    std::vector<float> inL(256, 0.1f), inR(256, -0.1f);
    std::vector<float> outL(256, 0.0f), outR(256, 0.0f);
    float* inCh[2] = {inL.data(), inR.data()};
    float* outCh[2] = {outL.data(), outR.data()};

    AudioBusBuffers inBus{};
    inBus.numChannels = 2;
    inBus.channelBuffers32 = inCh;

    AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outCh;

    ProcessContext ctx{};
    ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid | ProcessContext::kTimeSigValid;
    ctx.sampleRate = 48000.0;
    ctx.tempo = 120.0;
    ctx.timeSigNumerator = 4;
    ctx.timeSigDenominator = 4;

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = 256;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &inBus;
    data.outputs = &outBus;
    data.processContext = &ctx;

    if (processor->process(data) != kResultOk) {
        std::cerr << "process() failed\n";
        return 1;
    }
    std::cout << "process() succeeded (256 samples @ 48kHz)\n";

    IEditController* controller = nullptr;
    if (component->queryInterface(IEditController::iid, reinterpret_cast<void**>(&controller)) == kResultOk && controller) {
        std::cout << "Parameter count: " << controller->getParameterCount() << "\n";
        IPlugView* view = controller->createView(ViewType::kEditor);
        if (view) {
            const bool hwndOk = (view->isPlatformTypeSupported(kPlatformTypeHWND) == kResultTrue);
            ViewRect sz{};
            view->getSize(&sz);
            std::cout << "EditorView created: HWND=" << (hwndOk ? "yes" : "no")
                      << ", size=" << sz.getWidth() << "x" << sz.getHeight() << "\n";
            view->release();
        }
        controller->release();
    }

    component->setActive(false);
    processor->release();
    component->terminate();
    component->release();
    factory->release();

    if (exitDll) {
        exitDll();
    }
    FreeLibrary(mod);

    std::cout << "ALL VST3 SMOKE TESTS PASSED!\n";
    return 0;
}
