#
# Copyright (c) 2012, Oracle and/or its affiliates. All rights reserved.
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
ifeq ($(INCLUDE_JVMTI), false)
      CXXFLAGS += -DINCLUDE_JVMTI=0
      CFLAGS += -DINCLUDE_JVMTI=0

      Src_Files_EXCLUDE += jvmtiGetLoadedClasses.cpp forte.cpp jvmtiThreadState.cpp jvmtiExtensions.cpp \
	jvmtiImpl.cpp jvmtiManageCapabilities.cpp jvmtiRawMonitor.cpp jvmtiUtil.cpp jvmtiTrace.cpp \
	jvmtiCodeBlobEvents.cpp jvmtiEnv.cpp jvmtiRedefineClasses.cpp jvmtiEnvBase.cpp jvmtiEnvThreadState.cpp \
	jvmtiTagMap.cpp jvmtiEventController.cpp evmCompat.cpp jvmtiEnter.xsl jvmtiExport.cpp
endif

ifeq ($(INCLUDE_FPROF), false)
      CXXFLAGS += -DINCLUDE_FPROF=0
      CFLAGS += -DINCLUDE_FPROF=0

      Src_Files_EXCLUDE += fprofiler.cpp
endif

ifeq ($(INCLUDE_VM_STRUCTS), false)
      CXXFLAGS += -DINCLUDE_VM_STRUCTS=0
      CFLAGS += -DINCLUDE_VM_STRUCTS=0

      Src_Files_EXCLUDE += vmStructs.cpp
endif

ifeq ($(INCLUDE_JNI_CHECK), false)
      CXXFLAGS += -DINCLUDE_JNI_CHECK=0
      CFLAGS += -DINCLUDE_JNI_CHECK=0

      Src_Files_EXCLUDE += jniCheck.cpp
endif

ifeq ($(INCLUDE_SERVICES), false)
      CXXFLAGS += -DINCLUDE_SERVICES=0
      CFLAGS += -DINCLUDE_SERVICES=0

      Src_Files_EXCLUDE += heapDumper.cpp heapInspection.cpp \
	attachListener_linux.cpp attachListener.cpp
endif

ifeq ($(INCLUDE_MANAGEMENT), false)
      CXXFLAGS += -DINCLUDE_MANAGEMENT=0
      CFLAGS += -DINCLUDE_MANAGEMENT=0
endif

ifeq ($(INCLUDE_CDS), false)
      CXXFLAGS += -DINCLUDE_CDS=0
      CFLAGS += -DINCLUDE_CDS=0

      Src_Files_EXCLUDE += metaspaceShared.cpp
endif

ifeq ($(INCLUDE_ALTERNATE_GCS), false)
      CXXFLAGS += -DINCLUDE_ALTERNATE_GCS=0
      CFLAGS += -DINCLUDE_ALTERNATE_GCS=0

      CXXFLAGS += -DSERIALGC
      CFLAGS += -DSERIALGC
      Src_Files_EXCLUDE += \
	cmsAdaptiveSizePolicy.cpp cmsCollectorPolicy.cpp \
	cmsGCAdaptivePolicyCounters.cpp cmsLockVerifier.cpp cmsPermGen.cpp compactibleFreeListSpace.cpp \
	concurrentMarkSweepGeneration.cpp concurrentMarkSweepThread.cpp \
	freeChunk.cpp adaptiveFreeList.cpp promotionInfo.cpp vmCMSOperations.cpp collectionSetChooser.cpp \
	concurrentG1Refine.cpp concurrentG1RefineThread.cpp concurrentMark.cpp concurrentMarkThread.cpp \
	dirtyCardQueue.cpp g1AllocRegion.cpp g1BlockOffsetTable.cpp g1CollectedHeap.cpp g1GCPhaseTimes.cpp \
	g1CollectorPolicy.cpp g1ErgoVerbose.cpp g1_globals.cpp g1HRPrinter.cpp g1MarkSweep.cpp \
	g1MMUTracker.cpp g1MonitoringSupport.cpp g1RemSet.cpp g1SATBCardTableModRefBS.cpp heapRegion.cpp \
	heapRegionRemSet.cpp heapRegionSeq.cpp heapRegionSet.cpp heapRegionSets.cpp ptrQueue.cpp \
	satbQueue.cpp sparsePRT.cpp survRateGroup.cpp vm_operations_g1.cpp adjoiningGenerations.cpp \
	adjoiningVirtualSpaces.cpp asPSOldGen.cpp asPSYoungGen.cpp cardTableExtension.cpp \
	gcTaskManager.cpp gcTaskThread.cpp objectStartArray.cpp parallelScavengeHeap.cpp parMarkBitMap.cpp \
	pcTasks.cpp psAdaptiveSizePolicy.cpp psCompactionManager.cpp psGCAdaptivePolicyCounters.cpp \
	psGenerationCounters.cpp psMarkSweep.cpp psMarkSweepDecorator.cpp psOldGen.cpp psParallelCompact.cpp \
	psPermGen.cpp psPromotionLAB.cpp psPromotionManager.cpp psScavenge.cpp psTasks.cpp psVirtualspace.cpp \
	psYoungGen.cpp vmPSOperations.cpp asParNewGeneration.cpp parCardTableModRefBS.cpp \
	parGCAllocBuffer.cpp parNewGeneration.cpp mutableSpace.cpp gSpaceCounters.cpp allocationStats.cpp \
	spaceCounters.cpp gcAdaptivePolicyCounters.cpp mutableNUMASpace.cpp immutableSpace.cpp \
	immutableSpace.cpp g1MemoryPool.cpp psMemoryPool.cpp yieldWorkingGroup.cpp g1Log.cpp
endif 

ifeq ($(INCLUDE_NMT), false)
      CXXFLAGS += -DINCLUDE_NMT=0
      CFLAGS += -DINCLUDE_NMT=0

      Src_Files_EXCLUDE += \
	 memBaseline.cpp memPtr.cpp memRecorder.cpp memReporter.cpp memSnapshot.cpp memTrackWorker.cpp \
	 memTracker.cpp nmtDCmd.cpp
endif
