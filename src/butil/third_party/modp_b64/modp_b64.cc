/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 4 -*- */
/* vi: set expandtab shiftwidth=4 tabstop=4: */
/**
 * \file
 * <PRE>
 * MODP_B64 - High performance base64 encoder/decoder
 * Version 1.3 -- 17-Mar-2006
 * http://modp.com/release/base64
 *
 * Copyright &copy; 2005, 2006  Nick Galbreath -- nickg [at] modp [dot] com
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 *   Neither the name of the modp.com nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This is the standard "new" BSD license:
 * http://www.opensource.org/licenses/bsd-license.php
 * </PRE>
 */

/* public header */
#include "modp_b64.h"

/*
 * If you are ripping this out of the library, comment out the next
 * line and uncomment the next lines as approrpiate
 */
// #include "config.h"

/* if on motoral, sun, ibm; uncomment this */
/* #define WORDS_BIGENDIAN 1 */
/* else for Intel, Amd; uncomment this */
/* #undef WORDS_BIGENDIAN */

#include <string.h>

#include "modp_b64_data.h"
#if defined(__x86_64__)
#include <immintrin.h>
#endif

#if defined(__riscv) && defined(__riscv_vector)
#define USE_RVV_BASE64 1
#else
#define USE_RVV_BASE64 0
#endif

#define BADCHAR 0x01FFFFFF

/**
 * you can control if we use padding by commenting out this
 * next line.  However, I highly recommend you use padding and not
 * using it should only be for compatability with a 3rd party.
 * Also, 'no padding' is not tested!
 */
#define DOPAD 1

/*
 * if we aren't doing padding
 * set the pad character to NULL
 */
#ifndef DOPAD
#undef CHARPAD
#define CHARPAD '\0'
#endif

#if defined(__x86_64__)
namespace {

inline void modp_b64_x86_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t* eax,
                               uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "0"(leaf), "2"(subleaf));
}

inline uint64_t modp_b64_x86_xgetbv(uint32_t xcr) {
    uint32_t eax = 0;
    uint32_t edx = 0;
    __asm__ volatile(".byte 0x0f, 0x01, 0xd0"
                     : "=a"(eax), "=d"(edx)
                     : "c"(xcr));
    return (static_cast<uint64_t>(edx) << 32) | eax;
}

bool modp_b64_detect_ssse3() {
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    modp_b64_x86_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 1) {
        return false;
    }
    modp_b64_x86_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    return (ecx & (1U << 9)) != 0;
}

bool modp_b64_detect_avx2() {
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    modp_b64_x86_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 7) {
        return false;
    }
    modp_b64_x86_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    const uint32_t kAvxMask = (1U << 26) | (1U << 27) | (1U << 28);
    if ((ecx & kAvxMask) != kAvxMask) {
        return false;
    }
    if ((modp_b64_x86_xgetbv(0) & 0x6ULL) != 0x6ULL) {
        return false;
    }
    modp_b64_x86_cpuid(7, 0, &eax, &ebx, &ecx, &edx);
    return (ebx & (1U << 5)) != 0;
}

bool modp_b64_has_ssse3() {
    static const bool has = modp_b64_detect_ssse3();
    return has;
}

bool modp_b64_has_avx2() {
    static const bool has = modp_b64_detect_avx2();
    return has;
}

}  // namespace
#endif  // defined(__x86_64__)

static size_t modp_b64_encode_scalar_tail(uint8_t* p, const char* str, size_t i,
                                          size_t len,
                                          const uint8_t* dest_start) {
    uint8_t t1, t2, t3;
    if (len > 2) {
        for (; i < len - 2; i += 3) {
            t1 = static_cast<uint8_t>(str[i]);
            t2 = static_cast<uint8_t>(str[i + 1]);
            t3 = static_cast<uint8_t>(str[i + 2]);
            *p++ = static_cast<uint8_t>(e0[t1]);
            *p++ = static_cast<uint8_t>(
                e1[((t1 & 0x03) << 4) | ((t2 >> 4) & 0x0F)]);
            *p++ = static_cast<uint8_t>(
                e1[((t2 & 0x0F) << 2) | ((t3 >> 6) & 0x03)]);
            *p++ = static_cast<uint8_t>(e2[t3]);
        }
    }

    switch (len - i) {
        case 0:
            break;
        case 1:
            t1 = static_cast<uint8_t>(str[i]);
            *p++ = static_cast<uint8_t>(e0[t1]);
            *p++ = static_cast<uint8_t>(e1[(t1 & 0x03) << 4]);
            *p++ = CHARPAD;
            *p++ = CHARPAD;
            break;
        default: /* case 2 */
            t1 = static_cast<uint8_t>(str[i]);
            t2 = static_cast<uint8_t>(str[i + 1]);
            *p++ = static_cast<uint8_t>(e0[t1]);
            *p++ = static_cast<uint8_t>(
                e1[((t1 & 0x03) << 4) | ((t2 >> 4) & 0x0F)]);
            *p++ = static_cast<uint8_t>(e2[(t2 & 0x0F) << 2]);
            *p++ = CHARPAD;
            break;
    }

    *p = '\0';
    return static_cast<size_t>(p - dest_start);
}

