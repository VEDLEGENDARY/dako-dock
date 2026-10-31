#pragma once
#include <functional>
#include <algorithm>
#include <cmath>
namespace wallpaper {
struct CardPose {
    float x, scale, opacity, skew, z, widthScale;
};
inline CardPose Pose(float delta, float centerWidth) {
    float a = std::min(std::abs(delta), 2.5f), sign = delta < 0 ? -1.0f : 1.0f;
    float t = std::min(a, 1.0f);
    t = t * t * (3 - 2 * t);
    float strip = std::min(64.0f, centerWidth * .22f), first = (centerWidth + strip) / 2 + 12;
    float x = a <= 1 ? a * first : first + (a - 1) * (strip + 12);
    return {sign * x, 1 - .07f * t, std::clamp(1 - .18f * a, 0.0f, 1.0f), -.158f, -a, 1 - t + t * strip / centerWidth};
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
