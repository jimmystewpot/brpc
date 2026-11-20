// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_CPU_H_
#define BUTIL_CPU_H_

#include <stdint.h>

#include <string>

#include "butil/base_export.h"

namespace butil {

#if defined(UNIT_TEST)
namespace internal {

enum X86ExtFeatureBits : uint8_t {
    kExtFeaturePclmulqdq = static_cast<uint8_t>(1U << 0),
    kExtFeatureAvx2 = static_cast<uint8_t>(1U << 1),
    kExtFeatureBmi1 = static_cast<uint8_t>(1U << 2),
    kExtFeatureBmi2 = static_cast<uint8_t>(1U << 3),
    kExtFeatureFma3 = static_cast<uint8_t>(1U << 4),
    kExtFeatureAvx512f = static_cast<uint8_t>(1U << 5),
    kExtFeatureFastAvx512 = static_cast<uint8_t>(1U << 6),
};

BUTIL_EXPORT uint8_t ComputeX86ExtendedFeatures(uint32_t num_ids,
                                                uint32_t leaf1_ecx,
                                                uint32_t leaf7_ebx,
                                                uint32_t leaf7_ecx,
                                                uint64_t xcr0);
BUTIL_EXPORT bool HasExtendedCpuLeaf(uint32_t max_extended_leaf,
                                     uint32_t target_leaf);

}  // namespace internal
#endif

// Query information about the processor.
class BUTIL_EXPORT CPU {
   public:
    // Constructor
    CPU();

    enum IntelMicroArchitecture {
        PENTIUM,
        SSE,
        SSE2,
        SSE3,
        SSSE3,
        SSE41,
        SSE42,
        AVX,
        AVX2,
        AVX512F,
        MAX_INTEL_MICRO_ARCHITECTURE
    };

    // Accessors for CPU information.
    const std::string& vendor_name() const { return cpu_vendor_; }
    int signature() const { return signature_; }
    int stepping() const { return stepping_; }
    int model() const { return model_; }
    int family() const { return family_; }
    int type() const { return type_; }
    int extended_model() const { return ext_model_; }
    int extended_family() const { return ext_family_; }
    bool has_mmx() const { return has_mmx_; }
    bool has_sse() const { return has_sse_; }
    bool has_sse2() const { return has_sse2_; }
    bool has_sse3() const { return has_sse3_; }
    bool has_ssse3() const { return has_ssse3_; }
    bool has_sse41() const { return has_sse41_; }
    bool has_sse42() const { return has_sse42_; }
    bool has_avx() const { return has_avx_; }
    // has_avx_hardware returns true when AVX is present in the CPU. This might
    // differ from the value of |has_avx()| because |has_avx()| also tests for
    // operating system support needed to actually call AVX instuctions.
    // Note: you should never need to call this function. It was added in order
    // to workaround a bug in NSS but |has_avx()| is what you want.
    bool has_avx_hardware() const { return has_avx_hardware_; }
    bool has_aesni() const { return has_aesni_; }
    bool has_pclmulqdq() const { return (ext_features_ & (1U << 0)) != 0; }
    bool has_avx2() const { return (ext_features_ & (1U << 1)) != 0; }
    bool has_bmi1() const { return (ext_features_ & (1U << 2)) != 0; }
    bool has_bmi2() const { return (ext_features_ & (1U << 3)) != 0; }
    bool has_fma3() const { return (ext_features_ & (1U << 4)) != 0; }
    bool has_avx512f() const { return (ext_features_ & (1U << 5)) != 0; }
    // Returns true only when the CPU and OS support the full Ice Lake / AMD Zen
    // 4 AVX-512 feature bundle (AVX512F + DQ + CD + BW + VL + VBMI + VBMI2)
    // that avoids Skylake-X / Cascade Lake 512-bit frequency throttling.
    bool has_fast_avx512() const { return (ext_features_ & (1U << 6)) != 0; }
    bool has_non_stop_time_stamp_counter() const {
        return has_non_stop_time_stamp_counter_;
    }
    IntelMicroArchitecture GetIntelMicroArchitecture() const;
    const std::string& cpu_brand() const { return cpu_brand_; }

   private:
    // Query the processor for CPUID information.
    void Initialize();

    int signature_;  // raw form of type, family, model, and stepping
    int type_;       // process type
    int family_;     // family of the processor
    int model_;      // model of processor
    int stepping_;   // processor revision number
    int ext_model_;
    int ext_family_;
    bool has_mmx_;
    bool has_sse_;
    bool has_sse2_;
    bool has_sse3_;
    bool has_ssse3_;
    bool has_sse41_;
    bool has_sse42_;
    bool has_avx_;
    bool has_avx_hardware_;
    bool has_aesni_;
    bool has_non_stop_time_stamp_counter_;
    uint8_t ext_features_;
    std::string cpu_vendor_;
    std::string cpu_brand_;
};

}  // namespace butil

#endif  // BUTIL_CPU_H_
