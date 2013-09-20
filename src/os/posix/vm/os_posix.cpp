/*
* Copyright (c) 1999, 2012, Oracle and/or its affiliates. All rights reserved.
* DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
*
* This code is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License version 2 only, as
* published by the Free Software Foundation.
*
* This code is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
* version 2 for more details (a copy is included in the LICENSE file that
* accompanied this code).
*
* You should have received a copy of the GNU General Public License version
* 2 along with this work; if not, write to the Free Software Foundation,
* Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
*
* Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
* or visit www.oracle.com if you need additional information or have any
* questions.
*
*/

#include "prims/jvm.h"
#include "runtime/frame.inline.hpp"
#include "runtime/os.hpp"
#include "utilities/vmError.hpp"

#include <unistd.h>
#include <sys/resource.h>
#include <sys/utsname.h>


// Check core dump limit and report possible place where core can be found
void os::check_or_create_dump(void* exceptionRecord, void* contextRecord, char* buffer, size_t bufferSize) {
  int n;
  struct rlimit rlim;
  bool success;

  n = get_core_path(buffer, bufferSize);

  if (getrlimit(RLIMIT_CORE, &rlim) != 0) {
    jio_snprintf(buffer + n, bufferSize - n, "/core or core.%d (may not exist)", current_process_id());
    success = true;
  } else {
    switch(rlim.rlim_cur) {
      case RLIM_INFINITY:
        jio_snprintf(buffer + n, bufferSize - n, "/core or core.%d", current_process_id());
        success = true;
        break;
      case 0:
        jio_snprintf(buffer, bufferSize, "Core dumps have been disabled. To enable core dumping, try \"ulimit -c unlimited\" before starting Java again");
        success = false;
        break;
      default:
        jio_snprintf(buffer + n, bufferSize - n, "/core or core.%d (max size %lu kB). To ensure a full core dump, try \"ulimit -c unlimited\" before starting Java again", current_process_id(), (unsigned long)(rlim.rlim_cur >> 10));
        success = true;
        break;
    }
  }
  VMError::report_coredump_status(buffer, success);
}

address os::get_caller_pc(int n) {
#ifdef _NMT_NOINLINE_
  n ++;
#endif
  frame fr = os::current_frame();
  while (n > 0 && fr.pc() &&
    !os::is_first_C_frame(&fr) && fr.sender_pc()) {
    fr = os::get_sender_for_C_frame(&fr);
    n --;
  }
  if (n == 0) {
    return fr.pc();
  } else {
    return NULL;
  }
}

int os::get_last_error() {
  return errno;
}

bool os::is_debugger_attached() {
  // not implemented
  return false;
}

void os::wait_for_keypress_at_exit(void) {
  // don't do anything on posix platforms
  return;
}

// Multiple threads can race in this code, and can remap over each other with MAP_FIXED,
// so on posix, unmap the section at the start and at the end of the chunk that we mapped
// rather than unmapping and remapping the whole chunk to get requested alignment.
char* os::reserve_memory_aligned(size_t size, size_t alignment) {
  assert((alignment & (os::vm_allocation_granularity() - 1)) == 0,
      "Alignment must be a multiple of allocation granularity (page size)");
  assert((size & (alignment -1)) == 0, "size must be 'alignment' aligned");

  size_t extra_size = size + alignment;
  assert(extra_size >= size, "overflow, size is too large to allow alignment");

  char* extra_base = os::reserve_memory(extra_size, NULL, alignment);

  if (extra_base == NULL) {
    return NULL;
  }

  // Do manual alignment
  char* aligned_base = (char*) align_size_up((uintptr_t) extra_base, alignment);

  // [  |                                       |  ]
  // ^ extra_base
  //    ^ extra_base + begin_offset == aligned_base
  //     extra_base + begin_offset + size       ^
  //                       extra_base + extra_size ^
  // |<>| == begin_offset
  //                              end_offset == |<>|
  size_t begin_offset = aligned_base - extra_base;
  size_t end_offset = (extra_base + extra_size) - (aligned_base + size);

  if (begin_offset > 0) {
      os::release_memory(extra_base, begin_offset);
  }

  if (end_offset > 0) {
      os::release_memory(extra_base + begin_offset + size, end_offset);
  }

  return aligned_base;
}

