// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/atomicops.h"

#include <gtest/gtest.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <chrono>
#include <thread>
#include <type_traits>
#include <vector>

template <class AtomicType>
static void TestAtomicIncrement() {
    // For now, we just test single threaded execution

    // use a guard value to make sure the NoBarrier_AtomicIncrement doesn't go
    // outside the expected address bounds.  This is in particular to
    // test that some future change to the asm code doesn't cause the
    // 32-bit NoBarrier_AtomicIncrement doesn't do the wrong thing on 64-bit
    // machines.
    struct {
        AtomicType prev_word;
        AtomicType count;
        AtomicType next_word;
    } s;

    AtomicType prev_word_value, next_word_value;
    memset(&prev_word_value, 0xFF, sizeof(AtomicType));
    memset(&next_word_value, 0xEE, sizeof(AtomicType));

    s.prev_word = prev_word_value;
    s.count = 0;
    s.next_word = next_word_value;

    EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, 1), 1);
    EXPECT_EQ(s.count, 1);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);

    EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, 2), 3);
    EXPECT_EQ(s.count, 3);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);

    EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, 3), 6);
    EXPECT_EQ(s.count, 6);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);

    EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, -3), 3);
    EXPECT_EQ(s.count, 3);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);

    EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, -2), 1);
    EXPECT_EQ(s.count, 1);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);

    EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, -1), 0);
    EXPECT_EQ(s.count, 0);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);

    EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, -1), -1);
    EXPECT_EQ(s.count, -1);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);

    EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, -4), -5);
    EXPECT_EQ(s.count, -5);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);

    EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, 5), 0);
    EXPECT_EQ(s.count, 0);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);
}

#define NUM_BITS(T) (sizeof(T) * 8)

template <class AtomicType>
static void TestCompareAndSwap() {
    AtomicType value = 0;
    AtomicType prev = butil::subtle::NoBarrier_CompareAndSwap(&value, 0, 1);
    EXPECT_EQ(1, value);
    EXPECT_EQ(0, prev);

    // Use test value that has non-zero bits in both halves, more for testing
    // 64-bit implementation on 32-bit platforms.
    const AtomicType k_test_val =
        (static_cast<uint64_t>(1) << (NUM_BITS(AtomicType) - 2)) + 11;
    value = k_test_val;
    prev = butil::subtle::NoBarrier_CompareAndSwap(&value, 0, 5);
    EXPECT_EQ(k_test_val, value);
    EXPECT_EQ(k_test_val, prev);

    value = k_test_val;
    prev = butil::subtle::NoBarrier_CompareAndSwap(&value, k_test_val, 5);
    EXPECT_EQ(5, value);
    EXPECT_EQ(k_test_val, prev);
}

template <class AtomicType>
static void TestAtomicExchange() {
    AtomicType value = 0;
    AtomicType new_value = butil::subtle::NoBarrier_AtomicExchange(&value, 1);
    EXPECT_EQ(1, value);
    EXPECT_EQ(0, new_value);

    // Use test value that has non-zero bits in both halves, more for testing
    // 64-bit implementation on 32-bit platforms.
    const AtomicType k_test_val =
        (static_cast<uint64_t>(1) << (NUM_BITS(AtomicType) - 2)) + 11;
    value = k_test_val;
    new_value = butil::subtle::NoBarrier_AtomicExchange(&value, k_test_val);
    EXPECT_EQ(k_test_val, value);
    EXPECT_EQ(k_test_val, new_value);

    value = k_test_val;
    new_value = butil::subtle::NoBarrier_AtomicExchange(&value, 5);
    EXPECT_EQ(5, value);
    EXPECT_EQ(k_test_val, new_value);
}

template <class AtomicType>
static void TestAtomicIncrementBounds() {
    // Test at rollover boundary between int_max and int_min
    AtomicType test_val =
        (static_cast<uint64_t>(1) << (NUM_BITS(AtomicType) - 1));
    AtomicType value = -1 ^ test_val;
    AtomicType new_value = butil::subtle::NoBarrier_AtomicIncrement(&value, 1);
    EXPECT_EQ(test_val, value);
    EXPECT_EQ(value, new_value);

    butil::subtle::NoBarrier_AtomicIncrement(&value, -1);
    EXPECT_EQ(-1 ^ test_val, value);

    // Test at 32-bit boundary for 64-bit atomic type.
    test_val = static_cast<uint64_t>(1) << (NUM_BITS(AtomicType) / 2);
    value = test_val - 1;
    new_value = butil::subtle::NoBarrier_AtomicIncrement(&value, 1);
    EXPECT_EQ(test_val, value);
    EXPECT_EQ(value, new_value);

    butil::subtle::NoBarrier_AtomicIncrement(&value, -1);
    EXPECT_EQ(test_val - 1, value);
}

