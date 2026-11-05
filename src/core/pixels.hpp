#pragma once

#include <cstddef>
#include <cstdint>

namespace streetpano::core {

// A photo in memory, as 32-bit 0xAARRGGBB pixels.
struct ImageView {
    const std::uint32_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    std::size_t bytesPerLine = 0;
};

// The panorama in memory. Alpha is zero where nothing has been drawn yet.
struct CanvasView {
    std::uint32_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    std::size_t bytesPerLine = 0;
};

[[nodiscard]] inline std::uint32_t mixPixels(std::uint32_t from, std::uint32_t to,
                                             std::uint32_t weight) {
    // Weight runs from 0 to 256. Masks keep each pair of channels from overflowing.
    const std::uint32_t inverse = 256U - weight;
    const std::uint32_t redBlue =
        (((from & 0x00FF00FFU) * inverse + (to & 0x00FF00FFU) * weight) >> 8U) & 0x00FF00FFU;
    const std::uint32_t green =
        (((from & 0x0000FF00U) * inverse + (to & 0x0000FF00U) * weight) >> 8U) & 0x0000FF00U;
    return 0xFF000000U | redBlue | green;
}

[[nodiscard]] inline const std::uint32_t* rowOf(const ImageView& image, int y) {
    const auto* start = reinterpret_cast<const unsigned char*>(image.pixels);
    return reinterpret_cast<const std::uint32_t*>(start +
                                                  static_cast<std::size_t>(y) * image.bytesPerLine);
}

[[nodiscard]] inline std::uint32_t* rowOf(const CanvasView& canvas, int y) {
    auto* start = reinterpret_cast<unsigned char*>(canvas.pixels);
    return reinterpret_cast<std::uint32_t*>(start +
                                            static_cast<std::size_t>(y) * canvas.bytesPerLine);
}

[[nodiscard]] inline std::uint32_t withGain(std::uint32_t pixel, std::uint32_t gain256) {
    const auto scaled = [gain256](std::uint32_t channel) {
        const std::uint32_t value = (channel * gain256 + 128U) >> 8U;
        return value > 255U ? 255U : value;
    };
    return 0xFF000000U | (scaled((pixel >> 16U) & 0xFFU) << 16U) |
           (scaled((pixel >> 8U) & 0xFFU) << 8U) | scaled(pixel & 0xFFU);
}

}  // namespace streetpano::core
