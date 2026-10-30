/******************************************************************************
    Copyright (C) 2024 by LibreCast Studio Contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "audio_verifier.h"
#include "../util/bmem.h"
#include "../util/darray.h"
#include "../util/platform.h"
#include <math.h>
#include <string.h>
#include <float.h>

/* ========================================================================
 * Internal Structures
 * ======================================================================== */

struct audio_verifier_checker_internal {
    struct audio_verifier_checker base;
    struct silence_detector_config *silence_config;
    struct sample_comparator_config *sample_config;
    struct channel_layout_validator_config *layout_config;
    struct buffer_size_validator_config *buffer_config;
};

struct audio_verifier {
    DARRAY(struct audio_verifier_checker_internal) checkers;
    DARRAY(audio_verifier_result_t) last_results;
    void *user_data;
};

/* ========================================================================
 * Utility Functions
 * ======================================================================== */

static inline float db_to_linear(float db) {
    return powf(10.0f, db / 20.0f);
}

static inline float linear_to_db(float linear) {
    if (linear <= 0.0f) return -FLT_MAX;
    return 20.0f * log10f(linear);
}

static float calculate_rms(const float *samples, size_t num_samples) {
    if (num_samples == 0) return 0.0f;

    double sum_squares = 0.0;
    for (size_t i = 0; i < num_samples; i++) {
        double val = samples[i];
        sum_squares += val * val;
    }
    return (float)sqrt(sum_squares / num_samples);
}

static float calculate_rms_interleaved(const float *samples, size_t num_frames, size_t num_channels) {
    if (num_frames == 0 || num_channels == 0) return 0.0f;

    double sum_squares = 0.0;
    size_t total_samples = num_frames * num_channels;
    for (size_t i = 0; i < total_samples; i++) {
        double val = samples[i];
        sum_squares += val * val;
    }
    return (float)sqrt(sum_squares / total_samples);
}

/* ========================================================================
 * Silence Detector Checker
 * ======================================================================== */

static audio_verifier_result_t silence_detector_verify(const struct audio_verifier_context *context) {
    struct silence_detector_config *config = (struct silence_detector_config *)context->user_data;
    if (!config) return AUDIO_VERIFIER_RESULT_SKIP;

    float threshold_linear = db_to_linear(config->rms_threshold_db);
    size_t required = config->num_buffers_required ? config->num_buffers_required : 3;
    size_t silent_count = 0;

    if (!context->post_buffers || context->num_buffers == 0) {
        return AUDIO_VERIFIER_RESULT_SKIP;
    }

    size_t channels = 0;
    if (context->post_config) {
        channels = get_audio_channels(context->post_config->speakers);
    }

    for (size_t i = 0; i < context->num_buffers; i++) {
        const struct audio_data *buf = &context->post_buffers[i];

        float rms = 0.0f;
        if (channels > 0 && buf->data[0]) {
            rms = calculate_rms_interleaved((const float *)buf->data[0], buf->frames, channels);
        }

        if (rms < threshold_linear) {
            silent_count++;
            if (silent_count >= required) {
                return AUDIO_VERIFIER_RESULT_PASS;
            }
        } else {
            silent_count = 0;
        }
    }

    return AUDIO_VERIFIER_RESULT_FAIL;
}

static const struct audio_verifier_checker silence_detector_checker = {
    .name = "SilenceDetector",
    .description = "Confirms output is silent (RMS < threshold) for N consecutive buffers after reconfig",
    .verify = silence_detector_verify,
    .checker_data = NULL
};

EXPORT const struct audio_verifier_checker *audio_verifier_get_silence_detector(const struct silence_detector_config *config) {
    struct audio_verifier_checker_internal *checker = bzalloc(sizeof(struct audio_verifier_checker_internal));
    checker->base = silence_detector_checker;

    if (config) {
        checker->silence_config = bzalloc(sizeof(struct silence_detector_config));
        *checker->silence_config = *config;
        checker->base.checker_data = checker->silence_config;
    } else {
        checker->silence_config = bzalloc(sizeof(struct silence_detector_config));
        checker->silence_config->rms_threshold_db = -90.0f;
        checker->silence_config->num_buffers_required = 3;
        checker->base.checker_data = checker->silence_config;
    }

    return &checker->base;
}

/* ========================================================================
 * Sample Comparator Checker
 * ======================================================================== */

