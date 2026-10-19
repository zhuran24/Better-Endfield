#pragma once
#include "pose_policy.h"
#include <unordered_map>

namespace BetterEndfieldNext::Actions {
struct SeamDelta { PoseQuaternion dq; PoseVector dp; };
struct SeamEntry { int hash = 0; float target = 0, begin = 0; std::unordered_map<std::string, SeamDelta> bones; };
inline bool LoadSeamEntries(std::istream& in, std::vector<SeamEntry>& result) {
    result.clear();
    try {
        std::vector<SeamEntry> table;
        auto read = [&](auto& value) { return bool(in.read(reinterpret_cast<char*>(&value), sizeof(value))); };
        char magic[8]{}; uint32_t count = 0;
        if (!in.read(magic, 8) || std::memcmp(magic, "BESEAM03", 8) || !read(count) || count >= 256) return false;
        for (uint32_t i = 0; i < count; ++i) {
            SeamEntry entry; uint32_t bones = 0;
            if (!read(entry.hash) || !read(entry.target) || !read(entry.begin) ||
                !std::isfinite(entry.target) || !std::isfinite(entry.begin) ||
                entry.target < 0 || entry.target >= entry.begin || entry.begin >= .85f ||
                !read(bones) || bones > 4096) return false;
            for (uint32_t k = 0; k < bones; ++k) {
                uint16_t length = 0; SeamDelta delta;
                if (!read(length) || length == 0 || length >= 512) return false;
                std::string bone(length, '\0');
                if (!in.read(bone.data(), length) || bone.find('\0') != std::string::npos ||
                    !read(delta.dq) || !read(delta.dp)) return false;
                const BonePose pose{delta.dq, delta.dp};
                if (!PoseFinite(pose)) return false;
                for (float value : {delta.dq.x,delta.dq.y,delta.dq.z,delta.dq.w,delta.dp.x,delta.dp.y,delta.dp.z})
                    if (std::fabs(value) > 50) return false;
                const float norm = PoseDot(delta.dq, delta.dq);
                if (norm < .99f || norm > 1.01f) return false;
                delta.dq = PoseNormalize(delta.dq);
                if (!entry.bones.emplace(std::move(bone), delta).second) return false;
            }
            table.push_back(std::move(entry));
        }
        if (in.peek() != std::char_traits<char>::eof() || in.bad()) return false;
        result = std::move(table);
        return true;
    } catch (...) { return false; }
}
// Correction ramps over the last/first half of each lap; the initial entry
// retains its native start. Each endpoint contributes half the seam difference.
inline constexpr float kJsspsiSeamWindow = .5f;
inline float SeamShare(float phase, bool from_rewind) {
    const float window = kJsspsiSeamWindow;
    auto smooth = [](float x) { return x * x * (3 - 2 * x); };
    if (phase > 1 - window) return .5f * smooth((phase - (1 - window)) / window);
    if (phase < window && from_rewind) return -.5f * (1 - smooth(phase / window));
    return 0;
}
} // namespace BetterEndfieldNext::Actions
