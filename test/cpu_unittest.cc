// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/cpu.h"

#include <gtest/gtest.h>
#include <stdint.h>

#include <string>

#include "butil/build_config.h"

namespace {

struct BaselineCPULayout {
    int signature;
    int type;
    int family;
    int model;
    int stepping;
    int ext_model;
    int ext_family;
    bool has_mmx;
    bool has_sse;
    bool has_sse2;
    bool has_sse3;
    bool has_ssse3;
    bool has_sse41;
    bool has_sse42;
    bool has_avx;
    bool has_avx_hardware;
    bool has_aesni;
    bool has_non_stop_time_stamp_counter;
    std::string cpu_vendor;
    std::string cpu_brand;
};

#if defined(OS_POSIX) && defined(__x86_64__)
__attribute__((target("pclmul"))) void ExecutePclmulqdq() {
    __asm__ __volatile__("pclmulqdq $0, %%xmm0, %%xmm0\n" : : : "xmm0");
}

__attribute__((target("bmi"))) void ExecuteBmi1() {
    uint32_t a = 0x12345678U;
    uint32_t b = 0x0F0F0F0FU;
    uint32_t out = 0;
    __asm__ __volatile__("andnl %2, %1, %0\n"
                         : "=r"(out)
                         : "r"(a), "r"(b)
                         : "cc");
}

__attribute__((target("bmi2"))) void ExecuteBmi2() {
    uint32_t a = 0x12345678U;
    uint32_t b = 8U;
    uint32_t out = 0;
    __asm__ __volatile__("bzhil %2, %1, %0\n"
                         : "=r"(out)
                         : "r"(a), "r"(b)
                         : "cc");
}

__attribute__((target("fma"))) void ExecuteFma3() {
    __asm__ __volatile__(
        "vpxor %%xmm0, %%xmm0, %%xmm0\n"
        "vfmadd132ps %%xmm0, %%xmm0, %%xmm0\n"
        :
        :
        : "xmm0");
}

__attribute__((target("avx2"))) void ExecuteAvx2() {
    __asm__ __volatile__(
        "vpaddd %%ymm0, %%ymm0, %%ymm0\n"
        "vzeroupper\n"
        :
        :
        : "ymm0");
}

__attribute__((target("avx512f"))) void ExecuteAvx512f() {
    __asm__ __volatile__(
        "vpord %%zmm0, %%zmm0, %%zmm0\n"
        "vzeroupper\n"
        :
        :
        : "zmm0");
}

__attribute__((target("avx512f,avx512bw,avx512vl,avx512vbmi,avx512vbmi2"))) void
ExecuteFastAvx512() {
    __asm__ __volatile__(
        "vpshldvd %%zmm0, %%zmm0, %%zmm0\n"
        "vzeroupper\n"
        :
        :
        : "zmm0");
}
#endif

}  // namespace

TEST(CPU, AbiSizePreserved) {
    static_assert(
        sizeof(butil::CPU) == sizeof(BaselineCPULayout),
        "sizeof(butil::CPU) must remain unchanged for ABI compatibility");
    EXPECT_EQ(sizeof(butil::CPU), sizeof(BaselineCPULayout));
}