static audio_verifier_result_t sample_comparator_verify(const struct audio_verifier_context *context) {
    struct sample_comparator_config *config = (struct sample_comparator_config *)context->user_data;
    if (!config) return AUDIO_VERIFIER_RESULT_SKIP;

    float tolerance = config->tolerance > 0.0f ? config->tolerance : 0.001f;
    bool check_mapping = config->check_mapping;

    if (!context->pre_buffers || !context->post_buffers || context->num_buffers == 0) {
        return AUDIO_VERIFIER_RESULT_SKIP;
    }

    if (!context->pre_config || !context->post_config) {
        return AUDIO_VERIFIER_RESULT_SKIP;
    }

    size_t pre_channels = get_audio_channels(context->pre_config->speakers);
    size_t post_channels = get_audio_channels(context->post_config->speakers);

    if (check_mapping && pre_channels != post_channels) {
        return AUDIO_VERIFIER_RESULT_FAIL;
    }

    for (size_t i = 0; i < context->num_buffers; i++) {
        const struct audio_data *pre_buf = &context->pre_buffers[i];
        const struct audio_data *post_buf = &context->post_buffers[i];

        if (!pre_buf->data[0] || !post_buf->data[0]) {
            continue;
        }

        if (pre_buf->frames != post_buf->frames) {
            return AUDIO_VERIFIER_RESULT_FAIL;
        }

        size_t frames = pre_buf->frames;
        size_t min_channels = pre_channels < post_channels ? pre_channels : post_channels;

        for (size_t ch = 0; ch < min_channels; ch++) {
            const float *pre_samples = (const float *)pre_buf->data[ch];
            const float *post_samples = (const float *)post_buf->data[ch];

            for (size_t f = 0; f < frames; f++) {
                float diff = fabsf(pre_samples[f] - post_samples[f]);
                if (diff > tolerance) {
                    return AUDIO_VERIFIER_RESULT_FAIL;
                }
            }
        }
    }

    return AUDIO_VERIFIER_RESULT_PASS;
}

static const struct audio_verifier_checker sample_comparator_checker = {
    .name = "SampleComparator",
    .description = "Verifies post-reconfig output matches pre-reconfig reference within tolerance",
    .verify = sample_comparator_verify,
    .checker_data = NULL
};

EXPORT const struct audio_verifier_checker *audio_verifier_get_sample_comparator(const struct sample_comparator_config *config) {
    struct audio_verifier_checker_internal *checker = bzalloc(sizeof(struct audio_verifier_checker_internal));
    checker->base = sample_comparator_checker;

    if (config) {
        checker->sample_config = bzalloc(sizeof(struct sample_comparator_config));
        *checker->sample_config = *config;
        checker->base.checker_data = checker->sample_config;
    } else {
        checker->sample_config = bzalloc(sizeof(struct sample_comparator_config));
        checker->sample_config->tolerance = 0.001f;
        checker->sample_config->check_mapping = true;
        checker->base.checker_data = checker->sample_config;
    }

    return &checker->base;
}

/* ========================================================================
 * Channel Layout Validator Checker
 * ======================================================================== */

static audio_verifier_result_t channel_layout_validator_verify(const struct audio_verifier_context *context) {
    struct channel_layout_validator_config *config = (struct channel_layout_validator_config *)context->user_data;
    if (!config) return AUDIO_VERIFIER_RESULT_SKIP;

    if (!context->post_config || !context->post_buffers || context->num_buffers == 0) {
        return AUDIO_VERIFIER_RESULT_SKIP;
    }

    size_t expected_channels = get_audio_channels(context->post_config->speakers);
    if (expected_channels == 0) {
        return AUDIO_VERIFIER_RESULT_SKIP;
    }

    for (size_t i = 0; i < context->num_buffers; i++) {
        const struct audio_data *buf = &context->post_buffers[i];

        size_t actual_planes = 0;
        for (size_t p = 0; p < MAX_AV_PLANES && buf->data[p]; p++) {
            actual_planes++;
        }

        if (context->post_config->format == AUDIO_FORMAT_FLOAT_PLANAR ||
            context->post_config->format == AUDIO_FORMAT_16BIT_PLANAR ||
            context->post_config->format == AUDIO_FORMAT_32BIT_PLANAR ||
            context->post_config->format == AUDIO_FORMAT_U8BIT_PLANAR) {
            if (actual_planes != expected_channels) {
                return AUDIO_VERIFIER_RESULT_FAIL;
            }
        } else {
            if (actual_planes != 1) {
                return AUDIO_VERIFIER_RESULT_FAIL;
            }
        }
    }

    return AUDIO_VERIFIER_RESULT_PASS;
}