// Return an AtomicType with the value 0xa5a5a5..
template <class AtomicType>
static AtomicType TestFillValue() {
    AtomicType val = 0;
    memset(&val, 0xa5, sizeof(AtomicType));
    return val;
}

// This is a simple sanity check that values are correct. Not testing
// atomicity
template <class AtomicType>
static void TestStore() {
    const AtomicType kVal1 = TestFillValue<AtomicType>();
    const AtomicType kVal2 = static_cast<AtomicType>(-1);

    AtomicType value;

    butil::subtle::NoBarrier_Store(&value, kVal1);
    EXPECT_EQ(kVal1, value);
    butil::subtle::NoBarrier_Store(&value, kVal2);
    EXPECT_EQ(kVal2, value);

    butil::subtle::Acquire_Store(&value, kVal1);
    EXPECT_EQ(kVal1, value);
    butil::subtle::Acquire_Store(&value, kVal2);
    EXPECT_EQ(kVal2, value);

    butil::subtle::Release_Store(&value, kVal1);
    EXPECT_EQ(kVal1, value);
    butil::subtle::Release_Store(&value, kVal2);
    EXPECT_EQ(kVal2, value);
}

// This is a simple sanity check that values are correct. Not testing
// atomicity
template <class AtomicType>
static void TestLoad() {
    const AtomicType kVal1 = TestFillValue<AtomicType>();
    const AtomicType kVal2 = static_cast<AtomicType>(-1);

    AtomicType value;

    value = kVal1;
    EXPECT_EQ(kVal1, butil::subtle::NoBarrier_Load(&value));
    value = kVal2;
    EXPECT_EQ(kVal2, butil::subtle::NoBarrier_Load(&value));

    value = kVal1;
    EXPECT_EQ(kVal1, butil::subtle::Acquire_Load(&value));
    value = kVal2;
    EXPECT_EQ(kVal2, butil::subtle::Acquire_Load(&value));

    value = kVal1;
    EXPECT_EQ(kVal1, butil::subtle::Release_Load(&value));
    value = kVal2;
    EXPECT_EQ(kVal2, butil::subtle::Release_Load(&value));
}

