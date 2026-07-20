/**
 * @file utils.cpp
 * @brief Implementation of internal helpers: random tokens, SHA-256/HMAC,
 *        base64url and HTTP date formatting/parsing.
 */

#include "socketify/detail/utils.h"

#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <locale>
#include <random>
#include <sstream>

namespace socketify::detail {

// ---------------------------------------------------------------------------
// Hex / random
// ---------------------------------------------------------------------------

std::string hex_encode(const unsigned char* data, std::size_t len) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.resize(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out[2 * i]     = digits[data[i] >> 4];
        out[2 * i + 1] = digits[data[i] & 0x0F];
    }
    return out;
}

std::string random_token(std::size_t bytes) {
    // std::random_device on Linux reads from /dev/urandom (CSPRNG).
    std::random_device rd;
    std::string raw(bytes, '\0');
    std::size_t i = 0;
    while (i < bytes) {
        unsigned int v = rd();
        std::size_t take = std::min(sizeof(v), bytes - i);
        std::memcpy(&raw[i], &v, take);
        i += take;
    }
    return hex_encode(reinterpret_cast<const unsigned char*>(raw.data()), raw.size());
}

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4), self-contained so it works without OpenSSL
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline std::uint32_t rotr(std::uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

struct Sha256Ctx {
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint64_t total = 0;
    unsigned char buf[64];
    std::size_t buflen = 0;

    void process(const unsigned char* p) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(p[4 * i]) << 24) | (std::uint32_t(p[4 * i + 1]) << 16) |
                   (std::uint32_t(p[4 * i + 2]) << 8) | std::uint32_t(p[4 * i + 3]);
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            std::uint32_t ch = (e & f) ^ (~e & g);
            std::uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            std::uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const unsigned char* data, std::size_t len) {
        total += len;
        while (len > 0) {
            std::size_t take = std::min(len, sizeof(buf) - buflen);
            std::memcpy(buf + buflen, data, take);
            buflen += take;
            data += take;
            len -= take;
            if (buflen == 64) {
                process(buf);
                buflen = 0;
            }
        }
    }

    std::array<std::uint8_t, 32> finish() {
        std::uint64_t bits = total * 8;
        unsigned char pad = 0x80;
        update(&pad, 1);
        unsigned char zero = 0;
        while (buflen != 56) update(&zero, 1);
        unsigned char lenbuf[8];
        for (int i = 0; i < 8; ++i) lenbuf[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
        update(lenbuf, 8);
        std::array<std::uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i) {
            out[4 * i]     = static_cast<std::uint8_t>(h[i] >> 24);
            out[4 * i + 1] = static_cast<std::uint8_t>(h[i] >> 16);
            out[4 * i + 2] = static_cast<std::uint8_t>(h[i] >> 8);
            out[4 * i + 3] = static_cast<std::uint8_t>(h[i]);
        }
        return out;
    }
};

} // namespace

std::array<std::uint8_t, 32> sha256(std::string_view data) {
    Sha256Ctx ctx;
    ctx.update(reinterpret_cast<const unsigned char*>(data.data()), data.size());
    return ctx.finish();
}

std::array<std::uint8_t, 32> hmac_sha256(std::string_view key, std::string_view data) {
    unsigned char kblock[64] = {0};
    if (key.size() > 64) {
        auto kh = sha256(key);
        std::memcpy(kblock, kh.data(), kh.size());
    } else {
        std::memcpy(kblock, key.data(), key.size());
    }
    std::string inner;
    inner.resize(64);
    for (int i = 0; i < 64; ++i) inner[i] = static_cast<char>(kblock[i] ^ 0x36);
    inner.append(data);
    auto ih = sha256(inner);

    std::string outer;
    outer.resize(64);
    for (int i = 0; i < 64; ++i) outer[i] = static_cast<char>(kblock[i] ^ 0x5c);
    outer.append(reinterpret_cast<const char*>(ih.data()), ih.size());
    return sha256(outer);
}

// ---------------------------------------------------------------------------
// SHA-1 (FIPS 180-4) — used by Pulse WebSocket Accept
// ---------------------------------------------------------------------------

namespace {

inline std::uint32_t sha1_rotl(std::uint32_t x, unsigned n) {
    return (x << n) | (x >> (32 - n));
}

struct Sha1Ctx {
    std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::uint64_t total = 0;
    unsigned char buf[64];
    std::size_t buflen = 0;

