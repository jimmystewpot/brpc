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

    std::string inplace = kText;
    EXPECT_EQ(modp_b64_encode(inplace), kBase64Text);
    EXPECT_EQ(modp_b64_decode(inplace), kText);

    std::string empty_inplace;
    EXPECT_EQ(modp_b64_encode(empty_inplace), "");
    EXPECT_EQ(modp_b64_decode(empty_inplace), "");

    std::string invalid_inplace = "====";
    EXPECT_EQ(modp_b64_decode(invalid_inplace), "");
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

class GuardedPagePair {
   public:
    GuardedPagePair() = default;
    ~GuardedPagePair() { Reset(); }
    GuardedPagePair(const GuardedPagePair&) = delete;
    GuardedPagePair& operator=(const GuardedPagePair&) = delete;

    bool Init() {
        Reset();
        const long page_size_long = sysconf(_SC_PAGESIZE);
        page_size_ = page_size_long > 0 ? static_cast<size_t>(page_size_long)
                                        : static_cast<size_t>(4096);
        void* mapping = mmap(nullptr, 2 * page_size_, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapping == MAP_FAILED) {
            return false;
        }
        uint8_t* base = static_cast<uint8_t*>(mapping);
        if (mprotect(base + page_size_, page_size_, PROT_NONE) != 0) {
            munmap(mapping, 2 * page_size_);
            return false;
        }
        mapping_ = mapping;
        rw_page_ = base;
        return true;
    }

    uint8_t* rw_end() const { return rw_page_ + page_size_; }

   private:
    void Reset() {
        if (mapping_ != nullptr) {
            munmap(mapping_, 2 * page_size_);
            mapping_ = nullptr;
            rw_page_ = nullptr;
        }
    }

    void* mapping_ = nullptr;
    uint8_t* rw_page_ = nullptr;
    size_t page_size_ = 0;
};

}  // namespace