template <class AtomicType>
static void TestBarrierAtomicIncrement() {
    struct {
        AtomicType prev_word;
        AtomicType count;
        AtomicType next_word;
    } s;

    AtomicType prev_word_value, next_word_value;
    memset(&prev_word_value, 0xFF, sizeof(AtomicType));
    memset(&next_word_value, 0xEE, sizeof(AtomicType));

    s.prev_word = prev_word_value;
    s.count = 0;
    s.next_word = next_word_value;

    EXPECT_EQ(butil::subtle::Barrier_AtomicIncrement(&s.count, 1), 1);
    EXPECT_EQ(s.count, 1);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);

    EXPECT_EQ(butil::subtle::Barrier_AtomicIncrement(&s.count, 2), 3);
    EXPECT_EQ(s.count, 3);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);

    EXPECT_EQ(butil::subtle::Barrier_AtomicIncrement(&s.count, 3), 6);
    EXPECT_EQ(s.count, 6);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);

    EXPECT_EQ(butil::subtle::Barrier_AtomicIncrement(&s.count, -3), 3);
    EXPECT_EQ(s.count, 3);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);

    EXPECT_EQ(butil::subtle::Barrier_AtomicIncrement(&s.count, -2), 1);
    EXPECT_EQ(s.count, 1);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);

    EXPECT_EQ(butil::subtle::Barrier_AtomicIncrement(&s.count, -1), 0);
    EXPECT_EQ(s.count, 0);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);

    EXPECT_EQ(butil::subtle::Barrier_AtomicIncrement(&s.count, -1), -1);
    EXPECT_EQ(s.count, -1);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);

    EXPECT_EQ(butil::subtle::Barrier_AtomicIncrement(&s.count, -4), -5);
    EXPECT_EQ(s.count, -5);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);

    EXPECT_EQ(butil::subtle::Barrier_AtomicIncrement(&s.count, 5), 0);
    EXPECT_EQ(s.count, 0);
    EXPECT_EQ(s.prev_word, prev_word_value);
    EXPECT_EQ(s.next_word, next_word_value);

    // Test at rollover boundary between int_max and int_min.
    AtomicType test_val =
        (static_cast<uint64_t>(1) << (NUM_BITS(AtomicType) - 1));
    AtomicType value = -1 ^ test_val;
    AtomicType new_value = butil::subtle::Barrier_AtomicIncrement(&value, 1);
    EXPECT_EQ(test_val, value);
    EXPECT_EQ(value, new_value);

    EXPECT_EQ(-1 ^ test_val,
              butil::subtle::Barrier_AtomicIncrement(&value, -1));
    EXPECT_EQ(-1 ^ test_val, value);

    // Test at 32-bit boundary for 64-bit atomic type.
    test_val = static_cast<uint64_t>(1) << (NUM_BITS(AtomicType) / 2);
    value = test_val - 1;
    new_value = butil::subtle::Barrier_AtomicIncrement(&value, 1);
    EXPECT_EQ(test_val, value);
    EXPECT_EQ(value, new_value);

    EXPECT_EQ(test_val - 1, butil::subtle::Barrier_AtomicIncrement(&value, -1));
    EXPECT_EQ(test_val - 1, value);
}

template <class AtomicType>
static void TestAcquireReleaseCompareAndSwap() {
    const AtomicType k_test_val =
        (static_cast<uint64_t>(1) << (NUM_BITS(AtomicType) - 2)) + 11;
    const AtomicType k_min_val = static_cast<AtomicType>(
        static_cast<uint64_t>(1) << (NUM_BITS(AtomicType) - 1));
    const AtomicType k_max_val = -1 ^ k_min_val;

    // Acquire_CompareAndSwap: success and failure paths across boundaries.
    AtomicType value = 0;
    AtomicType prev = butil::subtle::Acquire_CompareAndSwap(&value, 0, 1);
    EXPECT_EQ(1, value);
    EXPECT_EQ(0, prev);

    value = k_test_val;
    prev = butil::subtle::Acquire_CompareAndSwap(&value, 0, 5);
    EXPECT_EQ(k_test_val, value);
    EXPECT_EQ(k_test_val, prev);

    value = k_test_val;
    prev = butil::subtle::Acquire_CompareAndSwap(&value, k_test_val, k_min_val);
    EXPECT_EQ(k_min_val, value);
    EXPECT_EQ(k_test_val, prev);

    prev = butil::subtle::Acquire_CompareAndSwap(&value, k_max_val, -1);
    EXPECT_EQ(k_min_val, value);
    EXPECT_EQ(k_min_val, prev);

    prev = butil::subtle::Acquire_CompareAndSwap(&value, k_min_val, k_max_val);
    EXPECT_EQ(k_max_val, value);
    EXPECT_EQ(k_min_val, prev);

    // Release_CompareAndSwap: success and failure paths across boundaries.
    value = 0;
    prev = butil::subtle::Release_CompareAndSwap(&value, 0, 1);
    EXPECT_EQ(1, value);
    EXPECT_EQ(0, prev);

    value = k_test_val;
    prev = butil::subtle::Release_CompareAndSwap(&value, 0, 5);
    EXPECT_EQ(k_test_val, value);
    EXPECT_EQ(k_test_val, prev);

    value = k_test_val;
    prev = butil::subtle::Release_CompareAndSwap(&value, k_test_val, k_max_val);
    EXPECT_EQ(k_max_val, value);
    EXPECT_EQ(k_test_val, prev);

    prev = butil::subtle::Release_CompareAndSwap(&value, k_min_val, -1);
    EXPECT_EQ(k_max_val, value);
    EXPECT_EQ(k_max_val, prev);

    prev = butil::subtle::Release_CompareAndSwap(&value, k_max_val, -1);
    EXPECT_EQ(static_cast<AtomicType>(-1), value);
    EXPECT_EQ(k_max_val, prev);
}

