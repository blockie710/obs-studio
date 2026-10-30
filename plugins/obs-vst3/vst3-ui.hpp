/******************************************************************************
 * obs-vst3: VST3 UI Embedding
 *
 * Native Qt window embedding for VST3 plugin editors
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#pragma once

#include "vst3-plugin.hpp"

#if defined(HAVE_VST3_SDK)
#include <pluginterfaces/vst/ivstplugview.h>
#endif

#include <QWidget>
#include <QWindow>
#include <memory>

namespace obs_vst3 {

// Platform-specific window embedding
class VST3ViewWrapper {
public:
    VST3ViewWrapper() = default;
    ~VST3ViewWrapper() { detach(); }

    // Attach VST3 editor to Qt parent widget
    bool attach(void* parent_widget, Steinberg::Vst::IPlugView* view) {
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

    void detach() {
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

    // Get editor size
    bool getSize(int& width, int& height) const {
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

    // Resize editor
    bool setSize(int width, int height) {
        if (!view_) return false;

#if defined(HAVE_VST3_SDK)
        Steinberg::ViewRect rect = {0, 0, width, height};
        return view_->setFrame(&rect) == Steinberg::kResultTrue;
#else
        return false;
#endif
    }

    // Focus handling
    bool setFocus(bool focus) {
        if (!view_) return false;
#if defined(HAVE_VST3_SDK)
        return view_->onFocus(focus ? 1 : 0) == Steinberg::kResultTrue;
#else
        return false;
#endif
    }

private:
    void* parent_widget_ = nullptr;
#if defined(HAVE_VST3_SDK)
    Steinberg::Vst::IPlugView* view_ = nullptr;
#else
    void* view_ = nullptr;
#endif

#if defined(_WIN32)
    HWND hwnd_parent_ = nullptr;
    HWND hwnd_editor_ = nullptr;

    bool attachWindows(void* parent_widget, Steinberg::Vst::IPlugView* view) {
        // Get HWND from QWidget
        QWidget* widget = static_cast<QWidget*>(parent_widget);
        if (!widget) return false;

        // Ensure widget has a native window handle
        widget->setAttribute(Qt::WA_NativeWindow, true);
        widget->setAttribute(Qt::WA_DontCreateNativeAncestors, true);

        hwnd_parent_ = (HWND)widget->winId();
        if (!hwnd_parent_) {
            blog(LOG_ERROR, "[obs-vst3] Failed to get HWND from parent widget");
            return false;
        }

        // Attach view
        Steinberg::String platform_type;
        USTRING(platform_type, "HWND");

        Steinberg::ViewRect rect = {0, 0, 400, 300};
        Steinberg::tresult result = view->attached(hwnd_parent_, platform_type, &rect);
        if (result != Steinberg::kResultTrue) {
            blog(LOG_ERROR, "[obs-vst3] Failed to attach VST3 view to HWND");
            return false;
        }

        // Get the editor window handle
        // The view creates a child window; we can find it
        hwnd_editor_ = FindWindowEx(hwnd_parent_, nullptr, nullptr, nullptr);
        if (!hwnd_editor_) {
            // Try to get from view
            // Some plugins return the HWND differently
        }

        blog(LOG_INFO, "[obs-vst3] Attached VST3 editor to HWND: %p", hwnd_parent_);
        return true;
    }

    void detachWindows() {
        if (view_) {
            view_->removed();
        }
        hwnd_parent_ = nullptr;
        hwnd_editor_ = nullptr;
    }

#elif defined(__APPLE__)

    bool attachMacOS(void* parent_widget, Steinberg::Vst::IPlugView* view) {
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

    void detachMacOS() {
        if (view_) {
            view_->removed();
        }
    }

#else  // Linux

    bool attachLinux(void* parent_widget, Steinberg::Vst::IPlugView* view) {
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

    void detachLinux() {
        if (view_) {
            view_->removed();
        }
    }

#endif
};

// Qt widget that hosts VST3 editor
class VST3EditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit VST3EditorWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_NativeWindow, true);
        setAttribute(Qt::WA_DontCreateNativeAncestors, true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumSize(400, 300);
    }

    ~VST3EditorWidget() {
        if (view_wrapper_) {
            view_wrapper_->detach();
        }
    }

    bool setPluginView(Steinberg::Vst::IPlugView* view) {
        if (view_wrapper_) {
            view_wrapper_->detach();
        }

        if (!view) return false;

        view_wrapper_ = std::make_unique<VST3ViewWrapper>();
        return view_wrapper_->attach(this, view);
    }

    void clearPluginView() {
        if (view_wrapper_) {
            view_wrapper_->detach();
            view_wrapper_ = nullptr;
        }
    }

    bool resizeEditor(int width, int height) {
        if (view_wrapper_) {
            return view_wrapper_->setSize(width, height);
        }
        return false;
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        if (view_wrapper_) {
            view_wrapper_->setSize(width(), height());
        }
    }

    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        if (view_wrapper_) {
            view_wrapper_->setFocus(true);
        }
    }

    void hideEvent(QHideEvent* event) override {
        QWidget::hideEvent(event);
        if (view_wrapper_) {
            view_wrapper_->setFocus(false);
        }
    }

private:
    std::unique_ptr<VST3ViewWrapper> view_wrapper_;
};

// Factory function for creating editor widget
inline VST3EditorWidget* createVST3EditorWidget(QWidget* parent = nullptr) {
    return new VST3EditorWidget(parent);
}

} // namespace obs_vst3