static size_t modp_b64_encode_scalar(char* dest, const char* str, size_t len) {
    uint8_t* p = reinterpret_cast<uint8_t*>(dest);
    return modp_b64_encode_scalar_tail(p, str, 0, len, p);
}

#if defined(__x86_64__)
__attribute__((target("ssse3"))) static size_t modp_b64_encode_ssse3(
    char* dest, const char* str, size_t len) {
    uint8_t* p = reinterpret_cast<uint8_t*>(dest);
    const uint8_t* dest_start = p;
    size_t i = 0;

    if (len >= 16) {
        const __m128i shuf =
            _mm_setr_epi8(1, 0, 2, 1, 4, 3, 5, 4, 7, 6, 8, 7, 10, 9, 11, 10);
        const __m128i mask0 = _mm_set1_epi32(0x0FC0FC00);
        const __m128i mul0 = _mm_set1_epi32(0x04000040);
        const __m128i mask1 = _mm_set1_epi32(0x003F03F0);
        const __m128i mul1 = _mm_set1_epi32(0x01000010);
        const __m128i c51 = _mm_set1_epi8(51);
        const __m128i c25 = _mm_set1_epi8(25);
        const __m128i offset_lut = _mm_setr_epi8(
            65, 71, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -19, -16, 0, 0);

        while (len - i >= 16) {
            __m128i in = _mm_loadu_si128(reinterpret_cast<const __m128i*>(
                static_cast<const void*>(str + i)));
            __m128i shuffled = _mm_shuffle_epi8(in, shuf);
            __m128i t0 = _mm_and_si128(shuffled, mask0);
            __m128i t1 = _mm_mulhi_epu16(t0, mul0);
            __m128i t2 = _mm_and_si128(shuffled, mask1);
            __m128i t3 = _mm_mullo_epi16(t2, mul1);
            __m128i indices = _mm_or_si128(t1, t3);

            __m128i red = _mm_subs_epu8(indices, c51);
            __m128i gt25 = _mm_cmpgt_epi8(indices, c25);
            __m128i lut_idx = _mm_sub_epi8(red, gt25);
            __m128i ascii =
                _mm_add_epi8(indices, _mm_shuffle_epi8(offset_lut, lut_idx));

            _mm_storeu_si128(reinterpret_cast<__m128i*>(static_cast<void*>(p)),
                             ascii);
            i += 12;
            p += 16;
        }
    }

    return modp_b64_encode_scalar_tail(p, str, i, len, dest_start);
}

