#include "seam_table.h"
#include "dash_policy.h"
#include <sstream>
#include <cstdio>
#include <cstdlib>
using namespace BetterEndfieldNext::Actions;
#include "actions_test_support.h"
int main() {
    constexpr size_t kEntryCount = 8, kTarget = 16, kBoneCount = 24;
    constexpr size_t kNameLength = 28, kQuaternionX = 37, kQuaternionW = 49;
    std::string bytes = "BESEAM03";
    auto put = [&](auto value) { bytes.append(reinterpret_cast<const char *>(&value), sizeof(value)); };
    put(uint32_t(1));
    put(int32_t(-445642423));
    put(.126f);
    put(.315f);
    put(uint32_t(1));
    put(uint16_t(7));
    bytes += "Bip001X";
    for (float f : {0.f, 0.f, 0.f, 1.f, 1.f, 2.f, 3.f})
        put(f);
    std::vector<SeamEntry> table;
    std::istringstream valid(bytes);
    CHECK(LoadSeamEntries(valid, table));
    CHECK(table.size() == 1 && table[0].hash == -445642423 && table[0].target == .126f && table[0].begin == .315f);
    CHECK(table[0].bones.at("Bip001X").dp.z == 3 && table[0].bones.at("Bip001X").dq.w == 1);
    for (size_t n = 0; n < bytes.size(); ++n) {
        std::istringstream truncated(bytes.substr(0, n));
        CHECK(!LoadSeamEntries(truncated, table) && table.empty());
    }
    auto reject = [&](std::string bad) {
        std::istringstream in(bad);
        CHECK(!LoadSeamEntries(in, table) && table.empty());
    };
    auto bad = bytes;
    bad[0] = 'X';
    reject(bad);
    bad = bytes;
    uint32_t count = 256;
    std::memcpy(bad.data() + kEntryCount, &count, 4);
    reject(bad);
    bad = bytes;
    count = 4097;
    std::memcpy(bad.data() + kBoneCount, &count, 4);
    reject(bad);
    bad = bytes;
    bad[kNameLength] = bad[kNameLength + 1] = 0;
    reject(bad);
    bad = bytes;
    bad[kNameLength] = 0;
    bad[kNameLength + 1] = 2;
    reject(bad);
    CHECK(SeamShare(0, true) == -.5f && SeamShare(1, true) == .5f);
    CHECK(SeamShare(0, false) == 0 && SeamShare(.25f, false) == 0 && SeamShare(.5f, true) == 0);
    CHECK(std::fabs(SeamShare(.25f, true) + .25f) < 1e-6f && std::fabs(SeamShare(.75f, true) - .25f) < 1e-6f);
    for (int i = 0; i <= 1000; ++i) {
        float p = i / 1000.f;
        CHECK(std::fabs(SeamShare(p, true) + SeamShare(1 - p, true)) < 1e-6f);
    }
    CHECK(kJsspsiLoop.period == kJsspsiLoop.begin - kJsspsiLoop.target && kJsspsiLoop.begin < .85f);
    CHECK(kJsspsiSideLoop.period == kJsspsiSideLoop.begin - kJsspsiSideLoop.target && kJsspsiSideLoop.begin < .85f);
    for (float invalid : {NAN, INFINITY, 51.f}) {
        bad = bytes;
        std::memcpy(bad.data() + kQuaternionX, &invalid, 4);
        reject(bad);
    }
    bad = bytes;
    float norm = 2;
    std::memcpy(bad.data() + kQuaternionW, &norm, 4);
    reject(bad);
    reject(bytes + "x");
    bad = bytes;
    count = 2;
    std::memcpy(bad.data() + kBoneCount, &count, 4);
    bad += bytes.substr(kNameLength);
    reject(bad);
    bad = bytes;
    float target = .4f;
    std::memcpy(bad.data() + kTarget, &target, 4);
    reject(bad);
    Frame frame;
    frame.allowed = frame.current_special = true;
    frame.length = 5.667f;
    frame.delta = .016f;
    LoopSchedule schedule;
    schedule.c = kJsspsiLoop;
    frame.time = .314f;
    CHECK(schedule.Update(true, frame) == LoopAction::None);
    frame.time = .32f;
    CHECK(schedule.Update(true, frame) == LoopAction::Blend);
    CHECK(std::fabs(schedule.TargetAt(frame.time) - .131f) < 1e-6f);
    schedule.Submitted(frame.time);
    frame.time = .131f;
    CHECK(schedule.Update(true, frame) == LoopAction::None && !schedule.pending);
    std::puts("Seam table: bounds, truncation, endpoint share, first entry and fixed loop windows passed.");
}
