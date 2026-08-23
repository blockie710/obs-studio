/******************************************************************************
 * obs-vst3: Native VST3 Plugin Host Filter
 *
 * Full VST3 audio processing, parameter automation, and native host GUI embedding
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#pragma once

#include <obs-module.h>
#include <util/threading.h>
#include <util/dstr.h>

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <functional>

// VST3 SDK forward declarations (when available)
// If VST3 SDK not found, we provide minimal stubs
#if defined(HAVE_VST3_SDK)
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <pluginterfaces/vst/ivstparameterupdates.h>
#include <pluginterfaces/vst/ivstplugview.h>
#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/base/funknown.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <public.sdk/source/vst/hosting/plugininterfacesupport.h>
#else
// Minimal VST3 stub types for building without SDK
namespace Steinberg {
namespace Vst {

using TUID = char[16];
using ParamID = int32_t;
using BusDirections = int32_t;
using BusTypes = int32_t;
using SpeakerArrangement = int64_t;
using SampleRate = double;
using ProcessPrecision = int32_t;
using SymbolicSampleSize = int32_t;
using TBool = int8_t;
using tresult = int32_t;

constexpr tresult kResultTrue = 1;
constexpr tresult kResultFalse = 0;
constexpr tresult kNotImplemented = -1;

struct ProcessSetup {
    SampleRate sampleRate;
    int32_t maxSamplesPerBlock;
    ProcessPrecision precision;
};

struct ProcessData {
    int32_t numInputs;
    int32_t numOutputs;
    int32_t numSamples;
    void** inputs;
    void** outputs;
    void* parameterChanges;
    void* outputParameterChanges;
};

struct IComponent {
    virtual tresult initialize(void* context) = 0;
    virtual tresult terminate() = 0;
    virtual tresult setActive(TBool state) = 0;
};

struct IAudioProcessor {
    virtual tresult setBusArrangements(const SpeakerArrangement* inputs, int32_t numIns,
                                       const SpeakerArrangement* outputs, int32_t numOuts) = 0;
    virtual tresult getBusArrangement(BusDirections dir, BusTypes type, int32_t index,
                                      SpeakerArrangement& arr) = 0;
    virtual tresult canProcessSampleSize(SymbolicSampleSize sampleSize) = 0;
    virtual tresult setupProcessing(ProcessSetup& setup) = 0;
    virtual tresult process(ProcessData& data) = 0;
};

struct IEditController {
    virtual tresult setComponentState(void* state) = 0;
    virtual tresult getParameterCount() = 0;
    virtual tresult getParameterInfo(int32_t index, void* info) = 0;
    virtual tresult getParamValueByID(ParamID id, void* value) = 0;
    virtual tresult setParamValueByID(ParamID id, void* value) = 0;
};

struct IPlugView {
    virtual tresult isPlatformTypeSupported(const char* type) = 0;
    virtual tresult attached(void* parent, const char* type) = 0;
    virtual tresult removed() = 0;
    virtual tresult onWheel(int32_t distance) = 0;
    virtual tresult onKeyDown(int32_t key, int32_t mod) = 0;
    virtual tresult onKeyUp(int32_t key, int32_t mod) = 0;
    virtual tresult setFrame(void* frame) = 0;
    virtual tresult getSize(void* size) = 0;
    virtual tresult onFocus(TBool state) = 0;
};

} // namespace Vst
} // namespace Steinberg

// Host callback interface
struct VST3HostCallback {
    virtual void onParamChange(uint32_t param_id, float value) = 0;
    virtual void onLatencyChange(uint32_t samples) = 0;
    virtual void onNoteOn(int32_t channel, int32_t pitch, float velocity) = 0;
    virtual void onNoteOff(int32_t channel, int32_t pitch) = 0;
};

#endif

namespace obs_vst3 {

// VST3 Plugin wrapper
class VST3Plugin {
public:
    struct ParameterInfo {
        uint32_t id;
        std::string name;
        std::string unit;
        float min_value;
        float max_value;
        float default_value;
        float step_count;
        int32_t flags;
    };

    struct BusInfo {
        std::string name;
        int32_t channel_count;
        int32_t bus_type;
        int32_t flags;
    };

    VST3Plugin();
    ~VST3Plugin();

    // Load plugin from .vst3 bundle / DLL
    bool load(const std::string& path);
    void unload();

    // Processing
    bool prepare(double sample_rate, int max_block_size, int num_inputs, int num_outputs);
    bool process(float** inputs, float** outputs, int num_frames);
    void setActive(bool active);
    bool isLoaded() const { return loaded_; }

    // Parameters
    std::vector<ParameterInfo> getParameters() const;
    float getParameter(uint32_t param_id) const;
    bool setParameter(uint32_t param_id, float value);
    int32_t getParameterIndex(uint32_t param_id) const;

    // Programs
    int32_t getProgramCount() const;
    std::string getProgramName(int32_t index) const;
    bool setProgram(int32_t index);

    // State
    bool getState(std::vector<uint8_t>& state) const;
    bool setState(const std::vector<uint8_t>& state);

    // GUI
    bool hasEditor() const;
    void* createEditor(void* parent_window);
    void destroyEditor();
    bool getEditorSize(int& width, int& height) const;

    // Info
    std::string getName() const;
    std::string getVendor() const;
    std::string getVersion() const;
    std::string GetPath() const { return path_; }
    bool IsLoaded() const { return loaded_; }

    // Callbacks
    void SetHostCallback(VST3HostCallback* callback) { host_callback_ = callback; }

private:
    std::string path_;
    bool loaded_ = false;

#if defined(HAVE_VST3_SDK)
    // VST3 SDK objects
    Steinberg::Vst::IComponent* component_ = nullptr;
    Steinberg::Vst::IAudioProcessor* processor_ = nullptr;
    Steinberg::Vst::IEditController* controller_ = nullptr;
    Steinberg::Vst::IPlugView* view_ = nullptr;
    void* module_handle_ = nullptr;
#else
    // Stub implementation
    void* module_handle_ = nullptr;
#endif

    VST3HostCallback* host_callback_ = nullptr;

    // Audio setup
    double sample_rate_ = 48000.0;
    int max_block_size_ = 1024;
    int num_inputs_ = 2;
    int num_outputs_ = 2;
    bool active_ = false;

    // Processing buffers
    std::vector<float*> input_ptrs_;
    std::vector<float*> output_ptrs_;

    // Parameter cache
    mutable std::mutex param_mutex_;
    std::vector<ParameterInfo> cached_parameters_;

    // Helper methods
    bool initializeComponent();
    void terminateComponent();
    bool createView(void* parent);
    void cacheParameters();
};

} // namespace obs_vst3