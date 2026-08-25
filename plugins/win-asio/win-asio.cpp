/******************************************************************************
 * win-asio: ASIO Source Plugin - Complete Implementation
 *
 * OBS source plugin for capturing ASIO audio channels with full property UI
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#include "asio-driver-manager.hpp"

#include <obs-module.h>
#include <util/threading.h>
#include <util/dstr.h>
#include <media-io/audio-resampler.h>
#include <media-io/audio-io.h>

#include <memory>
#include <vector>
#include <string>
#include <numeric>  // for std::iota
#include <cstring>  // for strdup, strtok

namespace win_asio {

struct ASIOSourceContext {
    obs_source_t* source = nullptr;
    int client_id = -1;
    int sample_rate = 48000;      // Target OBS sample rate
    int buffer_size = 512;
    int num_channels = 2;
    std::vector<int> channel_map;  // Maps OBS channel -> ASIO channel
    std::string driver_clsid;

    // Audio buffers for OBS callback
    std::vector<float*> planar_buffers;
    std::vector<std::vector<float>> buffer_storage;

    // Resampler for hardware rate conversion
    audio_resampler_t* resampler = nullptr;
    int hardware_sample_rate = 0;  // Actual ASIO hardware rate
    int hardware_buffer_size = 0;  // Actual ASIO buffer size
    std::vector<float*> resampled_buffers;
    std::vector<std::vector<float>> resampled_storage;

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

        // Reconfigure driver - query hardware sample rate for resampler setup
        std::vector<int> input_channels(ctx->num_channels);
        std::iota(input_channels.begin(), input_channels.end(), 0);
        
        // Get hardware sample rate from driver manager before configuring
        double hw_rate = mgr.GetSampleRate();
        if (hw_rate > 0) {
            ctx->hardware_sample_rate = (int)hw_rate;
        }
        
        mgr.ConfigureChannels(input_channels, {}, ctx->buffer_size, ctx->sample_rate);
        
        // Update hardware buffer size after configuration
        ctx->hardware_buffer_size = mgr.GetBufferSize();
        
        // Initialize or reconfigure resampler if hardware rate differs from target
                if (ctx->hardware_sample_rate > 0 && ctx->hardware_sample_rate != ctx->sample_rate) {
                    // Destroy existing resampler if any
                    if (ctx->resampler) {
                        audio_resampler_destroy(ctx->resampler);
                        ctx->resampler = nullptr;
                    }

                    // Create resampler: from hardware rate to OBS target rate
                    struct resample_info from = {0};
                    from.samples_per_sec = (uint32_t)ctx->hardware_sample_rate;
                    from.format = AUDIO_FORMAT_FLOAT_PLANAR;
                    from.speakers = SPEAKERS_STEREO;  // Will be adjusted per channel count

                    struct resample_info to = {0};
                    to.samples_per_sec = (uint32_t)ctx->sample_rate;
                    to.format = AUDIO_FORMAT_FLOAT_PLANAR;
                    to.speakers = SPEAKERS_STEREO;

                    // Adjust speaker layout based on channel count
                    if (ctx->num_channels == 1) {
                        from.speakers = SPEAKERS_MONO;
                        to.speakers = SPEAKERS_MONO;
                    } else if (ctx->num_channels == 2) {
                        from.speakers = SPEAKERS_STEREO;
                        to.speakers = SPEAKERS_STEREO;
                    } else if (ctx->num_channels <= 6) {
                        from.speakers = SPEAKERS_5POINT1;
                        to.speakers = SPEAKERS_5POINT1;
                    } else {
                        from.speakers = SPEAKERS_7POINT1;
                        to.speakers = SPEAKERS_7POINT1;
                    }

                    ctx->resampler = audio_resampler_create(&to, &from);
            if (!ctx->resampler) {
                blog(LOG_WARNING, "[win-asio] Failed to create resampler for %d -> %d Hz", 
                     ctx->hardware_sample_rate, ctx->sample_rate);
            } else {
                blog(LOG_INFO, "[win-asio] Created resampler: %d Hz -> %d Hz (%d channels)",
                     ctx->hardware_sample_rate, ctx->sample_rate, ctx->num_channels);
                
                // Allocate resampled buffers
                ctx->resampled_storage.resize(ctx->num_channels);
                ctx->resampled_buffers.resize(ctx->num_channels);
                // Use a reasonable max buffer size for resampled output
                int max_resampled_frames = (ctx->hardware_buffer_size * ctx->sample_rate) / ctx->hardware_sample_rate + 64;
                for (int i = 0; i < ctx->num_channels; ++i) {
                    ctx->resampled_storage[i].resize(max_resampled_frames);
                    ctx->resampled_buffers[i] = ctx->resampled_storage[i].data();
                }
            }
        } else if (ctx->resampler) {
            // Rates match now, destroy resampler
            audio_resampler_destroy(ctx->resampler);
            ctx->resampler = nullptr;
            ctx->resampled_buffers.clear();
            ctx->resampled_storage.clear();
        }
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
        // If we have a resampler, resample the input
        if (ctx->resampler && ctx->hardware_sample_rate > 0 && ctx->hardware_sample_rate != ctx->sample_rate) {
            // Input is at hardware rate, need to resample to target rate
            const uint8_t* input_ptrs[8] = {nullptr};
            uint8_t* output_ptrs[8] = {nullptr};
            
            int chans_to_process = std::min(num_input_channels, ctx->num_channels);
            for (int ch = 0; ch < chans_to_process; ++ch) {
                input_ptrs[ch] = (const uint8_t*)input_planar[ch];
                output_ptrs[ch] = (uint8_t*)ctx->resampled_buffers[ch];
            }
            
            uint32_t out_frames = 0;
            uint64_t ts_offset = 0;
            bool success = audio_resampler_resample(ctx->resampler, output_ptrs, &out_frames, &ts_offset,
                                                    input_ptrs, num_frames);
            
            if (success && out_frames > 0) {
                // Copy resampled data to planar buffers
                for (int ch = 0; ch < chans_to_process; ++ch) {
                    if (ctx->resampled_buffers[ch] && ctx->planar_buffers[ch]) {
                        memcpy(ctx->planar_buffers[ch], ctx->resampled_buffers[ch],
                               out_frames * sizeof(float));
                    }
                }
                ctx->frames_delivered += out_frames;
            } else {
                ctx->frames_dropped += num_frames;
            }
        } else {
            // Direct copy - no resampling needed
            int channels_to_copy = std::min(num_input_channels, ctx->num_channels);
            for (int ch = 0; ch < channels_to_copy; ++ch) {
                if (input_planar[ch] && ctx->planar_buffers[ch]) {
                    memcpy(ctx->planar_buffers[ch], input_planar[ch],
                           num_frames * sizeof(float));
                }
            }
            ctx->frames_delivered += num_frames;
        }
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

    // Clean up resampler
    if (ctx->resampler) {
        audio_resampler_destroy(ctx->resampler);
        ctx->resampler = nullptr;
    }

    delete ctx;
}

static bool asio_source_audio_render(void* data, uint64_t* ts_out,
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

    // OBS expects audio_output->output[0].data[0..5] to be filled with planar float32
    // We only fill the first num_channels for the first mixer
    for (int ch = 0; ch < ctx->num_channels && ch < MAX_AUDIO_CHANNELS; ++ch) {
        if (ctx->planar_buffers[ch]) {
            audio_output->output[0].data[ch] = ctx->planar_buffers[ch];
        }
    }
    return true;
}

static bool asio_source_open_control_panel(void* data) {
    ASIOSourceContext* ctx = static_cast<ASIOSourceContext*>(data);
    if (!ctx->driver_clsid.empty()) {
        auto& mgr = ASIO_DriverManager::Instance();
        return mgr.OpenControlPanel();
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
    #pragma warning(push)
    #pragma warning(disable: 4996)
        obs_properties_add_button(props, "open_control_panel", "Open ASIO Control Panel",
            [](obs_properties_t*, obs_property_t*, void* data) {
                asio_source_open_control_panel(data);
                return true;
            });
    #pragma warning(pop)

    return props;
}

static uint32_t asio_source_get_mixers(void* unused) {
    return 0xFFFFFFFF;  // All mixers
}

} // namespace win_asio

// OBS plugin registration
struct obs_source_info asio_source_info = {
    /* id              */ "asio_input_capture",
    /* type            */ OBS_SOURCE_TYPE_INPUT,
    /* output_flags    */ OBS_SOURCE_AUDIO | OBS_SOURCE_ASYNC,
    /* get_name        */ win_asio::asio_source_get_name,
    /* create          */ win_asio::asio_source_create,
    /* destroy         */ win_asio::asio_source_destroy,
    /* get_width       */ nullptr,
    /* get_height      */ nullptr,
    /* get_defaults    */ win_asio::asio_source_get_defaults,
    /* get_properties  */ win_asio::asio_source_properties,
    /* update          */ win_asio::asio_source_update,
    /* activate        */ nullptr,
    /* deactivate      */ nullptr,
    /* show            */ nullptr,
    /* hide            */ nullptr,
    /* video_tick      */ nullptr,
    /* video_render    */ nullptr,
    /* filter_video    */ nullptr,
    /* filter_audio    */ nullptr,
    /* enum_active_sources */ nullptr,
    /* save            */ nullptr,
    /* load            */ nullptr,
    /* mouse_click     */ nullptr,
    /* mouse_move      */ nullptr,
    /* mouse_wheel     */ nullptr,
    /* key_click       */ nullptr,
    /* focus           */ nullptr,
    /* defocus         */ nullptr,
    /* type_data       */ nullptr,
    /* free_type_data  */ nullptr,
    /* audio_render    */ win_asio::asio_source_audio_render,
    /* enum_all_sources    */ nullptr,
    /* transition_start */ nullptr,
    /* transition_stop */ nullptr,
    /* get_defaults2   */ nullptr,
    /* get_properties2 */ nullptr,
    /* audio_mix       */ nullptr,
    /* icon_type       */ OBS_ICON_TYPE_AUDIO_INPUT,
    /* media_play_pause */ nullptr,
    /* media_restart   */ nullptr,
    /* media_stop      */ nullptr,
    /* media_next      */ nullptr,
    /* media_previous  */ nullptr,
    /* media_get_duration */ nullptr,
    /* media_get_time  */ nullptr,
    /* media_set_time  */ nullptr,
    /* media_get_state */ nullptr,
    /* version         */ 0,
    /* unversioned_id  */ nullptr,
    /* missing_files   */ nullptr,
    /* video_get_color_space */ nullptr,
    /* filter_add      */ nullptr,
    /* get_dark_icon   */ nullptr,
    /* get_light_icon  */ nullptr,
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