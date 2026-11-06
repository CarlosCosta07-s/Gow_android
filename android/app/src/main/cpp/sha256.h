#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace gow::disc {

class Sha256 {
public:
    Sha256() noexcept;
    void update(const void *data, size_t size) noexcept;
    std::array<uint8_t, 32> finish() noexcept;

private:
    void transform(const uint8_t *block) noexcept;
    std::array<uint32_t, 8> state_{};
    std::array<uint8_t, 64> block_{};
    uint64_t bitCount_ = 0u;
    size_t blockSize_ = 0u;
    bool finished_ = false;
};

std::string sha256Hex(const std::array<uint8_t, 32> &digest);

} // namespace gow::disc