__attribute__((target("avx2"))) static size_t modp_b64_encode_avx2(
    char* dest, const char* str, size_t len) {
    uint8_t* p = reinterpret_cast<uint8_t*>(dest);
    const uint8_t* dest_start = p;
    size_t i = 0;

    if (len >= 28) {
        const __m256i shuf =
            _mm256_setr_epi8(1, 0, 2, 1, 4, 3, 5, 4, 7, 6, 8, 7, 10, 9, 11, 10,
                             1, 0, 2, 1, 4, 3, 5, 4, 7, 6, 8, 7, 10, 9, 11, 10);
        const __m256i mask0 = _mm256_set1_epi32(0x0FC0FC00);
        const __m256i mul0 = _mm256_set1_epi32(0x04000040);
        const __m256i mask1 = _mm256_set1_epi32(0x003F03F0);
        const __m256i mul1 = _mm256_set1_epi32(0x01000010);
        const __m256i c51 = _mm256_set1_epi8(51);
        const __m256i c25 = _mm256_set1_epi8(25);
        const __m256i offset_lut = _mm256_setr_epi8(
            65, 71, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -19, -16, 0, 0, 65,
            71, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -19, -16, 0, 0);

        while (len - i >= 28) {
            __m128i lo = _mm_loadu_si128(reinterpret_cast<const __m128i*>(
                static_cast<const void*>(str + i)));
            __m128i hi = _mm_loadu_si128(reinterpret_cast<const __m128i*>(
                static_cast<const void*>(str + i + 12)));
            __m256i in =
                _mm256_inserti128_si256(_mm256_castsi128_si256(lo), hi, 1);

            __m256i shuffled = _mm256_shuffle_epi8(in, shuf);
            __m256i t0 = _mm256_and_si256(shuffled, mask0);
            __m256i t1 = _mm256_mulhi_epu16(t0, mul0);
            __m256i t2 = _mm256_and_si256(shuffled, mask1);
            __m256i t3 = _mm256_mullo_epi16(t2, mul1);
            __m256i indices = _mm256_or_si256(t1, t3);

            __m256i red = _mm256_subs_epu8(indices, c51);
            __m256i gt25 = _mm256_cmpgt_epi8(indices, c25);
            __m256i lut_idx = _mm256_sub_epi8(red, gt25);
            __m256i ascii = _mm256_add_epi8(
                indices, _mm256_shuffle_epi8(offset_lut, lut_idx));

            _mm256_storeu_si256(
                reinterpret_cast<__m256i*>(static_cast<void*>(p)), ascii);
            i += 24;
            p += 32;
        }
    }

    if (len - i >= 16) {
        const __m128i shuf =
            _mm_setr_epi8(1, 0, 2, 1, 4, 3, 5, 4, 7, 6, 8, 7, 10, 9, 11, 10);
        const __m128i offset_lut = _mm_setr_epi8(
            65, 71, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -19, -16, 0, 0);
        __m128i in = _mm_loadu_si128(reinterpret_cast<const __m128i*>(
            static_cast<const void*>(str + i)));
        __m128i shuffled = _mm_shuffle_epi8(in, shuf);
        __m128i t0 = _mm_and_si128(shuffled, _mm_set1_epi32(0x0FC0FC00));
        __m128i t1 = _mm_mulhi_epu16(t0, _mm_set1_epi32(0x04000040));
        __m128i t2 = _mm_and_si128(shuffled, _mm_set1_epi32(0x003F03F0));
        __m128i t3 = _mm_mullo_epi16(t2, _mm_set1_epi32(0x01000010));
        __m128i indices = _mm_or_si128(t1, t3);

        __m128i red = _mm_subs_epu8(indices, _mm_set1_epi8(51));
        __m128i gt25 = _mm_cmpgt_epi8(indices, _mm_set1_epi8(25));
        __m128i lut_idx = _mm_sub_epi8(red, gt25);
        __m128i ascii =
            _mm_add_epi8(indices, _mm_shuffle_epi8(offset_lut, lut_idx));

        _mm_storeu_si128(reinterpret_cast<__m128i*>(static_cast<void*>(p)),
                         ascii);
        i += 12;
        p += 16;
    }

    return modp_b64_encode_scalar_tail(p, str, i, len, dest_start);
}
#endif  // defined(__x86_64__)

size_t modp_b64_encode(char* dest, const char* str, size_t len) {
#if USE_RVV_BASE64
    return rvv_modp_b64_encode(dest, str, len);
#elif defined(__x86_64__)
    typedef size_t (*EncodeFn)(char*, const char*, size_t);
    static const EncodeFn fn =
        modp_b64_has_avx2() ? &modp_b64_encode_avx2
                            : (modp_b64_has_ssse3() ? &modp_b64_encode_ssse3
                                                    : &modp_b64_encode_scalar);
    return fn(dest, str, len);
#else
    return modp_b64_encode_scalar(dest, str, len);
#endif
}

