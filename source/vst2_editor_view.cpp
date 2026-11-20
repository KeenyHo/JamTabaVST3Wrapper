#include "vst2_editor_view.h"
#include "jamtaba_wrapper.h"

#include <algorithm>
#include <cstring>

namespace jtw {

namespace {
#if defined(_WIN32)
constexpr wchar_t kContainerClassName[] = L"JamTabaVST3WrapperContainerWnd";

struct ResizeChildCtx {
    HWND parent;
    int width;
    int height;
};

BOOL CALLBACK resizeDirectChildProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<ResizeChildCtx*>(lParam);
    if (GetParent(hwnd) == ctx->parent) {
        SetWindowPos(
            hwnd,
            nullptr,
            0,
            0,
            ctx->width,
            ctx->height,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return TRUE;
}
#endif
} // namespace

Vst2EditorView::Vst2EditorView(JamTabaWrapperEffect* owner)
    : CPluginView(nullptr)
    , owner_(owner) {
    if (owner_ && owner_->hasVst2()) {
        int w = 0;
        int h = 0;
        if (owner_->queryVst2EditorRect(w, h) && w > 0 && h > 0) {
            width_ = w;
            height_ = h;
        }
    } else {
        width_ = 680;
        height_ = 380;
    }

    setRect(Steinberg::ViewRect(0, 0, width_, height_));
}

Vst2EditorView::~Vst2EditorView() {
    if (isAttached()) {
        removed();
    }
    if (owner_) {
        owner_->onEditorDestroyed(this);
        owner_ = nullptr;
    }
}

void Vst2EditorView::detachOwner() {
#if defined(_WIN32)
    if (containerHwnd_) {
        KillTimer(containerHwnd_, kIdleTimerId);
    }
#endif
    vst2EditorOpen_ = false;
    owner_ = nullptr;
}

Steinberg::tresult PLUGIN_API Vst2EditorView::isPlatformTypeSupported(Steinberg::FIDString type) {
#if defined(_WIN32)
    if (type && std::strcmp(type, Steinberg::kPlatformTypeHWND) == 0) {
        return Steinberg::kResultTrue;
    }
#endif
    return Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API Vst2EditorView::attached(void* parent, Steinberg::FIDString type) {
#if defined(_WIN32)
    if (!parent || !type || std::strcmp(type, Steinberg::kPlatformTypeHWND) != 0) {
        return Steinberg::kInvalidArgument;
    }

    if (isAttached()) {
        removed();
    }

    parentHwnd_ = static_cast<HWND>(parent);
    HMODULE hMod = currentModuleHandle();
    if (!ensureWindowClassRegistered(hMod)) {
        return Steinberg::kInternalError;
    }

    containerHwnd_ = CreateWindowExW(
        0,
        kContainerClassName,
        L"JamTaba 2",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0,
        0,
        width_,
        height_,
        parentHwnd_,
        nullptr,
        hMod,
        this);

    if (!containerHwnd_) {
        parentHwnd_ = nullptr;
        return Steinberg::kInternalError;
    }

    CPluginView::attached(parent, type);

    if (owner_ && owner_->hasVst2()) {
        vst2EditorOpen_ = owner_->openVst2Editor(containerHwnd_);
        int w = 0;
        int h = 0;
        if (owner_->queryVst2EditorRect(w, h) && w > 0 && h > 0) {
            width_ = w;
            height_ = h;
            setRect(Steinberg::ViewRect(0, 0, width_, height_));

            MoveWindow(containerHwnd_, 0, 0, width_, height_, TRUE);
            if (plugFrame) {
                Steinberg::ViewRect desired = getRect();
                plugFrame->resizeView(this, &desired);
            }
        }

        // Pump effEditIdle regularly so JamTaba's embedded Qt event loop stays alive.
        SetTimer(containerHwnd_, kIdleTimerId, kIdleIntervalMs, nullptr);
    } else {
        InvalidateRect(containerHwnd_, nullptr, TRUE);
    }

    return Steinberg::kResultOk;
#else
    (void)parent;
    (void)type;
    return Steinberg::kNotImplemented;
#endif
}

Steinberg::tresult PLUGIN_API Vst2EditorView::removed() {
#if defined(_WIN32)
    if (containerHwnd_) {
        KillTimer(containerHwnd_, kIdleTimerId);
    }

    if (owner_ && vst2EditorOpen_) {
        owner_->closeVst2Editor();
        vst2EditorOpen_ = false;
    }

    if (containerHwnd_) {
        SetWindowLongPtrW(containerHwnd_, GWLP_USERDATA, 0);
        DestroyWindow(containerHwnd_);
        containerHwnd_ = nullptr;
    }
    parentHwnd_ = nullptr;
#endif
    return CPluginView::removed();
}

Steinberg::tresult PLUGIN_API Vst2EditorView::getSize(Steinberg::ViewRect* size) {
    if (!size) {
        return Steinberg::kInvalidArgument;
    }

    if (owner_ && owner_->hasVst2() && !vst2EditorOpen_) {
        int w = 0;
        int h = 0;
        if (owner_->queryVst2EditorRect(w, h) && w > 0 && h > 0) {
            width_ = w;
            height_ = h;
            setRect(Steinberg::ViewRect(0, 0, width_, height_));
        }
    }

    *size = getRect();
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Vst2EditorView::onSize(Steinberg::ViewRect* newSize) {
    if (!newSize) {
        return Steinberg::kInvalidArgument;
    }

    Steinberg::tresult res = CPluginView::onSize(newSize);
    width_ = std::max(1, static_cast<int>(newSize->getWidth()));
    height_ = std::max(1, static_cast<int>(newSize->getHeight()));
    setRect(*newSize);

#if defined(_WIN32)
    if (containerHwnd_) {
        MoveWindow(containerHwnd_, 0, 0, width_, height_, TRUE);
        if (vst2EditorOpen_) {
            resizeEmbeddedChild(width_, height_);
        } else {
            InvalidateRect(containerHwnd_, nullptr, TRUE);
        }
    }
#endif
    return res;
}

Steinberg::tresult PLUGIN_API Vst2EditorView::canResize() {
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Vst2EditorView::checkSizeConstraint(Steinberg::ViewRect* r) {
    if (!r) {
        return Steinberg::kInvalidArgument;
    }
    constexpr int kMinW = 480;
    constexpr int kMinH = 320;
    if (r->getWidth() < kMinW) {
        r->right = r->left + kMinW;
    }
    if (r->getHeight() < kMinH) {
        r->bottom = r->top + kMinH;
    }
    return Steinberg::kResultOk;
}

bool Vst2EditorView::requestResizeFromPlugin(int width, int height) {
    if (width <= 0 || height <= 0 || inPluginResize_) {
        return false;
    }

    inPluginResize_ = true;
    Steinberg::ViewRect desired(0, 0, width, height);
    bool resized = false;

    if (plugFrame) {
        resized = (plugFrame->resizeView(this, &desired) == Steinberg::kResultOk);
    } else {
        onSize(&desired);
        resized = true;
    }

    inPluginResize_ = false;
    return resized;
}

#if defined(_WIN32)
HMODULE Vst2EditorView::currentModuleHandle() {
    HMODULE hMod = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Vst2EditorView::containerWndProc),
        &hMod);
    return hMod;
}

bool Vst2EditorView::ensureWindowClassRegistered(HMODULE moduleHandle) {
    WNDCLASSEXW wc{};
    if (GetClassInfoExW(moduleHandle, kContainerClassName, &wc) != 0) {
        return true;
    }

    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &Vst2EditorView::containerWndProc;
    wc.hInstance = moduleHandle;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kContainerClassName;

    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void Vst2EditorView::resizeEmbeddedChild(int width, int height) {
    if (!containerHwnd_) {
        return;
    }
    ResizeChildCtx ctx{containerHwnd_, width, height};
    EnumChildWindows(containerHwnd_, &resizeDirectChildProc, reinterpret_cast<LPARAM>(&ctx));
}

void Vst2EditorView::paintDiagnosticMessage(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc{};
    GetClientRect(hwnd, &rc);

    HBRUSH bgBrush = CreateSolidBrush(RGB(28, 30, 36));
    FillRect(hdc, &rc, bgBrush);
    DeleteObject(bgBrush);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(235, 238, 245));

    HFONT font = CreateFontW(
        -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(hdc, font);

    std::wstring message = L"JamTaba 2 VST3 Wrapper\r\n\r\n";
    if (owner_) {
        message += owner_->loadErrorMessage();
    } else {
        message += L"Plugin instance not available.";
    }

    RECT textRc = rc;
    InflateRect(&textRc, -24, -24);
    DrawTextW(hdc, message.c_str(), -1, &textRc, DT_LEFT | DT_WORDBREAK);

    SelectObject(hdc, oldFont);
    DeleteObject(font);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK Vst2EditorView::containerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    auto* self = reinterpret_cast<Vst2EditorView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
        case WM_TIMER:
            if (wParam == kIdleTimerId && self->owner_ && self->vst2EditorOpen_) {
                self->owner_->pumpVst2EditorIdle();
            }
            return 0;

        case WM_PAINT:
            if (!self->vst2EditorOpen_) {
                self->paintDiagnosticMessage(hwnd);
                return 0;
            }
            break;

        case WM_ERASEBKGND:
            if (!self->vst2EditorOpen_) {
                return 1;
            }
            break;

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
#endif

} // namespace jtw
