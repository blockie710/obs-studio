/**
 * LibreCast Studio - Windows Macro Hygiene Adversarial Stress Test
 * Location: test/test_windows_hygiene_stress.cpp
 *
 * Adversarially verifies:
 * 1. Clean compilation under multiple inclusion orderings with shared/util/windows-hygiene.hpp
 *    alongside Windows SDK headers (<windows.h>, <unknwn.h>, <objbase.h>, <rpcndr.h>, <wingdi.h>).
 * 2. Resistance to Win32 macro collisions:
 *    - min / max / std::numeric_limits<T>::max() / std::min / std::max
 *    - small (rpcndr.h macro)
 *    - near / far (minwindef.h macros)
 *    - interface (basetyps.h macro)
 *    - ERROR (wingdi.h macro)
 *    - RGB (wingdi.h macro)
 * 3. Deterministic diagnostic generation when hygiene envelope is disabled (toggleable triggers).
 */

#if defined(TRIGGER_NOMINMAX_COLLISION)
    #include <windows.h>
    #include <algorithm>
    #include <limits>
    int test_nominmax() {
        int a = 10, b = 20;
        int m = std::max(a, b);
        int l = std::numeric_limits<int>::max();
        return m + l;
    }

#elif defined(TRIGGER_SMALL_COLLISION)
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <rpc.h>
    #include <rpcndr.h>
    struct SmallCollisionStruct {
        int small; // Breaks with #define small char
    };
    int test_small() {
        SmallCollisionStruct s{42};
        return s.small;
    }

#elif defined(TRIGGER_NEARFAR_COLLISION)
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    struct NearFarCollisionStruct {
        float near; // Breaks with #define near
        float far;  // Breaks with #define far
    };
    float test_nearfar() {
        NearFarCollisionStruct n{1.0f, 100.0f};
        return n.near + n.far;
    }

#elif defined(TRIGGER_INTERFACE_COLLISION)
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <unknwn.h>
    #include <objbase.h>
    enum class InterfaceCollisionEnum {
        device,
        interface // Breaks with #define interface struct
    };
    InterfaceCollisionEnum test_interface() {
        return InterfaceCollisionEnum::interface;
    }

#elif defined(TRIGGER_ERROR_COLLISION)
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <wingdi.h>
    enum class ErrorCollisionEnum {
        INFO = 0,
        WARNING = 1,
        ERROR = 2 // Breaks with #define ERROR 0
    };
    ErrorCollisionEnum test_error() {
        return ErrorCollisionEnum::ERROR;
    }

#elif defined(TRIGGER_RGB_COLLISION)
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <wingdi.h>
    struct RgbCollisionStruct {
        static unsigned int RGB(unsigned char r, unsigned char g, unsigned char b) {
            return (unsigned int)(r | (g << 8) | (b << 16)); // Breaks with #define RGB(r,g,b)
        }
    };
    unsigned int test_rgb() {
        return RgbCollisionStruct::RGB(255, 128, 0);
    }

#else
// ============================================================================
// Clean Mode: Verify that windows-hygiene.hpp neutralizes all collisions
// ============================================================================

#if defined(TEST_ORDER_HYGIENE_AFTER_WINDOWS)
    // Scenario 2: Windows headers included BEFORE hygiene envelope
    #include <windows.h>
    #include <unknwn.h>
    #include <objbase.h>
    #include <rpcndr.h>
    #include <wingdi.h>
    #include <util/windows-hygiene.hpp>

#elif defined(TEST_ORDER_INTERLEAVED)
    // Scenario 3: Windows headers interleaved with hygiene envelope
    #include <windows.h>
    #include <util/windows-hygiene.hpp>
    #include <unknwn.h>
    #include <objbase.h>
    #include <rpcndr.h>
    #include <wingdi.h>

#else
    // Scenario 1 (Default): Hygiene envelope included FIRST, followed by Windows headers
    #include <util/windows-hygiene.hpp>
    #include <windows.h>
    #include <unknwn.h>
    #include <objbase.h>
    #include <rpcndr.h>
    #include <wingdi.h>
#endif

// C++ Standard Library headers
#include <algorithm>
#include <limits>
#include <cstdint>
#include <iostream>
#include <utility>

// 1. Stress test: min / max / std::min / std::max / std::numeric_limits<T>::max()
struct MinMaxStress {
    int min = 10;
    int max = 20;

    int calculate(int a, int b) const {
        int v_min = std::min(a, b);
        int v_max = std::max(a, b);
        int v_limit_max = std::numeric_limits<int>::max();
        int v_limit_min = std::numeric_limits<int>::min();
        return (min + max + v_min + v_max + (v_limit_max > v_limit_min ? 1 : 0));
    }
};

// 2. Stress test: small
struct SmallStress {
    int small;
    int regular;
    double large;
};

// 3. Stress test: near & far
struct FrustumStress {
    float near;
    float far;
    float fov;
};

// 4. Stress test: interface
struct InterfaceStress {
    int interface;
    enum class Kind {
        device,
        interface,
        endpoint
    } kind;
};

// 5. Stress test: ERROR
enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

struct ErrorStress {
    int ERROR;
    LogLevel level = LogLevel::ERROR;
};

// 6. Stress test: RGB
enum class ColorFormat {
    RGB,
    RGBA,
    BGR,
    BGRA,
    YUV
};

struct RgbStress {
    ColorFormat format;
    static uint32_t RGB(uint8_t r, uint8_t g, uint8_t b) {
        return static_cast<uint32_t>((r) | (static_cast<uint16_t>(g) << 8) | (static_cast<uint32_t>(b) << 16));
    }
};

int main() {
    // 1. Min/Max verification
    MinMaxStress mm;
    int mm_res = mm.calculate(5, 15);
    if (mm_res <= 0) {
        std::cerr << "MinMax calculation failed\n";
        return 1;
    }

    // 2. Small verification
    SmallStress sm{100, 200, 300.0};
    if (sm.small != 100 || sizeof(sm.small) != sizeof(int)) {
        std::cerr << "Small struct member failed\n";
        return 2;
    }

    // 3. Near / Far verification
    FrustumStress fr{0.5f, 500.0f, 75.0f};
    if (fr.near != 0.5f || fr.far != 500.0f) {
        std::cerr << "Frustum near/far failed\n";
        return 3;
    }

    // 4. Interface verification
    InterfaceStress iface{42, InterfaceStress::Kind::interface};
    if (iface.interface != 42 || iface.kind != InterfaceStress::Kind::interface) {
        std::cerr << "Interface identifier failed\n";
        return 4;
    }

    // 5. ERROR verification
    ErrorStress err{404, LogLevel::ERROR};
    if (err.ERROR != 404 || err.level != LogLevel::ERROR) {
        std::cerr << "ERROR identifier failed\n";
        return 5;
    }

    // 6. RGB verification
    uint32_t rgb_val = RgbStress::RGB(255, 128, 64);
    RgbStress rgb_inst{ColorFormat::RGB};
    if (rgb_val != (255 | (128 << 8) | (64 << 16)) || rgb_inst.format != ColorFormat::RGB) {
        std::cerr << "RGB identifier failed\n";
        return 6;
    }

    std::cout << "[PASS] Windows Macro Hygiene Stress Test completed with 0 errors.\n";
    return 0;
}

#endif
