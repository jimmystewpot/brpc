// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/cpu.h"

#include <string.h>

#include <algorithm>

#include "butil/basictypes.h"
#include "butil/build_config.h"

#if defined(ARCH_CPU_ARM_FAMILY) && (defined(OS_ANDROID) || defined(OS_LINUX))
#include "butil/file_util.h"
#include "butil/lazy_instance.h"
#endif

#if defined(ARCH_CPU_X86_FAMILY)
#if defined(_MSC_VER)
#include <immintrin.h>  // For _xgetbv()
#include <intrin.h>
#endif
#endif

namespace butil {

namespace internal {

#if defined(ARCH_CPU_X86_FAMILY) || defined(UNIT_TEST)

#if !defined(UNIT_TEST)
enum X86ExtFeatureBits : uint8_t {
    kExtFeaturePclmulqdq = static_cast<uint8_t>(1U << 0),
    kExtFeatureAvx2 = static_cast<uint8_t>(1U << 1),
    kExtFeatureBmi1 = static_cast<uint8_t>(1U << 2),
    kExtFeatureBmi2 = static_cast<uint8_t>(1U << 3),
    kExtFeatureFma3 = static_cast<uint8_t>(1U << 4),
    kExtFeatureAvx512f = static_cast<uint8_t>(1U << 5),
    kExtFeatureFastAvx512 = static_cast<uint8_t>(1U << 6),
};
#endif

#if !defined(UNIT_TEST)
static
#endif
    uint8_t ComputeX86ExtendedFeatures(uint32_t num_ids, uint32_t leaf1_ecx,
                                       uint32_t leaf7_ebx, uint32_t leaf7_ecx,
                                       uint64_t xcr0) {
    uint8_t ext = 0;
    const bool has_osxsave =
        (leaf1_ecx & 0x04000000U) != 0 && (leaf1_ecx & 0x08000000U) != 0;
    const bool has_avx_os = has_osxsave && ((xcr0 & 0x6U) == 0x6U);
    const bool has_avx = (leaf1_ecx & 0x10000000U) != 0 && has_avx_os;
    // XCR0 bits 1 (XMM), 2 (YMM), 5 (opmask k0-k7), 6 (ZMM_Hi256), 7 (Hi16_ZMM)
    // = 0xe6.
    const bool has_avx512_os = has_osxsave && ((xcr0 & 0xe6U) == 0xe6U);

    if ((leaf1_ecx & (1U << 1)) != 0) {
        ext |= kExtFeaturePclmulqdq;
    }
    if ((leaf1_ecx & (1U << 12)) != 0 && has_avx) {
        ext |= kExtFeatureFma3;
    }
    if (num_ids >= 7U) {
        if ((leaf7_ebx & (1U << 3)) != 0) {
            ext |= kExtFeatureBmi1;
        }
        if ((leaf7_ebx & (1U << 5)) != 0 && has_avx) {
            ext |= kExtFeatureAvx2;
        }
        if ((leaf7_ebx & (1U << 8)) != 0) {
            ext |= kExtFeatureBmi2;
        }
        if ((leaf7_ebx & (1U << 16)) != 0 && has_avx512_os) {
            ext |= kExtFeatureAvx512f;
        }
        // Ice Lake / AMD Zen 4 non-downclocking AVX-512 bundle:
        // EBX: AVX512F (16) | AVX512DQ (17) | AVX512CD (28) | AVX512BW (30) |
        // AVX512VL (31) ECX: AVX512_VBMI (1) | AVX512_VBMI2 (6)
        const uint32_t kIcelakeEbxMask =
            (1U << 16) | (1U << 17) | (1U << 28) | (1U << 30) | (1U << 31);
        const uint32_t kIcelakeEcxMask = (1U << 1) | (1U << 6);
        if (has_avx512_os && (leaf7_ebx & kIcelakeEbxMask) == kIcelakeEbxMask &&
            (leaf7_ecx & kIcelakeEcxMask) == kIcelakeEcxMask) {
            ext |= kExtFeatureFastAvx512;
        }
    }
    return ext;
}

#if !defined(UNIT_TEST)
static
#endif
    bool HasExtendedCpuLeaf(uint32_t max_extended_leaf, uint32_t target_leaf) {
    return max_extended_leaf >= 0x80000000U && max_extended_leaf >= target_leaf;
}

#endif  // defined(ARCH_CPU_X86_FAMILY) || defined(UNIT_TEST)

}  // namespace internal

CPU::CPU()
    : signature_(0),
      type_(0),
      family_(0),
      model_(0),
      stepping_(0),
      ext_model_(0),
      ext_family_(0),
      has_mmx_(false),
      has_sse_(false),
      has_sse2_(false),
      has_sse3_(false),
      has_ssse3_(false),
      has_sse41_(false),
      has_sse42_(false),
      has_avx_(false),
      has_avx_hardware_(false),
      has_aesni_(false),
      has_non_stop_time_stamp_counter_(false),
      ext_features_(0),
      cpu_vendor_("unknown") {
    Initialize();
}

namespace {

#if defined(ARCH_CPU_X86_FAMILY)
#if defined(_MSC_VER)

void CpuidEx(int cpu_info[4], int info_type, int info_index) {
    __cpuidex(cpu_info, info_type, info_index);
}

#else

#if defined(__pic__) && defined(__i386__)

void __cpuid(int cpu_info[4], int info_type) {
    __asm__ volatile(
        "mov %%ebx, %%edi\n"
        "cpuid\n"
        "xchg %%edi, %%ebx\n"
        : "=a"(cpu_info[0]), "=D"(cpu_info[1]), "=c"(cpu_info[2]),
          "=d"(cpu_info[3])
        : "a"(info_type), "c"(0));
}

void CpuidEx(int cpu_info[4], int info_type, int info_index) {
    __asm__ volatile(
        "mov %%ebx, %%edi\n"
        "cpuid\n"
        "xchg %%edi, %%ebx\n"
        : "=a"(cpu_info[0]), "=D"(cpu_info[1]), "=c"(cpu_info[2]),
          "=d"(cpu_info[3])
        : "0"(info_type), "2"(info_index));
}

#else

void __cpuid(int cpu_info[4], int info_type) {
    __asm__ volatile("cpuid \n\t"
                     : "=a"(cpu_info[0]), "=b"(cpu_info[1]), "=c"(cpu_info[2]),
                       "=d"(cpu_info[3])
                     : "a"(info_type), "c"(0));
}

void CpuidEx(int cpu_info[4], int info_type, int info_index) {
    __asm__ volatile("cpuid \n\t"
                     : "=a"(cpu_info[0]), "=b"(cpu_info[1]), "=c"(cpu_info[2]),
                       "=d"(cpu_info[3])
                     : "0"(info_type), "2"(info_index));
}

#endif

// _xgetbv returns the value of an Intel Extended Control Register (XCR).
// Currently only XCR0 is defined by Intel so |xcr| should always be zero.
uint64_t _xgetbv(uint32_t xcr) {
    uint32_t eax, edx;

// NOTE(gejun): xgetbv does not exist in gcc before 4.4, disable the use of
// AVX instruction set.
#if defined(COMPILER_GCC) && \
    (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 4))
    __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(xcr));
#else
    __asm__ volatile(".byte 0x0f, 0x01, 0xd0"
                     : "=a"(eax), "=d"(edx)
                     : "c"(xcr)
                     :);
#endif
    return (static_cast<uint64_t>(edx) << 32) | eax;
}

