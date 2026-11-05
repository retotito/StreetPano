#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/street_frame.hpp"

namespace streetpano::core {

struct StitchOptions {
    std::size_t maxPixels = std::size_t{64} * 1000 * 1000;
    int maxSide = 30000;
    int featherPixels = 16;
};

// The part of the panorama that one photo supplies, before feathering.
struct Strip {
    std::size_t camera = 0;
    int xStart = 0;
    int xEnd = 0;
};

// Where the panorama sits on the facade plane, and which photo supplies each column.
struct StitchPlan {
    int width = 0;
    int height = 0;
    double scale = 1.0;          // 1 means the photos' full detail.
    double unitsPerPixel = 0.0;  // On the facade, in the reconstruction's units.
    double leftOnPlane = 0.0;
    double topOnPlane = 0.0;
    std::vector<Strip> strips;
};

struct PlanResult {
    std::optional<StitchPlan> plan;
    std::string error;
};

// Gives each photo the columns closest to where its optical axis meets the facade. Straight
// vertical seams are a first step: foreground objects will be visibly cut at them.
[[nodiscard]] PlanResult makeStitchPlan(const StreetFrame& frame, std::span<const CameraPose> poses,
                                        double focalPixels, int photoWidth, int photoHeight,
                                        const StitchOptions& options);

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

// Projects one photo onto the panorama and blends it across the seams. focalPixels must
// match the size of the image passed in.
void drawStrip(const StreetFrame& frame, const StitchPlan& plan, const Strip& strip,
               const CameraPose& pose, double focalPixels, const ImageView& photo,
               const CanvasView& canvas, int featherPixels);

}  // namespace streetpano::core
