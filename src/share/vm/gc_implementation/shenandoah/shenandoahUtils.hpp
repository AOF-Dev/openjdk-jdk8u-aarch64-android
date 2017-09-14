/*
 * Copyright (c) 2017, Red Hat, Inc. and/or its affiliates.
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

#ifndef SHARE_VM_GC_SHENANDOAHUTILS_HPP
#define SHARE_VM_GC_SHENANDOAHUTILS_HPP

#include "gc_implementation/shared/isGCActiveMark.hpp"
#include "gc_implementation/shared/vmGCOperations.hpp"
#include "memory/allocation.hpp"
#include "gc_implementation/shenandoah/shenandoahCollectorPolicy.hpp"

class GCTimer;

class ShenandoahGCSession : public StackObj {
private:
  GCTimer*  _timer;

public:
  ShenandoahGCSession(bool is_full_gc = false);
  ~ShenandoahGCSession();
};

class ShenandoahGCPhase : public StackObj {
private:
  const ShenandoahCollectorPolicy::TimingPhase   _phase;
public:
  ShenandoahGCPhase(ShenandoahCollectorPolicy::TimingPhase phase);
  ~ShenandoahGCPhase();
};

// Aggregates all the things that should happen before/after the pause.
class ShenandoahGCPauseMark : public StackObj {
private:
  const ShenandoahGCPhase _phase_total;
  const ShenandoahGCPhase _phase_this;
  const SvcGCMarker       _svc_gc_mark;
  const IsGCActiveMark    _is_gc_active_mark;
public:
  ShenandoahGCPauseMark(ShenandoahCollectorPolicy::TimingPhase phase, SvcGCMarker::reason_type type);
  ~ShenandoahGCPauseMark();
};

class ShenandoahAllocTrace : public StackObj {
private:
  double _start;
  size_t _size;
  ShenandoahHeap::AllocType _alloc_type;
public:
  ShenandoahAllocTrace(size_t words_size, ShenandoahHeap::AllocType alloc_type);
  ~ShenandoahAllocTrace();
};

#endif // SHARE_VM_GC_SHENANDOAHUTILS_HPP