#if defined(__x86_64__)
namespace legacy_x86_asm {

using butil::subtle::Atomic32;
using butil::subtle::Atomic64;

inline Atomic32 NoBarrier_CompareAndSwap(volatile Atomic32* ptr,
                                         Atomic32 old_value,
                                         Atomic32 new_value) {
    Atomic32 prev;
    __asm__ __volatile__("lock; cmpxchgl %1,%2"
                         : "=a"(prev)
                         : "q"(new_value), "m"(*ptr), "0"(old_value)
                         : "memory");
    return prev;
}

inline Atomic32 NoBarrier_AtomicExchange(volatile Atomic32* ptr,
                                         Atomic32 new_value) {
    __asm__ __volatile__("xchgl %1,%0"
                         : "=r"(new_value)
                         : "m"(*ptr), "0"(new_value)
                         : "memory");
    return new_value;
}

inline Atomic32 NoBarrier_AtomicIncrement(volatile Atomic32* ptr,
                                          Atomic32 increment) {
    Atomic32 temp = increment;
    __asm__ __volatile__("lock; xaddl %0,%1"
                         : "+r"(temp), "+m"(*ptr)
                         :
                         : "memory");
    return static_cast<Atomic32>(static_cast<uint32_t>(temp) +
                                 static_cast<uint32_t>(increment));
}

inline Atomic32 Barrier_AtomicIncrement(volatile Atomic32* ptr,
                                        Atomic32 increment) {
    Atomic32 temp = increment;
    __asm__ __volatile__("lock; xaddl %0,%1"
                         : "+r"(temp), "+m"(*ptr)
                         :
                         : "memory");
    return static_cast<Atomic32>(static_cast<uint32_t>(temp) +
                                 static_cast<uint32_t>(increment));
}

inline Atomic32 Acquire_CompareAndSwap(volatile Atomic32* ptr,
                                       Atomic32 old_value, Atomic32 new_value) {
    return NoBarrier_CompareAndSwap(ptr, old_value, new_value);
}

inline Atomic32 Release_CompareAndSwap(volatile Atomic32* ptr,
                                       Atomic32 old_value, Atomic32 new_value) {
    return NoBarrier_CompareAndSwap(ptr, old_value, new_value);
}

inline Atomic64 NoBarrier_CompareAndSwap(volatile Atomic64* ptr,
                                         Atomic64 old_value,
                                         Atomic64 new_value) {
    Atomic64 prev;
    __asm__ __volatile__("lock; cmpxchgq %1,%2"
                         : "=a"(prev)
                         : "q"(new_value), "m"(*ptr), "0"(old_value)
                         : "memory");
    return prev;
}

inline Atomic64 NoBarrier_AtomicExchange(volatile Atomic64* ptr,
                                         Atomic64 new_value) {
    __asm__ __volatile__("xchgq %1,%0"
                         : "=r"(new_value)
                         : "m"(*ptr), "0"(new_value)
                         : "memory");
    return new_value;
}

inline Atomic64 NoBarrier_AtomicIncrement(volatile Atomic64* ptr,
                                          Atomic64 increment) {
    Atomic64 temp = increment;
    __asm__ __volatile__("lock; xaddq %0,%1"
                         : "+r"(temp), "+m"(*ptr)
                         :
                         : "memory");
    return static_cast<Atomic64>(static_cast<uint64_t>(temp) +
                                 static_cast<uint64_t>(increment));
}

inline Atomic64 Barrier_AtomicIncrement(volatile Atomic64* ptr,
                                        Atomic64 increment) {
    Atomic64 temp = increment;
    __asm__ __volatile__("lock; xaddq %0,%1"
                         : "+r"(temp), "+m"(*ptr)
                         :
                         : "memory");
    return static_cast<Atomic64>(static_cast<uint64_t>(temp) +
                                 static_cast<uint64_t>(increment));
}

inline Atomic64 Acquire_CompareAndSwap(volatile Atomic64* ptr,
                                       Atomic64 old_value, Atomic64 new_value) {
    return NoBarrier_CompareAndSwap(ptr, old_value, new_value);
}

inline Atomic64 Release_CompareAndSwap(volatile Atomic64* ptr,
                                       Atomic64 old_value, Atomic64 new_value) {
    return NoBarrier_CompareAndSwap(ptr, old_value, new_value);
}

#if defined(BUTIL_ATOMICOPS_INTERNALS_X86_GCC_H_)
inline Atomic64 LegacyK8Branch_Barrier_AtomicIncrement(volatile Atomic64* ptr,
                                                       Atomic64 increment) {
    Atomic64 temp = increment;
    __asm__ __volatile__("lock; xaddq %0,%1"
                         : "+r"(temp), "+m"(*ptr)
                         :
                         : "memory");
    if (AtomicOps_Internalx86CPUFeatures.has_amd_lock_mb_bug) {
        __asm__ __volatile__("lfence" : : : "memory");
    }
    return static_cast<Atomic64>(static_cast<uint64_t>(temp) +
                                 static_cast<uint64_t>(increment));
}

inline Atomic64 LegacyK8Branch_Acquire_CompareAndSwap(volatile Atomic64* ptr,
                                                      Atomic64 old_value,
                                                      Atomic64 new_value) {
    Atomic64 x = NoBarrier_CompareAndSwap(ptr, old_value, new_value);
    if (AtomicOps_Internalx86CPUFeatures.has_amd_lock_mb_bug) {
        __asm__ __volatile__("lfence" : : : "memory");
    }
    return x;
}
#endif

}  // namespace legacy_x86_asm