#endif  // !_MSC_VER
#endif  // ARCH_CPU_X86_FAMILY

#if defined(ARCH_CPU_ARM_FAMILY) && (defined(OS_ANDROID) || defined(OS_LINUX))

// Returns the string found in /proc/cpuinfo under the key "model name" or
// "Processor". "model name" is used in Linux 3.8 and later (3.7 and later for
// arm64) and is shown once per CPU. "Processor" is used in earler versions and
// is shown only once at the top of /proc/cpuinfo regardless of the number CPUs.
std::string ParseCpuInfo() {
    const char kModelNamePrefix[] = "model name\t: ";
    const char kProcessorPrefix[] = "Processor\t: ";
    std::string contents;
    ReadFileToString(FilePath("/proc/cpuinfo"), &contents);
    DCHECK(!contents.empty());
    std::string cpu_brand;
    if (!contents.empty()) {
        std::istringstream iss(contents);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.compare(0, strlen(kModelNamePrefix), kModelNamePrefix) ==
                0) {
                cpu_brand.assign(line.substr(strlen(kModelNamePrefix)));
                break;
            }
            if (line.compare(0, strlen(kProcessorPrefix), kProcessorPrefix) ==
                0) {
                cpu_brand.assign(line.substr(strlen(kProcessorPrefix)));
                break;
            }
        }
    }
    return cpu_brand;
}

class LazyCpuInfoValue {
   public:
    LazyCpuInfoValue() : value_(ParseCpuInfo()) {}
    const std::string& value() { return value_; }

   private:
    const std::string value_;
    DISALLOW_COPY_AND_ASSIGN(LazyCpuInfoValue);
};

butil::LazyInstance<LazyCpuInfoValue> g_lazy_cpu_brand =
    LAZY_INSTANCE_INITIALIZER;

const std::string& CpuBrandInfo() { return g_lazy_cpu_brand.Get().value(); }

#endif  // defined(ARCH_CPU_ARM_FAMILY) && (defined(OS_ANDROID) ||
        // defined(OS_LINUX))

}  // anonymous namespace

