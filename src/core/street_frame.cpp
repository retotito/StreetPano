#include "core/street_frame.hpp"

#include <algorithm>
#include <cstddef>
#include <random>

namespace streetpano::core {
namespace {

// Road surface can far outnumber the facade. With 9% facade points, this many attempts leaves
// about a one in a million chance of never picking three facade points together.
constexpr int kRansacAttempts = 20000;
constexpr std::size_t kMaxScoredPoints = 2000;  // Each attempt is scored on a sample of points.
constexpr double kRelativeTolerance = 0.015;    // Of the distance from camera to facade.
constexpr double kMinFacingCosine = 0.5;  // The facade must face the cameras within 60 degrees.
constexpr double kMaxVerticalLean = 0.5;  // And lean less than 30 degrees from the cameras' up.
constexpr std::size_t kMinPoints = 20;
constexpr int kJacobiSweeps = 32;

[[nodiscard]] double median(std::vector<double> values) {
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

[[nodiscard]] Vec3 meanOf(std::span<const Vec3> values) {
    Vec3 sum;
    for (const Vec3& value : values) {
        sum = sum + value;
    }
    return sum * (1.0 / static_cast<double>(values.size()));
}

// Refines a plane using every inlier, and returns its unit normal.
[[nodiscard]] Vec3 fitNormal(std::span<const Vec3> inliers, Vec3 centroid) {
    Mat3 scatter{};
    for (const Vec3& point : inliers) {
        const Vec3 p = point - centroid;
        scatter[0] = scatter[0] + p * p.x;
        scatter[1] = scatter[1] + p * p.y;
        scatter[2] = scatter[2] + p * p.z;
    }
    return smallestEigenvector(scatter);
}

}  // namespace

Vec3 smallestEigenvector(const Mat3& matrix) {
    // Jacobi rotations, which are simple and reliable for a small symmetric matrix.
    std::array<std::array<double, 3>, 3> a{{{matrix[0].x, matrix[0].y, matrix[0].z},
                                            {matrix[1].x, matrix[1].y, matrix[1].z},
                                            {matrix[2].x, matrix[2].y, matrix[2].z}}};
    std::array<std::array<double, 3>, 3> v{{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
    for (int sweep = 0; sweep < kJacobiSweeps; ++sweep) {
        for (std::size_t p = 0; p < 2; ++p) {
            for (std::size_t q = p + 1; q < 3; ++q) {
                if (std::abs(a[p][q]) < 1e-300) {
                    continue;
                }
                const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                const double t = (theta >= 0.0 ? 1.0 : -1.0) /
                                 (std::abs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0);
                const double s = t * c;
                for (std::size_t k = 0; k < 3; ++k) {
                    const double akp = a[k][p];
                    const double akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (std::size_t k = 0; k < 3; ++k) {
                    const double apk = a[p][k];
                    const double aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (std::size_t k = 0; k < 3; ++k) {
                    const double vkp = v[k][p];
                    const double vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
        }
    }
    std::size_t smallest = 0;
    for (std::size_t index = 1; index < 3; ++index) {
        if (a[index][index] < a[smallest][smallest]) {
            smallest = index;
        }
    }
    return normalized({v[0][smallest], v[1][smallest], v[2][smallest]});
}

FrameResult fitStreetFrame(std::span<const CameraPose> poses, std::span<const Vec3> points) {
    FrameResult result;
    if (poses.empty()) {
        result.error = "There are no camera positions.";
        return result;
    }
    if (points.size() < kMinPoints) {
        result.error = "There are too few 3D points to find the facade.";
        return result;
    }

    std::vector<Vec3> centers;
    centers.reserve(poses.size());
    Vec3 forwardSum;
    Vec3 upSum;
    for (const CameraPose& pose : poses) {
        centers.push_back(pose.center);
        forwardSum = forwardSum + pose.forward();
        upSum = upSum + pose.up();
    }
    const Vec3 viewDirection = normalized(forwardSum);
    const Vec3 upDirection = normalized(upSum);
    const Vec3 pathCenter = meanOf(centers);

    std::vector<double> distances;
    distances.reserve(points.size());
    for (const Vec3& point : points) {
        distances.push_back(std::abs(dot(point - pathCenter, viewDirection)));
    }
    const double tolerance = median(distances) * kRelativeTolerance;

    // A fixed seed, so that the same photos always give the same panorama.
    // NOLINTNEXTLINE(bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp)
    std::mt19937 random(20261105U);
    std::uniform_int_distribution<std::size_t> pick(0, points.size() - 1);
    const std::size_t scoringStep = std::max<std::size_t>(1, points.size() / kMaxScoredPoints);
    std::size_t bestCount = 0;
    Vec3 bestNormal;
    Vec3 bestPoint;
    for (int attempt = 0; attempt < kRansacAttempts; ++attempt) {
        const Vec3 a = points[pick(random)];
        const Vec3 normal = normalized(cross(points[pick(random)] - a, points[pick(random)] - a));
        // Without the second check, a slanted plane through a large area of road can hold more
        // points than the facade does.
        if (std::abs(dot(normal, viewDirection)) < kMinFacingCosine ||
            std::abs(dot(normal, upDirection)) > kMaxVerticalLean) {
            continue;
        }
        std::size_t count = 0;
        for (std::size_t index = 0; index < points.size(); index += scoringStep) {
            if (std::abs(dot(points[index] - a, normal)) < tolerance) {
                ++count;
            }
        }
        if (count > bestCount) {
            bestCount = count;
            bestNormal = normal;
            bestPoint = a;
        }
    }

    std::vector<Vec3> inliers;
    if (bestCount > 0) {
        inliers.reserve(bestCount * scoringStep);
        for (const Vec3& point : points) {
            if (std::abs(dot(point - bestPoint, bestNormal)) < tolerance) {
                inliers.push_back(point);
            }
        }
    }
    if (inliers.size() < kMinPoints) {
        result.error = "No plane facing the cameras was found.";
        return result;
    }
    const Vec3 centroid = meanOf(inliers);
    Vec3 normal = fitNormal(inliers, centroid);
    if (dot(normal, viewDirection) < 0.0) {
        normal = normal * -1.0;
    }

    // Averaging the cameras' up directions, then removing any part along the normal, cancels
    // both hand-held wobble and the camera being tilted up or down.
    const Vec3 up = normalized(upSum - normal * dot(upSum, normal));
    if (length(up) == 0.0) {
        result.error = "The cameras' up direction points straight at the facade.";
        return result;
    }

    std::vector<double> cameraDistances;
    cameraDistances.reserve(centers.size());
    for (const Vec3& center : centers) {
        cameraDistances.push_back(std::abs(dot(centroid - center, normal)));
    }

    StreetFrame frame;
    frame.normal = normal;
    frame.up = up;
    frame.right = cross(normal, up);
    frame.origin = centroid;
    frame.medianCameraDistance = median(cameraDistances);
    frame.inlierCount = inliers.size();
    if (frame.medianCameraDistance <= 0.0) {
        result.error = "The cameras lie on the facade plane.";
        return result;
    }
    result.frame = frame;
    return result;
}

}  // namespace streetpano::core
