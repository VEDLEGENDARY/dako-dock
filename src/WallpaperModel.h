#pragma once
#include <functional>
namespace wallpaper {
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
}