extern "C" {
__attribute__((noinline)) butil::subtle::Atomic32
InspectX86BarrierAtomicIncrement32(volatile butil::subtle::Atomic32* ptr,
                                   butil::subtle::Atomic32 inc) {
    return butil::subtle::Barrier_AtomicIncrement(ptr, inc);
}

__attribute__((noinline)) butil::subtle::Atomic64
InspectX86BarrierAtomicIncrement64(volatile butil::subtle::Atomic64* ptr,
                                   butil::subtle::Atomic64 inc) {
    return butil::subtle::Barrier_AtomicIncrement(ptr, inc);
}

__attribute__((noinline)) butil::subtle::Atomic32
InspectX86AcquireCompareAndSwap32(volatile butil::subtle::Atomic32* ptr,
                                  butil::subtle::Atomic32 old_val,
                                  butil::subtle::Atomic32 new_val) {
    return butil::subtle::Acquire_CompareAndSwap(ptr, old_val, new_val);
}

__attribute__((noinline)) butil::subtle::Atomic64
InspectX86AcquireCompareAndSwap64(volatile butil::subtle::Atomic64* ptr,
                                  butil::subtle::Atomic64 old_val,
                                  butil::subtle::Atomic64 new_val) {
    return butil::subtle::Acquire_CompareAndSwap(ptr, old_val, new_val);
}
}  // extern "C"

