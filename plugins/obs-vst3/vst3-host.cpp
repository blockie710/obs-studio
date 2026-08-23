/******************************************************************************
 * obs-vst3: VST3 Host Filter Implementation
 *
 * OBS filter plugin that hosts VST3 plugins
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#include "vst3-host.hpp"
#include "vst3-plugin.hpp"

#include <obs-module.h>
#include <util/platform.h>
#include <util/dstr.h>

#include <cmath>
#include <chrono>

namespace obs_vst3 {

static const char* vst3_filter_get_name(void* unused) {
    return "VST3 Plugin Host";
}

static void vst3_filter_defaults(obs_data_t* settings) {
    obs_data_set_default_string(settings, "plugin_path", "");
    obs_data_set_default_bool(settings, "enabled", true);
    obs_data_set_default_int(settings, "channels", 2);
}

static void vst3_filter_update(void* data, obs_data_t* settings) {
    VST3HostContext* ctx = static_cast<VST3HostContext*>(data);

    const char* plugin_path = obs_data_get_string(settings, "plugin_path");
    ctx->enabled = obs_data_get_bool(settings, "enabled");
    ctx->num_channels = (int)obs_data_get_int(settings, "channels");

    if (ctx->plugin) {
        if (plugin_path && *plugin_path && ctx->plugin_path != plugin_path) {
            // Load new plugin
            ctx->plugin->Unload();
            if (ctx->plugin->Load(plugin_path)) {
                ctx->plugin_path = plugin_path;
                ctx->plugin->Prepare(ctx->sample_rate, ctx->buffer_size, ctx->num_channels, ctx->num_channels);
            }
        }
    } else if (plugin_path && *plugin_path) {
        // First load
        ctx->plugin = std::make_unique<VST3Plugin>();
        if (ctx->plugin->Load(plugin_path)) {
            ctx->plugin_path = plugin_path;
            ctx->plugin->Prepare(ctx->sample_rate, ctx->buffer_size, ctx->num_channels, ctx->num_channels);
        }
    }

    // Reallocate buffers
    ctx->input_buffers.resize(ctx->num_channels);
    ctx->output_buffers.resize(ctx->num_channels);
    ctx->input_ptrs.resize(ctx->num_channels);
    ctx->output_ptrs.resize(ctx->num_channels);

    for (int i = 0; i < ctx->num_channels; ++i) {
        ctx->input_buffers[i].resize(ctx->buffer_size);
        ctx->output_buffers[i].resize(ctx->buffer_size);
        ctx->input_ptrs[i] = ctx->input_buffers[i].data();
        ctx->output_ptrs[i] = ctx->output_buffers[i].data();
    }
}

static void* vst3_filter_create(obs_data_t* settings, obs_source_t* source) {
    VST3HostContext* ctx = new VST3HostContext();
    ctx->source = source;

    // Get audio info
    struct obs_audio_info ainfo;
    obs_get_audio_info(&ainfo);
    ctx->sample_rate = ainfo.samples_per_sec;
    ctx->buffer_size = 1024; // Default, will be updated

    // Initial setup
    vst3_filter_update(ctx, settings);

    return ctx;
}

static void vst3_filter_destroy(void* data) {
    VST3HostContext* ctx = static_cast<VST3HostContext*>(data);

    if (ctx->plugin) {
        ctx->plugin->Unload();
    }

    delete ctx;
}

static void vst3_filter_audio_render(void* data, obs_source_t* source,
                                      const struct obs_source_audio* audio,
                                      struct obs_audio_data* output) {
    VST3HostContext* ctx = static_cast<VST3HostContext*>(data);

    UNUSED_PARAMETER(source);

    if (!ctx->enabled || !ctx->plugin || !ctx->plugin->IsLoaded()) {
        // Pass through
        for (size_t i = 0; i < audio->frames; ++i) {
            for (int ch = 0; ch < ctx->num_channels; ++ch) {
                if (ch < MAX_AUDIO_CHANNELS && audio->data[ch]) {
                    output->data[ch] = audio->data[ch];
                }
            }
        }
        return;
    }

    // Copy input to planar buffers
    size_t frames = audio->frames;
    for (int ch = 0; ch < ctx->num_channels && ch < MAX_AUDIO_CHANNELS; ++ch) {
        if (audio->data[ch]) {
            memcpy(ctx->input_ptrs[ch], audio->data[ch], frames * sizeof(float));
        } else {
            memset(ctx->input_ptrs[ch], 0, frames * sizeof(float));
        }
    }

    // Process through VST3 plugin
    auto start = std::chrono::high_resolution_clock::now();
    ctx->plugin->Process(ctx->input_ptrs.data(), ctx->output_ptrs.data(), (int)frames);
    auto end = std::chrono::high_resolution_clock::now();

    ctx->cpu_usage = std::chrono::duration<double, std::milli>(end - start).count();
    ctx->frames_processed += frames;

    // Copy output
    for (int ch = 0; ch < ctx->num_channels && ch < MAX_AUDIO_CHANNELS; ++ch) {
        output->data[ch] = ctx->output_ptrs[ch];
    }
}

static obs_properties_t* vst3_filter_properties(void* unused) {
    obs_properties_t* props = obs_properties_create();

    // Plugin path selection
    obs_properties_add_path(props, "plugin_path", "VST3 Plugin",
        OBS_PATH_FILE, "VST3 Plugins (*.vst3);;All Files (*.*)", "");

    // Enable toggle
    obs_properties_add_bool(props, "enabled", "Enabled");

    // Channel count
    obs_properties_add_int(props, "channels", "Channels", 1, 64, 1);

    return props;
}

// OBS filter plugin registration
struct obs_source_info vst3_filter_info = {
    .id = "vst3_filter",
    .type = OBS_SOURCE_TYPE_FILTER,
    .output_flags = OBS_SOURCE_AUDIO,
    .get_name = vst3_filter_get_name,
    .create = vst3_filter_create,
    .destroy = vst3_filter_destroy,
    .update = vst3_filter_update,
    .get_properties = vst3_filter_properties,
    .get_defaults = vst3_filter_defaults,
    .audio_render = vst3_filter_audio_render,
};

} // namespace obs_vst3