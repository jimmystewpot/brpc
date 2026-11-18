//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "butil/crc32c.h"

#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

namespace butil {
namespace crc32c {

#if defined(UNIT_TEST)
namespace internal {
uint32_t ExtendSlowForTesting(uint32_t crc, const char* buf, size_t size);
#if defined(__x86_64__)
bool IsSse42SupportedForTesting();
bool IsPclmulqdqSupportedForTesting();
uint32_t ExtendSse42ForTesting(uint32_t crc, const char* buf, size_t size);
uint32_t ExtendSse42PclmulForTesting(uint32_t crc, const char* buf,
                                     size_t size);
#endif
}  // namespace internal
#endif

class CRC : public testing::Test {};

TEST_F(CRC, StandardResults) {
  // From rfc3720 section B.4.
  char buf[32];

  memset(buf, 0, sizeof(buf));
  ASSERT_EQ(0x8a9136aaU, Value(buf, sizeof(buf)));

  memset(buf, 0xff, sizeof(buf));
  ASSERT_EQ(0x62a8ab43U, Value(buf, sizeof(buf)));

  for (int i = 0; i < 32; i++) {
    buf[i] = i;
  }
  ASSERT_EQ(0x46dd794eU, Value(buf, sizeof(buf)));

  for (int i = 0; i < 32; i++) {
    buf[i] = 31 - i;
  }
  ASSERT_EQ(0x113fdb5cU, Value(buf, sizeof(buf)));

  unsigned char data[48] = {
    0x01, 0xc0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x14, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x04, 0x00,
    0x00, 0x00, 0x00, 0x14,
    0x00, 0x00, 0x00, 0x18,
    0x28, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
  };
  ASSERT_EQ(0xd9963a56, Value(reinterpret_cast<char*>(data), sizeof(data)));
}

TEST_F(CRC, Values) {
  ASSERT_NE(Value("a", 1), Value("foo", 3));
}

TEST_F(CRC, Extend) {
  ASSERT_EQ(Value("hello world", 11),
            Extend(Value("hello ", 6), "world", 5));
}

TEST_F(CRC, Mask) {
  uint32_t crc = Value("foo", 3);
  ASSERT_NE(crc, Mask(crc));
  ASSERT_NE(crc, Mask(Mask(crc)));
  ASSERT_EQ(crc, Unmask(Mask(crc)));
  ASSERT_EQ(crc, Unmask(Unmask(Mask(Mask(crc)))));
}

TEST_F(CRC, fast_is_on) {
    std::cout << "IsFastCrc32Supported=" << IsFastCrc32Supported() << std::endl;
#if defined(__x86_64__)
    EXPECT_TRUE(IsFastCrc32Supported());
#endif
}

#if defined(UNIT_TEST)
TEST_F(CRC, AllTiersBitExactAcrossAlignmentsAndStripeBoundaries) {
    const size_t kMaxLen = 4096;
    std::vector<char> storage(kMaxLen + 64);
    for (size_t i = 0; i < storage.size(); ++i) {
        storage[i] = static_cast<char>((i * 131 + 17) & 0xff);
    }

    const uint32_t seeds[] = {0U, 1U, 0xffffffffU, 0x12345678U};

    std::vector<size_t> lengths;
    for (size_t len = 0; len <= 2500; ++len) {
        lengths.push_back(len);
    }
    lengths.push_back(4096);

    for (size_t len : lengths) {
        const bool is_boundary_len =
            (len <= 64) || (len >= 188 && len <= 196) ||
            (len >= 380 && len <= 388) || (len >= 1004 && len <= 1012) ||
            (len >= 2012 && len <= 2020) || (len == 2500) || (len == 4096);
        for (size_t align = 0; align < 16; ++align) {
            if (!is_boundary_len && align != (len & 15)) {
                continue;
            }
            const char* buf = storage.data() + align;
            for (size_t s = 0; s < sizeof(seeds) / sizeof(seeds[0]); ++s) {
                if (!is_boundary_len && s != (len & 3)) {
                    continue;
                }
                const uint32_t seed = seeds[s];
                const uint32_t expected =
                    internal::ExtendSlowForTesting(seed, buf, len);
                ASSERT_EQ(expected, Extend(seed, buf, len))
                    << "len=" << len << " align=" << align << " seed=" << seed;
#if defined(__x86_64__)
                if (internal::IsSse42SupportedForTesting()) {
                    ASSERT_EQ(expected,
                              internal::ExtendSse42ForTesting(seed, buf, len))
                        << "SSE4.2 mismatch at len=" << len
                        << " align=" << align << " seed=" << seed;
                }
                if (internal::IsPclmulqdqSupportedForTesting()) {
                    ASSERT_EQ(expected, internal::ExtendSse42PclmulForTesting(
                                            seed, buf, len))
                        << "PCLMUL mismatch at len=" << len
                        << " align=" << align << " seed=" << seed;
                }
#endif
            }
        }
    }

    // Incremental split checks across 192 and 1008 boundaries.
    const size_t total_len = 2500;
    const size_t splits[] = {1,    7,    8,    15,   16,   31,   32,  63,
                             64,   191,  192,  193,  383,  384,  385, 1007,
                             1008, 1009, 1200, 2015, 2016, 2017, 2499};
    for (size_t align = 0; align < 16; ++align) {
        const char* buf = storage.data() + align;
        const uint32_t full_crc =
            internal::ExtendSlowForTesting(0U, buf, total_len);
        for (size_t split : splits) {
            uint32_t c_ext = Extend(0U, buf, split);
            c_ext = Extend(c_ext, buf + split, total_len - split);
            ASSERT_EQ(full_crc, c_ext)
                << "split=" << split << " align=" << align;
#if defined(__x86_64__)
            if (internal::IsSse42SupportedForTesting()) {
                uint32_t c_sse =
                    internal::ExtendSse42ForTesting(0U, buf, split);
                c_sse = internal::ExtendSse42ForTesting(c_sse, buf + split,
                                                        total_len - split);
                ASSERT_EQ(full_crc, c_sse)
                    << "SSE4.2 split=" << split << " align=" << align;
            }
            if (internal::IsPclmulqdqSupportedForTesting()) {
                uint32_t c_pcl =
                    internal::ExtendSse42PclmulForTesting(0U, buf, split);
                c_pcl = internal::ExtendSse42PclmulForTesting(
                    c_pcl, buf + split, total_len - split);
                ASSERT_EQ(full_crc, c_pcl)
                    << "PCLMUL split=" << split << " align=" << align;
            }
#endif
        }
    }
}

TEST_F(CRC, GuardPageBoundaryNoOverRead) {
    const long page_size_long = sysconf(_SC_PAGESIZE);
    ASSERT_GT(page_size_long, 0);
    const size_t page_size = static_cast<size_t>(page_size_long);
    const size_t alloc_size = page_size * 2;

    void* mapping = mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(mapping, MAP_FAILED);
    char* base = static_cast<char*>(mapping);
    ASSERT_EQ(0, mprotect(base + page_size, page_size, PROT_NONE));

    for (size_t i = 0; i < page_size; ++i) {
        base[i] = static_cast<char>((i * 73 + 19) & 0xff);
    }

    const size_t test_sizes[] = {0,  1,  7,   8,   15,  16,   31,   32,
                                 63, 64, 191, 192, 193, 1007, 1008, 1009};
    for (size_t sz : test_sizes) {
        ASSERT_LE(sz, page_size);
        const char* buf = base + page_size - sz;
        const uint32_t expected =
            internal::ExtendSlowForTesting(0x12345678U, buf, sz);
        EXPECT_EQ(expected, Extend(0x12345678U, buf, sz)) << "sz=" << sz;
#if defined(__x86_64__)
        if (internal::IsSse42SupportedForTesting()) {
            EXPECT_EQ(expected,
                      internal::ExtendSse42ForTesting(0x12345678U, buf, sz))
                << "SSE4.2 sz=" << sz;
        }
        if (internal::IsPclmulqdqSupportedForTesting()) {
            EXPECT_EQ(expected, internal::ExtendSse42PclmulForTesting(
                                    0x12345678U, buf, sz))
                << "PCLMUL sz=" << sz;
        }
#endif
    }

    ASSERT_EQ(0, munmap(mapping, alloc_size));
}

TEST_F(CRC, BenchmarkAllTiersThroughput) {
    const size_t bench_sizes[] = {1,   7,    8,    16,   64,    192,
                                  256, 1008, 1024, 4096, 16384, 65536};
    std::vector<char> buf(65536);
    for (size_t i = 0; i < buf.size(); ++i) {
        buf[i] = static_cast<char>((i * 31 + 7) & 0xff);
    }

    volatile uint32_t sink = 0;
    for (size_t sz : bench_sizes) {
        const size_t total_target_bytes =
            (sz < 64) ? (4ULL << 20) : (32ULL << 20);
        const size_t iters =
            (total_target_bytes / sz > 1000) ? (total_target_bytes / sz) : 1000;
        const double total_gib =
            static_cast<double>(iters * sz) / (1024.0 * 1024.0 * 1024.0);

        auto t0 = std::chrono::steady_clock::now();
        uint32_t acc = 0;
        for (size_t i = 0; i < iters; ++i) {
            acc = internal::ExtendSlowForTesting(acc, buf.data(), sz);
        }
        auto t1 = std::chrono::steady_clock::now();
        sink ^= acc;
        double slow_sec = std::chrono::duration<double>(t1 - t0).count();
        double slow_gibs = total_gib / slow_sec;

#if defined(__x86_64__)
        double sse42_gibs = 0.0;
        if (internal::IsSse42SupportedForTesting()) {
            auto ts0 = std::chrono::steady_clock::now();
            uint32_t acc_s = 0;
            for (size_t i = 0; i < iters; ++i) {
                acc_s = internal::ExtendSse42ForTesting(acc_s, buf.data(), sz);
            }
            auto ts1 = std::chrono::steady_clock::now();
            sink ^= acc_s;
            sse42_gibs =
                total_gib / std::chrono::duration<double>(ts1 - ts0).count();
        }

        double pclmul_gibs = 0.0;
        if (internal::IsPclmulqdqSupportedForTesting()) {
            auto tp0 = std::chrono::steady_clock::now();
            uint32_t acc_p = 0;
            for (size_t i = 0; i < iters; ++i) {
                acc_p = internal::ExtendSse42PclmulForTesting(acc_p, buf.data(),
                                                              sz);
            }
            auto tp1 = std::chrono::steady_clock::now();
            sink ^= acc_p;
            pclmul_gibs =
                total_gib / std::chrono::duration<double>(tp1 - tp0).count();
        }
        std::printf(
            "[CRC32C Bench] size=%5zu B | Slow=%6.2f GiB/s | SSE4.2=%6.2f "
            "GiB/s "
            "| SSE4.2+PCLMULQDQ=%6.2f GiB/s\n",
            sz, slow_gibs, sse42_gibs, pclmul_gibs);
#else
        std::printf("[CRC32C Bench] size=%5zu B | Slow=%6.2f GiB/s\n", sz,
                    slow_gibs);
#endif
    }
    (void)sink;
}
#endif  // UNIT_TEST

}  // namespace crc32c
}  // namespace butil
