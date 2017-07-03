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

#include "precompiled.hpp"

#include "gc_implementation/shenandoah/shenandoahHeap.hpp"
#include "gc_implementation/shenandoah/shenandoahUtils.hpp"
#include "gc_implementation/shenandoah/shenandoahMarkCompact.hpp"
#include "gc_implementation/shared/gcTimer.hpp"


ShenandoahGCSession::ShenandoahGCSession(bool is_full_gc) {
  _timer = is_full_gc ? ShenandoahMarkCompact::gc_timer() :
                        ShenandoahHeap::heap()->gc_timer();

  _timer->register_gc_start();
}

ShenandoahGCSession::~ShenandoahGCSession() {
  _timer->register_gc_end();
}

ShenandoahGCPhase::ShenandoahGCPhase(const ShenandoahCollectorPolicy::TimingPhase phase) :
  _phase(phase) {
  ShenandoahHeap::heap()->shenandoahPolicy()->record_phase_start(_phase);
}

ShenandoahGCPhase::~ShenandoahGCPhase() {
  ShenandoahHeap::heap()->shenandoahPolicy()->record_phase_end(_phase);
}
