#pragma once
#include "pose_policy.h"
#include <bit>
#include <charconv>
#include <string_view>

// BEACC001: explicit little-endian decoding; no Unity or platform dependency.
namespace BetterEndfieldNext::Actions {
inline bool AccessoryMask(std::string_view text, uint32_t& out) {
    int base = 10;
    if (text.starts_with("0x") || text.starts_with("0X")) { base = 16; text.remove_prefix(2); }
    if (text.empty()) return false;
    uint32_t value = 0;
    auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
    out = value;
    return true;
}
inline bool AccessoryPhase(double u, double begin, double target, double& phase) {
    if (!std::isfinite(u) || !std::isfinite(begin) || !std::isfinite(target) || begin <= target) return false;
    const double turns = (u - target) / (begin - target);
    if (!std::isfinite(turns)) return false;
    phase = turns - std::floor(turns);
    if (phase >= 1) phase = 0;
    return true;
}
inline bool AccessoryLeaf(std::string_view name) {
    // Strict UTF-8 (no overlong forms, surrogates, NUL, separators or controls).
    if (name.empty() || name.size() > 255 || name == "." || name == "..") return false;
    for (size_t i = 0; i < name.size();) {
        uint32_t c = static_cast<unsigned char>(name[i++]);
        if (c < 0x80) { if (c < 0x20 || c == 0x7f || c == '/' || c == '\\') return false; continue; }
        unsigned more = 0; uint32_t minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) { more = 1; minimum = 0x80; c &= 0x1f; }
        else if (c >= 0xe0 && c <= 0xef) { more = 2; minimum = 0x800; c &= 0xf; }
        else if (c >= 0xf0 && c <= 0xf4) { more = 3; minimum = 0x10000; c &= 7; }
        else return false;
        if (more > name.size() - i) return false;
        while (more--) {
            const auto next = static_cast<unsigned char>(name[i++]);
            if ((next & 0xc0) != 0x80) return false;
            c = (c << 6) | (next & 0x3f);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
    }
    return true;
}
struct AccessoryBone { uint8_t group = 0; std::string name; };
struct AccessoryEntry {
    int32_t state_hash = 0;
    float begin = 0, target = 0;
    uint32_t sample_count = 0;
    std::vector<AccessoryBone> bones;
    std::vector<BonePose> data;
    bool Matches(int hash, float expected_begin, float expected_target) const {
        return hash == state_hash && std::fabs(double(begin) - expected_begin) < 1e-4 && std::fabs(double(target) - expected_target) < 1e-4;
    }
    bool Sample(size_t bone, double phase, BonePose& out) const {
        if (bone >= bones.size() || sample_count < 8 || data.size() != size_t(sample_count) * bones.size() || !std::isfinite(phase)) return false;
        double fraction = phase - std::floor(phase);
        if (fraction >= 1) fraction = 0; // Rounding for a tiny negative phase.
        const double frame = fraction * sample_count;
        const auto k = static_cast<uint32_t>(frame);
        const auto& a = data[size_t(k) * bones.size() + bone];
        const auto& b = data[size_t((k + 1) % sample_count) * bones.size() + bone];
        const float f = static_cast<float>(frame - k);
        out = {PoseSlerp(a.rotation, b.rotation, f),
            {std::lerp(a.position.x, b.position.x, f), std::lerp(a.position.y, b.position.y, f), std::lerp(a.position.z, b.position.z, f)}};
        return true;
    }
};
struct AccessoryTable {
    // Largest valid file: 8 entries, each 4096 samples x 512 bones.
    static constexpr uint64_t kMaxBytes = 16ull + 8ull * (20ull + 512ull * 258ull + 4096ull * 512ull * 28ull);
    std::vector<AccessoryEntry> entries;
    bool Load(std::istream& in, std::string& error) {
        entries.clear(); error.clear();
        auto fail = [&](const char* reason) { error = reason; return false; };
        try {
            const auto start = in.tellg();
            in.seekg(0, std::ios::end);
            const auto end = in.tellg();
            if (start < 0 || end < start || uint64_t(end - start) < 16 || uint64_t(end - start) > kMaxBytes) return fail("invalid file length");
            uint64_t remaining = uint64_t(end - start);
            in.seekg(start);
            auto bytes = [&](void* p, size_t n) {
                if (n > remaining || !in.read(static_cast<char*>(p), static_cast<std::streamsize>(n))) return false;
                remaining -= n; return true;
            };
            auto word = [&](uint32_t& value, size_t n = 4) {
                unsigned char raw[4]{};
                if (!bytes(raw, n)) return false;
                value = uint32_t(raw[0]) | uint32_t(raw[1]) << 8 | uint32_t(raw[2]) << 16 | uint32_t(raw[3]) << 24;
                return true;
            };
            auto real = [&](float& value) { uint32_t bits; if (!word(bits)) return false; value = std::bit_cast<float>(bits); return std::isfinite(value); };
            char magic[8]; uint32_t version = 0, count = 0;
            if (!bytes(magic, 8) || std::memcmp(magic, "BEACC001", 8)) return fail("invalid magic");
            if (!word(version) || version != 1) return fail("invalid version");
            if (!word(count) || count < 1 || count > 8) return fail("invalid entry_count");
            std::vector<AccessoryEntry> next;
            for (uint32_t e = 0; e < count; ++e) {
                AccessoryEntry entry; uint32_t hash = 0, m = 0;
                if (!word(hash) || !real(entry.begin) || !real(entry.target) || entry.begin <= entry.target) return fail("invalid entry window");
                entry.state_hash = std::bit_cast<int32_t>(hash);
                if (!word(entry.sample_count) || entry.sample_count < 8 || entry.sample_count > 4096 || !word(m) || m < 1 || m > 512) return fail("invalid N/M");
                const uint64_t data_bytes = uint64_t(entry.sample_count) * m * 28;
                if (remaining < uint64_t(m) * 4 + data_bytes) return fail("truncated entry");
                for (uint32_t b = 0; b < m; ++b) {
                    AccessoryBone bone; uint32_t length = 0;
                    if (!bytes(&bone.group, 1) || bone.group > 31 || !word(length, 2) || length < 1 || length > 255 || length > remaining) return fail("invalid bone header");
                    bone.name.resize(length);
                    if (!bytes(bone.name.data(), length) || !AccessoryLeaf(bone.name)) return fail("invalid UTF-8 leaf name");
                    entry.bones.push_back(std::move(bone));
                }
                if (data_bytes > remaining) return fail("truncated samples");
                entry.data.resize(size_t(entry.sample_count) * m);
                for (auto& pose : entry.data) {
                    if (!real(pose.rotation.x) || !real(pose.rotation.y) || !real(pose.rotation.z) || !real(pose.rotation.w) ||
                        !real(pose.position.x) || !real(pose.position.y) || !real(pose.position.z)) return fail("invalid/truncated pose value");
                    const float norm2 = PoseDot(pose.rotation, pose.rotation);
                    if (!std::isfinite(norm2) || std::fabs(norm2 - 1.f) > .01f) return fail("non-unit quaternion (squared norm tolerance 0.01)");
                    pose.rotation = PoseNormalize(pose.rotation);
                }
                next.push_back(std::move(entry));
            }
            if (remaining != 0 || in.peek() != std::char_traits<char>::eof() || in.bad()) return fail("trailing data or file changed while reading");
            entries = std::move(next);
            return true;
        } catch (const std::exception&) { return fail("stream or allocation failure"); }
    }
};
}
