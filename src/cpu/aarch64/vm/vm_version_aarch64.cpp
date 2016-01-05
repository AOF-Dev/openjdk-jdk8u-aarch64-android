/*
 * Copyright (c) 2013, Red Hat Inc.
 * Copyright (c) 1997, 2012, Oracle and/or its affiliates.
 * All rights reserved.
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

#include "precompiled.hpp"
#include "asm/macroAssembler.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/java.hpp"
#include "runtime/stubCodeGenerator.hpp"
#include "vm_version_aarch64.hpp"
#ifdef TARGET_OS_FAMILY_linux
# include "os_linux.inline.hpp"
#endif

#ifndef BUILTIN_SIM
#include <sys/auxv.h>
#include <asm/hwcap.h>
#else
#define getauxval(hwcap) 0
#endif

#ifndef HWCAP_AES
#define HWCAP_AES   (1<<3)
#endif

#ifndef HWCAP_SHA1
#define HWCAP_SHA1  (1<<5)
#endif

#ifndef HWCAP_SHA2
#define HWCAP_SHA2  (1<<6)
#endif

#ifndef HWCAP_CRC32
#define HWCAP_CRC32 (1<<7)
#endif

int VM_Version::_cpu;
int VM_Version::_model;
int VM_Version::_variant;
int VM_Version::_revision;
int VM_Version::_stepping;
int VM_Version::_cpuFeatures;
const char*           VM_Version::_features_str = "";

static BufferBlob* stub_blob;
static const int stub_size = 550;

extern "C" {
  typedef void (*getPsrInfo_stub_t)(void*);
}
static getPsrInfo_stub_t getPsrInfo_stub = NULL;


class VM_Version_StubGenerator: public StubCodeGenerator {
 public:

  VM_Version_StubGenerator(CodeBuffer *c) : StubCodeGenerator(c) {}

  address generate_getPsrInfo() {
    StubCodeMark mark(this, "VM_Version", "getPsrInfo_stub");
#   define __ _masm->
    address start = __ pc();

#ifdef BUILTIN_SIM
    __ c_stub_prolog(1, 0, MacroAssembler::ret_type_void);
#endif

    // void getPsrInfo(VM_Version::CpuidInfo* cpuid_info);

    address entry = __ pc();

    // TODO : redefine fields in CpuidInfo and generate
    // code to fill them in

    __ ret(lr);

#   undef __


  }
};


void VM_Version::get_processor_features() {
  _supports_cx8 = true;
  _supports_atomic_getset4 = true;
  _supports_atomic_getadd4 = true;
  _supports_atomic_getset8 = true;
  _supports_atomic_getadd8 = true;

  if (FLAG_IS_DEFAULT(AllocatePrefetchDistance))
    FLAG_SET_DEFAULT(AllocatePrefetchDistance, 256);
  if (FLAG_IS_DEFAULT(AllocatePrefetchStepSize))
    FLAG_SET_DEFAULT(AllocatePrefetchStepSize, 64);
  FLAG_SET_DEFAULT(PrefetchScanIntervalInBytes, 256);
  FLAG_SET_DEFAULT(PrefetchFieldsAhead, 256);
  FLAG_SET_DEFAULT(PrefetchCopyIntervalInBytes, 256);
  FLAG_SET_DEFAULT(UseSSE42Intrinsics, true);

  unsigned long auxv = getauxval(AT_HWCAP);

  char buf[512];

  strcpy(buf, "simd");
  if (auxv & HWCAP_CRC32) strcat(buf, ", crc");
  if (auxv & HWCAP_AES)   strcat(buf, ", aes");
  if (auxv & HWCAP_SHA1)  strcat(buf, ", sha1");
  if (auxv & HWCAP_SHA2)  strcat(buf, ", sha256");

  _features_str = strdup(buf);
  _cpuFeatures = auxv;

  if (FILE *f = fopen("/proc/cpuinfo", "r")) {
    char buf[128], *p;
    while (fgets(buf, sizeof (buf), f) != NULL) {
      if (p = strchr(buf, ':')) {
        long v = strtol(p+1, NULL, 0);
        if (strncmp(buf, "CPU implementer", sizeof "CPU implementer" - 1) == 0) {
          _cpu = v;
        } else if (strncmp(buf, "CPU variant", sizeof "CPU variant" - 1) == 0) {
          _variant = v;
        } else if (strncmp(buf, "CPU part", sizeof "CPU part" - 1) == 0) {
          _model = v;
        } else if (strncmp(buf, "CPU revision", sizeof "CPU revision" - 1) == 0) {
          _revision = v;
        }
      }
    }
    fclose(f);
  }

  // Enable vendor specific features
  if (_cpu == CPU_CAVIUM) _cpuFeatures |= CPU_DMB_ATOMICS;
  if (_cpu == CPU_ARM) _cpuFeatures |= CPU_A53MAC;

  if (FLAG_IS_DEFAULT(UseCRC32)) {
    UseCRC32 = (auxv & HWCAP_CRC32) != 0;
  }
  if (UseCRC32 && (auxv & HWCAP_CRC32) == 0) {
    warning("UseCRC32 specified, but not supported on this CPU");
  }
  if (auxv & HWCAP_AES) {
    UseAES = UseAES || FLAG_IS_DEFAULT(UseAES);
    UseAESIntrinsics =
        UseAESIntrinsics || (UseAES && FLAG_IS_DEFAULT(UseAESIntrinsics));
    if (UseAESIntrinsics && !UseAES) {
      warning("UseAESIntrinsics enabled, but UseAES not, enabling");
      UseAES = true;
    }
  } else {
    if (UseAES) {
      warning("UseAES specified, but not supported on this CPU");
    }
    if (UseAESIntrinsics) {
      warning("UseAESIntrinsics specified, but not supported on this CPU");
    }
  }

  if (FLAG_IS_DEFAULT(UseCRC32Intrinsics)) {
    UseCRC32Intrinsics = true;
  }

  if (auxv & (HWCAP_SHA1 | HWCAP_SHA2)) {
    if (FLAG_IS_DEFAULT(UseSHA)) {
      FLAG_SET_DEFAULT(UseSHA, true);
    }
  } else if (UseSHA) {
    warning("SHA instructions are not available on this CPU");
    FLAG_SET_DEFAULT(UseSHA, false);
  }

  if (!UseSHA) {
    FLAG_SET_DEFAULT(UseSHA1Intrinsics, false);
    FLAG_SET_DEFAULT(UseSHA256Intrinsics, false);
    FLAG_SET_DEFAULT(UseSHA512Intrinsics, false);
  } else {
    if (auxv & HWCAP_SHA1) {
      if (FLAG_IS_DEFAULT(UseSHA1Intrinsics)) {
        FLAG_SET_DEFAULT(UseSHA1Intrinsics, true);
      }
    } else if (UseSHA1Intrinsics) {
      warning("SHA1 instruction is not available on this CPU.");
      FLAG_SET_DEFAULT(UseSHA1Intrinsics, false);
    }
    if (auxv & HWCAP_SHA2) {
      if (FLAG_IS_DEFAULT(UseSHA256Intrinsics)) {
        FLAG_SET_DEFAULT(UseSHA256Intrinsics, true);
      }
    } else if (UseSHA256Intrinsics) {
      warning("SHA256 instruction (for SHA-224 and SHA-256) is not available on this CPU.");
      FLAG_SET_DEFAULT(UseSHA256Intrinsics, false);
    }
    if (UseSHA512Intrinsics) {
      warning("SHA512 instruction (for SHA-384 and SHA-512) is not available on this CPU.");
      FLAG_SET_DEFAULT(UseSHA512Intrinsics, false);
    }
  }

  if (FLAG_IS_DEFAULT(UseMultiplyToLenIntrinsic)) {
    UseMultiplyToLenIntrinsic = true;
  }

  if (FLAG_IS_DEFAULT(UseBarriersForVolatile)) {
    UseBarriersForVolatile = (_cpuFeatures & CPU_DMB_ATOMICS) != 0;
  }

  if (FLAG_IS_DEFAULT(UsePopCountInstruction)) {
    UsePopCountInstruction = true;
  }

#ifdef COMPILER2
  if (FLAG_IS_DEFAULT(OptoScheduling)) {
    OptoScheduling = true;
  }
#else
  if (ReservedCodeCacheSize > 128*M) {
    vm_exit_during_initialization("client compiler does not support ReservedCodeCacheSize > 128M");
  }
#endif
}

void VM_Version::initialize() {
  ResourceMark rm;

  stub_blob = BufferBlob::create("getPsrInfo_stub", stub_size);
  if (stub_blob == NULL) {
    vm_exit_during_initialization("Unable to allocate getPsrInfo_stub");
  }

  CodeBuffer c(stub_blob);
  VM_Version_StubGenerator g(&c);
  getPsrInfo_stub = CAST_TO_FN_PTR(getPsrInfo_stub_t,
                                   g.generate_getPsrInfo());

  get_processor_features();
}