void CPU::Initialize() {
#if defined(ARCH_CPU_X86_FAMILY)
    int cpu_info[4] = {-1};
    char cpu_string[48];

    // __cpuid with an InfoType argument of 0 returns the number of
    // valid Ids in CPUInfo[0] and the CPU identification string in
    // the other three array elements. The CPU identification string is
    // not in linear order. The code below arranges the information
    // in a human readable form. The human readable order is CPUInfo[1] |
    // CPUInfo[3] | CPUInfo[2]. CPUInfo[2] and CPUInfo[3] are swapped
    // before using memcpy to copy these three array elements to cpu_string.
    __cpuid(cpu_info, 0);
    int num_ids = cpu_info[0];
    std::swap(cpu_info[2], cpu_info[3]);
    memcpy(cpu_string, &cpu_info[1], 3 * sizeof(cpu_info[1]));
    cpu_vendor_.assign(cpu_string, 3 * sizeof(cpu_info[1]));

    // Interpret CPU feature information.
    if (num_ids > 0) {
        __cpuid(cpu_info, 1);
        signature_ = cpu_info[0];
        stepping_ = cpu_info[0] & 0xf;
        model_ = ((cpu_info[0] >> 4) & 0xf) + ((cpu_info[0] >> 12) & 0xf0);
        family_ = (cpu_info[0] >> 8) & 0xf;
        type_ = (cpu_info[0] >> 12) & 0x3;
        ext_model_ = (cpu_info[0] >> 16) & 0xf;
        ext_family_ = (cpu_info[0] >> 20) & 0xff;
        has_mmx_ = (cpu_info[3] & 0x00800000) != 0;
        has_sse_ = (cpu_info[3] & 0x02000000) != 0;
        has_sse2_ = (cpu_info[3] & 0x04000000) != 0;
        has_sse3_ = (cpu_info[2] & 0x00000001) != 0;
        has_ssse3_ = (cpu_info[2] & 0x00000200) != 0;
        has_sse41_ = (cpu_info[2] & 0x00080000) != 0;
        has_sse42_ = (cpu_info[2] & 0x00100000) != 0;
        has_avx_hardware_ = (cpu_info[2] & 0x10000000) != 0;
        // AVX instructions will generate an illegal instruction exception
        // unless
        //   a) they are supported by the CPU,
        //   b) XSAVE is supported by the CPU and
        //   c) XSAVE is enabled by the kernel.
        // See http://software.intel.com/en-us/blogs/2011/04/14/is-avx-enabled
        //
        // In addition, we have observed some crashes with the xgetbv
        // instruction even after following Intel's example code. (See
        // crbug.com/375968.) Because of that, we also test the XSAVE bit
        // because its description in the CPUID documentation suggests that it
        // signals xgetbv support.
        const bool has_xsave = (cpu_info[2] & 0x04000000) != 0 /* XSAVE */ &&
                               (cpu_info[2] & 0x08000000) != 0 /* OSXSAVE */;
        const uint64_t xcr0 = has_xsave ? _xgetbv(0) : 0;
        has_avx_ = has_avx_hardware_ && has_xsave &&
                   (xcr0 & 6) == 6 /* XSAVE enabled by kernel */;
        has_aesni_ = (cpu_info[2] & 0x02000000) != 0;

        int cpu_info7[4] = {0};
        if (num_ids >= 7) {
            CpuidEx(cpu_info7, 7, 0);
        }
        ext_features_ = internal::ComputeX86ExtendedFeatures(
            static_cast<uint32_t>(num_ids), static_cast<uint32_t>(cpu_info[2]),
            static_cast<uint32_t>(cpu_info7[1]),
            static_cast<uint32_t>(cpu_info7[2]), xcr0);
    }

    // Get the brand string of the cpu.
    __cpuid(cpu_info, static_cast<int>(0x80000000U));
    const uint32_t max_parameter = static_cast<uint32_t>(cpu_info[0]);
    const uint32_t kParameterEnd = 0x80000004U;

    if (internal::HasExtendedCpuLeaf(max_parameter, kParameterEnd)) {
        char* cpu_string_ptr = cpu_string;

        for (uint32_t parameter = 0x80000002U;
             parameter <= kParameterEnd &&
             cpu_string_ptr < &cpu_string[sizeof(cpu_string)];
             ++parameter) {
            __cpuid(cpu_info, static_cast<int>(parameter));
            memcpy(cpu_string_ptr, cpu_info, sizeof(cpu_info));
            cpu_string_ptr += sizeof(cpu_info);
        }
        cpu_brand_.assign(cpu_string,
                          static_cast<size_t>(cpu_string_ptr - cpu_string));
    }

    const uint32_t kParameterContainingNonStopTsc = 0x80000007U;
    if (internal::HasExtendedCpuLeaf(max_parameter,
                                     kParameterContainingNonStopTsc)) {
        __cpuid(cpu_info, static_cast<int>(kParameterContainingNonStopTsc));
        has_non_stop_time_stamp_counter_ =
            (static_cast<uint32_t>(cpu_info[3]) & (1U << 8)) != 0;
    }
#elif defined(ARCH_CPU_ARM_FAMILY) && (defined(OS_ANDROID) || defined(OS_LINUX))
    cpu_brand_.assign(CpuBrandInfo());
#endif
}

CPU::IntelMicroArchitecture CPU::GetIntelMicroArchitecture() const {
    if (has_fast_avx512()) return AVX512F;
    if (has_avx2()) return AVX2;
    if (has_avx()) return AVX;
    if (has_sse42()) return SSE42;
    if (has_sse41()) return SSE41;
    if (has_ssse3()) return SSSE3;
    if (has_sse3()) return SSE3;
    if (has_sse2()) return SSE2;
    if (has_sse()) return SSE;
    return PENTIUM;
}

}  // namespace butil
