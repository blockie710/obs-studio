/******************************************************************************
 * obs-vst3: VST3 UI Implementation
 *
 * Native Qt window embedding for VST3 plugin editors
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#include "vst3-ui.hpp"

namespace obs_vst3 {

#if defined(HAVE_VST3_SDK)

VST3ViewWrapper::VST3ViewWrapper() = default;

VST3ViewWrapper::~VST3ViewWrapper() {
    detach();
}

bool VST3ViewWrapper::attach(void* parent_widget, Steinberg::Vst::IPlugView* view) {
    if (!view) return false;

    parent_widget_ = parent_widget;
    view_ = view;

#if defined(_WIN32)
    return attachWindows(parent_widget, view);
#elif defined(__APPLE__)
    return attachMacOS(parent_widget, view);
#else
    return attachLinux(parent_widget, view);
#endif
}

void VST3ViewWrapper::detach() {
    if (view_) {
#if defined(_WIN32)
        detachWindows();
#elif defined(__APPLE__)
        detachMacOS();
#else
        detachLinux();
#endif
        view_->removed();
        view_ = nullptr;
    }
    parent_widget_ = nullptr;
}

bool VST3ViewWrapper::getSize(int& width, int& height) const {
    if (!view_) return false;

#if defined(HAVE_VST3_SDK)
    Steinberg::ViewRect rect;
    if (view_->getSize(&rect) == Steinberg::kResultTrue) {
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
        return true;
    }
#endif
    return false;
}

bool VST3ViewWrapper::setSize(int width, int height) {
    if (!view_) return false;

#if defined(HAVE_VST3_SDK)
    Steinberg::ViewRect rect = {0, 0, width, height};
    return view_->setFrame(&rect) == Steinberg::kResultTrue;
#else
    return false;
#endif
}

bool VST3ViewWrapper::setFocus(bool focus) {
    if (!view_) return false;
#if defined(HAVE_VST3_SDK)
    return view_->onFocus(focus ? 1 : 0) == Steinberg::kResultTrue;
#else
    return false;
#endif
}

#if defined(_WIN32)
bool VST3ViewWrapper::attachWindows(void* parent_widget, Steinberg::Vst::IPlugView* view) {
    QWidget* widget = static_cast<QWidget*>(parent_widget);
    if (!widget) return false;

    widget->setAttribute(Qt::WA_NativeWindow, true);
    widget->setAttribute(Qt::WA_DontCreateNativeAncestors, true);

    hwnd_parent_ = (HWND)widget->winId();
    if (!hwnd_parent_) {
        blog(LOG_ERROR, "[obs-vst3] Failed to get HWND from parent widget");
        return false;
    }

    Steinberg::String platform_type;
    USTRING(platform_type, "HWND");

    Steinberg::ViewRect rect = {0, 0, 400, 300};
    Steinberg::tresult result = view->attached(hwnd_parent_, platform_type, &rect);
    if (result != Steinberg::kResultTrue) {
        blog(LOG_ERROR, "[obs-vst3] Failed to attach VST3 view to HWND");
        return false;
    }

    hwnd_editor_ = FindWindowEx(hwnd_parent_, nullptr, nullptr, nullptr);
    blog(LOG_INFO, "[obs-vst3] Attached VST3 editor to HWND: %p", hwnd_parent_);
    return true;
}

void VST3ViewWrapper::detachWindows() {
    if (view_) {
        view_->removed();
    }
    hwnd_parent_ = nullptr;
    hwnd_editor_ = nullptr;
}

#elif defined(__APPLE__)
bool VST3ViewWrapper::attachMacOS(void* parent_widget, Steinberg::Vst::IPlugView* view) {
    QWidget* widget = static_cast<QWidget*>(parent_widget);
    if (!widget) return false;

    widget->setAttribute(Qt::WA_NativeWindow, true);

    NSView* nsview = (NSView*)widget->winId();
    if (!nsview) {
        blog(LOG_ERROR, "[obs-vst3] Failed to get NSView from parent widget");
        return false;
    }

    Steinberg::String platform_type;
    USTRING(platform_type, "NSView");

    Steinberg::ViewRect rect = {0, 0, 400, 300};
    Steinberg::tresult result = view->attached(nsview, platform_type, &rect);
    if (result != Steinberg::kResultTrue) {
        blog(LOG_ERROR, "[obs-vst3] Failed to attach VST3 view to NSView");
        return false;
    }

    blog(LOG_INFO, "[obs-vst3] Attached VST3 editor to NSView: %p", nsview);
    return true;
}

void VST3ViewWrapper::detachMacOS() {
    if (view_) {
        view_->removed();
    }
}

#else
bool VST3ViewWrapper::attachLinux(void* parent_widget, Steinberg::Vst::IPlugView* view) {
    QWidget* widget = static_cast<QWidget*>(parent_widget);
    if (!widget) return false;

    widget->setAttribute(Qt::WA_NativeWindow, true);

    Window x11_window = (Window)widget->winId();
    if (!x11_window) {
        blog(LOG_ERROR, "[obs-vst3] Failed to get X11 window from parent widget");
        return false;
    }

    Steinberg::String platform_type;
    USTRING(platform_type, "X11EmbedWindowID");

    Steinberg::ViewRect rect = {0, 0, 400, 300};
    Steinberg::tresult result = view->attached((void*)x11_window, platform_type, &rect);
    if (result != Steinberg::kResultTrue) {
        blog(LOG_ERROR, "[obs-vst3] Failed to attach VST3 view to X11 window");
        return false;
    }

    blog(LOG_INFO, "[obs-vst3] Attached VST3 editor to X11 window: %lu", x11_window);
    return true;
}

void VST3ViewWrapper::detachLinux() {
    if (view_) {
        view_->removed();
    }
}
#endif

VST3EditorWidget::VST3EditorWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_DontCreateNativeAncestors, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(400, 300);
}

VST3EditorWidget::~VST3EditorWidget() {
    if (view_wrapper_) {
        view_wrapper_->detach();
    }
}

bool VST3EditorWidget::setPluginView(Steinberg::Vst::IPlugView* view) {
    if (view_wrapper_) {
        view_wrapper_->detach();
    }

    if (!view) return false;

    view_wrapper_ = std::make_unique<VST3ViewWrapper>();
    return view_wrapper_->attach(this, view);
}

void VST3EditorWidget::clearPluginView() {
    if (view_wrapper_) {
        view_wrapper_->detach();
        view_wrapper_ = nullptr;
    }
}

bool VST3EditorWidget::resizeEditor(int width, int height) {
    if (view_wrapper_) {
        return view_wrapper_->setSize(width, height);
    }
    return false;
}

void VST3EditorWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (view_wrapper_) {
        view_wrapper_->setSize(width(), height());
    }
}

void VST3EditorWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (view_wrapper_) {
        view_wrapper_->setFocus(true);
    }
}

void VST3EditorWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (view_wrapper_) {
        view_wrapper_->setFocus(false);
    }
}

VST3EditorWidget* createVST3EditorWidget(QWidget* parent) {
    return new VST3EditorWidget(parent);
}

#endif // HAVE_VST3_SDK

} // namespace obs_vst3