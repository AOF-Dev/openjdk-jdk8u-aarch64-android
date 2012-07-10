#
# Copyright (c) 2006, 2012, Oracle and/or its affiliates. All rights reserved.
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# This code is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 only, as
# published by the Free Software Foundation.
#
# This code is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# version 2 for more details (a copy is included in the LICENSE file that
# accompanied this code).
#
# You should have received a copy of the GNU General Public License version
# 2 along with this work; if not, write to the Free Software Foundation,
# Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
# or visit www.oracle.com if you need additional information or have any
# questions.
#  
#

# The common definitions for hotspot linux builds.
# Include the top level defs.make under make directory instead of this one.
# This file is included into make/defs.make.

SLASH_JAVA ?= /java

# Need PLATFORM (os-arch combo names) for jdk and hotspot, plus libarch name
ARCH:=$(shell uname -m)
PATH_SEP = :
ifeq ($(LP64), 1)
  ARCH_DATA_MODEL ?= 64
else
  ARCH_DATA_MODEL ?= 32
endif

# zero
ifeq ($(findstring true, $(JVM_VARIANT_ZERO) $(JVM_VARIANT_ZEROSHARK)), true)
  ifeq ($(ARCH_DATA_MODEL), 64)
    MAKE_ARGS      += LP64=1
  endif
  PLATFORM         = linux-zero
  VM_PLATFORM      = linux_$(subst i386,i486,$(ZERO_LIBARCH))
  HS_ARCH          = zero
  ARCH             = zero
endif

# ia64
ifeq ($(ARCH), ia64)
  ARCH_DATA_MODEL = 64
  MAKE_ARGS      += LP64=1
  PLATFORM        = linux-ia64
  VM_PLATFORM     = linux_ia64
  HS_ARCH         = ia64
endif

# sparc
ifeq ($(ARCH), sparc64)
  ifeq ($(ARCH_DATA_MODEL), 64)
    ARCH_DATA_MODEL  = 64
    MAKE_ARGS        += LP64=1
    PLATFORM         = linux-sparcv9
    VM_PLATFORM      = linux_sparcv9
  else
    ARCH_DATA_MODEL  = 32
    PLATFORM         = linux-sparc
    VM_PLATFORM      = linux_sparc
  endif
  HS_ARCH            = sparc
endif

# x86_64
ifeq ($(ARCH), x86_64) 
  ifeq ($(ARCH_DATA_MODEL), 64)
    ARCH_DATA_MODEL = 64
    MAKE_ARGS       += LP64=1
    PLATFORM        = linux-amd64
    VM_PLATFORM     = linux_amd64
    HS_ARCH         = x86
  else
    ARCH_DATA_MODEL = 32
    PLATFORM        = linux-i586
    VM_PLATFORM     = linux_i486
    HS_ARCH         = x86
    # We have to reset ARCH to i686 since SRCARCH relies on it
    ARCH            = i686   
  endif
endif

# i686
ifeq ($(ARCH), i686)
  ARCH_DATA_MODEL  = 32
  PLATFORM         = linux-i586
  VM_PLATFORM      = linux_i486
  HS_ARCH          = x86
endif

# ARM
ifeq ($(ARCH), arm)
  ARCH_DATA_MODEL  = 32
  PLATFORM         = linux-arm
  VM_PLATFORM      = linux_arm
  HS_ARCH          = arm
endif

# PPC
ifeq ($(ARCH), ppc)
  ARCH_DATA_MODEL  = 32
  PLATFORM         = linux-ppc
  VM_PLATFORM      = linux_ppc
  HS_ARCH          = ppc
endif

# AARCH64

# AARCH64 is currently signalled by SRCARCH == aarch64
# however it relies upon builing with ARCH = x86_64
# the next line defaults x86_64 builds to aarch64
ifeq ($(ARCH), x86_64)
  SRCARCH = aarch64
endif

ifeq ($(SRCARCH), aarch64)
  ARCH_DATA_MODEL  = 64
  PLATFORM         = linux-aarch64
  VM_PLATFORM      = linux_aarch64
  HS_ARCH          = x86
endif

# On 32 bit linux we build server and client, on 64 bit just server.
ifeq ($(JVM_VARIANTS),)
  ifeq ($(ARCH_DATA_MODEL), 32)
    JVM_VARIANTS:=client,server
    JVM_VARIANT_CLIENT:=true
    JVM_VARIANT_SERVER:=true
  else
    JVM_VARIANTS:=server
    JVM_VARIANT_SERVER:=true
  endif
endif