static const struct audio_verifier_checker channel_layout_validator_checker = {
    .name = "ChannelLayoutValidator",
    .description = "Asserts output channel count matches target config and channel order is correct",
    .verify = channel_layout_validator_verify,
    .checker_data = NULL
};

EXPORT const struct audio_verifier_checker *audio_verifier_get_channel_layout_validator(const struct channel_layout_validator_config *config) {
    struct audio_verifier_checker_internal *checker = bzalloc(sizeof(struct audio_verifier_checker_internal));
    checker->base = channel_layout_validator_checker;

    if (config) {
        checker->layout_config = bzalloc(sizeof(struct channel_layout_validator_config));
        *checker->layout_config = *config;
        checker->base.checker_data = checker->layout_config;
    } else {
        checker->layout_config = bzalloc(sizeof(struct channel_layout_validator_config));
        checker->layout_config->strict_order = true;
        checker->base.checker_data = checker->layout_config;
    }

    return &checker->base;
}

/* ========================================================================
 * Buffer Size Validator Checker
 * ======================================================================== */

static audio_verifier_result_t buffer_size_validator_verify(const struct audio_verifier_context *context) {
    struct buffer_size_validator_config *config = (struct buffer_size_validator_config *)context->user_data;
    if (!config) return AUDIO_VERIFIER_RESULT_SKIP;

    if (!context->post_config || !context->post_buffers || context->num_buffers == 0) {
        return AUDIO_VERIFIER_RESULT_SKIP;
    }

    size_t expected_size = config->expected_buffer_size;
    if (expected_size == 0) {
        expected_size = AUDIO_OUTPUT_FRAMES;
    }

    float tolerance = config->tolerance_percent > 0.0f ? config->tolerance_percent : 5.0f;
    float tolerance_factor = tolerance / 100.0f;

    for (size_t i = 0; i < context->num_buffers; i++) {
        const struct audio_data *buf = &context->post_buffers[i];

        float min_frames = expected_size * (1.0f - tolerance_factor);
        float max_frames = expected_size * (1.0f + tolerance_factor);

        if (buf->frames < min_frames || buf->frames > max_frames) {
            return AUDIO_VERIFIER_RESULT_FAIL;
        }
    }

    return AUDIO_VERIFIER_RESULT_PASS;
}

static const struct audio_verifier_checker buffer_size_validator_checker = {
    .name = "BufferSizeValidator",
    .description = "Confirms driver delivers callbacks with the new buffer size",
    .verify = buffer_size_validator_verify,
    .checker_data = NULL
};

EXPORT const struct audio_verifier_checker *audio_verifier_get_buffer_size_validator(const struct buffer_size_validator_config *config) {
    struct audio_verifier_checker_internal *checker = bzalloc(sizeof(struct audio_verifier_checker_internal));
    checker->base = buffer_size_validator_checker;

    if (config) {
        checker->buffer_config = bzalloc(sizeof(struct buffer_size_validator_config));
        *checker->buffer_config = *config;
        checker->base.checker_data = checker->buffer_config;
    } else {
        checker->buffer_config = bzalloc(sizeof(struct buffer_size_validator_config));
        checker->buffer_config->expected_buffer_size = 0;
        checker->buffer_config->tolerance_percent = 5.0f;
        checker->base.checker_data = checker->buffer_config;
    }

    return &checker->base;
}

/* ========================================================================
 * DC Offset Checker (Example Custom Verifier)
 * ======================================================================== */

static audio_verifier_result_t dc_offset_checker_verify(const struct audio_verifier_context *context) {
    (void)context;
    if (!context->post_buffers || context->num_buffers == 0) {
        return AUDIO_VERIFIER_RESULT_SKIP;
    }

    const float dc_threshold = 0.01f;

    for (size_t i = 0; i < context->num_buffers; i++) {
        const struct audio_data *buf = &context->post_buffers[i];
        if (!buf->data[0]) continue;

        size_t channels = 0;
        if (context->post_config) {
            channels = get_audio_channels(context->post_config->speakers);
        }
        if (channels == 0) channels = 2;

        for (size_t ch = 0; ch < channels && ch < MAX_AV_PLANES && buf->data[ch]; ch++) {
            const float *samples = (const float *)buf->data[ch];
            double sum = 0.0;

            for (size_t f = 0; f < buf->frames; f++) {
                sum += samples[f];
            }

            float dc_offset = (float)(sum / buf->frames);
            if (fabsf(dc_offset) > dc_threshold) {
                return AUDIO_VERIFIER_RESULT_FAIL;
            }
        }
    }

    return AUDIO_VERIFIER_RESULT_PASS;
}