void os::Posix::print_load_average(outputStream* st) {
  st->print("load average:");
  double loadavg[3];
  os::loadavg(loadavg, 3);
  st->print("%0.02f %0.02f %0.02f", loadavg[0], loadavg[1], loadavg[2]);
  st->cr();
}

void os::Posix::print_rlimit_info(outputStream* st) {
  st->print("rlimit:");
  struct rlimit rlim;

  st->print(" STACK ");
  getrlimit(RLIMIT_STACK, &rlim);
  if (rlim.rlim_cur == RLIM_INFINITY) st->print("infinity");
  else st->print("%uk", rlim.rlim_cur >> 10);

  st->print(", CORE ");
  getrlimit(RLIMIT_CORE, &rlim);
  if (rlim.rlim_cur == RLIM_INFINITY) st->print("infinity");
  else st->print("%uk", rlim.rlim_cur >> 10);

  //Isn't there on solaris
#ifndef TARGET_OS_FAMILY_solaris
  st->print(", NPROC ");
  getrlimit(RLIMIT_NPROC, &rlim);
  if (rlim.rlim_cur == RLIM_INFINITY) st->print("infinity");
  else st->print("%d", rlim.rlim_cur);
#endif

  st->print(", NOFILE ");
  getrlimit(RLIMIT_NOFILE, &rlim);
  if (rlim.rlim_cur == RLIM_INFINITY) st->print("infinity");
  else st->print("%d", rlim.rlim_cur);

  st->print(", AS ");
  getrlimit(RLIMIT_AS, &rlim);
  if (rlim.rlim_cur == RLIM_INFINITY) st->print("infinity");
  else st->print("%uk", rlim.rlim_cur >> 10);
  st->cr();
}

void os::Posix::print_uname_info(outputStream* st) {
  // kernel
  st->print("uname:");
  struct utsname name;
  uname(&name);
  st->print(name.sysname); st->print(" ");
  st->print(name.release); st->print(" ");
  st->print(name.version); st->print(" ");
  st->print(name.machine);
  st->cr();
}

bool os::has_allocatable_memory_limit(julong* limit) {
  struct rlimit rlim;
  int getrlimit_res = getrlimit(RLIMIT_AS, &rlim);
  // if there was an error when calling getrlimit, assume that there is no limitation
  // on virtual memory.
  bool result;
  if ((getrlimit_res != 0) || (rlim.rlim_cur == RLIM_INFINITY)) {
    result = false;
  } else {
    *limit = (julong)rlim.rlim_cur;
    result = true;
  }
#ifdef _LP64
  return result;
#else
  // arbitrary virtual space limit for 32 bit Unices found by testing. If
  // getrlimit above returned a limit, bound it with this limit. Otherwise
  // directly use it.
  const julong max_virtual_limit = (julong)3800*M;
  if (result) {
    *limit = MIN2(*limit, max_virtual_limit);
  } else {
    *limit = max_virtual_limit;
  }

  // bound by actually allocatable memory. The algorithm uses two bounds, an
  // upper and a lower limit. The upper limit is the current highest amount of
  // memory that could not be allocated, the lower limit is the current highest
  // amount of memory that could be allocated.
  // The algorithm iteratively refines the result by halving the difference
  // between these limits, updating either the upper limit (if that value could
  // not be allocated) or the lower limit (if the that value could be allocated)
  // until the difference between these limits is "small".

  // the minimum amount of memory we care about allocating.
  const julong min_allocation_size = M;

  julong upper_limit = *limit;

  // first check a few trivial cases
  if (is_allocatable(upper_limit) || (upper_limit <= min_allocation_size)) {
    *limit = upper_limit;
  } else if (!is_allocatable(min_allocation_size)) {
    // we found that not even min_allocation_size is allocatable. Return it
    // anyway. There is no point to search for a better value any more.
    *limit = min_allocation_size;
  } else {
    // perform the binary search.
    julong lower_limit = min_allocation_size;
    while ((upper_limit - lower_limit) > min_allocation_size) {
      julong temp_limit = ((upper_limit - lower_limit) / 2) + lower_limit;
      temp_limit = align_size_down_(temp_limit, min_allocation_size);
      if (is_allocatable(temp_limit)) {
        lower_limit = temp_limit;
      } else {
        upper_limit = temp_limit;
      }
    }
    *limit = lower_limit;
  }
  return true;
#endif
}
