// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is an internal atomic implementation, use butil/atomicops.h
// instead.

#ifndef BUTIL_ATOMICOPS_INTERNALS_X86_GCC_H_
#define BUTIL_ATOMICOPS_INTERNALS_X86_GCC_H_

#include "butil/base_export.h"

// This struct is not part of the public API of this module; clients may not
// use it.  (However, it's exported via BUTIL_EXPORT because clients implicitly
// do use it at link time by inlining these functions.)
// Preserved for shared-library ABI compatibility with callers compiled against
// older headers.
struct AtomicOps_x86CPUFeatureStruct {
    bool has_amd_lock_mb_bug;
};
BUTIL_EXPORT extern struct AtomicOps_x86CPUFeatureStruct
    AtomicOps_Internalx86CPUFeatures;

namespace butil {
namespace subtle {

// 32-bit low-level operations on any platform.

inline Atomic32 NoBarrier_CompareAndSwap(volatile Atomic32* ptr,
                                         Atomic32 old_value,
                                         Atomic32 new_value) {
    __atomic_compare_exchange_n(ptr, &old_value, new_value, false,
                                __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    return old_value;
}

inline Atomic32 NoBarrier_AtomicExchange(volatile Atomic32* ptr,
                                         Atomic32 new_value) {
    return __atomic_exchange_n(ptr, new_value, __ATOMIC_RELAXED);
}

inline Atomic32 NoBarrier_AtomicIncrement(volatile Atomic32* ptr,
                                          Atomic32 increment) {
    return __atomic_add_fetch(ptr, increment, __ATOMIC_RELAXED);
}

inline Atomic32 Barrier_AtomicIncrement(volatile Atomic32* ptr,
                                        Atomic32 increment) {
    return __atomic_add_fetch(ptr, increment, __ATOMIC_SEQ_CST);
}

inline Atomic32 Acquire_CompareAndSwap(volatile Atomic32* ptr,
                                       Atomic32 old_value, Atomic32 new_value) {
    __atomic_compare_exchange_n(ptr, &old_value, new_value, false,
                                __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
    return old_value;
}

inline Atomic32 Release_CompareAndSwap(volatile Atomic32* ptr,
                                       Atomic32 old_value, Atomic32 new_value) {
    __atomic_compare_exchange_n(ptr, &old_value, new_value, false,
                                __ATOMIC_RELEASE, __ATOMIC_RELAXED);
    return old_value;
}

inline void NoBarrier_Store(volatile Atomic32* ptr, Atomic32 value) {
    __atomic_store_n(ptr, value, __ATOMIC_RELAXED);
}

inline void MemoryBarrier() { __atomic_thread_fence(__ATOMIC_SEQ_CST); }

inline void Acquire_Store(volatile Atomic32* ptr, Atomic32 value) {
    __atomic_store_n(ptr, value, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

inline void Release_Store(volatile Atomic32* ptr, Atomic32 value) {
    __atomic_store_n(ptr, value, __ATOMIC_RELEASE);
}

inline Atomic32 NoBarrier_Load(volatile const Atomic32* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

inline Atomic32 Acquire_Load(volatile const Atomic32* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

inline Atomic32 Release_Load(volatile const Atomic32* ptr) {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

#if defined(__x86_64__)

// 64-bit low-level operations on 64-bit platform.

inline Atomic64 NoBarrier_CompareAndSwap(volatile Atomic64* ptr,
                                         Atomic64 old_value,
                                         Atomic64 new_value) {
    __atomic_compare_exchange_n(ptr, &old_value, new_value, false,
                                __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    return old_value;
}

inline Atomic64 NoBarrier_AtomicExchange(volatile Atomic64* ptr,
                                         Atomic64 new_value) {
    return __atomic_exchange_n(ptr, new_value, __ATOMIC_RELAXED);
}

inline Atomic64 NoBarrier_AtomicIncrement(volatile Atomic64* ptr,
                                          Atomic64 increment) {
    return __atomic_add_fetch(ptr, increment, __ATOMIC_RELAXED);
}

inline Atomic64 Barrier_AtomicIncrement(volatile Atomic64* ptr,
                                        Atomic64 increment) {
    return __atomic_add_fetch(ptr, increment, __ATOMIC_SEQ_CST);
}

inline void NoBarrier_Store(volatile Atomic64* ptr, Atomic64 value) {
    __atomic_store_n(ptr, value, __ATOMIC_RELAXED);
}

inline void Acquire_Store(volatile Atomic64* ptr, Atomic64 value) {
    __atomic_store_n(ptr, value, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

inline void Release_Store(volatile Atomic64* ptr, Atomic64 value) {
    __atomic_store_n(ptr, value, __ATOMIC_RELEASE);
}

inline Atomic64 NoBarrier_Load(volatile const Atomic64* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

inline Atomic64 Acquire_Load(volatile const Atomic64* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

inline Atomic64 Release_Load(volatile const Atomic64* ptr) {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

inline Atomic64 Acquire_CompareAndSwap(volatile Atomic64* ptr,
                                       Atomic64 old_value, Atomic64 new_value) {
    __atomic_compare_exchange_n(ptr, &old_value, new_value, false,
                                __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
    return old_value;
}

inline Atomic64 Release_CompareAndSwap(volatile Atomic64* ptr,
                                       Atomic64 old_value, Atomic64 new_value) {
    __atomic_compare_exchange_n(ptr, &old_value, new_value, false,
                                __ATOMIC_RELEASE, __ATOMIC_RELAXED);
    return old_value;
}

#endif  // defined(__x86_64__)

}  // namespace subtle
}  // namespace butil

#endif  // BUTIL_ATOMICOPS_INTERNALS_X86_GCC_H_
