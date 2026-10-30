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

#pragma once

#include "audio-io.h"
#include "../util/darray.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct audio_verifier;
struct audio_verifier_checker;

/**
 * Result of a verification check
 */
typedef enum {
    AUDIO_VERIFIER_RESULT_PASS = 0,
    AUDIO_VERIFIER_RESULT_FAIL = 1,
    AUDIO_VERIFIER_RESULT_SKIP = 2,
    AUDIO_VERIFIER_RESULT_ERROR = -1
} audio_verifier_result_t;

/**
 * Verification context passed to checkers
 */
struct audio_verifier_context {
    const struct audio_output_info *pre_config;
    const struct audio_output_info *post_config;
    const struct audio_data *pre_buffers;
    const struct audio_data *post_buffers;
    size_t num_buffers;
    void *user_data;
};

/**
 * Function signature for a verification checker
 * @param context  Verification context with pre/post configs and buffers
 * @return         Result of the check
 */
typedef audio_verifier_result_t (*audio_verifier_check_fn)(const struct audio_verifier_context *context);

/**
 * Checker descriptor - implemented by each built-in checker
 */
struct audio_verifier_checker {
    const char *name;
    const char *description;
    audio_verifier_check_fn verify;
    void *checker_data;
};

/**
 * Audio verifier configuration
 */
struct audio_verifier_config {
    const struct audio_verifier_checker **checkers;
    size_t num_checkers;
    void *user_data;
};

/**
 * Audio verifier handle
 */
typedef struct audio_verifier audio_verifier_t;

/**
 * Creates a new audio verifier with the given configuration
 * @param config  Verifier configuration (checkers to use)
 * @return        Opaque verifier handle, or NULL on failure
 */
EXPORT audio_verifier_t *audio_verifier_create(const struct audio_verifier_config *config);

/**
 * Destroys an audio verifier
 * @param verifier  Verifier to destroy
 */
EXPORT void audio_verifier_destroy(audio_verifier_t *verifier);

/**
 * Runs all registered checkers against pre/post reconfiguration data
 * @param verifier      Verifier instance
 * @param pre_config    Audio configuration before reconfig
 * @param post_config   Audio configuration after reconfig
 * @param pre_buffers   Audio buffers captured before reconfig (can be NULL)
 * @param post_buffers  Audio buffers captured after reconfig
 * @param num_buffers   Number of buffers in each array
 * @return              Overall result (PASS if all checkers pass, FAIL if any fail)
 */
EXPORT audio_verifier_result_t audio_verifier_verify(
    audio_verifier_t *verifier,
    const struct audio_output_info *pre_config,
    const struct audio_output_info *post_config,
    const struct audio_data *pre_buffers,
    const struct audio_data *post_buffers,
    size_t num_buffers);

/**
 * Adds a custom checker to the verifier
 * @param verifier  Verifier instance
 * @param checker   Checker to add (must remain valid for verifier lifetime)
 * @return          True on success
 */
EXPORT bool audio_verifier_add_checker(audio_verifier_t *verifier, const struct audio_verifier_checker *checker);

/**
 * Gets the last verification result for a specific checker
 * @param verifier      Verifier instance
 * @param checker_name  Name of the checker
 * @return              Last result for that checker, or AUDIO_VERIFIER_RESULT_ERROR if not found
 */
EXPORT audio_verifier_result_t audio_verifier_get_last_result(const audio_verifier_t *verifier, const char *checker_name);

/* ========================================================================
 * Built-in Checkers
 * ======================================================================== */

/**
 * Silence Detector Configuration
 * Checks that output is silent (RMS < threshold) for N buffers after reconfig
 */
struct silence_detector_config {
    float rms_threshold_db;      /**< RMS threshold in dBFS (default: -90.0f) */
    size_t num_buffers_required; /**< Number of consecutive silent buffers required (default: 3) */
};

/**
 * Gets the built-in silence detector checker
 * @param config  Optional configuration (can be NULL for defaults)
 * @return        Checker descriptor
 */
EXPORT const struct audio_verifier_checker *audio_verifier_get_silence_detector(const struct silence_detector_config *config);

/**
 * Sample Comparator Configuration
 * Compares post-reconfig output against pre-reconfig reference buffer
 */
struct sample_comparator_config {
    float tolerance;       /**< Sample value tolerance (default: 0.001f) */
    bool check_mapping;    /**< Verify channel mapping is correct (default: true) */
};

/**
 * Gets the built-in sample comparator checker
 * @param config  Optional configuration (can be NULL for defaults)
 * @return        Checker descriptor
 */
EXPORT const struct audio_verifier_checker *audio_verifier_get_sample_comparator(const struct sample_comparator_config *config);

/**
 * Channel Layout Validator Configuration
 * Validates output channel count and order match target configuration
 */
struct channel_layout_validator_config {
    bool strict_order;  /**< Require exact channel order match (default: true) */
};

/**
 * Gets the built-in channel layout validator checker
 * @param config  Optional configuration (can be NULL for defaults)
 * @return        Checker descriptor
 */
EXPORT const struct audio_verifier_checker *audio_verifier_get_channel_layout_validator(const struct channel_layout_validator_config *config);

/**
 * Buffer Size Validator Configuration
 * Validates driver delivers callbacks with the new buffer size
 */
struct buffer_size_validator_config {
    size_t expected_buffer_size;  /**< Expected buffer size in frames (0 = derive from config) */
    float tolerance_percent;      /**< Allowed deviation as percentage (default: 5.0f) */
};

/**
 * Gets the built-in buffer size validator checker
 * @param config  Optional configuration (can be NULL for defaults)
 * @return        Checker descriptor
 */
EXPORT const struct audio_verifier_checker *audio_verifier_get_buffer_size_validator(const struct buffer_size_validator_config *config);

/* ========================================================================
 * Example Custom Verifier
 * ======================================================================== */

/**
 * Example: Custom verifier that checks for DC offset
 * Demonstrates how to implement a custom checker
 */
EXPORT const struct audio_verifier_checker *audio_verifier_get_dc_offset_checker(void);

#ifdef __cplusplus
}
#endif