#ifdef WORDS_BIGENDIAN /* BIG ENDIAN -- SUN / IBM / MOTOROLA */
int modp_b64_decode(char* dest, const char* src, int len) {
    if (len == 0) return 0;

#ifdef DOPAD
    /* if padding is used, then the message must be at least
       4 chars and be a multiple of 4.
       there can be at most 2 pad chars at the end */
    if (len < 4 || (len % 4 != 0)) return MODP_B64_ERROR;
    if (src[len - 1] == CHARPAD) {
        len--;
        if (src[len - 1] == CHARPAD) {
            len--;
        }
    }
#endif /* DOPAD */

    size_t i;
    int leftover = len % 4;
    size_t chunks = (leftover == 0) ? len / 4 - 1 : len / 4;

    uint8_t* p = (uint8_t*)dest;
    uint32_t x = 0;
    uint32_t* destInt = (uint32_t*)p;
    uint32_t* srcInt = (uint32_t*)src;
    uint32_t y = *srcInt++;
    for (i = 0; i < chunks; ++i) {
        x = d0[y >> 24 & 0xff] | d1[y >> 16 & 0xff] | d2[y >> 8 & 0xff] |
            d3[y & 0xff];

        if (x >= BADCHAR) return MODP_B64_ERROR;
        *destInt = x << 8;
        p += 3;
        destInt = (uint32_t*)p;
        y = *srcInt++;
    }

    switch (leftover) {
        case 0:
            x = d0[y >> 24 & 0xff] | d1[y >> 16 & 0xff] | d2[y >> 8 & 0xff] |
                d3[y & 0xff];
            if (x >= BADCHAR) return MODP_B64_ERROR;
            *p++ = ((uint8_t*)&x)[1];
            *p++ = ((uint8_t*)&x)[2];
            *p = ((uint8_t*)&x)[3];
            return (chunks + 1) * 3;
        case 1:
            x = d3[y >> 24];
            *p = (uint8_t)x;
            break;
        case 2:
            x = d3[y >> 24] * 64 + d3[(y >> 16) & 0xff];
            *p = (uint8_t)(x >> 4);
            break;
        default: /* case 3 */
            x = (d3[y >> 24] * 64 + d3[(y >> 16) & 0xff]) * 64 +
                d3[(y >> 8) & 0xff];
            *p++ = (uint8_t)(x >> 10);
            *p = (uint8_t)(x >> 2);
            break;
    }

    if (x >= BADCHAR) return MODP_B64_ERROR;
    return 3 * chunks + (6 * leftover) / 8;
}

#else /* LITTLE  ENDIAN -- INTEL AND FRIENDS */

static size_t modp_b64_decode_scalar_tail(uint8_t* p, const uint8_t* s,
                                          size_t rem,
                                          const uint8_t* dest_start) {
    const size_t prefix_bytes = static_cast<size_t>(p - dest_start);
    if (rem == 0) {
        return prefix_bytes;
    }
    const size_t leftover = rem % 4;
    const size_t chunks = (leftover == 0) ? (rem / 4 - 1) : (rem / 4);

    uint32_t x = 0;
    uint32_t y = 0;
    for (size_t i = 0; i < chunks; ++i) {
        memcpy(&y, s, 4);
        s += 4;
        x = d0[y & 0xff] | d1[(y >> 8) & 0xff] | d2[(y >> 16) & 0xff] |
            d3[(y >> 24) & 0xff];

        if (x >= BADCHAR) return MODP_B64_ERROR;
        memcpy(p, &x, 3);
        p += 3;
    }

    switch (leftover) {
        case 0:
            memcpy(&y, s, 4);
            x = d0[y & 0xff] | d1[(y >> 8) & 0xff] | d2[(y >> 16) & 0xff] |
                d3[(y >> 24) & 0xff];

            if (x >= BADCHAR) return MODP_B64_ERROR;
            *p++ = reinterpret_cast<uint8_t*>(&x)[0];
            *p++ = reinterpret_cast<uint8_t*>(&x)[1];
            *p = reinterpret_cast<uint8_t*>(&x)[2];
            return prefix_bytes + (chunks + 1) * 3;
        case 1: /* 1-char final Base64 quantum is never valid */
            return MODP_B64_ERROR;
        case 2: /* case 2, 1 output byte */
            x = d0[s[0]] | d1[s[1]];
            if (x >= BADCHAR) return MODP_B64_ERROR;
            *p = *reinterpret_cast<uint8_t*>(&x);
            break;
        default: /* case 3, 2 output bytes */
            x = d0[s[0]] | d1[s[1]] | d2[s[2]];
            if (x >= BADCHAR) return MODP_B64_ERROR;
            *p++ = reinterpret_cast<uint8_t*>(&x)[0];
            *p = reinterpret_cast<uint8_t*>(&x)[1];
            break;
    }

    return prefix_bytes + 3 * chunks + (6 * leftover) / 8;
}

