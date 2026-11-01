#pragma once
#include "SurfaceModel.h"
#include "DockMotion.h"

namespace surface {
// One choreography, sampled by the existing FrameClock. This class never arms a timer.
class SurfaceMotion {
  public:
    enum class Phase { Closed, Opening, Open, Retargeting, Closing };
    dock::Spring reveal{0}, width{theme::DockWidth}, height{theme::DockHeight}, anchor{0}, content{0}, blend{1};
    Phase phase = Phase::Closed;
    float age = 0, restWidth = theme::DockWidth, restHeight = theme::DockHeight;
    bool fadeBeforeClose = false, label = false;
    static constexpr dock::motion::Spec Retarget{24, .9f, .22f}, Crossfade{34, 1, .16f};
    static constexpr dock::motion::Spec Content{34, 1, .16f}, CloseFade{34, 1, .10f}, Shrink{24, .9f, .20f};
    void Rest(float w, float h) {
        restWidth = w;
        restHeight = h;
        if (phase == Phase::Closed) {
            width.Snap(w);
            height.Snap(h);
        }
    }
    void Open(float x, float w, float h, bool instant = false) {
        bool opening = phase == Phase::Closed || phase == Phase::Closing;
        phase = opening ? Phase::Opening : Phase::Retargeting;
        age = 0;
        fadeBeforeClose = false;
        anchor.goal = x;
        width.goal = w;
        height.goal = h;
        reveal.goal = 1;
        if (opening)
            content.Snap(0);
        else {
            content.goal = 1;
            blend.Snap(0);
        }
        blend.goal = 1;
        if (label) {
            reveal.Snap(1);
            width.Snap(w);
            height.Snap(h);
            if (opening)
                anchor.Snap(x);
            content.goal = 1;
        }
        if (instant)
            Snap();
    }
    void Resize(float w, float h, bool instant = false) {
        if (phase == Phase::Closed || phase == Phase::Closing)
            return;
        if (width.goal == w && height.goal == h)
            return;
        width.goal = w;
        height.goal = h;
        if (phase == Phase::Open) {
            phase = Phase::Retargeting;
            age = 0;
        }
        if (instant)
            Snap();
    }
    void Close(bool instant = false) {
        if (phase == Phase::Closed)
            return;
        phase = Phase::Closing;
        age = 0;
        fadeBeforeClose = true;
        content.goal = 0;
        if (instant)
            Snap();
    }
    bool Moving() const {
        return fadeBeforeClose || reveal.Moving() || width.Moving() || height.Moving() || anchor.Moving() ||
               content.Moving() || blend.Moving();
    }
    void Snap() {
        if (phase == Phase::Closing) {
            reveal.goal = content.goal = 0;
            width.goal = restWidth;
            height.goal = restHeight;
        } else if (phase != Phase::Closed)
            reveal.goal = content.goal = 1;
        for (auto *spring : {&reveal, &width, &height, &anchor, &content, &blend})
            spring->Snap(spring->goal);
        fadeBeforeClose = false;
        phase = reveal.value == 0 ? Phase::Closed : Phase::Open;
    }
    bool Step(float elapsed, float speed = 1, bool instant = false) {
        if (instant || elapsed > .5f) {
            Snap();
            return false;
        }
        if (label) {
            while (elapsed > 0) {
                float dt = std::min(elapsed, .05f);
                content.Step(dt, CloseFade, speed);
                blend.Step(dt, Crossfade, speed);
                anchor.Step(dt, Retarget, speed);
                elapsed -= dt;
            }
            if (!content.Moving() && phase == Phase::Closing) {
                Snap();
                return false;
            }
            bool moving = content.Moving() || blend.Moving() || anchor.Moving();
            if (!moving)
                phase = Phase::Open;
            return moving;
        }
        speed = std::clamp(speed, .5f, 2.0f);
        while (elapsed > 0) {
            float dt = std::min(elapsed, .05f), before = age;
            age += dt;
            elapsed -= dt;
            if (phase == Phase::Closing && fadeBeforeClose) {
                content.Step(dt, CloseFade, speed);
                if (age >= .10f / speed || content.value <= .001f) {
                    fadeBeforeClose = false;
                    reveal.goal = 0;
                    width.goal = restWidth;
                    height.goal = restHeight;
                }
            }
            if (!fadeBeforeClose) {
                bool opening = phase == Phase::Opening, closing = phase == Phase::Closing;
                auto response = closing ? Shrink : phase == Phase::Retargeting ? Retarget : dock::motion::Height;
                reveal.Step(dt, response, speed);
                height.Step(dt, response, speed);
                float widthTime =
                    opening ? std::max(0.0f, age - .04f / speed) - std::max(0.0f, before - .04f / speed) : dt;
                width.Step(widthTime, opening ? dock::motion::Width : response, speed);
                anchor.Step(dt, Retarget, speed);
                float travel = (height.value - restHeight) / std::max(1.0f, height.goal - restHeight);
                if (opening && travel >= .6f)
                    content.goal = 1;
                content.Step(dt, Content, speed);
                blend.Step(dt, Crossfade, speed);
            }
        }
        if (!Moving())
            phase = reveal.value == 0 ? Phase::Closed : Phase::Open;
        return Moving();
    }
};
} // namespace surface
