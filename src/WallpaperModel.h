#pragma once
#include <functional>
#include <algorithm>
#include <cmath>
namespace wallpaper {
struct CardPose {
    float x, scale, opacity, skew, z;
};
inline CardPose Pose(float delta, float centerWidth) {
    float a = std::min(std::abs(delta), 2.5f), sign = delta < 0 ? -1.0f : 1.0f;
    float ease = 1 - std::exp(-1.8f * a);
    return {sign * (centerWidth * .62f * ease + 34 * std::max(0.0f, a - 1)), 1 - .16f * std::min(a, 2.0f),
            std::clamp(1 - .34f * a, 0.0f, 1.0f), -.158f * std::clamp(delta, -1.0f, 1.0f), -a};
}
enum class Result { Applied, InvalidTarget, FitFailed, ImageFailed, RollbackFailed };
struct Operations {
    std::function<bool()> validate;
    std::function<int()> readFit;
    std::function<bool(int)> setFit;
    std::function<bool()> setImage;
};
inline Result Apply(int fit, const Operations &operations) {
    if (!operations.validate())
        return Result::InvalidTarget;
    int previous = operations.readFit();
    if (previous < 0)
        return Result::FitFailed;
    bool changed = previous != fit;
    if (changed && !operations.setFit(fit))
        return Result::FitFailed;
    if (operations.setImage())
        return Result::Applied;
    if (changed && !operations.setFit(previous))
        return Result::RollbackFailed;
    return Result::ImageFailed;
}
} // namespace wallpaper
