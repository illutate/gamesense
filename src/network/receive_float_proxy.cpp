#include "skeet/network/receive_float_proxy.hpp"
#include "skeet/engine/native_data_image.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <xmmintrin.h>
#include <emmintrin.h>

namespace skeet::network {
namespace {
ReceiveFloatProxyFunction* original_cell = nullptr;
template <class T> T read(const void* object, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::size_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
std::uint32_t shift_left(std::uint32_t value, std::uint32_t count) {
    return value << (count & 31);
}
std::uint32_t shift_right(std::uint32_t value, std::uint32_t count) {
    return value >> (count & 31);
}
std::uint32_t mask(std::uint32_t bits) {
    return bits == 32 ? 0xffffffffu : shift_left(1, bits) - 1;
}
float rounded_float(double value) {
    return _mm_cvtss_f32(_mm_cvtpd_ps(_mm_set_sd(value)));
}
double unsigned_double(std::uint32_t value) {
    // Native signed CVTDQ2PD plus the unsigned2^32 correction also preserves
    // positive zero under MXCSR round-down. C++'s generic uint32 conversion
    // may instead lower to an equal-double subtraction producing minus zero.
    const auto integer = _mm_cvtsi32_si128(std::bit_cast<std::int32_t>(value));
    return _mm_cvtsd_f64(
        _mm_add_sd(_mm_cvtepi32_pd(integer), _mm_set_sd(value >> 31 ? 4294967296.0 : 0.0)));
}
float multiply(float first, float second) {
    return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(first), _mm_set_ss(second)));
}
//433EC30F truncates a signed64-bit integer and this caller consumes only
// EAX. Its nonfinite/out-of-range indefinite result has a zero low dword.
std::uint32_t truncated_low_word(double value) {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    const auto exponent = std::int32_t((bits >> 52) & 0x7ff) - 1023;
    if (exponent < 0 || exponent >= 63)
        return 0;
    const auto significand = (bits & 0x000fffffffffffffull) | 0x0010000000000000ull;
    const auto magnitude = exponent >= 52 ? significand << unsigned(exponent - 52)
                                          : significand >> unsigned(52 - exponent);
    return std::uint32_t(bits >> 63 ? 0 - magnitude : magnitude);
}
bool scale_fits(float scale, double extent, std::uint32_t maximum) {
    const double product = double(scale) * extent;
    // Native COMISD/JBE accepts an unordered product in this exact gate.
    return truncated_low_word(product) <= maximum && !(product > unsigned_double(maximum));
}
void initialize_scale(void* descriptor) {
    if (read<float>(descriptor, 0x34) != 0.0f)
        return;
    const auto high = read<float>(descriptor, 0x14), low = read<float>(descriptor, 0x10);
    const auto width = _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(high), _mm_set_ss(low)));
    const auto bits = read<std::uint32_t>(descriptor, 0x0c);
    const auto maximum = bits == 32 ? 0xfffffffeu : shift_left(1, bits) - 1;
    const double extent = double(width), upper = unsigned_double(maximum);
    const float absolute = std::bit_cast<float>(std::bit_cast<std::uint32_t>(width) & 0x7fffffffu);
    const double ratio = absolute <= 0.0010000000474974513f ? upper : upper / extent;
    auto scale = rounded_float(ratio);
    if (!scale_fits(scale, extent, maximum)) {
        constexpr std::array factors{0.9998999834060669f, 0.9900000095367432f, 0.8999999761581421f,
                                     0.800000011920929f, 0.699999988079071f};
        bool selected = false;
        const auto initial = rounded_float(upper / extent);
        for (const auto factor : factors) {
            scale = multiply(factor, initial);
            if (scale_fits(scale, extent, maximum)) {
                selected = true;
                break;
            }
        }
        if (!selected)
            scale = 0.0f;
    }
    write(descriptor, 0x34, scale);
}
std::uint32_t read_bits(void* reader, std::uint32_t bits, bool& failed) {
    const auto available = read<std::int32_t>(reader, 0x14);
    const auto cache = read<std::uint32_t>(reader, 0x10);
    auto* next = read<const std::uint32_t*>(reader, 0x18);
    const auto* end = read<const std::uint32_t*>(reader, 0x1c);
    const auto next_address = reinterpret_cast<std::uintptr_t>(next);
    const auto end_address = reinterpret_cast<std::uintptr_t>(end);
    const auto advance = [&] {
        write(reader, 0x18, reinterpret_cast<const std::uint32_t*>(next_address + 4));
    };
    if (available >= std::int32_t(bits)) {
        const auto result = cache & mask(bits);
        const auto remaining = std::uint32_t(available) - bits;
        write(reader, 0x14, std::bit_cast<std::int32_t>(remaining));
        if (remaining)
            write(reader, 0x10, shift_right(cache, bits));
        else {
            write(reader, 0x14, std::int32_t(32));
            std::uint32_t replacement = 0;
            if (next_address == end_address) {
                advance();
                write(reader, 0x14, std::int32_t(1));
            } else if (next_address > end_address)
                write(reader, 4, std::uint8_t(1));
            else {
                replacement = read<std::uint32_t>(next, 0);
                advance();
            }
            write(reader, 0x10, replacement);
        }
        return result;
    }
    const auto missing = bits - std::uint32_t(available);
    std::uint32_t replacement = 0;
    if (next_address == end_address) {
        advance();
        write(reader, 0x14, std::int32_t(1));
        write(reader, 4, std::uint8_t(1));
        failed = true;
    } else if (next_address > end_address) {
        write(reader, 4, std::uint8_t(1));
        failed = true;
    } else {
        replacement = read<std::uint32_t>(next, 0);
        advance();
        failed = read<std::uint8_t>(reader, 4) != 0;
    }
    write(reader, 0x10, replacement);
    if (failed)
        return 0;
    write(reader, 0x14, std::int32_t(32 - missing));
    const auto result = shift_left(replacement & mask(missing), std::uint32_t(available)) | cache;
    write(reader, 0x10, shift_right(replacement, missing));
    return result;
}
}
void receive_float_value(void* argument, ReceiveFloatProxyFunction const& original) {
    auto* const descriptor = read<void*>(argument, 0x30);
    if (read<std::uint32_t>(descriptor, 0x3c) & 0x3f026u) {
        original(argument);
        return;
    }
    initialize_scale(descriptor);
    auto* const reader = read<void*>(argument, 0x34);
    const auto bits = read<std::uint32_t>(descriptor, 0x0c);
    bool failed = false;
    const auto encoded = read_bits(reader, bits, failed);
    float value;
    if (failed || encoded == 0)
        value = read<float>(descriptor, 0x10);
    else if (encoded >= shift_left(1, bits) - 1)
        value = read<float>(descriptor, 0x14);
    else {
        const auto converted = rounded_float(unsigned_double(encoded));
        const auto scaled =
            _mm_div_ss(_mm_set_ss(converted), _mm_set_ss(read<float>(descriptor, 0x34)));
        value = _mm_cvtss_f32(_mm_add_ss(scaled, _mm_set_ss(read<float>(descriptor, 0x10))));
    }
    write(argument, 8, value);
    if (auto* const property = read<const void*>(argument, 0)) {
        using Proxy = void(__cdecl*)(void*, std::uintptr_t, std::uintptr_t);
        const auto fourth = read<std::uintptr_t>(argument, 0x2c);
        const auto third = read<std::uintptr_t>(argument, 0x28);
        read<Proxy>(property, 0x20)(argument, third, fourth);
    }
}
void bind_receive_float_proxy(engine::NativeDataImage& data) noexcept {
    original_cell = &data.native_cell<ReceiveFloatProxyFunction>(0x43476A44u);
}
void __cdecl receive_float_proxy(void* argument) {
    receive_float_value(argument, *original_cell);
}
} //namespace skeet::network
