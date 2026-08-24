#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <string>

namespace hytalevr {

enum class VrProfileStage : size_t {
    Frame,
    Actions,
    Resolution,
    Capture,
    WarpLeft,
    WarpRight,
    OverlayLeft,
    OverlayRight,
    Submit,
    WaitPoses,
    Count,
};

class VrFrameProfiler {
public:
    VrFrameProfiler();

    [[nodiscard]] bool enabled() const { return enabled_; }
    [[nodiscard]] int64_t timestamp() const;
    void record(VrProfileStage stage, int64_t start_ticks);
    [[nodiscard]] std::string report_if_due(uint64_t frame_serial);

private:
    struct Bucket {
        uint64_t ticks = 0;
        uint64_t maximum = 0;
        uint64_t count = 0;
    };

    bool enabled_ = false;
    int64_t frequency_ = 0;
    ULONGLONG last_report_tick_ = 0;
    std::array<Bucket, static_cast<size_t>(VrProfileStage::Count)> buckets_{};
};

class VrProfileScope {
public:
    VrProfileScope(VrFrameProfiler& profiler, VrProfileStage stage)
        : profiler_(profiler), stage_(stage), start_(profiler.timestamp()) {}
    ~VrProfileScope() { profiler_.record(stage_, start_); }

    VrProfileScope(const VrProfileScope&) = delete;
    VrProfileScope& operator=(const VrProfileScope&) = delete;

private:
    VrFrameProfiler& profiler_;
    VrProfileStage stage_;
    int64_t start_ = 0;
};

} // namespace hytalevr