template <class AtomicType>
static void RunDifferentialAgainstLegacyAsm(const AtomicType* inputs,
                                            size_t num_inputs) {
    volatile AtomicType actual = 0;
    volatile AtomicType expected = 0;

    for (size_t i = 0; i < num_inputs; ++i) {
        using UnsignedType = typename std::make_unsigned<AtomicType>::type;
        const AtomicType v = inputs[i];
        const AtomicType neg_v =
            static_cast<AtomicType>(0U - static_cast<UnsignedType>(v));
        const AtomicType next = inputs[(i + 1) % num_inputs];

        // NoBarrier_AtomicIncrement
        EXPECT_EQ(legacy_x86_asm::NoBarrier_AtomicIncrement(&expected, v),
                  butil::subtle::NoBarrier_AtomicIncrement(&actual, v));
        EXPECT_EQ(expected, actual);

        // Barrier_AtomicIncrement
        EXPECT_EQ(legacy_x86_asm::Barrier_AtomicIncrement(&expected, neg_v),
                  butil::subtle::Barrier_AtomicIncrement(&actual, neg_v));
        EXPECT_EQ(expected, actual);

        // NoBarrier_AtomicExchange
        EXPECT_EQ(legacy_x86_asm::NoBarrier_AtomicExchange(&expected, v),
                  butil::subtle::NoBarrier_AtomicExchange(&actual, v));
        EXPECT_EQ(expected, actual);

        // NoBarrier_CompareAndSwap (hit and miss)
        EXPECT_EQ(
            legacy_x86_asm::NoBarrier_CompareAndSwap(&expected, v ^ 1, next),
            butil::subtle::NoBarrier_CompareAndSwap(&actual, v ^ 1, next));
        EXPECT_EQ(expected, actual);
        EXPECT_EQ(legacy_x86_asm::NoBarrier_CompareAndSwap(&expected, v, next),
                  butil::subtle::NoBarrier_CompareAndSwap(&actual, v, next));
        EXPECT_EQ(expected, actual);

        // Acquire_CompareAndSwap (hit and miss)
        EXPECT_EQ(
            legacy_x86_asm::Acquire_CompareAndSwap(&expected, next ^ 1, v),
            butil::subtle::Acquire_CompareAndSwap(&actual, next ^ 1, v));
        EXPECT_EQ(expected, actual);
        EXPECT_EQ(legacy_x86_asm::Acquire_CompareAndSwap(&expected, next, v),
                  butil::subtle::Acquire_CompareAndSwap(&actual, next, v));
        EXPECT_EQ(expected, actual);

        // Release_CompareAndSwap (hit and miss)
        EXPECT_EQ(
            legacy_x86_asm::Release_CompareAndSwap(&expected, v ^ 1, next),
            butil::subtle::Release_CompareAndSwap(&actual, v ^ 1, next));
        EXPECT_EQ(expected, actual);
        EXPECT_EQ(legacy_x86_asm::Release_CompareAndSwap(&expected, v, next),
                  butil::subtle::Release_CompareAndSwap(&actual, v, next));
        EXPECT_EQ(expected, actual);
    }
}

TEST(AtomicOpsTest, DifferentialAgainstLegacyInlineAsm) {
    const butil::subtle::Atomic32 kInputs32[] = {
        0,
        1,
        -1,
        2,
        -2,
        127,
        -128,
        32767,
        -32768,
        0x12345678,
        -0x12345678,
        0x7ffffffe,
        0x7fffffff,
        -0x7fffffff,
        static_cast<butil::subtle::Atomic32>(0x80000000U)};
    RunDifferentialAgainstLegacyAsm(kInputs32,
                                    sizeof(kInputs32) / sizeof(kInputs32[0]));

    const butil::subtle::Atomic64 kInputs64[] = {
        0LL,
        1LL,
        -1LL,
        0x7fffffffLL,
        -0x80000000LL,
        0x100000000LL,
        -0x100000000LL,
        0x123456789abcdef0LL,
        -0x123456789abcdef0LL,
        0x7ffffffffffffffeLL,
        0x7fffffffffffffffLL,
        static_cast<butil::subtle::Atomic64>(0x8000000000000000ULL)};
    RunDifferentialAgainstLegacyAsm(kInputs64,
                                    sizeof(kInputs64) / sizeof(kInputs64[0]));
}

TEST(AtomicOpsTest, LegacyExportedSymbolAndNoLfenceBranch) {
#if defined(BUTIL_ATOMICOPS_INTERNALS_X86_GCC_H_)
    const volatile AtomicOps_x86CPUFeatureStruct* features_ptr =
        &AtomicOps_Internalx86CPUFeatures;
    ASSERT_NE(nullptr, features_ptr);
    EXPECT_FALSE(features_ptr->has_amd_lock_mb_bug);
#endif

    volatile butil::subtle::Atomic32 v32 = 40;
    EXPECT_EQ(42, InspectX86BarrierAtomicIncrement32(&v32, 2));
    EXPECT_EQ(42, InspectX86AcquireCompareAndSwap32(&v32, 42, 100));
    EXPECT_EQ(100, v32);

    volatile butil::subtle::Atomic64 v64 = 400;
    EXPECT_EQ(420, InspectX86BarrierAtomicIncrement64(&v64, 20));
    EXPECT_EQ(420, InspectX86AcquireCompareAndSwap64(&v64, 420, 1000));
    EXPECT_EQ(1000, v64);
}
#endif  // defined(__x86_64__)

