#include "actions_test_support.h"
#include "accessory_table_fixture.h"
#include <limits>
void Tests() {
    auto f = Make(); AccessoryTable table; std::string error;
    CHECK(Load(f.bytes, table, error));
    CHECK(table.entries.size() == 2 && table.entries[0].state_hash == -445642423 && table.entries[1].state_hash == 528430122);
    CHECK(table.entries[0].bones[0].name == "tail_01_jnt" && table.entries[0].bones[1].group == 31);
    auto reject = [&](const std::string& bytes, const char* label) {
        const bool rejected = !Load(bytes, table, error) && table.entries.empty() && !error.empty();
        if (!rejected) std::fprintf(stderr, "Accepted invalid accessory table: %s\n", label);
        CHECK(rejected);
    };
    // Every possible truncation, including the second entry: no partial table survives.
    for (size_t i = 0; i < f.bytes.size(); ++i) reject(f.bytes.substr(0, i), "truncation");
    reject(f.bytes + 'x', "trailing byte");
    auto bad = f.bytes; bad[0] = 'X'; reject(bad, "magic");
    auto mutate = [&](size_t offset, uint32_t value, const char* label, size_t size = 4) {
        auto bytes = f.bytes; Patch(bytes, offset, value, size); reject(bytes, label);
    };
    mutate(8, 2, "version"); mutate(12, 0, "zero entries"); mutate(12, 9, "too many entries");
    for (auto n : {0u, 7u, 4097u, UINT32_MAX}) mutate(f.entries[0] + 12, n, "bad sample_count");
    for (auto m : {0u, 513u, UINT32_MAX}) mutate(f.entries[0] + 16, m, "bad bone_count");
    mutate(f.names[0], 32, "bad group", 1);
    mutate(f.names[0] + 1, 0, "empty name", 2); mutate(f.names[0] + 1, 256, "long name", 2);
    for (unsigned char byte : {0u, 0x2fu, 0x5cu, 0xffu, 0x7fu}) {
        bad = f.bytes; bad[f.names[0] + 3] = static_cast<char>(byte); reject(bad, "invalid leaf");
    }
    mutate(f.entries[0] + 4, std::bit_cast<uint32_t>(.126f), "equal window");
    mutate(f.entries[0] + 4, std::bit_cast<uint32_t>(.1f), "inverted window");
    for (auto value : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        mutate(f.entries[0] + 4, std::bit_cast<uint32_t>(value), "nonfinite window");
        for (size_t i = 0; i < 7; ++i) mutate(f.data[0] + i * 4, std::bit_cast<uint32_t>(value), "nonfinite pose");
    }
    mutate(f.data[0] + 12, 0, "zero quaternion");
    mutate(f.data[0] + 12, std::bit_cast<uint32_t>(1.01f), "non-unit quaternion");
    mutate(f.data[1] + 12, 0, "bad second entry discards first");
    bad = f.bytes; Patch(bad, f.data[0] + 12, std::bit_cast<uint32_t>(1.004f));
    CHECK(Load(bad, table, error));
    CHECK(Near(PoseDot(table.entries[0].data[0].rotation, table.entries[0].data[0].rotation), 1));
    CHECK(Load(Make(8, 8, 1, 255).bytes, table, error));
    CHECK(Load(Make(1, 4096, 1).bytes, table, error));
    CHECK(Load(Make(1, 8, 512).bytes, table, error));
    CHECK(AccessoryLeaf("\xe5\xb0\xbe\xe5\xb7\xb4_jnt") && AccessoryLeaf("\xf0\x9f\x98\x80") && !AccessoryLeaf(std::string("\xc0\x80", 2)) &&
        !AccessoryLeaf(std::string("\xed\xa0\x80", 3)) && !AccessoryLeaf(std::string("\xf4\x90\x80\x80", 4)) &&
        !AccessoryLeaf(std::string("\xe4\xb8", 2)) && !AccessoryLeaf(".."));
    uint32_t mask = 123;
    CHECK(AccessoryMask("15", mask) && mask == 15 && AccessoryMask("0x0F", mask) && mask == 15 &&
        AccessoryMask("0XFFFFFFFF", mask) && mask == UINT32_MAX && AccessoryMask("4294967295", mask) && mask == UINT32_MAX &&
        AccessoryMask("010", mask) && mask == 10 && AccessoryMask("0", mask) && mask == 0);
    for (auto value : {"", "0x", "-1", "+1", "4294967296", "0x100000000", "15junk", "0xfz", " 1"}) CHECK(!AccessoryMask(value, mask));
    double phase = 0;
    for (auto u : {.126, .315, .504, -.063}) CHECK(AccessoryPhase(u, .315, .126, phase) && (Near(phase, 0) || Near(phase, 1)));
    CHECK(AccessoryPhase(.316, .315, .126, phase) && Near(phase, .001 / .189));
    CHECK(AccessoryPhase(.125, .315, .126, phase) && Near(phase, 1 - .001 / .189));
    CHECK(!AccessoryPhase(0, 1, 1, phase) && !AccessoryPhase(INFINITY, 1, 0, phase) && !AccessoryPhase(0, NAN, 0, phase));
    CHECK(Load(f.bytes, table, error));
    const auto& entry = table.entries[0]; BonePose pose, other;
    CHECK(entry.Matches(-445642423, .31505f, .12605f) && !entry.Matches(-445642423, .3152f, .126f) && !entry.Matches(528430122, .315f, .126f));
    CHECK(entry.Sample(1, 2.0 / 8, pose) && Near(pose.position.x, 2) && Near(pose.position.y, 1));
    CHECK(entry.Sample(0, .5 / 8, pose) && Near(pose.position.x, .5) && Near(pose.rotation.z, std::sin(.05)) && Near(pose.rotation.w, std::cos(.05)));
    CHECK(entry.Sample(0, 7.5 / 8, pose) && Near(pose.position.x, 3.5) && Near(std::fabs(pose.rotation.z), std::sin(.35)));
    CHECK(entry.Sample(0, -1.0 / 16, other) && PoseSame(pose.position, other.position) && PoseSame(pose.rotation, other.rotation));
    CHECK(entry.Sample(0, 1, pose) && Near(pose.position.x, 0) && entry.Sample(0, -1e-300, pose));
    CHECK(!entry.Sample(2, 0, pose) && !entry.Sample(0, NAN, pose));
    auto extreme = table.entries[0]; extreme.data[0].position.x = std::numeric_limits<float>::max(); extreme.data[2].position.x = -std::numeric_limits<float>::max();
    CHECK(extreme.Sample(0, .5 / 8, pose) && std::isfinite(pose.position.x) && pose.position.x == 0);
    // Boundaries: a true minimum table and stream failure after a valid load.
    const auto minimum = Make(1, 8, 1, 1);
    CHECK(minimum.bytes.size() == 264);
    CHECK(Load(minimum.bytes, table, error));
    for (unsigned k = 0; k < 8; ++k) {
        CHECK(table.entries[0].Sample(0, double(k) / 8, pose) && Near(pose.position.x, k));
    }
    for (size_t i = 0; i < minimum.bytes.size(); ++i) reject(minimum.bytes.substr(0, i), "minimum table every truncation");
    CHECK(Load(minimum.bytes, table, error));
    std::istringstream throwing(""); throwing.exceptions(std::ios::failbit | std::ios::badbit);
    CHECK(!table.Load(throwing, error) && table.entries.empty());
    auto duplicates = Make(1, 8, 2, 1);
    CHECK(Load(duplicates.bytes, table, error) && table.entries[0].bones[0].name == table.entries[0].bones[1].name);
    CHECK(table.entries[0].Sample(0, std::nextafter(1.0, 0.0), pose));
    // Deterministic malformed-byte smoke fuzz: public Sample must remain safe on every accepted table.
    uint32_t rng = 0x1532;
    for (unsigned i = 0; i < 2000; ++i) {
        auto bytes = f.bytes; rng = rng * 1664525u + 1013904223u; const auto index = rng % bytes.size();
        rng = rng * 1664525u + 1013904223u; bytes[index] = char(rng >> 24);
        if (Load(bytes, table, error)) for (const auto& e : table.entries) for (size_t b = 0; b < e.bones.size(); ++b)
            CHECK(e.Sample(b, .999, pose) && std::isfinite(PoseDot(pose.rotation, pose.rotation)));
        else CHECK(table.entries.empty());
    }
}

int main() { Tests(); std::puts("Actions accessory table: parsing, masks and sampling passed."); }