# determine if HotSpot is being built in JDK6 or earlier version
JDK6_OR_EARLIER=0
ifeq "$(shell expr \( '$(JDK_MAJOR_VERSION)' != '' \& '$(JDK_MINOR_VERSION)' != '' \& '$(JDK_MICRO_VERSION)' != '' \))" "1"
  # if the longer variable names (newer build style) are set, then check those
  ifeq "$(shell expr \( $(JDK_MAJOR_VERSION) = 1 \& $(JDK_MINOR_VERSION) \< 7 \))" "1"
    JDK6_OR_EARLIER=1
  endif
else
  # the longer variables aren't set so check the shorter variable names
  ifeq "$(shell expr \( '$(JDK_MAJOR_VER)' = 1 \& '$(JDK_MINOR_VER)' \< 7 \))" "1"
    JDK6_OR_EARLIER=1
  endif
endif

STRIP_POLICY=no_strip

ifeq ($(JDK6_OR_EARLIER),0)
  # Full Debug Symbols is supported on JDK7 or newer.
  # The Full Debug Symbols (FDS) default for BUILD_FLAVOR == product
  # builds is enabled with debug info files ZIP'ed to save space. For
  # BUILD_FLAVOR != product builds, FDS is always enabled, after all a
  # debug build without debug info isn't very useful.
  # The ZIP_DEBUGINFO_FILES option only has meaning when FDS is enabled.
  #
  # If you invoke a build with FULL_DEBUG_SYMBOLS=0, then FDS will be
  # disabled for a BUILD_FLAVOR == product build.
  #
  # Note: Use of a different variable name for the FDS override option
  # versus the FDS enabled check is intentional (FULL_DEBUG_SYMBOLS
  # versus ENABLE_FULL_DEBUG_SYMBOLS). For auto build systems that pass
  # in options via environment variables, use of distinct variables
  # prevents strange behaviours. For example, in a BUILD_FLAVOR !=
  # product build, the FULL_DEBUG_SYMBOLS environment variable will be
  # 0, but the ENABLE_FULL_DEBUG_SYMBOLS make variable will be 1. If
  # the same variable name is used, then different values can be picked
  # up by different parts of the build. Just to be clear, we only need
  # two variable names because the incoming option value can be
  # overridden in some situations, e.g., a BUILD_FLAVOR != product
  # build.

  ifeq ($(BUILD_FLAVOR), product)
    FULL_DEBUG_SYMBOLS ?= 1
    ENABLE_FULL_DEBUG_SYMBOLS = $(FULL_DEBUG_SYMBOLS)
  else
    # debug variants always get Full Debug Symbols (if available)
    ENABLE_FULL_DEBUG_SYMBOLS = 1
  endif
  _JUNK_ := $(shell \
    echo >&2 "INFO: ENABLE_FULL_DEBUG_SYMBOLS=$(ENABLE_FULL_DEBUG_SYMBOLS)")
  # since objcopy is optional, we set ZIP_DEBUGINFO_FILES later

  ifeq ($(ENABLE_FULL_DEBUG_SYMBOLS),1)
    # Default OBJCOPY comes from GNU Binutils on Linux:
    DEF_OBJCOPY=/usr/bin/objcopy
    ifdef CROSS_COMPILE_ARCH
      # don't try to generate .debuginfo files when cross compiling
      _JUNK_ := $(shell \
        echo >&2 "INFO: cross compiling for ARCH $(CROSS_COMPILE_ARCH)," \
          "skipping .debuginfo generation.")
      OBJCOPY=
    else
      OBJCOPY=$(shell test -x $(DEF_OBJCOPY) && echo $(DEF_OBJCOPY))
      ifneq ($(ALT_OBJCOPY),)
        _JUNK_ := $(shell echo >&2 "INFO: ALT_OBJCOPY=$(ALT_OBJCOPY)")
        OBJCOPY=$(shell test -x $(ALT_OBJCOPY) && echo $(ALT_OBJCOPY))
      endif
    endif
  else
    OBJCOPY=
  endif

  ifeq ($(OBJCOPY),)
    _JUNK_ := $(shell \
      echo >&2 "INFO: no objcopy cmd found so cannot create .debuginfo files.")
    ENABLE_FULL_DEBUG_SYMBOLS=0
    _JUNK_ := $(shell \
      echo >&2 "INFO: ENABLE_FULL_DEBUG_SYMBOLS=$(ENABLE_FULL_DEBUG_SYMBOLS)")
  else
    _JUNK_ := $(shell \
      echo >&2 "INFO: $(OBJCOPY) cmd found so will create .debuginfo files.")

    # Library stripping policies for .debuginfo configs:
    #   all_strip - strips everything from the library
    #   min_strip - strips most stuff from the library; leaves minimum symbols
    #   no_strip  - does not strip the library at all
    #
    # Oracle security policy requires "all_strip". A waiver was granted on
    # 2011.09.01 that permits using "min_strip" in the Java JDK and Java JRE.
    #
    # Currently, STRIP_POLICY is only used when Full Debug Symbols is enabled.
    #
    STRIP_POLICY ?= min_strip

    _JUNK_ := $(shell \
      echo >&2 "INFO: STRIP_POLICY=$(STRIP_POLICY)")

    ZIP_DEBUGINFO_FILES ?= 1

    _JUNK_ := $(shell \
      echo >&2 "INFO: ZIP_DEBUGINFO_FILES=$(ZIP_DEBUGINFO_FILES)")
  endif
