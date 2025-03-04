// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_CLIENT_SHARED_MEMORY_LIMITS_H_
#define GPU_COMMAND_BUFFER_CLIENT_SHARED_MEMORY_LIMITS_H_

#include <stddef.h>

#include "base/sys_info.h"
#include "build/build_config.h"

namespace gpu {

struct SharedMemoryLimits {
  SharedMemoryLimits() {
// We can't call AmountOfPhysicalMemory under NACL, so leave the default.
#if !defined(OS_NACL)
    // Max mapped memory to use for a texture upload depends on device ram.
    // Do not use more than 5% of extra shared memory, and do not use any extra
    // for memory contrained devices (<=1GB).
    max_mapped_memory_for_texture_upload =
        base::SysInfo::AmountOfPhysicalMemory() > 1024 * 1024 * 1024
            ? base::saturated_cast<uint32_t>(
                  base::SysInfo::AmountOfPhysicalMemory() / 20)
            : 0;

    // On memory constrained devices, switch to lower limits.
    // TODO(ericrk): Temporarily disabling this path to check if it was
    // responsible for a large OOM rate improvement we saw. Will revert this
    // within 1 week. crbug.com/773067
    if (false && base::SysInfo::AmountOfPhysicalMemoryMB() <= 512) {
      command_buffer_size = 512 * 1024;
      start_transfer_buffer_size = 256 * 1024;
      min_transfer_buffer_size = 128 * 1024;
      mapped_memory_chunk_size = 256 * 1024;
    }
#endif
  }

  int32_t command_buffer_size = 1024 * 1024;
  uint32_t start_transfer_buffer_size = 1024 * 1024;
  uint32_t min_transfer_buffer_size = 256 * 1024;
  uint32_t max_transfer_buffer_size = 16 * 1024 * 1024;

  static constexpr uint32_t kNoLimit = 0;
  uint32_t mapped_memory_reclaim_limit = kNoLimit;
  uint32_t mapped_memory_chunk_size = 2 * 1024 * 1024;
  uint32_t max_mapped_memory_for_texture_upload = 0;

  // These are limits for contexts only used for creating textures, mailboxing
  // them and dealing with synchronization.
  static SharedMemoryLimits ForMailboxContext() {
    SharedMemoryLimits limits;
    limits.command_buffer_size = 64 * 1024;
    limits.start_transfer_buffer_size = 64 * 1024;
    limits.min_transfer_buffer_size = 64 * 1024;
    return limits;
  }
};

}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_CLIENT_SHARED_MEMORY_LIMITS_H_
