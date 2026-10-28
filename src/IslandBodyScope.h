#pragma once
#include "Island.h"

struct DockBodyScope {
    Island &app;
    D2D1::Matrix3x2F saved;
    explicit DockBodyScope(Island &value) : app(value) {
        app.buildingBody = true;
        if (!app.layoutOnly) {
            app.target->GetTransform(&saved);
            app.target->PushAxisAlignedClip(app.Body(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            app.target->SetTransform(D2D1::Matrix3x2F::Translation(0, -app.scroll) * saved);
        }
    }
    ~DockBodyScope() {
        app.buildingBody = false;
        if (!app.layoutOnly) {
            app.target->SetTransform(saved);
            app.target->PopAxisAlignedClip();
        }
    }
};
