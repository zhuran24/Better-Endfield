#pragma once
#include "accessory_table.h"
#include <sstream>
using namespace BetterEndfieldNext::Actions;
namespace {
bool Near(double a, double b, double tolerance = 1e-5) { return std::fabs(a - b) < tolerance; }
void Word(std::string& bytes, uint32_t value, size_t size = 4) {
    for (size_t i = 0; i < size; ++i) bytes.push_back(static_cast<char>((value >> (8 * i)) & 255));
}
void Float(std::string& bytes, float value) { Word(bytes, std::bit_cast<uint32_t>(value)); }
void Patch(std::string& bytes, size_t offset, uint32_t value, size_t size = 4) {
    for (size_t i = 0; i < size; ++i) bytes.at(offset + i) = static_cast<char>((value >> (8 * i)) & 255);
}
struct Fixture {
    std::string bytes;
    std::vector<size_t> entries, names, data;
};
Fixture Make(uint32_t count = 2, uint32_t n = 8, uint32_t m = 2, size_t name_length = 0) {
    Fixture f; f.bytes = "BEACC001"; Word(f.bytes, 1); Word(f.bytes, count);
    for (uint32_t e = 0; e < count; ++e) {
        f.entries.push_back(f.bytes.size());
        Word(f.bytes, std::bit_cast<uint32_t>(e % 2 ? int32_t(528430122) : int32_t(-445642423)));
        Float(f.bytes, .315f); Float(f.bytes, .126f); Word(f.bytes, n); Word(f.bytes, m);
        for (uint32_t b = 0; b < m; ++b) {
            f.names.push_back(f.bytes.size());
            const auto name = name_length ? std::string(name_length, 'a') : b == 0 ? "tail_01_jnt" : "bone_" + std::to_string(b);
            Word(f.bytes, b == 0 ? 0 : 31, 1); Word(f.bytes, static_cast<uint32_t>(name.size()), 2); f.bytes += name;
        }
        f.data.push_back(f.bytes.size());
        for (uint32_t k = 0; k < n; ++k) for (uint32_t b = 0; b < m; ++b) {
            // Odd samples use the antipodal quaternion, physically identical.
            const float angle = float(k) * .2f, sign = k % 2 ? -1.f : 1.f;
            Float(f.bytes, 0); Float(f.bytes, 0); Float(f.bytes, sign * std::sin(angle / 2)); Float(f.bytes, sign * std::cos(angle / 2));
            Float(f.bytes, float(k)); Float(f.bytes, float(b)); Float(f.bytes, float(e));
        }
    }
    return f;
}
bool Load(const std::string& bytes, AccessoryTable& table, std::string& error) {
    std::istringstream input(bytes, std::ios::binary); return table.Load(input, error);
}
}