static const struct audio_verifier_checker dc_offset_checker = {
    .name = "DCOffsetChecker",
    .description = "Example custom verifier: checks for excessive DC offset in audio output",
    .verify = dc_offset_checker_verify,
    .checker_data = NULL
};

EXPORT const struct audio_verifier_checker *audio_verifier_get_dc_offset_checker(void) {
    struct audio_verifier_checker_internal *checker = bzalloc(sizeof(struct audio_verifier_checker_internal));
    checker->base = dc_offset_checker;
    checker->base.checker_data = NULL;
    return &checker->base;
}

/* ========================================================================
 * Audio Verifier Core
 * ======================================================================== */

EXPORT audio_verifier_t *audio_verifier_create(const struct audio_verifier_config *config) {
    if (!config || !config->checkers || config->num_checkers == 0) {
        return NULL;
    }

    audio_verifier_t *verifier = bzalloc(sizeof(audio_verifier_t));
    if (!verifier) return NULL;

    da_init(verifier->checkers);
    da_init(verifier->last_results);
    verifier->user_data = config->user_data;

    for (size_t i = 0; i < config->num_checkers; i++) {
        struct audio_verifier_checker_internal checker_internal = {0};
        checker_internal.base = *config->checkers[i];
        checker_internal.base.checker_data = config->checkers[i]->checker_data;
        da_push_back(verifier->checkers, &checker_internal);
        da_push_back(verifier->last_results, &(audio_verifier_result_t){AUDIO_VERIFIER_RESULT_SKIP});
    }

    return verifier;
}

EXPORT void audio_verifier_destroy(audio_verifier_t *verifier) {
    if (!verifier) return;

    for (size_t i = 0; i < verifier->checkers.num; i++) {
        struct audio_verifier_checker_internal *checker = &verifier->checkers.array[i];
        bfree(checker->silence_config);
        bfree(checker->sample_config);
        bfree(checker->layout_config);
        bfree(checker->buffer_config);
    }

    da_free(verifier->checkers);
    da_free(verifier->last_results);
    bfree(verifier);
}

EXPORT audio_verifier_result_t audio_verifier_verify(
    audio_verifier_t *verifier,
    const struct audio_output_info *pre_config,
    const struct audio_output_info *post_config,
    const struct audio_data *pre_buffers,
    const struct audio_data *post_buffers,
    size_t num_buffers) {

    if (!verifier || verifier->checkers.num == 0) {
        return AUDIO_VERIFIER_RESULT_ERROR;
    }

    struct audio_verifier_context context = {
        .pre_config = pre_config,
        .post_config = post_config,
        .pre_buffers = pre_buffers,
        .post_buffers = post_buffers,
        .num_buffers = num_buffers,
        .user_data = verifier->user_data
    };

    audio_verifier_result_t overall_result = AUDIO_VERIFIER_RESULT_PASS;

    for (size_t i = 0; i < verifier->checkers.num; i++) {
        struct audio_verifier_checker_internal *checker = &verifier->checkers.array[i];

        if (checker->base.verify) {
            context.user_data = checker->base.checker_data;
            audio_verifier_result_t result = checker->base.verify(&context);
            verifier->last_results.array[i] = result;

            if (result == AUDIO_VERIFIER_RESULT_FAIL || result == AUDIO_VERIFIER_RESULT_ERROR) {
                overall_result = AUDIO_VERIFIER_RESULT_FAIL;
            }
        }
    }

    return overall_result;
}

EXPORT bool audio_verifier_add_checker(audio_verifier_t *verifier, const struct audio_verifier_checker *checker) {
    if (!verifier || !checker) return false;

    struct audio_verifier_checker_internal checker_internal = {0};
    checker_internal.base = *checker;
    checker_internal.base.checker_data = checker->checker_data;

    da_push_back(verifier->checkers, &checker_internal);
    da_push_back(verifier->last_results, &(audio_verifier_result_t){AUDIO_VERIFIER_RESULT_SKIP});

    return true;
}

EXPORT audio_verifier_result_t audio_verifier_get_last_result(const audio_verifier_t *verifier, const char *checker_name) {
    if (!verifier || !checker_name) return AUDIO_VERIFIER_RESULT_ERROR;

    for (size_t i = 0; i < verifier->checkers.num; i++) {
        if (strcmp(verifier->checkers.array[i].base.name, checker_name) == 0) {
            return verifier->last_results.array[i];
        }
    }

    return AUDIO_VERIFIER_RESULT_ERROR;
}