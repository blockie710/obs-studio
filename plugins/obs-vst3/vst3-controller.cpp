/******************************************************************************
 * obs-vst3: VST3 Controller Implementation
 *
 * Parameter automation and state management
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#include "vst3-controller.hpp"

namespace obs_vst3 {

#if defined(HAVE_VST3_SDK)

// VST3ControllerWrapper implementation
VST3ControllerWrapper::VST3ControllerWrapper(Steinberg::Vst::IEditController* controller)
    : controller_(controller) {
}

VST3ControllerWrapper::~VST3ControllerWrapper() = default;

bool VST3ControllerWrapper::getParameterInfo(uint32_t param_id, VST3Plugin::ParameterInfo& info) const {
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

float VST3ControllerWrapper::getParameterNormalized(uint32_t param_id) const {
    if (!controller_) return 0.0f;
    Steinberg::Vst::ParamValue value = 0.0;
    controller_->getParamValueByID(param_id, value);
    return value;
}

bool VST3ControllerWrapper::setParameterNormalized(uint32_t param_id, float value) {
    if (!controller_) return false;
    return controller_->setParamValueByID(param_id, value) == Steinberg::kResultTrue;
}

std::string VST3ControllerWrapper::getParameterString(uint32_t param_id) const {
    if (!controller_) return "";
    Steinberg::String128 str;
    if (controller_->getParamStringByValue(param_id, 0, str) == Steinberg::kResultTrue) {
        return Steinberg::String(str).text();
    }
    return "";
}

bool VST3ControllerWrapper::getState(std::vector<uint8_t>& state) const {
    // Controller state is typically not persisted separately
    return false;
}

bool VST3ControllerWrapper::setState(const std::vector<uint8_t>& state) {
    return false;
}

bool VST3ControllerWrapper::getComponentState(std::vector<uint8_t>& state, Steinberg::Vst::IComponent* component) const {
    if (!component) return false;

    VST3MemoryStream stream(state);
    return component->getState(&stream) == Steinberg::kResultTrue;
}

bool VST3ControllerWrapper::setComponentState(const std::vector<uint8_t>& state, Steinberg::Vst::IComponent* component) {
    if (!component) return false;

    VST3MemoryStream stream(const_cast<std::vector<uint8_t>&>(state));
    return component->setState(&stream) == Steinberg::kResultTrue;
}

int32_t VST3ControllerWrapper::getProgramCount() const {
    return 0;
}

std::string VST3ControllerWrapper::getProgramName(int32_t index) const {
    return "";
}

bool VST3ControllerWrapper::setProgram(int32_t index) {
    return false;
}

// VST3MemoryStream implementation
VST3MemoryStream::VST3MemoryStream(std::vector<uint8_t>& buffer) : buffer_(buffer), pos_(0) {
}

VST3MemoryStream::VST3MemoryStream(const std::vector<uint8_t>& buffer) : buffer_(const_cast<std::vector<uint8_t>&>(buffer)), pos_(0) {
}

Steinberg::tresult VST3MemoryStream::read(void* buffer, int32_t numBytes, int32_t* numBytesRead) {
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

Steinberg::tresult VST3MemoryStream::write(const void* buffer, int32_t numBytes, int32_t* numBytesWritten) {
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

Steinberg::tresult VST3MemoryStream::seek(int64_t pos, int32_t mode, int64_t* result) {
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

Steinberg::tresult VST3MemoryStream::tell(int64_t* pos) {
    if (pos) *pos = pos_;
    return Steinberg::kResultTrue;
}

// VST3AutomatedParam implementation
float VST3AutomatedParam::getValueAt(uint64_t timestamp_ns) const {
    if (!is_automated || automation_points.empty()) return 0.0f;

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

#endif // HAVE_VST3_SDK

} // namespace obs_vst3