#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>

namespace telemetry {
struct Point {
    double cpu = 0, ram = 0, receive = 0, send = 0;
    bool cpuValid = false, ramValid = false, networkValid = false;
};
class History {
    std::array<Point, 60> points_{};
    size_t first_ = 0, count_ = 0;
    uint64_t last_ = 0;

  public:
    void Push(uint64_t milliseconds, Point point) {
        if (last_ && milliseconds <= last_)
            return;
        if (last_ && milliseconds - last_ > 2500)
            first_ = count_ = 0;
        last_ = milliseconds;
        size_t index = (first_ + count_) % points_.size();
        points_[index] = point;
        if (count_ < points_.size())
            ++count_;
        else
            first_ = (first_ + 1) % points_.size();
    }
    size_t Size() const { return count_; }
    const Point &At(size_t index) const { return points_[(first_ + index) % points_.size()]; }
};
struct ProcessTimes {
    uint64_t created = 0, cpu = 0, milliseconds = 0;
};
class ProcessDeltas {
    std::map<uint32_t, ProcessTimes> previous_, current_;

  public:
    void Begin() { current_.clear(); }
    std::optional<double> Sample(uint32_t pid, ProcessTimes now, unsigned processors) {
        current_[pid] = now;
        auto found = previous_.find(pid);
        if (!processors || found == previous_.end())
            return {};
        const auto &old = found->second;
        if (old.created != now.created || now.cpu < old.cpu || now.milliseconds <= old.milliseconds ||
            now.milliseconds - old.milliseconds > 2500)
            return {};
        double capacity = 10000.0 * (now.milliseconds - old.milliseconds) * processors;
        return std::clamp(100.0 * (now.cpu - old.cpu) / capacity, 0.0, 100.0);
    }
    void End() { previous_ = std::move(current_); }
};
}