static size_t modp_b64_decode_scalar(char* dest, const char* src, size_t len) {
    if (len == 0) return 0;

#ifdef DOPAD
    if (len < 4 || (len % 4 != 0)) return MODP_B64_ERROR;
    if (src[len - 1] == CHARPAD) {
        len--;
        if (src[len - 1] == CHARPAD) {
            len--;
        }
    }
#endif

    uint8_t* p = reinterpret_cast<uint8_t*>(dest);
    const uint8_t* s = reinterpret_cast<const uint8_t*>(src);
    return modp_b64_decode_scalar_tail(p, s, len, p);
}

#if defined(__x86_64__)
__attribute__((target("ssse3"))) static size_t modp_b64_decode_ssse3(
    char* dest, const char* src, size_t len) {
    if (len == 0) return 0;

#ifdef DOPAD
    if (len < 4 || (len % 4 != 0)) return MODP_B64_ERROR;
    if (src[len - 1] == CHARPAD) {
        len--;
        if (src[len - 1] == CHARPAD) {
            len--;
        }
    }
#endif

    uint8_t* p = reinterpret_cast<uint8_t*>(dest);
    const uint8_t* dest_start = p;
    const uint8_t* s = reinterpret_cast<const uint8_t*>(src);
    size_t rem = len;

    if (rem >= 20) {
        const __m128i lut_lo =
            _mm_setr_epi8(0x15, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                          0x11, 0x13, 0x1A, 0x1B, 0x1B, 0x1B, 0x1A);
        const __m128i lut_hi =
            _mm_setr_epi8(0x10, 0x10, 0x01, 0x02, 0x04, 0x08, 0x04, 0x08, 0x10,
                          0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10);
        const __m128i lut_roll = _mm_setr_epi8(0, 16, 19, 4, -65, -65, -71, -71,
                                               0, 0, 0, 0, 0, 0, 0, 0);
        const __m128i mask_0f = _mm_set1_epi8(0x0F);
        const __m128i c_2f = _mm_set1_epi8(0x2F);
        const __m128i merge_ab_cd = _mm_set1_epi32(0x01400140);
        const __m128i merge_abcd = _mm_set1_epi32(0x00011000);
        const __m128i pack_shuf = _mm_setr_epi8(2, 1, 0, 6, 5, 4, 10, 9, 8, 14,
                                                13, 12, -1, -1, -1, -1);

        while (rem >= 20) {
            __m128i input = _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(static_cast<const void*>(s)));
            __m128i lo_nibbles = _mm_and_si128(input, mask_0f);
            __m128i hi_nibbles =
                _mm_and_si128(_mm_srli_epi32(input, 4), mask_0f);
            __m128i lo = _mm_shuffle_epi8(lut_lo, lo_nibbles);
            __m128i hi = _mm_shuffle_epi8(lut_hi, hi_nibbles);

            if (_mm_movemask_epi8(_mm_cmpeq_epi8(
                    _mm_and_si128(lo, hi), _mm_setzero_si128())) != 0xFFFF) {
                return MODP_B64_ERROR;
            }

            __m128i eq_2f = _mm_cmpeq_epi8(input, c_2f);
            __m128i roll =
                _mm_shuffle_epi8(lut_roll, _mm_add_epi8(eq_2f, hi_nibbles));
            __m128i sextets = _mm_add_epi8(input, roll);

            __m128i merged = _mm_madd_epi16(
                _mm_maddubs_epi16(sextets, merge_ab_cd), merge_abcd);
            __m128i packed12 = _mm_shuffle_epi8(merged, pack_shuf);

            _mm_storel_epi64(reinterpret_cast<__m128i*>(static_cast<void*>(p)),
                             packed12);
            uint32_t tail4 = static_cast<uint32_t>(
                _mm_cvtsi128_si32(_mm_srli_si128(packed12, 8)));
            memcpy(p + 8, &tail4, 4);

            s += 16;
            p += 12;
            rem -= 16;
        }
    }

    return modp_b64_decode_scalar_tail(p, s, rem, dest_start);
}

