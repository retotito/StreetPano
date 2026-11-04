#include "core/photo_listing.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using streetpano::core::isPhotoFile;
using streetpano::core::listPhotos;
using streetpano::core::naturalLess;

namespace {

class TemporaryFolder {
  public:
    TemporaryFolder() {
        std::random_device random;
        path_ = fs::temp_directory_path() / ("streetpano-test-" + std::to_string(random()));
        fs::create_directories(path_);
    }
    ~TemporaryFolder() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    TemporaryFolder(const TemporaryFolder&) = delete;
    TemporaryFolder& operator=(const TemporaryFolder&) = delete;
    TemporaryFolder(TemporaryFolder&&) = delete;
    TemporaryFolder& operator=(TemporaryFolder&&) = delete;

    [[nodiscard]] const fs::path& path() const { return path_; }
    void touch(const std::string& name) const { std::ofstream(path_ / name).put('x'); }

  private:
    fs::path path_;
};

std::vector<std::string> names(const streetpano::core::PhotoListing& listing) {
    std::vector<std::string> result;
    for (const fs::path& photo : listing.photos) {
        result.push_back(photo.filename().string());
    }
    return result;
}

}  // namespace

TEST_CASE("names sort naturally") {
    REQUIRE(naturalLess("img2.jpg", "img10.jpg"));
    REQUIRE_FALSE(naturalLess("img10.jpg", "img2.jpg"));
    REQUIRE(naturalLess("IMG1.jpg", "img2.jpg"));
    REQUIRE(naturalLess("a", "ab"));
    REQUIRE(naturalLess("a1", "a1b"));
    REQUIRE(naturalLess("a9", "b1"));
}

TEST_CASE("natural order is consistent for names that look equal") {
    REQUIRE_FALSE(naturalLess("same", "same"));
    REQUIRE(naturalLess("img007", "img7") != naturalLess("img7", "img007"));
    REQUIRE(naturalLess("IMG", "img") != naturalLess("img", "IMG"));
    REQUIRE(naturalLess("0", "00") != naturalLess("00", "0"));
}

TEST_CASE("natural order is a valid ordering for sorting") {
    const std::vector<std::string> samples = {"",   "1",   "01", "001", "a", "A",   "a1", "a01",
                                              "a2", "a10", "b",  "10",  "9", "a1b", "a1B"};
    for (const std::string& left : samples) {
        REQUIRE_FALSE(naturalLess(left, left));
        for (const std::string& right : samples) {
            if (left != right) {
                REQUIRE(naturalLess(left, right) != naturalLess(right, left));
            }
            for (const std::string& third : samples) {
                if (naturalLess(left, right) && naturalLess(right, third)) {
                    REQUIRE(naturalLess(left, third));
                }
            }
        }
    }
}

TEST_CASE("photo files are recognised by their suffix") {
    REQUIRE(isPhotoFile("a.jpg"));
    REQUIRE(isPhotoFile("a.JPEG"));
    REQUIRE(isPhotoFile("a.Png"));
    REQUIRE(isPhotoFile("a.tiff"));
    REQUIRE_FALSE(isPhotoFile("a.txt"));
    REQUIRE_FALSE(isPhotoFile("jpg"));
    REQUIRE_FALSE(isPhotoFile(""));
}

TEST_CASE("hidden files are excluded") {
    REQUIRE_FALSE(isPhotoFile(".hidden.jpg"));
    REQUIRE_FALSE(isPhotoFile("._IMG_0001.jpg"));
}

TEST_CASE("a folder lists only its photos in natural order") {
    const TemporaryFolder folder;
    folder.touch("img10.jpg");
    folder.touch("img2.jpg");
    folder.touch("notes.txt");
    folder.touch("._img2.jpg");
    fs::create_directory(folder.path() / "subfolder.jpg");

    const auto listing = listPhotos(folder.path());
    REQUIRE(listing.ok());
    REQUIRE(names(listing) == std::vector<std::string>{"img2.jpg", "img10.jpg"});
}

TEST_CASE("an empty folder gives an empty list") {
    const TemporaryFolder folder;
    const auto listing = listPhotos(folder.path());
    REQUIRE(listing.ok());
    REQUIRE(listing.photos.empty());
}

TEST_CASE("a missing folder gives an error instead of throwing") {
    const TemporaryFolder folder;
    const auto listing = listPhotos(folder.path() / "missing");
    REQUIRE_FALSE(listing.ok());
    REQUIRE(listing.photos.empty());
}
