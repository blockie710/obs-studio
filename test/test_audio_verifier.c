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

#include "libobs/media-io/audio_verifier.h"
#include "libobs/media-io/audio-io.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Simple test to demonstrate audio verifier usage */
int main(void) {
    printf("Testing audio verifier module...\n");
    
    // Create a verifier with all built-in checkers
    const struct audio_verifier_checker *checkers[4];
    struct silence_detector_config silence_config = {-60.0f, 2};
    struct sample_comparator_config sample_config = {0.01f, true};
    struct channel_layout_validator_config layout_config = {true};
    struct buffer_size_validator_config buffer_config = {1024, 5.0f};

    checkers[0] = audio_verifier_get_silence_detector(&silence_config);
    checkers[1] = audio_verifier_get_sample_comparator(&sample_config);
    checkers[2] = audio_verifier_get_channel_layout_validator(&layout_config);
    checkers[3] = audio_verifier_get_buffer_size_validator(&buffer_config);

    struct audio_verifier_config config = {
        .checkers = checkers,
        .num_checkers = 4,
        .user_data = NULL
    };

    audio_verifier_t *verifier = audio_verifier_create(&config);
    if (!verifier) {
        printf("Failed to create audio verifier\n");
        return 1;
    }

    printf("Audio verifier created successfully\n");
    
    // Test getting last results (should all be SKIP initially)
    for (size_t i = 0; i < 4; i++) {
        audio_verifier_result_t result = audio_verifier_get_last_result(verifier, checkers[i]->name);
        printf("Checker %s initial result: %d\n", checkers[i]->name, result);
    }

    // Clean up
    audio_verifier_destroy(verifier);
    
    printf("Audio verifier test completed\n");
    return 0;
}