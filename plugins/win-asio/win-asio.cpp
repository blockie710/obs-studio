/******************************************************************************
 * win-asio: ASIO Source Plugin - Complete Implementation
 *
 * OBS source plugin for capturing ASIO audio channels with full property UI
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#include "asio-source.hpp"
#include "asio-driver-manager.hpp"

#include <obs-module.h>
#include <util/threading.h>
#include <util/dstr.h>

#include <memory>
#include <vector>
#include <string>
#include <numeric>  // for std::iota
#include <cstring>  // for strdup, strtok

namespace win_asio {

struct ASIOSourceContext {
    obs_source_t* source = nullptr;
    int client_id = -1;
    int sample_rate = 48000;
    int buffer_size = 512;
    int num_channels = 2;
    std::vector<int> channel_map;  // Maps OBS channel -> ASIO channel
    std::string driver_clsid;

    // Audio buffers for OBS callback
    std::vector<float*> planar_buffers;
    std::vector<std::vector<float>> buffer_storage;

    // Statistics
    uint64_t frames_delivered = 0;
    uint64_t frames_dropped = 0;
};

static const char* asio_source_get_name(void* unused) {
    return "ASIO Input Capture";
}

static void asio_source_get_defaults(obs_data_t* settings) {
    obs_data_set_default_int(settings, "sample_rate", 48000);
    obs_data_set_default_int(settings, "buffer_size", 512);
    obs_data_set_default_int(settings, "channels", 2);
    obs_data_set_default_string(settings, "driver_clsid", "");
    obs_data_set_default_string(settings, "channel_config", "[]");
}

static void asio_source_update(void* data, obs_data_t* settings) {
    ASIOSourceContext* ctx = static_cast<ASIOSourceContext*>(data);

    ctx->sample_rate = (int)obs_data_get_int(settings, "sample_rate");
    ctx->buffer_size = (int)obs_data_get_int(settings, "buffer_size");
    ctx->num_channels = (int)obs_data_get_int(settings, "channels");
    ctx->driver_clsid = obs_data_get_string(settings, "driver_clsid");

    // Parse channel configuration (comma-separated for now)
    const char* channel_config = obs_data_get_string(settings, "channel_config");
    ctx->channel_map.clear();
    if (channel_config && *channel_config) {
        char* copy = strdup(channel_config);
        char* token = strtok(copy, ",");
        while (token && (int)ctx->channel_map.size() < ctx->num_channels) {
            ctx->channel_map.push_back(atoi(token));
            token = strtok(nullptr, ",");
        }
        free(copy);
    }

    // Default 1:1 mapping if none specified
    for (int i = (int)ctx->channel_map.size(); i < ctx->num_channels; ++i) {
        ctx->channel_map.push_back(i);
    }

    // Reconfigure driver if needed
    if (ctx->client_id >= 0) {
        auto& mgr = ASIO_DriverManager::Instance();

        // Set up client routing
        std::vector<asio_channel_router_t::route_t> routes;
        for (int i = 0; i < ctx->num_channels; ++i) {
            asio_channel_router_t::route_t route;
            route.asio_channel = ctx->channel_map[i];
            route.obs_channel = i;
            route.gain = 1.0f;
            route.invert = false;
            routes.push_back(route);
        }
        mgr.SetClientInputRouting(ctx->client_id, routes);

        // Reconfigure driver
        std::vector<int> input_channels(ctx->num_channels);
        std::iota(input_channels.begin(), input_channels.end(), 0);
        mgr.ConfigureChannels(input_channels, {}, ctx->buffer_size, ctx->sample_rate);
    }
}

static void* asio_source_create(obs_data_t* settings, obs_source_t* source) {
    ASIOSourceContext* ctx = new ASIOSourceContext();
    ctx->source = source;

    // Allocate planar buffers
    ctx->buffer_storage.resize(8);  // Max 8 channels
    ctx->planar_buffers.resize(8);
    for (int i = 0; i < 8; ++i) {
        ctx->buffer_storage[i].resize(8192);  // Max buffer size
        ctx->planar_buffers[i] = ctx->buffer_storage[i].data();
    }

    // Register with driver manager
    auto& mgr = ASIO_DriverManager::Instance();
    ctx->client_id = mgr.AcquireClient();

    // Set audio callback
    mgr.SetAudioCallback(ctx->client_id, [ctx](float** input_planar, float** output_planar,
                                                  int num_frames, int num_input_channels,
                                                  int num_output_channels, double timestamp) {
        // Copy input to our planar buffers (up to num_channels)
        int channels_to_copy = std::min(num_input_channels, ctx->num_channels);
        for (int ch = 0; ch < channels_to_copy; ++ch) {
            if (input_planar[ch] && ctx->planar_buffers[ch]) {
                memcpy(ctx->planar_buffers[ch], input_planar[ch],
                       num_frames * sizeof(float));
            }
        }
        ctx->frames_delivered += num_frames;
    });

    // Apply settings
    asio_source_update(ctx, settings);

    // Start driver if not already running
    if (!mgr.IsRunning()) {
        mgr.Start();
    }

    return ctx;
}

static void asio_source_destroy(void* data) {
    ASIOSourceContext* ctx = static_cast<ASIOSourceContext*>(data);

    if (ctx->client_id >= 0) {
        auto& mgr = ASIO_DriverManager::Instance();
        mgr.ReleaseClient(ctx->client_id);
        ctx->client_id = -1;
    }

    delete ctx;
}

static void asio_source_audio_render(void* data, uint64_t* ts_out,
                                      struct obs_source_audio_mix* audio_output,
                                      uint32_t mixers, size_t channels,
                                      size_t sample_rate) {
    ASIOSourceContext* ctx = static_cast<ASIOSourceContext*>(data);

    // This is called from the audio thread - must be real-time safe!
    // Data is already in our buffers via the ASIO callback
    // Just copy to OBS output

    UNUSED_PARAMETER(ts_out);
    UNUSED_PARAMETER(mixers);
    UNUSED_PARAMETER(channels);
    UNUSED_PARAMETER(sample_rate);

    // OBS expects audio_output->output[0..5] to be filled with planar float32
    // We only fill the first num_channels
    for (int ch = 0; ch < ctx->num_channels && ch < 6; ++ch) {
        if (ctx->planar_buffers[ch]) {
            audio_output->output[ch] = ctx->planar_buffers[ch];
        }
    }
}

static bool asio_source_open_control_panel(void* data) {
    ASIOSourceContext* ctx = static_cast<ASIOSourceContext*>(data);
    if (!ctx->driver_clsid.empty()) {
        auto& mgr = ASIO_DriverManager::Instance();
        // Would need to call ASIOControlPanel on the driver
        // For now, return false
    }
    return false;
}

static obs_properties_t* asio_source_properties(void* unused) {
    obs_properties_t* props = obs_properties_create();

    // Driver selection
    auto& mgr = ASIO_DriverManager::Instance();
    auto drivers = mgr.DiscoverDrivers();

    obs_property_t* driver_list = obs_properties_add_list(props, "driver_clsid",
        "ASIO Driver", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(driver_list, "Select ASIO Driver...", "");
    for (const auto& d : drivers) {
        obs_property_list_add_string(driver_list, d.name.c_str(), d.clsid.c_str());
    }

    // Sample rate
    obs_properties_add_int(props, "sample_rate", "Sample Rate", 44100, 192000, 1);

    // Buffer size
    obs_properties_add_int(props, "buffer_size", "Buffer Size (frames)", 64, 4096, 64);

    // Channel count
    obs_properties_add_int(props, "channels", "Channels", 1, 64, 1);

    // Channel configuration (comma-separated ASIO channel indices)
    obs_properties_add_text(props, "channel_config", "ASIO Channel Mapping (comma-separated, e.g. 0,1,2,3)",
        OBS_TEXT_DEFAULT);

    // Control panel button
    obs_properties_add_button(props, "open_control_panel", "Open ASIO Control Panel",
        [](obs_properties_t*, obs_property_t*, void* data) {
            return asio_source_open_control_panel(data) ? true : false;
        });

    return props;
}

static uint32_t asio_source_get_mixers(void* unused) {
    return 0xFFFFFFFF;  // All mixers
}

} // namespace win_asio

// OBS plugin registration
struct obs_source_info asio_source_info = {
    .id = "asio_input_capture",
    .type = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_ASYNC,
    .get_name = win_asio::asio_source_get_name,
    .create = win_asio::asio_source_create,
    .destroy = win_asio::asio_source_destroy,
    .update = win_asio::asio_source_update,
    .get_properties = win_asio::asio_source_properties,
    .get_defaults = win_asio::asio_source_get_defaults,
    .audio_render = win_asio::asio_source_audio_render,
    .get_mixers = win_asio::asio_source_get_mixers,
};

// Main plugin entry
OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("win-asio", "en-US")

bool obs_module_load(void) {
    // Initialize COM for ASIO
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        blog(LOG_ERROR, "[win-asio] Failed to initialize COM: 0x%08X", hr);
        return false;
    }

    // Register source
    obs_register_source(&asio_source_info);

    blog(LOG_INFO, "[win-asio] Module loaded successfully");
    return true;
}

void obs_module_unload(void) {
    // Clean up driver manager
    auto& mgr = win_asio::ASIO_DriverManager::Instance();
    mgr.CloseDriver();

    CoUninitialize();
    blog(LOG_INFO, "[win-asio] Module unloaded");
}