TEST(CPU, SyntheticCpuidFeatureVectors) {
    using namespace butil::internal;
    const uint32_t kLeaf1AvxFmaPclmulOsxsave = (1U << 1) |   // PCLMULQDQ
                                               (1U << 12) |  // FMA3
                                               (1U << 26) |  // XSAVE
                                               (1U << 27) |  // OSXSAVE
                                               (1U << 28);   // AVX

    const uint32_t kLeaf7SkylakeXEbx = (1U << 3) |   // BMI1
                                       (1U << 5) |   // AVX2
                                       (1U << 8) |   // BMI2
                                       (1U << 16) |  // AVX512F
                                       (1U << 17) |  // AVX512DQ
                                       (1U << 28) |  // AVX512CD
                                       (1U << 30) |  // AVX512BW
                                       (1U << 31);   // AVX512VL
    const uint32_t kLeaf7SkylakeXEcx =
        0U;  // No VBMI / VBMI2 on Skylake-X/Cascade Lake

    const uint32_t kLeaf7IcelakeEcx = (1U << 1) |  // AVX512_VBMI
                                      (1U << 6);   // AVX512_VBMI2

    // 1. Skylake-X / Cascade Lake (0x55): has_avx512f is true, but
    // has_fast_avx512 is false.
    const uint8_t skx = ComputeX86ExtendedFeatures(7, kLeaf1AvxFmaPclmulOsxsave,
                                                   kLeaf7SkylakeXEbx,
                                                   kLeaf7SkylakeXEcx, 0xe7ULL);
    EXPECT_NE(skx & kExtFeaturePclmulqdq, 0);
    EXPECT_NE(skx & kExtFeatureAvx2, 0);
    EXPECT_NE(skx & kExtFeatureBmi1, 0);
    EXPECT_NE(skx & kExtFeatureBmi2, 0);
    EXPECT_NE(skx & kExtFeatureFma3, 0);
    EXPECT_NE(skx & kExtFeatureAvx512f, 0);
    EXPECT_EQ(skx & kExtFeatureFastAvx512, 0);

    // 2. Ice Lake / Emerald Rapids / AMD Zen 4: both has_avx512f and
    // has_fast_avx512 are true.
    const uint8_t icx = ComputeX86ExtendedFeatures(7, kLeaf1AvxFmaPclmulOsxsave,
                                                   kLeaf7SkylakeXEbx,
                                                   kLeaf7IcelakeEcx, 0xe7ULL);
    EXPECT_NE(icx & kExtFeatureAvx512f, 0);
    EXPECT_NE(icx & kExtFeatureFastAvx512, 0);

    // 3. XCR0 only enables XMM + YMM (0x07), not ZMM/opmask (0xe6): AVX2/FMA3
    // remain enabled,
    //    but AVX512F and FastAvx512 must be disabled.
    const uint8_t ymm_only = ComputeX86ExtendedFeatures(
        7, kLeaf1AvxFmaPclmulOsxsave, kLeaf7SkylakeXEbx, kLeaf7IcelakeEcx,
        0x07ULL);
    EXPECT_NE(ymm_only & kExtFeatureAvx2, 0);
    EXPECT_NE(ymm_only & kExtFeatureFma3, 0);
    EXPECT_EQ(ymm_only & kExtFeatureAvx512f, 0);
    EXPECT_EQ(ymm_only & kExtFeatureFastAvx512, 0);

    // 4. OSXSAVE disabled in Leaf 1 ECX: all vector extensions requiring XSAVE
    // (AVX2, FMA3,
    //    AVX512F, FastAvx512) must be disabled, while GPR/XMM extensions
    //    (PCLMULQDQ, BMI1, BMI2) remain enabled.
    const uint32_t kLeaf1NoOsxsave = kLeaf1AvxFmaPclmulOsxsave & ~(1U << 27);
    const uint8_t no_osxsave = ComputeX86ExtendedFeatures(
        7, kLeaf1NoOsxsave, kLeaf7SkylakeXEbx, kLeaf7IcelakeEcx, 0xe7ULL);
    EXPECT_NE(no_osxsave & kExtFeaturePclmulqdq, 0);
    EXPECT_NE(no_osxsave & kExtFeatureBmi1, 0);
    EXPECT_NE(no_osxsave & kExtFeatureBmi2, 0);
    EXPECT_EQ(no_osxsave & kExtFeatureAvx2, 0);
    EXPECT_EQ(no_osxsave & kExtFeatureFma3, 0);
    EXPECT_EQ(no_osxsave & kExtFeatureAvx512f, 0);
    EXPECT_EQ(no_osxsave & kExtFeatureFastAvx512, 0);

    // 5. num_ids < 7: Leaf 7 features (AVX2, BMI1, BMI2, AVX512F, FastAvx512)
    // must not be set.
    const uint8_t old_cpuid = ComputeX86ExtendedFeatures(
        6, kLeaf1AvxFmaPclmulOsxsave, kLeaf7SkylakeXEbx, kLeaf7IcelakeEcx,
        0xe7ULL);
    EXPECT_NE(old_cpuid & kExtFeaturePclmulqdq, 0);
    EXPECT_NE(old_cpuid & kExtFeatureFma3, 0);
    EXPECT_EQ(old_cpuid & kExtFeatureAvx2, 0);
    EXPECT_EQ(old_cpuid & kExtFeatureBmi1, 0);
    EXPECT_EQ(old_cpuid & kExtFeatureBmi2, 0);
    EXPECT_EQ(old_cpuid & kExtFeatureAvx512f, 0);
    EXPECT_EQ(old_cpuid & kExtFeatureFastAvx512, 0);
}

