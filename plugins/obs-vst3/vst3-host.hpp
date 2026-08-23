/******************************************************************************
 * obs-vst3: VST3 Host Implementation
 *
 * OBS filter plugin that hosts VST3 plugins
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#pragma once

#include <obs-module.h>
#include <util/threading.h>

#include <string>
#include <vector>
#include <memory>
#include <mutex>

#include "vst3-plugin.hpp"

namespace obs_vst3 {

struct VST3HostContext {
    obs_source_t* source = nullptr;
    std::unique_ptr<VST3Plugin> plugin;
    std::string plugin_path;
    int sample_rate = 48000;
    int buffer_size = 512;
    int num_channels = 2;
    bool enabled = true;

    // Audio buffers
    std::vector<std::vector<float>> input_buffers;
    std::vector<std::vector<float>> output_buffers;
    std::vector<float*> input_ptrs;
    std::vector<float*> output_ptrs;

    // Parameter automation
    struct AutomatedParam {
        uint32_t param_id;
        obs_data_t* automation_data = nullptr;
        size_t automation_index = 0;
    };
    std::vector<AutomatedParam> automated_params;

    // GUI
    void* editor_window = nullptr;
    bool editor_open = false;

    // Statistics
    uint64_t frames_processed = 0;
    double cpu_usage = 0.0;
};

// OBS filter callbacks
const char* vst3_filter_get_name(void* unused);
void* vst3_filter_create(obs_data_t* settings, obs_source_t* source);
void vst3_filter_destroy(void* data);
void vst3_filter_update(void* data, obs_data_t* settings);
obs_properties_t* vst3_filter_properties(void* unused);
void vst3_filter_defaults(obs_data_t* settings);

void vst3_filter_audio_render(void* data, obs_source_t* source,
                               const struct obs_source_audio* audio,
                               struct obs_audio_data* output);

} // namespace obs_vst3