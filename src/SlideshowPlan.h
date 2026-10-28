#pragma once
#include <cstdint>
#include <string>
#include <utility>

class SlideshowPlan {
    std::wstring folder_;
    unsigned minutes_ = 0, generation_ = 0;
    bool armed_ = false, applying_ = false;

  public:
    bool Configure(std::wstring folder, unsigned minutes) {
        if (folder == folder_ && minutes == minutes_)
            return false;
        folder_ = std::move(folder);
        minutes_ = minutes;
        ++generation_;
        armed_ = applying_ = false;
        return true;
    }
    bool Enabled() const { return !folder_.empty() && minutes_ > 0 && minutes_ <= 1440; }
    unsigned Generation() const { return generation_; }
    uint64_t Delay() const { return static_cast<uint64_t>(minutes_) * 60000; }
    void Arm() {
        if (Enabled() && !applying_)
            armed_ = true;
    }
    bool Due(unsigned generation) {
        if (generation != generation_ || !Enabled() || !armed_ || applying_)
            return false;
        armed_ = false;
        applying_ = true;
        return true;
    }
    bool Complete(unsigned generation, bool success) {
        if (generation != generation_ || !applying_)
            return false;
        applying_ = false;
        return success && Enabled();
    }
};
