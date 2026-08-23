/******************************************************************************
    Copyright (C) 2025 by OBS Studio Contributors.

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

#include <obs-module.h>
#include <media-io/audio-resampler.h>
#include <media-io/audio-io.h>
#include <memory>

namespace win_asio {

struct ResamplerContext {
    std::shared_ptr<audio_resampler> resampler;
    int inputRate = 0;
    int outputRate = 0;
    int channels = 0;
    
    bool ensureResampler(int inRate, int outRate, int ch) {
        if (resampler && inputRate == inRate && outputRate == outRate && channels == ch) {
            return true;
        }
        
        struct resample_info dst = {};
        dst.samples_per_sec = static_cast<uint32_t>(outRate);
        dst.format = AUDIO_FORMAT_FLOAT_PLANAR;
        dst.speakers = ch == 1 ? SPEAKERS_MONO : SPEAKERS_STEREO;
        
        struct resample_info src = {};
        src.samples_per_sec = static_cast<uint32_t>(inRate);
        src.format = AUDIO_FORMAT_FLOAT_PLANAR;
        src.speakers = ch == 1 ? SPEAKERS_MONO : SPEAKERS_STEREO;
        
        audio_resampler* newResampler = audio_resampler_create(&dst, &src);
        if (!newResampler) {
            blog(LOG_ERROR, "[ASIO] Failed to create resampler: %d->%d Hz, %d channels", inRate, outRate, ch);
            return false;
        }
        
        resampler.reset(newResampler, [](audio_resampler* r) { audio_resampler_destroy(r); });
        inputRate = inRate;
        outputRate = outRate;
        channels = ch;
        return true;
    }
    
    /**
     * Resample audio from ASIO buffer to OBS output format.
     * @param input Planar input buffers [channels][samples]
     * @param inputSamples Number of samples per channel in input
     * @param output Planar output buffers [channels][samples]
     * @param outputSamples Pointer to output samples count (updated on return)
     * @param maxOutputSamples Maximum samples output buffer can hold per channel
     * @return true on success
     */
    bool resample(float** input, int inputSamples, float** output, int* outputSamples, int maxOutputSamples) {
        if (!resampler) return false;
        
        uint8_t* outputPtrs[64];
        for (int i = 0; i < channels; ++i) {
            outputPtrs[i] = reinterpret_cast<uint8_t*>(output[i]);
        }
        
        const uint8_t* inputPtrs[64];
        for (int i = 0; i < channels; ++i) {
            inputPtrs[i] = reinterpret_cast<const uint8_t*>(input[i]);
        }
        
        uint32_t outFrames = 0;
        uint64_t tsOffset = 0;
        
        bool result = audio_resampler_resample(
            resampler.get(),
            outputPtrs,
            &outFrames,
            &tsOffset,
            inputPtrs,
            static_cast<uint32_t>(inputSamples)
        );
        
        *outputSamples = static_cast<int>(outFrames);
        return result;
    }
    
    void reset() {
        if (resampler) {
            audio_resampler_destroy(resampler.get());
            resampler.reset();
        }
        inputRate = 0;
        outputRate = 0;
        channels = 0;
    }
};

} // namespace win_asio