TEST(AtomicOpsTest, MultiThreadedContentionAndOrdering) {
    constexpr int kNumThreads = 4;
    constexpr int kIterations = 50000;

    volatile butil::subtle::Atomic32 barrier_inc32 = 0;
    volatile butil::subtle::Atomic32 acq_cas32 = 0;
    volatile butil::subtle::AtomicWord barrier_inc_word = 0;
    volatile butil::subtle::AtomicWord acq_cas_word = 0;

    // Producer-consumer handshakes for Release_Store / Acquire_Load ordering.
    struct Channel {
        std::vector<butil::subtle::AtomicWord> non_atomic_payloads;
        volatile butil::subtle::Atomic32 published_seq;
    };
    Channel channels[kNumThreads];
    for (int t = 0; t < kNumThreads; ++t) {
        channels[t].non_atomic_payloads.assign(kIterations + 1, 0);
        butil::subtle::NoBarrier_Store(&channels[t].published_seq, 0);
    }

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);
    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([&, t]() {
            const int peer = (t + 1) % kNumThreads;
            butil::subtle::Atomic32 last_seen_peer_seq = 0;
            for (int i = 1; i <= kIterations; ++i) {
                butil::subtle::Barrier_AtomicIncrement(&barrier_inc32, 1);
                butil::subtle::Barrier_AtomicIncrement(&barrier_inc_word, 1);

                // Contended 32-bit Acquire_CompareAndSwap loop.
                butil::subtle::Atomic32 old32 =
                    butil::subtle::NoBarrier_Load(&acq_cas32);
                while (true) {
                    butil::subtle::Atomic32 prev32 =
                        butil::subtle::Acquire_CompareAndSwap(&acq_cas32, old32,
                                                              old32 + 1);
                    if (prev32 == old32) {
                        break;
                    }
                    old32 = prev32;
                }

                // Contended word-sized Release_CompareAndSwap loop.
                butil::subtle::AtomicWord oldw =
                    butil::subtle::NoBarrier_Load(&acq_cas_word);
                while (true) {
                    butil::subtle::AtomicWord prevw =
                        butil::subtle::Release_CompareAndSwap(&acq_cas_word,
                                                              oldw, oldw + 1);
                    if (prevw == oldw) {
                        break;
                    }
                    oldw = prevw;
                }

                // Publish immutable per-sequence non-atomic payload slot via
                // Release_Store.
                channels[t].non_atomic_payloads[i] =
                    static_cast<butil::subtle::AtomicWord>(i * 17 + t);
                butil::subtle::Release_Store(&channels[t].published_seq, i);

                // Observe peer's published sequence via Acquire_Load and verify
                // the non-atomic payload slot written prior to Release_Store is
                // visible without racing with later iterations.
                butil::subtle::Atomic32 peer_seq =
                    butil::subtle::Acquire_Load(&channels[peer].published_seq);
                if (peer_seq > last_seen_peer_seq) {
                    butil::subtle::AtomicWord observed_payload =
                        channels[peer].non_atomic_payloads[peer_seq];
                    EXPECT_EQ(static_cast<butil::subtle::AtomicWord>(
                                  peer_seq * 17 + peer),
                              observed_payload);
                    last_seen_peer_seq = peer_seq;
                }
            }
        });
    }

    for (size_t t = 0; t < threads.size(); ++t) {
        threads[t].join();
    }

    const butil::subtle::Atomic32 kExpected32 = kNumThreads * kIterations;
    const butil::subtle::AtomicWord kExpectedWord =
        static_cast<butil::subtle::AtomicWord>(kNumThreads) * kIterations;
    EXPECT_EQ(kExpected32, butil::subtle::Acquire_Load(&barrier_inc32));
    EXPECT_EQ(kExpected32, butil::subtle::Acquire_Load(&acq_cas32));
    EXPECT_EQ(kExpectedWord, butil::subtle::Acquire_Load(&barrier_inc_word));
    EXPECT_EQ(kExpectedWord, butil::subtle::Acquire_Load(&acq_cas_word));
    for (int t = 0; t < kNumThreads; ++t) {
        EXPECT_EQ(kIterations,
                  butil::subtle::Acquire_Load(&channels[t].published_seq));
        EXPECT_EQ(static_cast<butil::subtle::AtomicWord>(kIterations * 17 + t),
                  channels[t].non_atomic_payloads[kIterations]);
    }

    // Single-threaded hot-loop throughput measurement (ns/op).
    constexpr int kBenchIters = 2000000;
