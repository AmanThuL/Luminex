//----------------------------------------------------------------------------------------------------------------------
/// @file Sha256.h
/// @brief Declares a self-contained SHA-256 hasher and its lowercase hex digest helper.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace lmx {

/// Minimal self-contained SHA-256 (FIPS 180-4), processed in fixed 512-bit chunks with no data-
/// dependent branching beyond the algorithm's own definition -- deterministic on every machine.
class Sha256 {
public:
    //==================================================================================================================
    /// Appends `data` to the message, hashing each complete 64-byte chunk as it fills.
    void update(std::span<const std::byte> data) {
        for (const std::byte b : data) {
            m_buffer[m_bufferLen++] = static_cast<uint8_t>(b);
            m_bitLength += 8;
            if (m_bufferLen == 64) {
                processChunk(m_buffer.data());
                m_bufferLen = 0;
            }
        }
    }

    //==================================================================================================================
    /// Pads the message, hashes its final chunks and returns the 32-byte big-endian digest. Call
    /// once; the hasher is not reusable afterwards.
    std::array<uint8_t, 32> finish() {
        uint64_t bitLength = m_bitLength;
        m_buffer[m_bufferLen++] = 0x80;
        if (m_bufferLen > 56) {
            std::fill(m_buffer.begin() + m_bufferLen, m_buffer.end(), uint8_t{0});
            processChunk(m_buffer.data());
            m_bufferLen = 0;
        }
        std::fill(m_buffer.begin() + m_bufferLen, m_buffer.begin() + 56, uint8_t{0});
        for (int i = 0; i < 8; ++i) {
            m_buffer[56 + i] = static_cast<uint8_t>(bitLength >> (56 - 8 * i));
        }
        processChunk(m_buffer.data());

        std::array<uint8_t, 32> digest{};
        for (int i = 0; i < 8; ++i) {
            digest[i * 4 + 0] = static_cast<uint8_t>(m_h[i] >> 24);
            digest[i * 4 + 1] = static_cast<uint8_t>(m_h[i] >> 16);
            digest[i * 4 + 2] = static_cast<uint8_t>(m_h[i] >> 8);
            digest[i * 4 + 3] = static_cast<uint8_t>(m_h[i]);
        }
        return digest;
    }

private:
    //==================================================================================================================
    static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

    //==================================================================================================================
    void processChunk(const uint8_t* chunk) {
        static constexpr uint32_t kK[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
            0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
            0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
            0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2};

        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(chunk[i * 4]) << 24) |
                   (static_cast<uint32_t>(chunk[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(chunk[i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(chunk[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3];
        uint32_t e = m_h[4], f = m_h[5], g = m_h[6], h = m_h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = h + s1 + ch + kK[i] + w[i];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        m_h[0] += a;
        m_h[1] += b;
        m_h[2] += c;
        m_h[3] += d;
        m_h[4] += e;
        m_h[5] += f;
        m_h[6] += g;
        m_h[7] += h;
    }

    std::array<uint32_t, 8> m_h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<uint8_t, 64> m_buffer{};
    size_t m_bufferLen = 0;
    uint64_t m_bitLength = 0;
};

/// Lowercase hex SHA-256 of `bytes`: the standard digest any conforming implementation computes,
/// with no external crypto dependency.
std::string sha256Hex(std::span<const std::byte> bytes);

} // namespace lmx
