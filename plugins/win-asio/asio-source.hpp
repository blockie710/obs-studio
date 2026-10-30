/******************************************************************************
 * win-asio: ASIO Source Header
 *
 * OBS source plugin for capturing ASIO audio channels
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#pragma once

#include <obs-module.h>
#include <util/threading.h>

#include <memory>
#include <vector>

namespace win_asio {

struct ASIOSourceContext {
    obs_source_t* source = nullptr;
    int client_id = -1;
    int sample_rate = 48000;
    int buffer_size = 512;
    int num_channels = 2;
    std::vector<int> channel_map;  // Maps OBS channel -> ASIO channel

    // Audio buffers for OBS callback
    std::vector<float*> planar_buffers;
    std::vector<std::vector<float>> buffer_storage;

    // Statistics
    uint64_t frames_delivered = 0;
    uint64_t frames_dropped = 0;
};

// OBS source callbacks
const char* asio_source_get_name(void* unused);
void asio_source_update(void* data, obs_data_t* settings);
void* asio_source_create(obs_data_t* settings, obs_source_t* source);
void asio_source_destroy(void* data);
void asio_source_audio_render(void* data, uint64_t* ts_out,
                               struct obs_source_audio_mix* audio_output,
                               uint32_t mixers, size_t channels,
                               size_t sample_rate);
obs_properties_t* asio_source_properties(void* unused);
void asio_source_defaults(obs_data_t* settings);
uint32_t asio_source_get_mixers(void* unused);

} // namespace win_asio