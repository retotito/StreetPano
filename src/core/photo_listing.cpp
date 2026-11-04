#include "core/photo_listing.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <system_error>

namespace streetpano::core {
namespace {

constexpr std::array<std::string_view, 5> kPhotoSuffixes = {".jpg", ".jpeg", ".png", ".tif",
                                                            ".tiff"};

[[nodiscard]] bool isDigit(char value) { return value >= '0' && value <= '9'; }

[[nodiscard]] char lowerAscii(char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    std::string result;
    result.reserve(text.size());
    std::transform(text.begin(), text.end(), std::back_inserter(result),
                   [](char8_t value) { return static_cast<char>(value); });
    return result;
}

[[nodiscard]] std::string_view digitRun(std::string_view text, std::size_t start) {
    std::size_t end = start;
    while (end < text.size() && isDigit(text[end])) {
        ++end;
    }
    return text.substr(start, end - start);
}

[[nodiscard]] std::string_view withoutLeadingZeros(std::string_view digits) {
    const std::size_t first = digits.find_first_not_of('0');
    return first == std::string_view::npos ? std::string_view{} : digits.substr(first);
}

}  // namespace

bool naturalLess(std::string_view left, std::string_view right) {
    std::size_t leftIndex = 0;
    std::size_t rightIndex = 0;
    while (leftIndex < left.size() && rightIndex < right.size()) {
        if (isDigit(left[leftIndex]) && isDigit(right[rightIndex])) {
            const std::string_view leftDigits = digitRun(left, leftIndex);
            const std::string_view rightDigits = digitRun(right, rightIndex);
            const std::string_view leftValue = withoutLeadingZeros(leftDigits);
            const std::string_view rightValue = withoutLeadingZeros(rightDigits);
            if (leftValue.size() != rightValue.size()) {
                return leftValue.size() < rightValue.size();
            }
            if (leftValue != rightValue) {
                return leftValue < rightValue;
            }
            leftIndex += leftDigits.size();
            rightIndex += rightDigits.size();
            continue;
        }
        const char leftChar = lowerAscii(left[leftIndex]);
        const char rightChar = lowerAscii(right[rightIndex]);
        if (leftChar != rightChar) {
            return leftChar < rightChar;
        }
        ++leftIndex;
        ++rightIndex;
    }
    if ((left.size() - leftIndex) != (right.size() - rightIndex)) {
        return (left.size() - leftIndex) < (right.size() - rightIndex);
    }
    // Names that differ only in case or zeros still need a consistent order.
    return left < right;
}

bool isPhotoFile(const std::filesystem::path& path) {
    const std::string name = toUtf8(path.filename());
    if (name.empty() || name.front() == '.') {
        return false;
    }
    std::string suffix = toUtf8(path.extension());
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), lowerAscii);
    return std::find(kPhotoSuffixes.begin(), kPhotoSuffixes.end(), suffix) != kPhotoSuffixes.end();
}

PhotoListing listPhotos(const std::filesystem::path& folder) {
    PhotoListing listing;
    std::error_code error;
    std::filesystem::directory_iterator entries(folder, error);
    if (error) {
        listing.error = "Can't open folder: " + error.message();
        return listing;
    }
    const std::filesystem::directory_iterator end;
    for (; entries != end; entries.increment(error)) {
        if (error) {
            listing.error = "Can't read folder: " + error.message();
            listing.photos.clear();
            return listing;
        }
        std::error_code typeError;
        if (entries->is_regular_file(typeError) && !typeError && isPhotoFile(entries->path())) {
            listing.photos.push_back(entries->path());
        }
    }
    std::sort(listing.photos.begin(), listing.photos.end(),
              [](const std::filesystem::path& left, const std::filesystem::path& right) {
                  return naturalLess(toUtf8(left.filename()), toUtf8(right.filename()));
              });
    return listing;
}

}  // namespace streetpano::core
