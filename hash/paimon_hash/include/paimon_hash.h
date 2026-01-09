// inimal C++ implementation of Paimon row hash (Murmur3_32, seed=42)
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace paimon_hash {

// Murmur3 constants (same as Paimon/Java implementation)
static inline uint32_t c1() {
    return 0xcc9e2d51u;
}
static inline uint32_t c2() {
    return 0x1b873593u;
}
static inline uint32_t default_seed() {
    return 42u;
}

// Rotate left (32-bit)
static inline uint32_t rotl32(uint32_t x, int8_t r) {
    return (x << r) | (x >> (32 - r));
}

// Mix functions (match MurmurHashUtils in Paimon)
static inline uint32_t mix_k1(uint32_t k1) {
    k1 *= c1();
    k1 = rotl32(k1, 15);
    k1 *= c2();
    return k1;
}

static inline uint32_t mix_h1(uint32_t h1, uint32_t k1) {
    h1 ^= k1;
    h1 = rotl32(h1, 13);
    h1 = h1 * 5u + 0xe6546b64u;
    return h1;
}

static inline uint32_t fmix32_len(uint32_t h1, uint32_t length) {
    h1 ^= length;
    // finalization avalanche (same constants as Java)
    h1 ^= (h1 >> 16);
    h1 *= 0x85ebca6bu;
    h1 ^= (h1 >> 13);
    h1 *= 0xc2b2ae35u;
    h1 ^= (h1 >> 16);
    return h1;
}

// Read 32-bit little-endian word from bytes (unaligned-safe)
static inline uint32_t read_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Compute Murmur3_32 identical to Paimon’s MemorySegmentUtils.hashByWords
// - Requires len % 4 == 0
// - Uses seed 42 by default
inline int32_t paimon_hash_by_words(const void* data, size_t len, uint32_t seed = default_seed()) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t h1 = seed;

    // length must be 4-byte aligned (assert to catch misuse in dev; safe in release)
    // This mirrors the Java assert in MurmurHashUtils.hashBytesByInt
    if ((len & 3u) != 0u) {
        // Fallback behavior: process only full 4-byte words like Java version
        // that requires alignment. Tail is ignored to match hashByWords.
        size_t aligned = len & ~3u;
        for (size_t i = 0; i < aligned; i += 4) {
            uint32_t k1 = read_u32_le(bytes + i);
            k1 = mix_k1(k1);
            h1 = mix_h1(h1, k1);
        }
        return static_cast<int32_t>(fmix32_len(h1, static_cast<uint32_t>(len)));
    }

    for (size_t i = 0; i < len; i += 4) {
        uint32_t k1 = read_u32_le(bytes + i);
        k1 = mix_k1(k1);
        h1 = mix_h1(h1, k1);
    }
    return static_cast<int32_t>(fmix32_len(h1, static_cast<uint32_t>(len)));
}

// Compute Murmur3_32 identical to Paimon’s MemorySegmentUtils.hash / MurmurHashUtils.hashBytes
// - Handles tail bytes (1..3) the same as Java implementation
inline int32_t paimon_hash(const void* data, size_t len, uint32_t seed = default_seed()) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    size_t aligned = len & ~3u; // len - (len % 4)

    uint32_t h1 = seed;

    // process 4-byte words
    for (size_t i = 0; i < aligned; i += 4) {
        uint32_t k1 = read_u32_le(bytes + i);
        k1 = mix_k1(k1);
        h1 = mix_h1(h1, k1);
    }

    // process tail bytes one-by-one, matching Java's signed byte behavior
    for (size_t i = aligned; i < len; ++i) {
        int32_t half = static_cast<int8_t>(bytes[i]); // sign-extend like Java byte -> int
        uint32_t k1 = mix_k1(static_cast<uint32_t>(half));
        h1 = mix_h1(h1, k1);
    }

    return static_cast<int32_t>(fmix32_len(h1, static_cast<uint32_t>(len)));
}

// Compute bucket index like PaimonBucketFunction: Math.abs(hash % numBuckets)
inline int32_t paimon_bucket_from_hash(int32_t hash, int32_t num_buckets) {
    if (num_buckets <= 0) return 0; // guard
    int32_t r = hash % num_buckets;
    if (r < 0) r = -r;
    return r;
}

inline int32_t paimon_bucket_by_words(const void* data, size_t len, int32_t num_buckets) {
    return paimon_bucket_from_hash(paimon_hash_by_words(data, len), num_buckets);
}

inline int32_t paimon_bucket(const void* data, size_t len, int32_t num_buckets) {
    return paimon_bucket_from_hash(paimon_hash(data, len), num_buckets);
}

class BinaryRowBuilder {
public:
    explicit BinaryRowBuilder(int arity, std::size_t initial_var_cap = 64)
            : arity_(arity),
              null_bits_size_(calculate_bitset_width_in_bytes(arity)),
              fixed_size_(null_bits_size_ + static_cast<std::size_t>(arity) * 8u),
              cursor_(fixed_size_) {
        buf_.assign(fixed_size_ + initial_var_cap, 0u);
        // RowKind header is byte 0; zero means INSERT. Null bits are zeroed by assign.
    }

    void reset() {
        // Zero header + null bits; keep fixed-slot values as zero until written.
        if (buf_.size() < fixed_size_) buf_.resize(fixed_size_, 0u);
        std::fill(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(null_bits_size_), 0u);
        // Keep header 0 (=INSERT) and clear fixed part as well for determinism.
        std::fill(buf_.begin() + static_cast<std::ptrdiff_t>(null_bits_size_),
                  buf_.begin() + static_cast<std::ptrdiff_t>(fixed_size_), 0u);
        cursor_ = fixed_size_;
    }