endif

JDK_INCLUDE_SUBDIR=linux

# Library suffix
LIBRARY_SUFFIX=so

# FIXUP: The subdirectory for a debug build is NOT the same on all platforms
VM_DEBUG=jvmg

EXPORT_LIST += $(EXPORT_DOCS_DIR)/platform/jvmti/jvmti.html

# client and server subdirectories have symbolic links to ../libjsig.so
EXPORT_LIST += $(EXPORT_JRE_LIB_ARCH_DIR)/libjsig.$(LIBRARY_SUFFIX)
ifeq ($(ENABLE_FULL_DEBUG_SYMBOLS),1)
  ifeq ($(ZIP_DEBUGINFO_FILES),1)
    EXPORT_LIST += $(EXPORT_JRE_LIB_ARCH_DIR)/libjsig.diz
  else
    EXPORT_LIST += $(EXPORT_JRE_LIB_ARCH_DIR)/libjsig.debuginfo
  endif
endif
EXPORT_SERVER_DIR = $(EXPORT_JRE_LIB_ARCH_DIR)/server
EXPORT_CLIENT_DIR = $(EXPORT_JRE_LIB_ARCH_DIR)/client

EXPORT_LIST += $(EXPORT_JRE_LIB_DIR)/wb.jar

ifeq ($(findstring true, $(JVM_VARIANT_SERVER) $(JVM_VARIANT_ZERO) $(JVM_VARIANT_ZEROSHARK)), true)
  EXPORT_LIST += $(EXPORT_SERVER_DIR)/Xusage.txt
  EXPORT_LIST += $(EXPORT_SERVER_DIR)/libjvm.$(LIBRARY_SUFFIX)
  ifeq ($(ENABLE_FULL_DEBUG_SYMBOLS),1)
    ifeq ($(ZIP_DEBUGINFO_FILES),1)
      EXPORT_LIST += $(EXPORT_SERVER_DIR)/libjvm.diz
    else
      EXPORT_LIST += $(EXPORT_SERVER_DIR)/libjvm.debuginfo
    endif
  endif
endif

ifeq ($(JVM_VARIANT_CLIENT),true)
  EXPORT_LIST += $(EXPORT_CLIENT_DIR)/Xusage.txt
  EXPORT_LIST += $(EXPORT_CLIENT_DIR)/libjvm.$(LIBRARY_SUFFIX)
  ifeq ($(ENABLE_FULL_DEBUG_SYMBOLS),1)
    ifeq ($(ZIP_DEBUGINFO_FILES),1)
      EXPORT_LIST += $(EXPORT_CLIENT_DIR)/libjvm.diz
    else
      EXPORT_LIST += $(EXPORT_CLIENT_DIR)/libjvm.debuginfo
    endif
  endif 
endif

# Serviceability Binaries
# No SA Support for PPC, IA64, ARM or zero
ADD_SA_BINARIES/x86   = $(EXPORT_JRE_LIB_ARCH_DIR)/libsaproc.$(LIBRARY_SUFFIX) \
                        $(EXPORT_LIB_DIR)/sa-jdi.jar 
ADD_SA_BINARIES/sparc = $(EXPORT_JRE_LIB_ARCH_DIR)/libsaproc.$(LIBRARY_SUFFIX) \
                        $(EXPORT_LIB_DIR)/sa-jdi.jar 
ifeq ($(ENABLE_FULL_DEBUG_SYMBOLS),1)
  ifeq ($(ZIP_DEBUGINFO_FILES),1)
    ADD_SA_BINARIES/x86   += $(EXPORT_JRE_LIB_ARCH_DIR)/libsaproc.diz
    ADD_SA_BINARIES/sparc += $(EXPORT_JRE_LIB_ARCH_DIR)/libsaproc.diz
  else
    ADD_SA_BINARIES/x86   += $(EXPORT_JRE_LIB_ARCH_DIR)/libsaproc.debuginfo
    ADD_SA_BINARIES/sparc += $(EXPORT_JRE_LIB_ARCH_DIR)/libsaproc.debuginfo
  endif
endif
ADD_SA_BINARIES/ppc   = 
ADD_SA_BINARIES/ia64  = 
ADD_SA_BINARIES/arm   = 
ADD_SA_BINARIES/zero  = 

EXPORT_LIST += $(ADD_SA_BINARIES/$(HS_ARCH))


