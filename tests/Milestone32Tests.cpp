#include <windows.h>
#include "../src/TelemetryModel.h"
#include "../src/WallpaperModel.h"
#include "../src/ControlsService.h"
#include <cstdio>
#include <stdexcept>
void Verify32(bool condition, const char *text) {
    if (!condition)
        throw std::runtime_error(text);
}
int main() {
    try {
        for (int render = 0; render < 2; ++render)
            for (int fixture = 0; fixture < 2; ++fixture)
                for (int preview = 0; preview < 2; ++preview)
                    Verify32(controls::MayWrite(render != 0, fixture != 0, preview != 0) ==
                                 (!render && !fixture && !preview),
                             "hardware writes require a normal interactive instance");
        telemetry::History history;
        for (uint64_t i = 1; i <= 75; ++i) {
            telemetry::Point point;
            point.cpu = static_cast<double>(i);
            history.Push(i * 1000, point);
        }
        Verify32(history.Size() == 60 && history.At(0).cpu == 16 && history.At(59).cpu == 75,
                 "history retains exactly 60 ordered samples");
        history.Push(74000, {});
        Verify32(history.Size() == 60, "stale samples do not corrupt history");
        history.Push(80000, {});
        Verify32(history.Size() == 1, "a hidden-view gap starts a new contiguous history");
        telemetry::ProcessDeltas deltas;
        deltas.Begin();
        Verify32(!deltas.Sample(10, {100, 100000, 1000}, 4), "first process sample has no invented CPU rate");
        deltas.End();
        deltas.Begin();
        auto value = deltas.Sample(10, {100, 10100000, 2000}, 4);
        Verify32(value && *value == 25, "process CPU normalizes one second of CPU across four logical processors");
        Verify32(!deltas.Sample(11, {100, 10100000, 2000}, 4), "unseen PID has no borrowed delta");
        deltas.End();
        deltas.Begin();
        Verify32(!deltas.Sample(10, {200, 10200000, 3000}, 4), "PID reuse resets the CPU baseline");
        deltas.End();
        deltas.Begin();
        Verify32(!deltas.Sample(10, {200, 200, 4000}, 4), "reset counters never underflow into high CPU");
        deltas.End();
        deltas.Begin();
        Verify32(!deltas.Sample(10, {200, 400, 9000}, 4), "hidden-view intervals do not produce stale rankings");
        deltas.End();
        int fit = 4, images = 0, fitWrites = 0;
        bool valid = true, imageSuccess = true, fitSuccess = true, rollbackSuccess = true;
        wallpaper::Operations operations;
        operations.validate = [&] { return valid; };
        operations.readFit = [&] { return fit; };
        operations.setFit = [&](int next) {
            ++fitWrites;
            if (!fitSuccess || (next == 4 && !rollbackSuccess))
                return false;
            fit = next;
            return true;
        };
        operations.setImage = [&] {
            ++images;
            return imageSuccess;
        };
        Verify32(wallpaper::Apply(4, operations) == wallpaper::Result::Applied && images == 1 && fitWrites == 0,
                 "unchanged global fit is not rewritten");
        valid = false;
        Verify32(wallpaper::Apply(3, operations) == wallpaper::Result::InvalidTarget && images == 1 && fitWrites == 0,
                 "a disconnected target prevents every wallpaper/settings write");
        valid = true;
        fitSuccess = false;
        Verify32(wallpaper::Apply(3, operations) == wallpaper::Result::FitFailed && images == 1,
                 "fit failure prevents image application");
        fitSuccess = true;
        imageSuccess = false;
        Verify32(wallpaper::Apply(3, operations) == wallpaper::Result::ImageFailed && fit == 4,
                 "image failure restores the prior global fit");
        rollbackSuccess = false;
        Verify32(wallpaper::Apply(3, operations) == wallpaper::Result::RollbackFailed,
                 "rollback failure is reported rather than claimed restored");
        std::puts("PASS: inert write gates, 60-second history, per-PID CPU deltas/reuse, wallpaper target validation "
                  "and fit rollback.");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
