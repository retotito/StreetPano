#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/camera_pose.hpp"
#include "core/facade_surface.hpp"

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

// Where the panorama sits on the facade surface, and which photo supplies each column.
struct StitchPlan {
    int width = 0;
    int height = 0;
    double scale = 1.0;          // 1 means the photos' full detail.
    double unitsPerPixel = 0.0;  // On the facade, in the reconstruction's units.
    double leftArc = 0.0;
    double topHeight = 0.0;
    std::vector<Strip> strips;
};

struct PlanResult {
    std::optional<StitchPlan> plan;
    std::string error;
};

// Gives each photo the columns closest to where its optical axis meets the facade. These are
// starting points, and drawStrip moves the seams from there.
[[nodiscard]] PlanResult makeStitchPlan(const FacadeSurface& surface,
                                        std::span<const CameraPose> poses, double focalPixels,
                                        int photoWidth, int photoHeight,
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

// Returns what to multiply a new photo by to match what's drawn so far, where they overlap.
// region holds the new photo from column firstX onwards, with zero alpha where it has no pixels.
[[nodiscard]] double brightnessGain(const CanvasView& canvas, const ImageView& region, int firstX);

// Finds the cheapest top-to-bottom path through the overlap of what's drawn so far and a new
// photo. Cost is how much they disagree, so the path runs around objects that differ between
// photos, such as cars. Nearby objects can't be replaced if no photo shows what's behind them.
// region holds the new photo from column firstX onwards, with zero alpha where it has no pixels.
// Returns one column per canvas row.
[[nodiscard]] std::vector<int> findSeam(const CanvasView& canvas, const ImageView& region,
                                        int firstX, int preferredX, int reach);

// Projects one photo onto the panorama, up to one reach past its own strip, and joins it along
// the cheapest seam. Draw strips from left to right. focalPixels must match the image's size.
void drawStrip(const FacadeSurface& surface, const StitchPlan& plan, const Strip& strip,
               const CameraPose& pose, double focalPixels, const ImageView& photo,
               const CanvasView& canvas, int featherPixels);

}  // namespace streetpano::core
