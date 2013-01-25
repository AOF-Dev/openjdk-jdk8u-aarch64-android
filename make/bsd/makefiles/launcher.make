#
# Copyright (c) 2005, 2012, Oracle and/or its affiliates. All rights reserved.
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

# Rules to build gamma launcher, used by vm.make


LAUNCHER_SCRIPT = hotspot
LAUNCHER   = gamma

LAUNCHERDIR   := $(GAMMADIR)/src/os/posix/launcher
LAUNCHERDIR_SHARE := $(GAMMADIR)/src/share/tools/launcher
LAUNCHERFLAGS := $(ARCHFLAG) \
                -I$(LAUNCHERDIR) -I$(GAMMADIR)/src/share/vm/prims \
                -I$(LAUNCHERDIR_SHARE) \
                -DFULL_VERSION=\"$(HOTSPOT_RELEASE_VERSION)\" \
                -DJDK_MAJOR_VERSION=\"$(JDK_MAJOR_VERSION)\" \
                -DJDK_MINOR_VERSION=\"$(JDK_MINOR_VERSION)\" \
                -DARCH=\"$(LIBARCH)\" \
                -DGAMMA \
                -DLAUNCHER_TYPE=\"gamma\" \
                -DLINK_INTO_$(LINK_INTO) \
                $(TARGET_DEFINES)
# Give the launcher task_for_pid() privileges so that it can be used to run JStack, JInfo, et al.
LFLAGS_LAUNCHER += -sectcreate __TEXT __info_plist $(GAMMADIR)/src/os/bsd/launcher/Info-privileged.plist

ifeq ($(LINK_INTO),AOUT)
  LAUNCHER.o                 = launcher.o $(JVM_OBJ_FILES)
  LAUNCHER_MAPFILE           = mapfile_reorder
  LFLAGS_LAUNCHER$(LDNOMAP) += $(MAPFLAG:FILENAME=$(LAUNCHER_MAPFILE))
  LFLAGS_LAUNCHER           += $(SONAMEFLAG:SONAME=$(LIBJVM)) $(STATIC_LIBGCC)
  LIBS_LAUNCHER             += $(STATIC_STDCXX) $(LIBS)
else
  LAUNCHER.o                 = launcher.o
  LFLAGS_LAUNCHER           += -L`pwd`

  # The gamma launcher runs the JDK from $JAVA_HOME, overriding the JVM with a
  # freshly built JVM at ./libjvm.{so|dylib}.  This is accomplished by setting
  # the library searchpath using ({DY}LD_LIBRARY_PATH) to find the local JVM
  # first.  Gamma dlopen()s libjava from $JAVA_HOME/jre/lib{/$arch}, which is
  # statically linked with CoreFoundation framework libs. Unfortunately, gamma's
  # unique searchpath results in some unresolved symbols in the framework
  # libraries, because JDK libraries are inadvertently discovered first on the
  # searchpath, e.g. libjpeg.  On Mac OS X, filenames are case *insensitive*.
  # So, the actual filename collision is libjpeg.dylib and libJPEG.dylib.
  # To resolve this, gamma needs to also statically link with the CoreFoundation
  # framework libraries.

  ifeq ($(OS_VENDOR),Darwin)
    LFLAGS_LAUNCHER         += -framework CoreFoundation
  endif

  LIBS_LAUNCHER             += -l$(JVM) $(LIBS)
endif

LINK_LAUNCHER = $(LINK.CC)

LINK_LAUNCHER/PRE_HOOK  = $(LINK_LIB.CXX/PRE_HOOK)
LINK_LAUNCHER/POST_HOOK = $(LINK_LIB.CXX/POST_HOOK)

LAUNCHER_OUT = launcher

SUFFIXES += .d

SOURCES := $(shell find $(LAUNCHERDIR) -name "*.c")
SOURCES_SHARE := $(shell find $(LAUNCHERDIR_SHARE) -name "*.c")

OBJS := $(patsubst $(LAUNCHERDIR)/%.c,$(LAUNCHER_OUT)/%.o,$(SOURCES)) $(patsubst $(LAUNCHERDIR_SHARE)/%.c,$(LAUNCHER_OUT)/%.o,$(SOURCES_SHARE))

DEPFILES := $(patsubst %.o,%.d,$(OBJS))
-include $(DEPFILES)

$(LAUNCHER_OUT)/%.o: $(LAUNCHERDIR_SHARE)/%.c
	$(QUIETLY) [ -d $(LAUNCHER_OUT) ] || { mkdir -p $(LAUNCHER_OUT); }
	$(QUIETLY) $(CC) -g -o $@ -c $< -MMD $(LAUNCHERFLAGS) $(CXXFLAGS)

$(LAUNCHER_OUT)/%.o: $(LAUNCHERDIR)/%.c
	$(QUIETLY) [ -d $(LAUNCHER_OUT) ] || { mkdir -p $(LAUNCHER_OUT); }
	$(QUIETLY) $(CC) -g -o $@ -c $< -MMD $(LAUNCHERFLAGS) $(CXXFLAGS)

$(LAUNCHER): $(OBJS) $(LIBJVM) $(LAUNCHER_MAPFILE)
	$(QUIETLY) echo Linking launcher...
	$(QUIETLY) $(LINK_LAUNCHER/PRE_HOOK)
	$(QUIETLY) $(LINK_LAUNCHER) $(LFLAGS_LAUNCHER) -o $@ $(sort $(OBJS)) $(LIBS_LAUNCHER)
	$(QUIETLY) $(LINK_LAUNCHER/POST_HOOK)
	# Sign the launcher with the development certificate (if present) so that it can be used
	# to run JStack, JInfo, et al.
	$(QUIETLY) -codesign -s openjdk_codesign $@

$(LAUNCHER): $(LAUNCHER_SCRIPT)

$(LAUNCHER_SCRIPT): $(LAUNCHERDIR)/launcher.script
	$(QUIETLY) sed -e 's/@@LIBARCH@@/$(LIBARCH)/g' $< > $@
	$(QUIETLY) chmod +x $@