TEST(Base64Test, DifferentialAllTiersSizesAndAlignments) {
    auto verify_canaries = [](const std::vector<char>& storage, size_t align,
                              size_t written_len, uint8_t fill, const char* tag,
                              size_t len) {
        const char expected = static_cast<char>(fill);
        for (size_t i = 0; i < align; ++i) {
            if (storage[i] != expected) {
                FAIL() << tag << " pre-canary clobbered len=" << len
                       << " align=" << align << " idx=" << i;
            }
        }
        for (size_t i = align + written_len; i < storage.size(); ++i) {
            if (storage[i] != expected) {
                FAIL() << tag << " post-canary clobbered len=" << len
                       << " align=" << align << " idx=" << i;
            }
        }
    };

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
            memset(enc_disp_storage.data(), 0x5A, enc_disp_storage.size());
            char* enc_disp = enc_disp_storage.data() + align;

            size_t enc_len = modp_b64_encode(enc_disp, src_ptr, len);
            ASSERT_EQ(enc_len, modp_b64_encode_strlen(len))
                << "len=" << len << " align=" << align;
            ASSERT_EQ(enc_disp[enc_len], '\0')
                << "len=" << len << " align=" << align;
            ASSERT_NO_FATAL_FAILURE(verify_canaries(
                enc_disp_storage, align, enc_len + 1, 0x5A, "enc_disp", len));

            memset(dec_disp_storage.data(), 0xA5, dec_disp_storage.size());
            char* dec_disp = dec_disp_storage.data() + align;
            size_t dec_len = modp_b64_decode(dec_disp, enc_disp, enc_len);
            ASSERT_EQ(dec_len, len) << "len=" << len << " align=" << align;
            if (len > 0) {
                ASSERT_EQ(memcmp(dec_disp, raw.data(), len), 0)
                    << "len=" << len << " align=" << align;
            }
            ASSERT_NO_FATAL_FAILURE(verify_canaries(
                dec_disp_storage, align, len, 0xA5, "dec_disp", len));

#if defined(UNIT_TEST) && defined(__x86_64__)
            memset(enc_scalar_storage.data(), 0x5A, enc_scalar_storage.size());
            char* enc_scalar = enc_scalar_storage.data() + align;
            size_t enc_scalar_len =
                modp_b64_encode_scalar_for_testing(enc_scalar, src_ptr, len);
            ASSERT_EQ(enc_scalar_len, enc_len);
            ASSERT_EQ(enc_scalar[enc_scalar_len], '\0');
            ASSERT_EQ(memcmp(enc_scalar, enc_disp, enc_len + 1), 0);
            ASSERT_NO_FATAL_FAILURE(verify_canaries(enc_scalar_storage, align,
                                                    enc_len + 1, 0x5A,
                                                    "enc_scalar", len));

            memset(dec_scalar_storage.data(), 0xA5, dec_scalar_storage.size());
            char* dec_scalar = dec_scalar_storage.data() + align;
            size_t dec_scalar_len = modp_b64_decode_scalar_for_testing(
                dec_scalar, enc_scalar, enc_scalar_len);
            ASSERT_EQ(dec_scalar_len, len);
            if (len > 0) {
                ASSERT_EQ(memcmp(dec_scalar, raw.data(), len), 0);
            }
            ASSERT_NO_FATAL_FAILURE(verify_canaries(
                dec_scalar_storage, align, len, 0xA5, "dec_scalar", len));

            if (has_ssse3) {
                memset(enc_ssse3_storage.data(), 0x5A,
                       enc_ssse3_storage.size());
                char* enc_ssse3 = enc_ssse3_storage.data() + align;
                size_t enc_ssse3_len =
                    modp_b64_encode_ssse3_for_testing(enc_ssse3, src_ptr, len);
                ASSERT_EQ(enc_ssse3_len, enc_len);
                ASSERT_EQ(enc_ssse3[enc_ssse3_len], '\0');
                ASSERT_EQ(memcmp(enc_ssse3, enc_scalar, enc_len + 1), 0);
                ASSERT_NO_FATAL_FAILURE(
                    verify_canaries(enc_ssse3_storage, align, enc_len + 1, 0x5A,
                                    "enc_ssse3", len));

                memset(dec_ssse3_storage.data(), 0xA5,
                       dec_ssse3_storage.size());
                char* dec_ssse3 = dec_ssse3_storage.data() + align;
                size_t dec_ssse3_len = modp_b64_decode_ssse3_for_testing(
                    dec_ssse3, enc_scalar, enc_scalar_len);
                ASSERT_EQ(dec_ssse3_len, len);
                if (len > 0) {
                    ASSERT_EQ(memcmp(dec_ssse3, raw.data(), len), 0);
                }
                ASSERT_NO_FATAL_FAILURE(verify_canaries(
                    dec_ssse3_storage, align, len, 0xA5, "dec_ssse3", len));
            }

            if (has_avx2) {
                memset(enc_avx2_storage.data(), 0x5A, enc_avx2_storage.size());
                char* enc_avx2 = enc_avx2_storage.data() + align;
                size_t enc_avx2_len =
                    modp_b64_encode_avx2_for_testing(enc_avx2, src_ptr, len);
                ASSERT_EQ(enc_avx2_len, enc_len);
                ASSERT_EQ(enc_avx2[enc_avx2_len], '\0');
                ASSERT_EQ(memcmp(enc_avx2, enc_scalar, enc_len + 1), 0);
                ASSERT_NO_FATAL_FAILURE(verify_canaries(enc_avx2_storage, align,
                                                        enc_len + 1, 0x5A,
                                                        "enc_avx2", len));

                memset(dec_avx2_storage.data(), 0xA5, dec_avx2_storage.size());
                char* dec_avx2 = dec_avx2_storage.data() + align;
                size_t dec_avx2_len = modp_b64_decode_avx2_for_testing(
                    dec_avx2, enc_scalar, enc_scalar_len);
                ASSERT_EQ(dec_avx2_len, len);
                if (len > 0) {
                    ASSERT_EQ(memcmp(dec_avx2, raw.data(), len), 0);
                }
                ASSERT_NO_FATAL_FAILURE(verify_canaries(
                    dec_avx2_storage, align, len, 0xA5, "dec_avx2", len));
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
    GuardedPagePair src_guard;
    GuardedPagePair dst_guard;
    ASSERT_TRUE(src_guard.Init());
    ASSERT_TRUE(dst_guard.Init());

#if defined(UNIT_TEST) && defined(__x86_64__)
    const bool has_ssse3 = modp_b64_x86_has_ssse3_for_testing();
    const bool has_avx2 = modp_b64_x86_has_avx2_for_testing();
#endif

    for (size_t len = 0; len <= 128; ++len) {
        std::vector<uint8_t> raw = MakeDeterministicBytes(len);
        const size_t enc_buf_len = modp_b64_encode_len(len);
        char* guarded_raw = reinterpret_cast<char*>(src_guard.rw_end() - len);
        if (len > 0) {
            memcpy(guarded_raw, raw.data(), len);
        }
        char* guarded_enc =
            reinterpret_cast<char*>(dst_guard.rw_end() - enc_buf_len);

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

        // Test decode with strict exact output buffer (len bytes) at exact
        // guard page boundary (NOT modp_b64_decode_len which adds +2 slack).
        const size_t strict_dec_alloc = len;
        char* guarded_b64_src =
            reinterpret_cast<char*>(src_guard.rw_end() - enc_len);
        if (enc_len > 0) {
            memcpy(guarded_b64_src, expected_enc.data(), enc_len);
        }
        char* guarded_dec_dst =
            reinterpret_cast<char*>(dst_guard.rw_end() - strict_dec_alloc);

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
}

TEST(Base64Test, RejectsInvalidTrailingQuantumWithoutClobberingDest) {
    auto check_canary = [](const char* input, size_t enc_len, size_t canary_len,
                           size_t (*decode_fn)(char*, const char*, size_t),
                           const char* tier) {
        std::vector<char> out(canary_len, 0x5A);
        EXPECT_EQ(decode_fn(out.data(), input, enc_len), MODP_B64_ERROR)
            << tier << " input=" << input;
        for (size_t i = 0; i < canary_len; ++i) {
            EXPECT_EQ(static_cast<unsigned char>(out[i]), 0x5Au)
                << tier << " input=" << input << " byte=" << i;
        }
    };

#if defined(UNIT_TEST) && defined(__x86_64__)
    const bool has_ssse3 = modp_b64_x86_has_ssse3_for_testing();
    const bool has_avx2 = modp_b64_x86_has_avx2_for_testing();
#endif

    // 1-byte canary buffer for "====" (enc_len = 4, 0 valid bytes).
    check_canary("====", 4, 1, &modp_b64_decode, "dispatched");
#if defined(UNIT_TEST) && defined(__x86_64__)
    check_canary("====", 4, 1, &modp_b64_decode_scalar_for_testing, "scalar");
    if (has_ssse3) {
        check_canary("====", 4, 1, &modp_b64_decode_ssse3_for_testing, "ssse3");
    }
    if (has_avx2) {
        check_canary("====", 4, 1, &modp_b64_decode_avx2_for_testing, "avx2");
    }
#endif

    // 2-byte canary buffer for 4-char invalid trailing quanta across all tiers.
    for (const char* input : {"A===", "A!==", "AA!="}) {
        check_canary(input, 4, 2, &modp_b64_decode, "dispatched");
#if defined(UNIT_TEST) && defined(__x86_64__)
        check_canary(input, 4, 2, &modp_b64_decode_scalar_for_testing,
                     "scalar");
        if (has_ssse3) {
            check_canary(input, 4, 2, &modp_b64_decode_ssse3_for_testing,
                         "ssse3");
        }
        if (has_avx2) {
            check_canary(input, 4, 2, &modp_b64_decode_avx2_for_testing,
                         "avx2");
        }
#endif
    }

    // Guard-page test: 3 valid prefix bytes ("AAA") followed by invalid 2nd
    // quantum with only 3 writable bytes before the PROT_NONE guard page.
    GuardedPagePair src_guard;
    GuardedPagePair dst_guard;
    ASSERT_TRUE(src_guard.Init());
    ASSERT_TRUE(dst_guard.Init());

    auto check_guarded_3bytes = [&](const char* input,
                                    size_t (*decode_fn)(char*, const char*,
                                                        size_t),
                                    const char* tier) {
        const size_t enc_len = 8;
        char* guarded_src =
            reinterpret_cast<char*>(src_guard.rw_end() - enc_len);
        memcpy(guarded_src, input, enc_len);
        char* guarded_dst = reinterpret_cast<char*>(dst_guard.rw_end() - 3);
        memset(guarded_dst, 0x5A, 3);
        EXPECT_EQ(decode_fn(guarded_dst, guarded_src, enc_len), MODP_B64_ERROR)
            << tier << " input=" << input;
    };

    for (const char* input : {"QUFB====", "QUFB!A=="}) {
        check_guarded_3bytes(input, &modp_b64_decode, "dispatched");
#if defined(UNIT_TEST) && defined(__x86_64__)
        check_guarded_3bytes(input, &modp_b64_decode_scalar_for_testing,
                             "scalar");
        if (has_ssse3) {
            check_guarded_3bytes(input, &modp_b64_decode_ssse3_for_testing,
                                 "ssse3");
        }
        if (has_avx2) {
            check_guarded_3bytes(input, &modp_b64_decode_avx2_for_testing,
                                 "avx2");
        }
#endif
    }
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
            std::chrono::duration<double, std::nano>(t1 - t0).count() /
            static_cast<double>(iters);
        double dec_ns =
            std::chrono::duration<double, std::nano>(t2 - t1).count() /
            static_cast<double>(iters);
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
            std::chrono::duration<double, std::nano>(ts1 - ts0).count() /
            static_cast<double>(iters);
        double dec_s_ns =
            std::chrono::duration<double, std::nano>(ts2 - ts1).count() /
            static_cast<double>(iters);
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
                static_cast<double>(iters);
            double dec_ss_ns =
                std::chrono::duration<double, std::nano>(tss2 - tss1).count() /
                static_cast<double>(iters);
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
                static_cast<double>(iters);
            double dec_a_ns =
                std::chrono::duration<double, std::nano>(ta2 - ta1).count() /
                static_cast<double>(iters);
            printf(
                "[Base64 Bench AVX2      ] size=%5zu B | encode=%8.2f ns | "
                "decode=%8.2f ns\n",
                sz, enc_a_ns, dec_a_ns);
        }
#endif
    }
}

}  // namespace butil
