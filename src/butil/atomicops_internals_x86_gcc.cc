// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/atomicops.h"

// This file only makes sense with atomicops_internals_x86_gcc.h -- it
// depends on structs that are defined in that file.  If atomicops.h
// doesn't sub-include that file, then we aren't needed, and shouldn't
// try to do anything.
#ifdef BUTIL_ATOMICOPS_INTERNALS_X86_GCC_H_

// Preserved at link time for shared-library ABI compatibility with callers
// compiled against older headers that referenced this symbol.
struct AtomicOps_x86CPUFeatureStruct AtomicOps_Internalx86CPUFeatures = {
    false,
};

#endif  // ifdef BUTIL_ATOMICOPS_INTERNALS_X86_GCC_H_