__attribute__((target("avx2"))) static size_t modp_b64_decode_avx2(
    char* dest, const char* src, size_t len) {
    if (len == 0) return 0;

#ifdef DOPAD
    if (len < 4 || (len % 4 != 0)) return MODP_B64_ERROR;
    if (src[len - 1] == CHARPAD) {
        len--;
        if (src[len - 1] == CHARPAD) {
            len--;
        }
    }
#endif

    uint8_t* p = reinterpret_cast<uint8_t*>(dest);
    const uint8_t* dest_start = p;
    const uint8_t* s = reinterpret_cast<const uint8_t*>(src);
    size_t rem = len;

    if (rem >= 36) {
        const __m256i lut_lo = _mm256_setr_epi8(
            0x15, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x13,
            0x1A, 0x1B, 0x1B, 0x1B, 0x1A, 0x15, 0x11, 0x11, 0x11, 0x11, 0x11,
            0x11, 0x11, 0x11, 0x11, 0x13, 0x1A, 0x1B, 0x1B, 0x1B, 0x1A);
        const __m256i lut_hi = _mm256_setr_epi8(
            0x10, 0x10, 0x01, 0x02, 0x04, 0x08, 0x04, 0x08, 0x10, 0x10, 0x10,
            0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x01, 0x02, 0x04, 0x08,
            0x04, 0x08, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10);
        const __m256i lut_roll = _mm256_setr_epi8(
            0, 16, 19, 4, -65, -65, -71, -71, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 19,
            4, -65, -65, -71, -71, 0, 0, 0, 0, 0, 0, 0, 0);
        const __m256i mask_0f = _mm256_set1_epi8(0x0F);
        const __m256i c_2f = _mm256_set1_epi8(0x2F);
        const __m256i merge_ab_cd = _mm256_set1_epi32(0x01400140);
        const __m256i merge_abcd = _mm256_set1_epi32(0x00011000);
        const __m256i pack_shuf = _mm256_setr_epi8(
            2, 1, 0, 6, 5, 4, 10, 9, 8, 14, 13, 12, -1, -1, -1, -1, 2, 1, 0, 6,
            5, 4, 10, 9, 8, 14, 13, 12, -1, -1, -1, -1);

        while (rem >= 36) {
            __m256i input = _mm256_loadu_si256(
                reinterpret_cast<const __m256i*>(static_cast<const void*>(s)));
            __m256i lo_nibbles = _mm256_and_si256(input, mask_0f);
            __m256i hi_nibbles =
                _mm256_and_si256(_mm256_srli_epi32(input, 4), mask_0f);
            __m256i lo = _mm256_shuffle_epi8(lut_lo, lo_nibbles);
            __m256i hi = _mm256_shuffle_epi8(lut_hi, hi_nibbles);

            if (!_mm256_testz_si256(lo, hi)) {
                return MODP_B64_ERROR;
            }

            __m256i eq_2f = _mm256_cmpeq_epi8(input, c_2f);
            __m256i roll = _mm256_shuffle_epi8(
                lut_roll, _mm256_add_epi8(eq_2f, hi_nibbles));
            __m256i sextets = _mm256_add_epi8(input, roll);

            __m256i merged = _mm256_madd_epi16(
                _mm256_maddubs_epi16(sextets, merge_ab_cd), merge_abcd);
            __m256i packed = _mm256_shuffle_epi8(merged, pack_shuf);

            __m128i lo12 = _mm256_castsi256_si128(packed);
            __m128i hi12 = _mm256_extracti128_si256(packed, 1);

            _mm_storel_epi64(reinterpret_cast<__m128i*>(static_cast<void*>(p)),
                             lo12);
            uint32_t lo_tail4 = static_cast<uint32_t>(
                _mm_cvtsi128_si32(_mm_srli_si128(lo12, 8)));
            memcpy(p + 8, &lo_tail4, 4);

            _mm_storel_epi64(
                reinterpret_cast<__m128i*>(static_cast<void*>(p + 12)), hi12);
            uint32_t hi_tail4 = static_cast<uint32_t>(
                _mm_cvtsi128_si32(_mm_srli_si128(hi12, 8)));
            memcpy(p + 20, &hi_tail4, 4);

            s += 32;
            p += 24;
            rem -= 32;
        }
    }

    if (rem >= 20) {
        const __m128i lut_lo =
            _mm_setr_epi8(0x15, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                          0x11, 0x13, 0x1A, 0x1B, 0x1B, 0x1B, 0x1A);
        const __m128i lut_hi =
            _mm_setr_epi8(0x10, 0x10, 0x01, 0x02, 0x04, 0x08, 0x04, 0x08, 0x10,
                          0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10);
        const __m128i lut_roll = _mm_setr_epi8(0, 16, 19, 4, -65, -65, -71, -71,
                                               0, 0, 0, 0, 0, 0, 0, 0);
        const __m128i mask_0f = _mm_set1_epi8(0x0F);
        const __m128i pack_shuf = _mm_setr_epi8(2, 1, 0, 6, 5, 4, 10, 9, 8, 14,
                                                13, 12, -1, -1, -1, -1);

        __m128i input = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(static_cast<const void*>(s)));
        __m128i lo_nibbles = _mm_and_si128(input, mask_0f);
        __m128i hi_nibbles = _mm_and_si128(_mm_srli_epi32(input, 4), mask_0f);
        __m128i lo = _mm_shuffle_epi8(lut_lo, lo_nibbles);
        __m128i hi = _mm_shuffle_epi8(lut_hi, hi_nibbles);

        if (_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_and_si128(lo, hi),
                                             _mm_setzero_si128())) != 0xFFFF) {
            return MODP_B64_ERROR;
        }

        __m128i eq_2f = _mm_cmpeq_epi8(input, _mm_set1_epi8(0x2F));
        __m128i roll =
            _mm_shuffle_epi8(lut_roll, _mm_add_epi8(eq_2f, hi_nibbles));
        __m128i sextets = _mm_add_epi8(input, roll);

        __m128i merged = _mm_madd_epi16(
            _mm_maddubs_epi16(sextets, _mm_set1_epi32(0x01400140)),
            _mm_set1_epi32(0x00011000));
        __m128i packed12 = _mm_shuffle_epi8(merged, pack_shuf);

        _mm_storel_epi64(reinterpret_cast<__m128i*>(static_cast<void*>(p)),
                         packed12);
        uint32_t tail4 = static_cast<uint32_t>(
            _mm_cvtsi128_si32(_mm_srli_si128(packed12, 8)));
        memcpy(p + 8, &tail4, 4);

        s += 16;
        p += 12;
        rem -= 16;
    }

    return modp_b64_decode_scalar_tail(p, s, rem, dest_start);
}
#endif  // defined(__x86_64__)

