#include "vr_frame_profiler.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace hytalevr {
namespace {

constexpr std::array<const char*, static_cast<size_t>(VrProfileStage::Count)> kStageNames{
    "frame", "actions", "resolution", "capture", "warpL", "warpR",
    "overlayL", "overlayR", "submit", "waitPoses",
};

bool profile_requested() {
    char value[16]{};
    const DWORD size = GetEnvironmentVariableA("HYTALE_VR_PROFILE", value,
                                                static_cast<DWORD>(sizeof(value)));
    return size > 0 && size < sizeof(value) && value[0] != '0';
}

} // namespace

VrFrameProfiler::VrFrameProfiler() : enabled_(profile_requested()) {
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
        enabled_ = false;
        return;
    }
    frequency_ = frequency.QuadPart;
    last_report_tick_ = GetTickCount64();
}

int64_t VrFrameProfiler::timestamp() const {
    if (!enabled_) return 0;
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

void VrFrameProfiler::record(VrProfileStage stage, int64_t start_ticks) {
    if (!enabled_ || start_ticks <= 0) return;
    const int64_t end = timestamp();
    if (end <= start_ticks) return;
    Bucket& bucket = buckets_[static_cast<size_t>(stage)];
    const uint64_t elapsed = static_cast<uint64_t>(end - start_ticks);
    bucket.ticks += elapsed;
    bucket.maximum = (std::max)(bucket.maximum, elapsed);
    ++bucket.count;
}

std::string VrFrameProfiler::report_if_due(uint64_t frame_serial) {
    if (!enabled_ || frequency_ <= 0) return {};
    const ULONGLONG now = GetTickCount64();
    if (now - last_report_tick_ < 2000) return {};
    last_report_tick_ = now;

    std::ostringstream out;
    out << "profile frame=" << frame_serial << std::fixed << std::setprecision(2);
    for (size_t i = 0; i < buckets_.size(); ++i) {
        const Bucket& bucket = buckets_[i];
        if (bucket.count == 0) continue;
        const double average_ms =
            static_cast<double>(bucket.ticks) * 1000.0 /
            static_cast<double>(frequency_) / static_cast<double>(bucket.count);
        const double maximum_ms =
            static_cast<double>(bucket.maximum) * 1000.0 /
            static_cast<double>(frequency_);
        out << ' ' << kStageNames[i] << '=' << average_ms << '/' << maximum_ms;
    }
    out << "ms(avg/max)";
    buckets_ = {};
    return out.str();
}

} // namespace hytalevr
