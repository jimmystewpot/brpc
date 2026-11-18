// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/base64.h"

#include <gtest/gtest.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <vector>

#include "butil/third_party/modp_b64/modp_b64.h"

#if defined(UNIT_TEST) && defined(__x86_64__)
extern "C" {
bool modp_b64_x86_has_ssse3_for_testing();
bool modp_b64_x86_has_avx2_for_testing();
size_t modp_b64_encode_scalar_for_testing(char* dest, const char* str,
                                          size_t len);
size_t modp_b64_encode_ssse3_for_testing(char* dest, const char* str,
                                         size_t len);
size_t modp_b64_encode_avx2_for_testing(char* dest, const char* str,
                                        size_t len);
size_t modp_b64_decode_scalar_for_testing(char* dest, const char* src,
                                          size_t len);
size_t modp_b64_decode_ssse3_for_testing(char* dest, const char* src,
                                         size_t len);
size_t modp_b64_decode_avx2_for_testing(char* dest, const char* src,
                                        size_t len);
}
#endif

namespace butil {

TEST(Base64Test, Basic) {
    const std::string kText = "hello world";
    const std::string kBase64Text = "aGVsbG8gd29ybGQ=";

    std::string encoded;
    std::string decoded;
    bool ok;

    Base64Encode(kText, &encoded);
    EXPECT_EQ(kBase64Text, encoded);

    ok = Base64Decode(encoded, &decoded);
    EXPECT_TRUE(ok);
    EXPECT_EQ(kText, decoded);
}

TEST(Base64Test, RejectsMalformedPaddingAndLengths) {
    for (const char* input : {"A", "AA", "AAA", "AAAAA", "=",
                              "====", "A===", "AA======", "AA=A", "AA==AAAA"}) {
        std::string decoded;
        EXPECT_FALSE(butil::Base64Decode(input, &decoded)) << input;
    }
    for (const char* input : {"", "AA==", "AAA=", "AAAA"}) {
        std::string decoded;
        EXPECT_TRUE(butil::Base64Decode(input, &decoded)) << input;
    }
}

namespace {

std::vector<uint8_t> MakeDeterministicBytes(size_t len) {
    std::vector<uint8_t> buf(len);
    uint32_t state = 0x9E3779B9U ^ static_cast<uint32_t>(len);
    for (size_t i = 0; i < len; ++i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        buf[i] = static_cast<uint8_t>((state + i * 31) & 0xFF);
    }
    return buf;
}

}  // namespace

TEST(Base64Test, DifferentialAllTiersSizesAndAlignments) {
    std::vector<size_t> sizes;
    for (size_t s = 0; s <= 1024; ++s) {
        sizes.push_back(s);
    }
    sizes.push_back(2048);
    sizes.push_back(4096);
    sizes.push_back(65536);

    for (size_t len : sizes) {
        std::vector<uint8_t> raw = MakeDeterministicBytes(len);
        const size_t enc_alloc = modp_b64_encode_len(len);
        const size_t dec_alloc = modp_b64_decode_len(enc_alloc);

        std::vector<char> src_storage(len + 32, 0);
        std::vector<char> enc_disp_storage(enc_alloc + 32, 0);
        std::vector<char> dec_disp_storage(dec_alloc + 32, 0);

#if defined(UNIT_TEST) && defined(__x86_64__)
        const bool has_ssse3 = modp_b64_x86_has_ssse3_for_testing();
        const bool has_avx2 = modp_b64_x86_has_avx2_for_testing();
        std::vector<char> enc_scalar_storage(enc_alloc + 32, 0);
        std::vector<char> enc_ssse3_storage(enc_alloc + 32, 0);
        std::vector<char> enc_avx2_storage(enc_alloc + 32, 0);
        std::vector<char> dec_scalar_storage(dec_alloc + 32, 0);
        std::vector<char> dec_ssse3_storage(dec_alloc + 32, 0);
        std::vector<char> dec_avx2_storage(dec_alloc + 32, 0);
#endif

        const size_t max_align = (len <= 128 || len == 2048) ? 15 : 3;
        for (size_t align = 0; align <= max_align; ++align) {
            char* src_ptr = src_storage.data() + align;
            if (len > 0) {
                memcpy(src_ptr, raw.data(), len);
            }
            char* enc_disp = enc_disp_storage.data() + align;
            memset(enc_disp, 0x5A, enc_alloc);

            size_t enc_len = modp_b64_encode(enc_disp, src_ptr, len);
            ASSERT_EQ(enc_len, modp_b64_encode_strlen(len))
                << "len=" << len << " align=" << align;
            ASSERT_EQ(enc_disp[enc_len], '\0')
                << "len=" << len << " align=" << align;

            char* dec_disp = dec_disp_storage.data() + align;
            memset(dec_disp, 0xA5, dec_alloc);
            size_t dec_len = modp_b64_decode(dec_disp, enc_disp, enc_len);
            ASSERT_EQ(dec_len, len) << "len=" << len << " align=" << align;
            if (len > 0) {
                ASSERT_EQ(memcmp(dec_disp, raw.data(), len), 0)
                    << "len=" << len << " align=" << align;
            }

#if defined(UNIT_TEST) && defined(__x86_64__)
            char* enc_scalar = enc_scalar_storage.data() + align;
            memset(enc_scalar, 0x5A, enc_alloc);
            size_t enc_scalar_len =
                modp_b64_encode_scalar_for_testing(enc_scalar, src_ptr, len);
            ASSERT_EQ(enc_scalar_len, enc_len);
            ASSERT_EQ(enc_scalar[enc_scalar_len], '\0');
            ASSERT_EQ(memcmp(enc_scalar, enc_disp, enc_len + 1), 0);

            char* dec_scalar = dec_scalar_storage.data() + align;
            memset(dec_scalar, 0xA5, dec_alloc);
            size_t dec_scalar_len = modp_b64_decode_scalar_for_testing(
                dec_scalar, enc_scalar, enc_scalar_len);
            ASSERT_EQ(dec_scalar_len, len);
            if (len > 0) {
                ASSERT_EQ(memcmp(dec_scalar, raw.data(), len), 0);
            }

            if (has_ssse3) {
                char* enc_ssse3 = enc_ssse3_storage.data() + align;
                memset(enc_ssse3, 0x5A, enc_alloc);
                size_t enc_ssse3_len =
                    modp_b64_encode_ssse3_for_testing(enc_ssse3, src_ptr, len);
                ASSERT_EQ(enc_ssse3_len, enc_len);
                ASSERT_EQ(enc_ssse3[enc_ssse3_len], '\0');
                ASSERT_EQ(memcmp(enc_ssse3, enc_scalar, enc_len + 1), 0);

                char* dec_ssse3 = dec_ssse3_storage.data() + align;
                memset(dec_ssse3, 0xA5, dec_alloc);
                size_t dec_ssse3_len = modp_b64_decode_ssse3_for_testing(
                    dec_ssse3, enc_scalar, enc_scalar_len);
                ASSERT_EQ(dec_ssse3_len, len);
                if (len > 0) {
                    ASSERT_EQ(memcmp(dec_ssse3, raw.data(), len), 0);
                }
            }

            if (has_avx2) {
                char* enc_avx2 = enc_avx2_storage.data() + align;
                memset(enc_avx2, 0x5A, enc_alloc);
                size_t enc_avx2_len =
                    modp_b64_encode_avx2_for_testing(enc_avx2, src_ptr, len);
                ASSERT_EQ(enc_avx2_len, enc_len);
                ASSERT_EQ(enc_avx2[enc_avx2_len], '\0');
                ASSERT_EQ(memcmp(enc_avx2, enc_scalar, enc_len + 1), 0);

                char* dec_avx2 = dec_avx2_storage.data() + align;
                memset(dec_avx2, 0xA5, dec_alloc);
                size_t dec_avx2_len = modp_b64_decode_avx2_for_testing(
                    dec_avx2, enc_scalar, enc_scalar_len);
                ASSERT_EQ(dec_avx2_len, len);
                if (len > 0) {
                    ASSERT_EQ(memcmp(dec_avx2, raw.data(), len), 0);
                }
            }
#endif
        }

        std::string b64_str;
        std::string roundtrip;
        Base64Encode(
            StringPiece(reinterpret_cast<const char*>(raw.data()), len),
            &b64_str);
        ASSERT_EQ(b64_str.size(), modp_b64_encode_strlen(len));
        ASSERT_TRUE(Base64Decode(b64_str, &roundtrip));
        ASSERT_EQ(roundtrip.size(), len);
        if (len > 0) {
            ASSERT_EQ(memcmp(roundtrip.data(), raw.data(), len), 0);
        }
    }
}

TEST(Base64Test, GuardPageAndExactBufferBoundsAllTiers) {
    const long page_size_long = sysconf(_SC_PAGESIZE);
    const size_t page_size =
        page_size_long > 0 ? static_cast<size_t>(page_size_long) : 4096;

    auto alloc_two_pages = [&](uint8_t** rw_page_out) -> void* {
        void* mapping = mmap(nullptr, 2 * page_size, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapping == MAP_FAILED) {
            return MAP_FAILED;
        }
        uint8_t* base = static_cast<uint8_t*>(mapping);
        if (mprotect(base + page_size, page_size, PROT_NONE) != 0) {
            munmap(mapping, 2 * page_size);
            return MAP_FAILED;
        }
        *rw_page_out = base;
        return mapping;
    };

    uint8_t* src_page = nullptr;
    uint8_t* dst_page = nullptr;
    void* src_map = alloc_two_pages(&src_page);
    void* dst_map = alloc_two_pages(&dst_page);
    ASSERT_NE(src_map, MAP_FAILED);
    ASSERT_NE(dst_map, MAP_FAILED);

#if defined(UNIT_TEST) && defined(__x86_64__)
    const bool has_ssse3 = modp_b64_x86_has_ssse3_for_testing();
    const bool has_avx2 = modp_b64_x86_has_avx2_for_testing();
#endif

    for (size_t len = 0; len <= 128; ++len) {
        std::vector<uint8_t> raw = MakeDeterministicBytes(len);
        const size_t enc_buf_len = modp_b64_encode_len(len);
        char* guarded_raw = reinterpret_cast<char*>(src_page + page_size - len);
        if (len > 0) {
            memcpy(guarded_raw, raw.data(), len);
        }
        char* guarded_enc =
            reinterpret_cast<char*>(dst_page + page_size - enc_buf_len);

        // Test dispatched encode at exact page boundary.
        memset(guarded_enc, 0x7F, enc_buf_len);
        size_t enc_len = modp_b64_encode(guarded_enc, guarded_raw, len);
        ASSERT_EQ(enc_len, modp_b64_encode_strlen(len));
        ASSERT_EQ(guarded_enc[enc_len], '\0');

        std::string expected_enc(guarded_enc, enc_len);

#if defined(UNIT_TEST) && defined(__x86_64__)
        memset(guarded_enc, 0x7F, enc_buf_len);
        ASSERT_EQ(
            modp_b64_encode_scalar_for_testing(guarded_enc, guarded_raw, len),
            enc_len);
        ASSERT_EQ(guarded_enc[enc_len], '\0');
        ASSERT_EQ(memcmp(guarded_enc, expected_enc.data(), enc_len), 0);

        if (has_ssse3) {
            memset(guarded_enc, 0x7F, enc_buf_len);
            ASSERT_EQ(modp_b64_encode_ssse3_for_testing(guarded_enc,
                                                        guarded_raw, len),
                      enc_len);
            ASSERT_EQ(guarded_enc[enc_len], '\0');
            ASSERT_EQ(memcmp(guarded_enc, expected_enc.data(), enc_len), 0);
        }
        if (has_avx2) {
            memset(guarded_enc, 0x7F, enc_buf_len);
            ASSERT_EQ(
                modp_b64_encode_avx2_for_testing(guarded_enc, guarded_raw, len),
                enc_len);
            ASSERT_EQ(guarded_enc[enc_len], '\0');
            ASSERT_EQ(memcmp(guarded_enc, expected_enc.data(), enc_len), 0);
        }
#endif

        // Test decode with strict (enc_len / 4 * 3) output buffer at exact
        // guard page boundary (NOT modp_b64_decode_len which adds +2 slack).
        const size_t strict_dec_alloc = (enc_len / 4) * 3;
        char* guarded_b64_src =
            reinterpret_cast<char*>(src_page + page_size - enc_len);
        if (enc_len > 0) {
            memcpy(guarded_b64_src, expected_enc.data(), enc_len);
        }
        char* guarded_dec_dst =
            reinterpret_cast<char*>(dst_page + page_size - strict_dec_alloc);

        memset(guarded_dec_dst, 0x33, strict_dec_alloc);
        size_t dec_len =
            modp_b64_decode(guarded_dec_dst, guarded_b64_src, enc_len);
        ASSERT_EQ(dec_len, len) << "len=" << len;
        if (len > 0) {
            ASSERT_EQ(memcmp(guarded_dec_dst, raw.data(), len), 0);
        }

#if defined(UNIT_TEST) && defined(__x86_64__)
        memset(guarded_dec_dst, 0x33, strict_dec_alloc);
        ASSERT_EQ(modp_b64_decode_scalar_for_testing(guarded_dec_dst,
                                                     guarded_b64_src, enc_len),
                  len);
        if (len > 0) {
            ASSERT_EQ(memcmp(guarded_dec_dst, raw.data(), len), 0);
        }
        if (has_ssse3) {
            memset(guarded_dec_dst, 0x33, strict_dec_alloc);
            ASSERT_EQ(modp_b64_decode_ssse3_for_testing(
                          guarded_dec_dst, guarded_b64_src, enc_len),
                      len);
            if (len > 0) {
                ASSERT_EQ(memcmp(guarded_dec_dst, raw.data(), len), 0);
            }
        }
        if (has_avx2) {
            memset(guarded_dec_dst, 0x33, strict_dec_alloc);
            ASSERT_EQ(modp_b64_decode_avx2_for_testing(
                          guarded_dec_dst, guarded_b64_src, enc_len),
                      len);
            if (len > 0) {
                ASSERT_EQ(memcmp(guarded_dec_dst, raw.data(), len), 0);
            }
        }
#endif
    }

    munmap(src_map, 2 * page_size);
    munmap(dst_map, 2 * page_size);
}

TEST(Base64Test, ExhaustiveInvalidByteAtEveryPositionAllTiers) {
    const size_t encoded_lengths[] = {4,  8,  12, 16, 20, 28, 32,
                                      36, 48, 64, 68, 96, 128};
#if defined(UNIT_TEST) && defined(__x86_64__)
    const bool has_ssse3 = modp_b64_x86_has_ssse3_for_testing();
    const bool has_avx2 = modp_b64_x86_has_avx2_for_testing();
#endif

    for (size_t enc_len : encoded_lengths) {
        const size_t raw_len = (enc_len / 4) * 3;
        std::vector<uint8_t> raw = MakeDeterministicBytes(raw_len);
        std::string valid_b64;
        Base64Encode(
            StringPiece(reinterpret_cast<const char*>(raw.data()), raw_len),
            &valid_b64);
        ASSERT_EQ(valid_b64.size(), enc_len);

        std::vector<char> out_disp(modp_b64_decode_len(enc_len), 0);
#if defined(UNIT_TEST) && defined(__x86_64__)
        std::vector<char> out_scalar(modp_b64_decode_len(enc_len), 0);
        std::vector<char> out_ssse3(modp_b64_decode_len(enc_len), 0);
        std::vector<char> out_avx2(modp_b64_decode_len(enc_len), 0);
#endif

        for (size_t pos = 0; pos < enc_len; ++pos) {
            for (int byte_val = 0; byte_val < 256; ++byte_val) {
                std::string mutated = valid_b64;
                mutated[pos] = static_cast<char>(byte_val);

                size_t res_disp =
                    modp_b64_decode(out_disp.data(), mutated.data(), enc_len);
#if defined(UNIT_TEST) && defined(__x86_64__)
                size_t res_scalar = modp_b64_decode_scalar_for_testing(
                    out_scalar.data(), mutated.data(), enc_len);
                ASSERT_EQ(res_disp, res_scalar)
                    << "enc_len=" << enc_len << " pos=" << pos
                    << " byte=" << byte_val;
                if (res_scalar != MODP_B64_ERROR) {
                    ASSERT_EQ(
                        memcmp(out_disp.data(), out_scalar.data(), res_scalar),
                        0);
                }

                if (has_ssse3) {
                    size_t res_ssse3 = modp_b64_decode_ssse3_for_testing(
                        out_ssse3.data(), mutated.data(), enc_len);
                    ASSERT_EQ(res_ssse3, res_scalar)
                        << "SSSE3 mismatch enc_len=" << enc_len
                        << " pos=" << pos << " byte=" << byte_val;
                    if (res_scalar != MODP_B64_ERROR) {
                        ASSERT_EQ(memcmp(out_ssse3.data(), out_scalar.data(),
                                         res_scalar),
                                  0);
                    }
                }

                if (has_avx2) {
                    size_t res_avx2 = modp_b64_decode_avx2_for_testing(
                        out_avx2.data(), mutated.data(), enc_len);
                    ASSERT_EQ(res_avx2, res_scalar)
                        << "AVX2 mismatch enc_len=" << enc_len << " pos=" << pos
                        << " byte=" << byte_val;
                    if (res_scalar != MODP_B64_ERROR) {
                        ASSERT_EQ(memcmp(out_avx2.data(), out_scalar.data(),
                                         res_scalar),
                                  0);
                    }
                }
#else
                (void)res_disp;
#endif
            }
        }
    }
}

TEST(Base64Test, BenchmarkAllTiersThroughput) {
    const size_t bench_sizes[] = {1, 8, 16, 28, 64, 256, 1024, 4096, 65536};
    for (size_t sz : bench_sizes) {
        std::vector<uint8_t> raw = MakeDeterministicBytes(sz);
        std::vector<char> enc(modp_b64_encode_len(sz), 0);
        std::vector<char> dec(modp_b64_decode_len(enc.size()), 0);
        const size_t iters =
            (sz <= 64) ? 200000 : ((sz <= 4096) ? 20000 : 2000);

        size_t enc_len = modp_b64_encode(
            enc.data(), reinterpret_cast<const char*>(raw.data()), sz);
        ASSERT_EQ(enc_len, modp_b64_encode_strlen(sz));

        auto t0 = std::chrono::steady_clock::now();
        volatile size_t sink = 0;
        for (size_t i = 0; i < iters; ++i) {
            sink += modp_b64_encode(
                enc.data(), reinterpret_cast<const char*>(raw.data()), sz);
        }
        auto t1 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < iters; ++i) {
            sink += modp_b64_decode(dec.data(), enc.data(), enc_len);
        }
        auto t2 = std::chrono::steady_clock::now();
        double enc_ns =
            std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
        double dec_ns =
            std::chrono::duration<double, std::nano>(t2 - t1).count() / iters;
        printf(
            "[Base64 Bench Dispatched] size=%5zu B | encode=%8.2f ns | "
            "decode=%8.2f ns (sink=%zu)\n",
            sz, enc_ns, dec_ns, static_cast<size_t>(sink));

#if defined(UNIT_TEST) && defined(__x86_64__)
        auto ts0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < iters; ++i) {
            sink += modp_b64_encode_scalar_for_testing(
                enc.data(), reinterpret_cast<const char*>(raw.data()), sz);
        }
        auto ts1 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < iters; ++i) {
            sink += modp_b64_decode_scalar_for_testing(dec.data(), enc.data(),
                                                       enc_len);
        }
        auto ts2 = std::chrono::steady_clock::now();
        double enc_s_ns =
            std::chrono::duration<double, std::nano>(ts1 - ts0).count() / iters;
        double dec_s_ns =
            std::chrono::duration<double, std::nano>(ts2 - ts1).count() / iters;
        printf(
            "[Base64 Bench Scalar    ] size=%5zu B | encode=%8.2f ns | "
            "decode=%8.2f ns\n",
            sz, enc_s_ns, dec_s_ns);

        if (modp_b64_x86_has_ssse3_for_testing()) {
            auto tss0 = std::chrono::steady_clock::now();
            for (size_t i = 0; i < iters; ++i) {
                sink += modp_b64_encode_ssse3_for_testing(
                    enc.data(), reinterpret_cast<const char*>(raw.data()), sz);
            }
            auto tss1 = std::chrono::steady_clock::now();
            for (size_t i = 0; i < iters; ++i) {
                sink += modp_b64_decode_ssse3_for_testing(dec.data(),
                                                          enc.data(), enc_len);
            }
            auto tss2 = std::chrono::steady_clock::now();
            double enc_ss_ns =
                std::chrono::duration<double, std::nano>(tss1 - tss0).count() /
                iters;
            double dec_ss_ns =
                std::chrono::duration<double, std::nano>(tss2 - tss1).count() /
                iters;
            printf(
                "[Base64 Bench SSSE3     ] size=%5zu B | encode=%8.2f ns | "
                "decode=%8.2f ns\n",
                sz, enc_ss_ns, dec_ss_ns);
        }

        if (modp_b64_x86_has_avx2_for_testing()) {
            auto ta0 = std::chrono::steady_clock::now();
            for (size_t i = 0; i < iters; ++i) {
                sink += modp_b64_encode_avx2_for_testing(
                    enc.data(), reinterpret_cast<const char*>(raw.data()), sz);
            }
            auto ta1 = std::chrono::steady_clock::now();
            for (size_t i = 0; i < iters; ++i) {
                sink += modp_b64_decode_avx2_for_testing(dec.data(), enc.data(),
                                                         enc_len);
            }
            auto ta2 = std::chrono::steady_clock::now();
            double enc_a_ns =
                std::chrono::duration<double, std::nano>(ta1 - ta0).count() /
                iters;
            double dec_a_ns =
                std::chrono::duration<double, std::nano>(ta2 - ta1).count() /
                iters;
            printf(
                "[Base64 Bench AVX2      ] size=%5zu B | encode=%8.2f ns | "
                "decode=%8.2f ns\n",
                sz, enc_a_ns, dec_a_ns);
        }
#endif
    }
}

}  // namespace butil
