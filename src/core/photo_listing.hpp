#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace streetpano::core {

struct PhotoListing {
    std::vector<std::filesystem::path> photos;
    std::string error;

    [[nodiscard]] bool ok() const { return error.empty(); }
};

// Compares names so that "img2" comes before "img10", ignoring ASCII case.
[[nodiscard]] bool naturalLess(std::string_view left, std::string_view right);

// Hidden files are excluded, including the "._" files macOS leaves on external drives.
[[nodiscard]] bool isPhotoFile(const std::filesystem::path& path);

// Lists a folder's photos in natural order. Never throws on a file system problem.
[[nodiscard]] PhotoListing listPhotos(const std::filesystem::path& folder);

}  // namespace streetpano::core
