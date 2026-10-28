#pragma once
#include <algorithm>

// Pure hover/scroll state machine. No Win32, no timers: the caller feeds events and elapsed seconds,
// and runs a frame tick only while NeedsTick() is true, so an idle island wakes up zero times.
namespace island {
enum class Mode { Dock, Peek, Expanded };

struct Timing {
    float dwell = 0.16f;        // pointer must rest on the island before it opens
    float leave = 0.55f;        // pointer must stay outside this long before it collapses
    float cooldown = 0.8f;      // after a dismiss, ignore hover so it doesn't reopen under the pointer
    float mediaDebounce = 0.3f; // track changes must settle before media counts as present
};

class Machine {
    Timing timing_;
    Mode mode_ = Mode::Dock;
    float dwell_ = 0, outside_ = 0, cooldown_ = 0, mediaPending_ = 0;
    bool inside_ = false, busy_ = false, mediaRaw_ = false, media_ = false;

    void Collapse() {
        mode_ = Mode::Dock;
        dwell_ = outside_ = 0;
        cooldown_ = timing_.cooldown;
    }

  public:
    explicit Machine(Timing timing = {}) : timing_(timing) {}
    Mode Current() const { return mode_; }
    bool HasMedia() const { return media_; }
    void Configure(Timing timing) { timing_ = timing; }
    void OnDismiss(bool cooldown = true) {
        Collapse();
        if (!cooldown)
            cooldown_ = 0;
    }

    // True while a timer is pending. Stop the frame clock when this is false.
    bool NeedsTick() const {
        return cooldown_ > 0 || mediaPending_ > 0 || (mode_ == Mode::Dock && inside_ && cooldown_ <= 0) ||
               (mode_ != Mode::Dock && !inside_ && !busy_);
    }

    void OnPointer(bool inside) {
        inside_ = inside;
        if (inside)
            outside_ = 0;
        else
            dwell_ = 0;
    }
    // A flyout, drag or text field is active: never collapse while true.
    void OnBusy(bool busy) {
        busy_ = busy;
        if (busy)
            outside_ = 0;
    }
    void OnMedia(bool present) {
        if (present == mediaRaw_)
            return;
        mediaRaw_ = present;
        mediaPending_ = timing_.mediaDebounce;
    }
    // Scroll toward the centre, click, or the hotkey's "open" intent.
    void OnExpand() {
        if (mode_ != Mode::Expanded) {
            mode_ = Mode::Expanded;
            dwell_ = outside_ = 0;
        }
    }
    void OnToggle() {
        if (mode_ == Mode::Expanded)
            Collapse();
        else
            OnExpand();
    }
    // Escape steps back one level; flyouts consume Escape before this is called.
    void OnEscape() {
        if (mode_ == Mode::Expanded && media_ && inside_)
            mode_ = Mode::Peek;
        else if (mode_ != Mode::Dock)
            Collapse();
    }

    // Returns true when the mode changed.
    bool Step(float seconds) {
        seconds = std::clamp(seconds, 0.0f, 0.2f);
        Mode before = mode_;
        cooldown_ = std::max(0.0f, cooldown_ - seconds);
        if (mediaPending_ > 0) {
            mediaPending_ = std::max(0.0f, mediaPending_ - seconds);
            if (mediaPending_ == 0) {
                media_ = mediaRaw_;
                if (!media_ && mode_ == Mode::Peek)
                    mode_ = inside_ ? Mode::Expanded : Mode::Dock;
            }
        }
        if (mode_ == Mode::Dock) {
            dwell_ = inside_ && cooldown_ <= 0 ? dwell_ + seconds : 0;
            if (dwell_ >= timing_.dwell && mediaPending_ == 0) {
                mode_ = media_ ? Mode::Peek : Mode::Expanded;
                dwell_ = 0;
            }
        } else {
            outside_ = inside_ || busy_ ? 0 : outside_ + seconds;
            if (outside_ >= timing_.leave)
                Collapse();
        }
        return mode_ != before;
    }
};
} // namespace island