    // Null handling
    void set_null_at(int pos) {
        check_pos(pos);
        set_null_bit(pos);
        // Zero the 8-byte fixed slot
        std::memset(&buf_[field_offset(pos)], 0, 8);
    }

    // Fixed-length primitives (native-endian, like Java Unsafe putX)
    void write_boolean(int pos, bool v) { write_primitive(pos, &v, 1); }
    void write_byte(int pos, int8_t v) { write_primitive(pos, &v, 1); }
    void write_short(int pos, int16_t v) { write_primitive(pos, &v, 2); }
    void write_int(int pos, int32_t v) { write_primitive(pos, &v, 4); }
    void write_long(int pos, int64_t v) { write_primitive(pos, &v, 8); }
    void write_float(int pos, float v) { write_primitive(pos, &v, 4); }
    void write_double(int pos, double v) { write_primitive(pos, &v, 8); }

    // UTF-8 string (small-in-fixed if len <= 7, else var-part)
    void write_string(int pos, const char* data, std::size_t len) {
        check_pos(pos);
        if (len <= 7u) {
            write_bytes_to_fixed(field_offset(pos), data, len);
        } else {
            write_bytes_to_var(pos, data, len);
        }
    }
    void write_string(int pos, const std::string& s) { write_string(pos, s.data(), s.size()); }

    // Final size of the row (used size of buffer)
    std::size_t size() const { return cursor_; }
    const uint8_t* data() const { return buf_.data(); }
    uint8_t* data() { return buf_.data(); }

    // Hash and bucket helpers
    int32_t hash_code() const { return paimon_hash_by_words(buf_.data(), cursor_); }
    int32_t bucket(int32_t num_buckets) const { return paimon_bucket_from_hash(hash_code(), num_buckets); }

private:
    // Layout helpers
    static std::size_t calculate_bitset_width_in_bytes(int arity) {
        return static_cast<std::size_t>(((arity + 63 + 8) / 64) * 8);
    }
    std::size_t field_offset(int pos) const { return null_bits_size_ + static_cast<std::size_t>(pos) * 8u; }
    static bool is_little_endian() {
        const uint16_t x = 0x0102;
        return *reinterpret_cast<const uint8_t*>(&x) == 0x02;
    }

    void check_pos(int pos) const {
        (void)pos; // simple assert-like check in release builds
        // Optional: add runtime checks if desired.
    }

    void ensure_capacity(std::size_t need) {
        if (buf_.size() >= need) return;
        std::size_t old = buf_.size();
        std::size_t neu = old + (old >> 1);
        if (neu < need) neu = need;
        buf_.resize(neu, 0u);
    }

    static std::size_t round_to_nearest_word(std::size_t n) {
        std::size_t rem = (n & 0x07u);
        return rem == 0 ? n : (n + (8u - rem));
    }

    void set_null_bit(int ordinal) {
        // Bit index offset by header 8 bits
        int bit_index = ordinal + 8;
        std::size_t byte_index = static_cast<std::size_t>(bit_index >> 3);
        int bit = bit_index & 7;
        buf_[byte_index] = static_cast<uint8_t>(buf_[byte_index] | (1u << bit));
    }

    void write_primitive(int pos, const void* src, std::size_t len) {
        check_pos(pos);
        std::size_t off = field_offset(pos);
        // Ensure fixed part exists
        if (buf_.size() < off + len) ensure_capacity(off + len);
        std::memcpy(&buf_[off], src, len);
        // Leave remaining bytes of the 8-byte slot as zero (buffer initialized)
    }

    void write_bytes_to_fixed(std::size_t field_off, const char* bytes, std::size_t len) {
        // Encode like AbstractBinaryWriter.writeBytesToFixLenPart
        // Build the 64-bit value in host-endian and memcpy it.
        uint64_t first_byte = static_cast<uint64_t>((len & 0x7Fu) | 0x80u); // mark + len
        uint64_t seven = 0u;
        if (is_little_endian()) {
            for (std::size_t i = 0; i < len; ++i) {
                seven |= (static_cast<uint64_t>(static_cast<uint8_t>(bytes[i])) << (i * 8u));
            }
        } else {
            for (std::size_t i = 0; i < len; ++i) {
                seven |= (static_cast<uint64_t>(static_cast<uint8_t>(bytes[i])) << ((6u - i) * 8u));
            }
        }
        uint64_t offset_and_size = (first_byte << 56) | seven;
        if (buf_.size() < field_off + 8) ensure_capacity(field_off + 8);
        std::memcpy(&buf_[field_off], &offset_and_size, 8);
    }

    void write_bytes_to_var(int pos, const char* bytes, std::size_t len) {
        std::size_t rounded = round_to_nearest_word(len);
        ensure_capacity(cursor_ + rounded);
        // Write payload
        std::memcpy(&buf_[cursor_], bytes, len);
        // Zero padding
        if (rounded > len) {
            std::memset(&buf_[cursor_ + len], 0, rounded - len);
        }
        // Write (offset,len) into fixed slot
        uint64_t off = static_cast<uint64_t>(cursor_ & 0xFFFFFFFFu);
        uint64_t offs_and_len = (off << 32) | static_cast<uint64_t>(len & 0xFFFFFFFFu);
        std::size_t foff = field_offset(pos);
        if (buf_.size() < foff + 8) ensure_capacity(foff + 8);
        std::memcpy(&buf_[foff], &offs_and_len, 8);
        cursor_ += rounded;
    }

    int arity_;
    std::size_t null_bits_size_;
    std::size_t fixed_size_;
    std::vector<uint8_t> buf_;
    std::size_t cursor_;
};

} // namespace paimon_hash
