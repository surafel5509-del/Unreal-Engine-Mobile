#include "rhi/RhiTypes.h"

namespace nova::rhi {

usize formatTexelSize(Format format) {
    switch (format) {
        case Format::RGBA8_UNorm:
        case Format::RGBA8_SRGB:
        case Format::BGRA8_UNorm:
        case Format::BGRA8_SRGB:
            return 4;
        case Format::RGBA16_SFloat:
            return 8;
        case Format::R32_SFloat:
            return 4;
        case Format::RG32_SFloat:
            return 8;
        case Format::RGB32_SFloat:
            return 12;
        case Format::RGBA32_SFloat:
            return 16;
        case Format::D32_SFloat:
            return 4;
        case Format::Unknown:
            return 0;
    }
    return 0;
}

const char* formatName(Format format) {
    switch (format) {
        case Format::RGBA8_UNorm: return "RGBA8_UNorm";
        case Format::RGBA8_SRGB: return "RGBA8_SRGB";
        case Format::BGRA8_UNorm: return "BGRA8_UNorm";
        case Format::BGRA8_SRGB: return "BGRA8_SRGB";
        case Format::RGBA16_SFloat: return "RGBA16_SFloat";
        case Format::R32_SFloat: return "R32_SFloat";
        case Format::RG32_SFloat: return "RG32_SFloat";
        case Format::RGB32_SFloat: return "RGB32_SFloat";
        case Format::RGBA32_SFloat: return "RGBA32_SFloat";
        case Format::D32_SFloat: return "D32_SFloat";
        case Format::Unknown: return "Unknown";
    }
    return "Unknown";
}

}  // namespace nova::rhi