#if defined(__x86_64__) && defined(BUTIL_ATOMICOPS_INTERNALS_X86_GCC_H_)
    volatile butil::subtle::Atomic64 legacy_bench_val = 0;
    const auto lt0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kBenchIters; ++i) {
        legacy_x86_asm::LegacyK8Branch_Barrier_AtomicIncrement(
            &legacy_bench_val, 1);
        legacy_x86_asm::LegacyK8Branch_Acquire_CompareAndSwap(
            &legacy_bench_val, 2 * i + 1, 2 * i + 2);
    }
    const auto lt1 = std::chrono::steady_clock::now();
    const double legacy_elapsed_ns =
        std::chrono::duration<double, std::nano>(lt1 - lt0).count();
    const double legacy_ns_per_op = legacy_elapsed_ns / (2.0 * kBenchIters);
    EXPECT_EQ(static_cast<butil::subtle::Atomic64>(2 * kBenchIters),
              legacy_bench_val);
#endif
    volatile butil::subtle::AtomicWord bench_val = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kBenchIters; ++i) {
        butil::subtle::Barrier_AtomicIncrement(&bench_val, 1);
        butil::subtle::Acquire_CompareAndSwap(&bench_val, 2 * i + 1, 2 * i + 2);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed_ns =
        std::chrono::duration<double, std::nano>(t1 - t0).count();
    const double ns_per_op = elapsed_ns / (2.0 * kBenchIters);
#if defined(__x86_64__) && defined(BUTIL_ATOMICOPS_INTERNALS_X86_GCC_H_)
    printf(
        "[AtomicOps Throughput] legacy_x86_asm (K8 branch): %.2f ns/op | "
        "__atomic builtins: %.2f ns/op (final=%ld)\n",
        legacy_ns_per_op, ns_per_op,
        static_cast<long>(butil::subtle::NoBarrier_Load(&bench_val)));
#else
    printf(
        "[AtomicOps Throughput] Barrier_AtomicIncrement + "
        "Acquire_CompareAndSwap: %.2f ns/op (final=%ld)\n",
        ns_per_op,
        static_cast<long>(butil::subtle::NoBarrier_Load(&bench_val)));
#endif
    EXPECT_EQ(static_cast<butil::subtle::AtomicWord>(2 * kBenchIters),
              butil::subtle::NoBarrier_Load(&bench_val));
}

TEST(AtomicOpsTest, Inc) {
    TestAtomicIncrement<butil::subtle::Atomic32>();
    TestAtomicIncrement<butil::subtle::AtomicWord>();
    TestBarrierAtomicIncrement<butil::subtle::Atomic32>();
    TestBarrierAtomicIncrement<butil::subtle::AtomicWord>();
}

TEST(AtomicOpsTest, CompareAndSwap) {
    TestCompareAndSwap<butil::subtle::Atomic32>();
    TestCompareAndSwap<butil::subtle::AtomicWord>();
    TestAcquireReleaseCompareAndSwap<butil::subtle::Atomic32>();
    TestAcquireReleaseCompareAndSwap<butil::subtle::AtomicWord>();
}

TEST(AtomicOpsTest, BarrierAtomicIncrement) {
    TestBarrierAtomicIncrement<butil::subtle::Atomic32>();
    TestBarrierAtomicIncrement<butil::subtle::AtomicWord>();
}

TEST(AtomicOpsTest, AcquireReleaseCompareAndSwap) {
    TestAcquireReleaseCompareAndSwap<butil::subtle::Atomic32>();
    TestAcquireReleaseCompareAndSwap<butil::subtle::AtomicWord>();
}

TEST(AtomicOpsTest, Exchange) {
    TestAtomicExchange<butil::subtle::Atomic32>();
    TestAtomicExchange<butil::subtle::AtomicWord>();
}

TEST(AtomicOpsTest, IncrementBounds) {
    TestAtomicIncrementBounds<butil::subtle::Atomic32>();
    TestAtomicIncrementBounds<butil::subtle::AtomicWord>();
}

TEST(AtomicOpsTest, Store) {
    TestStore<butil::subtle::Atomic32>();
    TestStore<butil::subtle::AtomicWord>();
}

TEST(AtomicOpsTest, Load) {
    TestLoad<butil::subtle::Atomic32>();
    TestLoad<butil::subtle::AtomicWord>();
}
