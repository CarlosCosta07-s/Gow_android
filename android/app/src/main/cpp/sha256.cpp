#include "sha256.h"

#include <algorithm>

namespace gow::disc {
namespace {
constexpr std::array<uint32_t, 64> kRoundConstants{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

constexpr uint32_t rotateRight(uint32_t value, unsigned count) noexcept {
    return (value >> count) | (value << (32u - count));
}

uint32_t readBigEndian32(const uint8_t *source) noexcept {
    return (static_cast<uint32_t>(source[0]) << 24u) |
           (static_cast<uint32_t>(source[1]) << 16u) |
           (static_cast<uint32_t>(source[2]) << 8u) |
           static_cast<uint32_t>(source[3]);
}
} // namespace

Sha256::Sha256() noexcept
    : state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
             0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u} {
}

void Sha256::transform(const uint8_t *block) noexcept {
    std::array<uint32_t, 64> words{};
    for (size_t i = 0u; i < 16u; ++i) words[i] = readBigEndian32(block + i * 4u);
    for (size_t i = 16u; i < words.size(); ++i) {
        const uint32_t s0 = rotateRight(words[i - 15u], 7u) ^
                            rotateRight(words[i - 15u], 18u) ^
                            (words[i - 15u] >> 3u);
        const uint32_t s1 = rotateRight(words[i - 2u], 17u) ^
                            rotateRight(words[i - 2u], 19u) ^
                            (words[i - 2u] >> 10u);
        words[i] = words[i - 16u] + s0 + words[i - 7u] + s1;
    }

    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (size_t i = 0u; i < words.size(); ++i) {
        const uint32_t sum1 = rotateRight(e, 6u) ^ rotateRight(e, 11u) ^
                              rotateRight(e, 25u);
        const uint32_t choose = (e & f) ^ (~e & g);
        const uint32_t temp1 = h + sum1 + choose + kRoundConstants[i] + words[i];
        const uint32_t sum0 = rotateRight(a, 2u) ^ rotateRight(a, 13u) ^
                              rotateRight(a, 22u);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = sum0 + majority;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void Sha256::update(const void *data, size_t size) noexcept {
    if (finished_ || data == nullptr || size == 0u) return;
    const auto *bytes = static_cast<const uint8_t *>(data);
    bitCount_ += static_cast<uint64_t>(size) * 8u;
    size_t offset = 0u;
    while (offset < size) {
        const size_t count = std::min(block_.size() - blockSize_, size - offset);
        std::copy_n(bytes + offset, count,
                    block_.begin() + static_cast<std::ptrdiff_t>(blockSize_));
        blockSize_ += count;
        offset += count;
        if (blockSize_ == block_.size()) {
            transform(block_.data());
            blockSize_ = 0u;
        }
    }
}

std::array<uint8_t, 32> Sha256::finish() noexcept {
    if (!finished_) {
        const uint64_t originalBitCount = bitCount_;
        block_[blockSize_++] = 0x80u;
        if (blockSize_ > 56u) {
            std::fill(block_.begin() + static_cast<std::ptrdiff_t>(blockSize_),
                      block_.end(), 0u);
            transform(block_.data());
            blockSize_ = 0u;
        }
        std::fill(block_.begin() + static_cast<std::ptrdiff_t>(blockSize_),
                  block_.begin() + 56, 0u);
        for (size_t i = 0u; i < 8u; ++i)
            block_[63u - i] = static_cast<uint8_t>(originalBitCount >> (i * 8u));
        transform(block_.data());
        finished_ = true;
    }

    std::array<uint8_t, 32> digest{};
    for (size_t i = 0u; i < state_.size(); ++i) {
        digest[i * 4u] = static_cast<uint8_t>(state_[i] >> 24u);
        digest[i * 4u + 1u] = static_cast<uint8_t>(state_[i] >> 16u);
        digest[i * 4u + 2u] = static_cast<uint8_t>(state_[i] >> 8u);
        digest[i * 4u + 3u] = static_cast<uint8_t>(state_[i]);
    }
    return digest;
}

std::string sha256Hex(const std::array<uint8_t, 32> &digest) {
    constexpr char kHex[] = "0123456789abcdef";
    std::string output(64u, '0');
    for (size_t i = 0u; i < digest.size(); ++i) {
        output[i * 2u] = kHex[digest[i] >> 4u];
        output[i * 2u + 1u] = kHex[digest[i] & 0x0fu];
    }
    return output;
}
} // namespace gow::disc