    void process(const unsigned char* p) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(p[4 * i]) << 24) | (std::uint32_t(p[4 * i + 1]) << 16) |
                   (std::uint32_t(p[4 * i + 2]) << 8) | std::uint32_t(p[4 * i + 3]);
        for (int i = 16; i < 80; ++i)
            w[i] = sha1_rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            std::uint32_t t = sha1_rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = sha1_rotl(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

    void update(const unsigned char* data, std::size_t len) {
        total += len;
        while (len > 0) {
            std::size_t n = std::min(len, 64 - buflen);
            std::memcpy(buf + buflen, data, n);
            buflen += n;
            data += n;
            len -= n;
            if (buflen == 64) {
                process(buf);
                buflen = 0;
            }
        }
    }

    std::array<std::uint8_t, 20> finish() {
        std::uint64_t bits = total * 8;
        unsigned char pad = 0x80;
        update(&pad, 1);
        unsigned char zero = 0;
        while (buflen != 56) update(&zero, 1);
        unsigned char lenbuf[8];
        for (int i = 0; i < 8; ++i) lenbuf[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
        update(lenbuf, 8);
        std::array<std::uint8_t, 20> out{};
        for (int i = 0; i < 5; ++i) {
            out[4 * i]     = static_cast<std::uint8_t>(h[i] >> 24);
            out[4 * i + 1] = static_cast<std::uint8_t>(h[i] >> 16);
            out[4 * i + 2] = static_cast<std::uint8_t>(h[i] >> 8);
            out[4 * i + 3] = static_cast<std::uint8_t>(h[i]);
        }
        return out;
    }
};

} // namespace

std::array<std::uint8_t, 20> sha1(std::string_view data) {
    Sha1Ctx ctx;
    ctx.update(reinterpret_cast<const unsigned char*>(data.data()), data.size());
    return ctx.finish();
}

bool constant_time_equal(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return diff == 0;
}

// ---------------------------------------------------------------------------
// base64 (RFC 4648 §4, padded) and base64url (RFC 4648 §5, unpadded)
// ---------------------------------------------------------------------------

static constexpr char kB64Std[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64_encode(const unsigned char* data, std::size_t len) {
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 3 <= len) {
        std::uint32_t v = (std::uint32_t(data[i]) << 16) | (std::uint32_t(data[i + 1]) << 8) | data[i + 2];
        out.push_back(kB64Std[(v >> 18) & 63]);
        out.push_back(kB64Std[(v >> 12) & 63]);
        out.push_back(kB64Std[(v >> 6) & 63]);
        out.push_back(kB64Std[v & 63]);
        i += 3;
    }
    if (len - i == 1) {
        std::uint32_t v = std::uint32_t(data[i]) << 16;
        out.push_back(kB64Std[(v >> 18) & 63]);
        out.push_back(kB64Std[(v >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (len - i == 2) {
        std::uint32_t v = (std::uint32_t(data[i]) << 16) | (std::uint32_t(data[i + 1]) << 8);
        out.push_back(kB64Std[(v >> 18) & 63]);
        out.push_back(kB64Std[(v >> 12) & 63]);
        out.push_back(kB64Std[(v >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

std::string base64url_encode(const unsigned char* data, std::size_t len) {
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 3 <= len) {
        std::uint32_t v = (std::uint32_t(data[i]) << 16) | (std::uint32_t(data[i + 1]) << 8) | data[i + 2];
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back(kB64[(v >> 6) & 63]);
        out.push_back(kB64[v & 63]);
        i += 3;
    }
    if (len - i == 1) {
        std::uint32_t v = std::uint32_t(data[i]) << 16;
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
    } else if (len - i == 2) {
        std::uint32_t v = (std::uint32_t(data[i]) << 16) | (std::uint32_t(data[i + 1]) << 8);
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back(kB64[(v >> 6) & 63]);
    }
    return out;
}

std::optional<std::string> base64url_decode(std::string_view in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '-') return 62;
        if (c == '_') return 63;
        return -1;
    };
    if (in.size() % 4 == 1) return std::nullopt;
    std::string out;
    out.reserve(in.size() * 3 / 4);
    std::uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        int v = val(c);
        if (v < 0) return std::nullopt;
        acc = (acc << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// HTTP dates
// ---------------------------------------------------------------------------

std::string http_date(std::int64_t unix_seconds) {
    std::time_t t = static_cast<std::time_t>(unix_seconds);
    std::tm gmt{};
#if defined(_WIN32)
    gmtime_s(&gmt, &t);
#else
    gmtime_r(&t, &gmt);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &gmt);
    return std::string(buf);
}

std::string http_date_now() {
    using namespace std::chrono;
    return http_date(duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}

std::optional<std::int64_t> parse_http_date(std::string_view s) {
    std::tm tm{};
    std::istringstream iss{std::string(s)};
    iss.imbue(std::locale::classic());
    iss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S GMT");
    if (iss.fail()) return std::nullopt;
#if defined(_WIN32)
    return static_cast<std::int64_t>(_mkgmtime(&tm));
#else
    return static_cast<std::int64_t>(timegm(&tm));
#endif
}

} // namespace socketify::detail