size_t modp_b64_decode(char* dest, const char* src, size_t len) {
#if USE_RVV_BASE64
    return rvv_modp_b64_decode(dest, src, len);
#elif defined(__x86_64__)
    typedef size_t (*DecodeFn)(char*, const char*, size_t);
    static const DecodeFn fn =
        modp_b64_has_avx2() ? &modp_b64_decode_avx2
                            : (modp_b64_has_ssse3() ? &modp_b64_decode_ssse3
                                                    : &modp_b64_decode_scalar);
    return fn(dest, src, len);
#else
    return modp_b64_decode_scalar(dest, src, len);
#endif
}

#if defined(UNIT_TEST) && defined(__x86_64__)
extern "C" {

bool modp_b64_x86_has_ssse3_for_testing() { return modp_b64_has_ssse3(); }

bool modp_b64_x86_has_avx2_for_testing() { return modp_b64_has_avx2(); }

size_t modp_b64_encode_scalar_for_testing(char* dest, const char* str,
                                          size_t len) {
    return modp_b64_encode_scalar(dest, str, len);
}

size_t modp_b64_encode_ssse3_for_testing(char* dest, const char* str,
                                         size_t len) {
    if (!modp_b64_has_ssse3()) {
        return modp_b64_encode_scalar(dest, str, len);
    }
    return modp_b64_encode_ssse3(dest, str, len);
}

size_t modp_b64_encode_avx2_for_testing(char* dest, const char* str,
                                        size_t len) {
    if (!modp_b64_has_avx2()) {
        return modp_b64_encode_scalar(dest, str, len);
    }
    return modp_b64_encode_avx2(dest, str, len);
}

size_t modp_b64_decode_scalar_for_testing(char* dest, const char* src,
                                          size_t len) {
    return modp_b64_decode_scalar(dest, src, len);
}

size_t modp_b64_decode_ssse3_for_testing(char* dest, const char* src,
                                         size_t len) {
    if (!modp_b64_has_ssse3()) {
        return modp_b64_decode_scalar(dest, src, len);
    }
    return modp_b64_decode_ssse3(dest, src, len);
}

size_t modp_b64_decode_avx2_for_testing(char* dest, const char* src,
                                        size_t len) {
    if (!modp_b64_has_avx2()) {
        return modp_b64_decode_scalar(dest, src, len);
    }
    return modp_b64_decode_avx2(dest, src, len);
}

}  // extern "C"
#endif  // defined(UNIT_TEST) && defined(__x86_64__)

#endif /* if bigendian / else / endif */
