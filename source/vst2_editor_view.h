#pragma once

#include "public.sdk/source/common/pluginview.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <string>

namespace jtw {

class JamTabaWrapperEffect;

class Vst2EditorView final : public Steinberg::CPluginView {
public:
    explicit Vst2EditorView(JamTabaWrapperEffect* owner);
    ~Vst2EditorView() override;

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API removed() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getSize(Steinberg::ViewRect* size) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canResize() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) SMTG_OVERRIDE;

    // Called from audioMasterSizeWindow when JamTaba requests a new editor size.
    bool requestResizeFromPlugin(int width, int height);

    // Called by owner if the processor is being terminated while view is still alive.
    void detachOwner();

private:
#if defined(_WIN32)
    static constexpr UINT_PTR kIdleTimerId = 1;
    static constexpr UINT kIdleIntervalMs = 20; // ~50 Hz for Qt event loop pumping

    static LRESULT CALLBACK containerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static bool ensureWindowClassRegistered(HMODULE moduleHandle);
    static HMODULE currentModuleHandle();

    void resizeEmbeddedChild(int width, int height);
    void paintDiagnosticMessage(HWND hwnd);

    HWND parentHwnd_ = nullptr;
    HWND containerHwnd_ = nullptr;
#endif

    JamTabaWrapperEffect* owner_ = nullptr;
    bool inPluginResize_ = false;
    bool vst2EditorOpen_ = false;
    int width_ = 1024;
    int height_ = 680;
};

} // namespace jtw
