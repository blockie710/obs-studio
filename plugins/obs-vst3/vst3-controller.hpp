/******************************************************************************
 * obs-vst3: VST3 Controller Wrapper
 *
 * Parameter automation and state management
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#pragma once

#include "vst3-plugin.hpp"

#include <string>
#include <vector>
#include <memory>

namespace obs_vst3 {

#if defined(HAVE_VST3_SDK)
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <pluginterfaces/vst/ivstparameterupdates.h>
#include <pluginterfaces/base/ibstream.h>

// IBStream wrapper for state serialization
class VST3MemoryStream : public Steinberg::IBStream {
public:
    VST3MemoryStream(std::vector<uint8_t>& buffer) : buffer_(buffer), pos_(0) {}
    VST3MemoryStream(const std::vector<uint8_t>& buffer) : buffer_(const_cast<std::vector<uint8_t>&>(buffer)), pos_(0) {}

    Steinberg::tresult PLUGIN_API read(void* buffer, int32_t numBytes, int32_t* numBytesRead) override {
        if (pos_ + numBytes > buffer_.size()) {
            numBytes = (int32_t)(buffer_.size() - pos_);
        }
        if (numBytes > 0) {
            memcpy(buffer, buffer_.data() + pos_, numBytes);
            pos_ += numBytes;
        }
        if (numBytesRead) *numBytesRead = numBytes;
        return numBytes > 0 ? Steinberg::kResultTrue : Steinberg::kResultFalse;
    }

    Steinberg::tresult PLUGIN_API write(const void* buffer, int32_t numBytes, int32_t* numBytesWritten) override {
        if (pos_ + numBytes > buffer_.size()) {
            buffer_.resize(pos_ + numBytes);
        }
        if (numBytes > 0) {
            memcpy(buffer_.data() + pos_, buffer, numBytes);
            pos_ += numBytes;
        }
        if (numBytesWritten) *numBytesWritten = numBytes;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API seek(int64_t pos, int32_t mode, int64_t* result) override {
        int64_t newPos = 0;
        switch (mode) {
            case 0:  // kIBSeekSet
                newPos = pos;
                break;
            case 1:  // kIBSeekCur
                newPos = pos_ + pos;
                break;
            case 2:  // kIBSeekEnd
                newPos = (int64_t)buffer_.size() + pos;
                break;
        }
        if (newPos < 0) newPos = 0;
        if (newPos > (int64_t)buffer_.size()) newPos = (int64_t)buffer_.size();
        pos_ = (size_t)newPos;
        if (result) *result = pos_;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API tell(int64_t* pos) override {
        if (pos) *pos = pos_;
        return Steinberg::kResultTrue;
    }

private:
    std::vector<uint8_t>& buffer_;
    size_t pos_;
};

// Controller wrapper for parameter automation
class VST3ControllerWrapper {
public:
    explicit VST3ControllerWrapper(Steinberg::Vst::IEditController* controller)
        : controller_(controller) {}

    ~VST3ControllerWrapper() = default;

    // Get parameter info by ID
    bool getParameterInfo(uint32_t param_id, VST3Plugin::ParameterInfo& info) const {
        if (!controller_) return false;

        int32_t index = controller_->getParameterIndex(param_id);
        if (index < 0) return false;

        Steinberg::Vst::ParameterInfo pi;
        if (controller_->getParameterInfo(index, pi) != Steinberg::kResultTrue) {
            return false;
        }

        info.id = pi.id;
        info.name = Steinberg::String(pi.title).text();
        info.unit = Steinberg::String(pi.units).text();
        info.min_value = 0.0f;
        info.max_value = 1.0f;
        info.default_value = pi.defaultNormalizedValue;
        info.step_count = pi.stepCount;
        info.flags = pi.flags;
        return true;
    }

    // Normalized value [0, 1]
    float getParameterNormalized(uint32_t param_id) const {
        if (!controller_) return 0.0f;
        Steinberg::Vst::ParamValue value = 0.0;
        controller_->getParamValueByID(param_id, value);
        return value;
    }

    bool setParameterNormalized(uint32_t param_id, float value) {
        if (!controller_) return false;
        return controller_->setParamValueByID(param_id, value) == Steinberg::kResultTrue;
    }

    // Plain value (display)
    std::string getParameterString(uint32_t param_id) const {
        if (!controller_) return "";
        Steinberg::String128 str;
        if (controller_->getParamStringByValue(param_id, 0, str) == Steinberg::kResultTrue) {
            return Steinberg::String(str).text();
        }
        return "";
    }

    // State serialization
    bool getState(std::vector<uint8_t>& state) const {
        if (!controller_) return false;

        // VST3 state is typically stored in component, not controller
        // But controller can have its own state
        return false;
    }

    bool setState(const std::vector<uint8_t>& state) {
        if (!controller_) return false;
        return false;
    }

    // Component state (full plugin state including programs)
    bool getComponentState(std::vector<uint8_t>& state, Steinberg::Vst::IComponent* component) const {
        if (!component) return false;

        VST3MemoryStream stream(state);
        return component->getState(&stream) == Steinberg::kResultTrue;
    }

    bool setComponentState(const std::vector<uint8_t>& state, Steinberg::Vst::IComponent* component) {
        if (!component) return false;

        VST3MemoryStream stream(const_cast<std::vector<uint8_t>&>(state));
        return component->setState(&stream) == Steinberg::kResultTrue;
    }

    // Program management
    int32_t getProgramCount() const {
        // VST3 programs are typically handled via unit info or preset files
        // For now, return 0 - would need IUnitInfo implementation
        return 0;
    }

    std::string getProgramName(int32_t index) const {
        return "";
    }

    bool setProgram(int32_t index) {
        return false;
    }

private:
    Steinberg::Vst::IEditController* controller_ = nullptr;
};

#endif

// Automation parameter for OBS timeline integration
struct VST3AutomatedParam {
    uint32_t param_id = 0;
    std::string param_name;
    std::vector<std::pair<uint64_t, float>> automation_points;  // (timestamp_ns, value)
    bool is_automated = false;

    float getValueAt(uint64_t timestamp_ns) const {
        if (!is_automated || automation_points.empty()) return 0.0f;

        // Linear interpolation between automation points
        if (timestamp_ns <= automation_points.front().first) {
            return automation_points.front().second;
        }
        if (timestamp_ns >= automation_points.back().first) {
            return automation_points.back().second;
        }

        for (size_t i = 1; i < automation_points.size(); ++i) {
            if (timestamp_ns < automation_points[i].first) {
                const auto& p0 = automation_points[i - 1];
                const auto& p1 = automation_points[i];
                double t = double(timestamp_ns - p0.first) / double(p1.first - p0.first);
                return float(p0.second + t * (p1.second - p0.second));
            }
        }
        return automation_points.back().second;
    }
};

} // namespace obs_vst3