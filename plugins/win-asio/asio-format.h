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

#include <cstdint>
#include <cstring>

// Include ASIO SDK types
#include <asio.h>

namespace win_asio {

/**
 * Get sample size in bytes for ASIO format.
 */
inline int getSampleSize(ASIOSampleType format) {
    switch (format) {
        case ASIOSTInt16LSB: return 2;
        case ASIOSTInt24LSB: return 3;
        case ASIOSTInt32LSB: return 4;
        case ASIOSTFloat32LSB: return 4;
        case ASIOSTFloat64LSB: return 8;
        default: return 4;
    }
}

/**
 * Convert ASIO sample format to 32-bit float (planar).
 */
inline void convertASIOToFloat(const void* src, float* dst, int samples, ASIOSampleType format) {
    switch (format) {
        case ASIOSTInt16LSB: {
            const int16_t* s = static_cast<const int16_t*>(src);
            for (int i = 0; i < samples; ++i) {
                dst[i] = static_cast<float>(s[i]) * (1.0f / 32768.0f);
            }
            break;
        }
        case ASIOSTInt24LSB: {
            const uint8_t* s = static_cast<const uint8_t*>(src);
            for (int i = 0; i < samples; ++i) {
                int32_t sample = static_cast<int32_t>(s[i * 3]) |
                                (static_cast<int32_t>(s[i * 3 + 1]) << 8) |
                                (static_cast<int32_t>(s[i * 3 + 2]) << 16);
                // Sign extend 24-bit to 32-bit
                if (sample & 0x800000) sample |= 0xFF000000;
                dst[i] = static_cast<float>(sample) * (1.0f / 8388608.0f);
            }
            break;
        }
        case ASIOSTInt32LSB: {
            const int32_t* s = static_cast<const int32_t*>(src);
            for (int i = 0; i < samples; ++i) {
                dst[i] = static_cast<float>(s[i]) * (1.0f / 2147483648.0f);
            }
            break;
        }
        case ASIOSTFloat32LSB: {
            const float* s = static_cast<const float*>(src);
            std::memcpy(dst, s, samples * sizeof(float));
            break;
        }
        case ASIOSTFloat64LSB: {
            const double* s = static_cast<const double*>(src);
            for (int i = 0; i < samples; ++i) {
                dst[i] = static_cast<float>(s[i]);
            }
            break;
        }
        default: {
            // Unsupported format - zero output
            std::memset(dst, 0, samples * sizeof(float));
            break;
        }
    }
}

/**
 * Convert 32-bit float to ASIO sample format.
 */
inline void convertFloatToASIO(const float* src, void* dst, int samples, ASIOSampleType format) {
    switch (format) {
        case ASIOSTInt16LSB: {
            int16_t* d = static_cast<int16_t*>(dst);
            for (int i = 0; i < samples; ++i) {
                float v = src[i] * 32767.0f;
                if (v > 32767.0f) v = 32767.0f;
                else if (v < -32768.0f) v = -32768.0f;
                d[i] = static_cast<int16_t>(v);
            }
            break;
        }
        case ASIOSTInt24LSB: {
            uint8_t* d = static_cast<uint8_t*>(dst);
            for (int i = 0; i < samples; ++i) {
                float v = src[i] * 8388607.0f;
                if (v > 8388607.0f) v = 8388607.0f;
                else if (v < -8388608.0f) v = -8388608.0f;
                int32_t sample = static_cast<int32_t>(v);
                d[i * 3] = static_cast<uint8_t>(sample & 0xFF);
                d[i * 3 + 1] = static_cast<uint8_t>((sample >> 8) & 0xFF);
                d[i * 3 + 2] = static_cast<uint8_t>((sample >> 16) & 0xFF);
            }
            break;
        }
        case ASIOSTInt32LSB: {
            int32_t* d = static_cast<int32_t*>(dst);
            for (int i = 0; i < samples; ++i) {
                float v = src[i] * 2147483647.0f;
                if (v > 2147483647.0f) v = 2147483647.0f;
                else if (v < -2147483648.0f) v = -2147483648.0f;
                d[i] = static_cast<int32_t>(v);
            }
            break;
        }
        case ASIOSTFloat32LSB: {
            float* d = static_cast<float*>(dst);
            std::memcpy(d, src, samples * sizeof(float));
            break;
        }
        case ASIOSTFloat64LSB: {
            double* d = static_cast<double*>(dst);
            for (int i = 0; i < samples; ++i) {
                d[i] = src[i];
            }
            break;
        }
        default: {
            std::memset(dst, 0, samples * getSampleSize(format));
            break;
        }
    }
}

/**
 * Check if format is floating point.
 */
inline bool isFloatFormat(ASIOSampleType format) {
    return format == ASIOSTFloat32LSB || format == ASIOSTFloat64LSB;
}

/**
 * Check if format is signed integer.
 */
inline bool isIntFormat(ASIOSampleType format) {
    return format == ASIOSTInt16LSB || format == ASIOSTInt24LSB || 
           format == ASIOSTInt32LSB;
}

} // namespace win_asio