// Tests whether we can run extended instructions represented by the CPU
// information. This test actually executes some extended instructions (such as
// MMX, SSE, etc.) supported by the CPU and sees we can run them without
// "undefined instruction" exceptions. That is, this test succeeds when this
// test finishes without a crash.
TEST(CPU, RunExtendedInstructions) {
#if defined(ARCH_CPU_X86_FAMILY)
    // Retrieve the CPU information.
    butil::CPU cpu;

// TODO(jschuh): crbug.com/168866 Find a way to enable this on Win64.
#if defined(OS_WIN) && !defined(_M_X64)
  ASSERT_TRUE(cpu.has_mmx());

  // Execute an MMX instruction.
  __asm emms;

  if (cpu.has_sse()) {
    // Execute an SSE instruction.
    __asm xorps xmm0, xmm0;
  }

  if (cpu.has_sse2()) {
    // Execute an SSE 2 instruction.
    __asm psrldq xmm0, 0;
  }

  if (cpu.has_sse3()) {
    // Execute an SSE 3 instruction.
    __asm addsubpd xmm0, xmm0;
  }

  if (cpu.has_ssse3()) {
    // Execute a Supplimental SSE 3 instruction.
    __asm psignb xmm0, xmm0;
  }

  if (cpu.has_sse41()) {
    // Execute an SSE 4.1 instruction.
    __asm pmuldq xmm0, xmm0;
  }

  if (cpu.has_sse42()) {
    // Execute an SSE 4.2 instruction.
    __asm crc32 eax, eax;
  }
#elif defined(OS_POSIX) && defined(__x86_64__)
  ASSERT_TRUE(cpu.has_mmx());

  // Execute an MMX instruction.
  __asm__ __volatile__("emms\n" : : : "mm0");

  if (cpu.has_sse()) {
    // Execute an SSE instruction.
    __asm__ __volatile__("xorps %%xmm0, %%xmm0\n" : : : "xmm0");
  }

  if (cpu.has_sse2()) {
    // Execute an SSE 2 instruction.
    __asm__ __volatile__("psrldq $0, %%xmm0\n" : : : "xmm0");
  }

  if (cpu.has_sse3()) {
    // Execute an SSE 3 instruction.
    __asm__ __volatile__("addsubpd %%xmm0, %%xmm0\n" : : : "xmm0");
  }

  // NOTE(gejun): Not work in gcc 3.4
#if !defined(COMPILER_GCC) || __GNUC__ >= 4
  if (cpu.has_ssse3()) {
    // Execute a Supplimental SSE 3 instruction.
    __asm__ __volatile__("psignb %%xmm0, %%xmm0\n" : : : "xmm0");
  }

  if (cpu.has_sse41()) {
    // Execute an SSE 4.1 instruction.
    __asm__ __volatile__("pmuldq %%xmm0, %%xmm0\n" : : : "xmm0");
  }

  if (cpu.has_sse42()) {
    // Execute an SSE 4.2 instruction.
    __asm__ __volatile__("crc32 %%eax, %%eax\n" : : : "eax");
  }

  if (cpu.has_pclmulqdq()) {
    ExecutePclmulqdq();
  }

  if (cpu.has_bmi1()) {
      ExecuteBmi1();
  }

  if (cpu.has_bmi2()) {
      ExecuteBmi2();
  }

  if (cpu.has_fma3()) {
      EXPECT_TRUE(cpu.has_avx());
      ExecuteFma3();
  }

  if (cpu.has_avx2()) {
      EXPECT_TRUE(cpu.has_avx());
      ExecuteAvx2();
  }

  if (cpu.has_avx512f()) {
      EXPECT_TRUE(cpu.has_avx2());
      ExecuteAvx512f();
  }

  if (cpu.has_fast_avx512()) {
      EXPECT_TRUE(cpu.has_avx512f());
      ExecuteFastAvx512();
      EXPECT_EQ(cpu.GetIntelMicroArchitecture(), butil::CPU::AVX512F);
  } else if (cpu.has_avx2()) {
      EXPECT_EQ(cpu.GetIntelMicroArchitecture(), butil::CPU::AVX2);
  } else if (cpu.has_avx()) {
      EXPECT_EQ(cpu.GetIntelMicroArchitecture(), butil::CPU::AVX);
  }
#endif

#endif
#else
    butil::CPU cpu;
    EXPECT_FALSE(cpu.has_pclmulqdq());
    EXPECT_FALSE(cpu.has_avx2());
    EXPECT_FALSE(cpu.has_bmi1());
    EXPECT_FALSE(cpu.has_bmi2());
    EXPECT_FALSE(cpu.has_fma3());
    EXPECT_FALSE(cpu.has_avx512f());
    EXPECT_FALSE(cpu.has_fast_avx512());
#endif
}
