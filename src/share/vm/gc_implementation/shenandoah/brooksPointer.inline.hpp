/*
 * Copyright (c) 2015, Red Hat, Inc. and/or its affiliates.
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

#ifndef SHARE_VM_GC_SHENANDOAH_BROOKSPOINTER_INLINE_HPP
#define SHARE_VM_GC_SHENANDOAH_BROOKSPOINTER_INLINE_HPP

#include "gc_implementation/shenandoah/brooksPointer.hpp"
#include "gc_implementation/shenandoah/shenandoahVerifier.hpp"
#include "gc_implementation/shenandoah/shenandoahHeap.hpp"
#include "gc_implementation/shenandoah/shenandoahHeapRegion.hpp"
#include "gc_implementation/shenandoah/shenandoahLogging.hpp"
#include "runtime/atomic.hpp"

inline HeapWord** BrooksPointer::brooks_ptr_addr(oop obj) {
  return (HeapWord**)((HeapWord*) obj + word_offset());
}

inline void BrooksPointer::initialize(oop obj) {
  log_develop_trace(gc)("Init forwardee for "PTR_FORMAT" to self", p2i(obj));
#ifdef ASSERT
  assert(ShenandoahHeap::heap()->is_in(obj), "oop must point to a heap address");
#endif
  *brooks_ptr_addr(obj) = (HeapWord*) obj;
}

inline void BrooksPointer::set_forwardee(oop holder, oop update) {
#ifdef ASSERT
  ShenandoahVerifier::verify_oop_fwdptr(holder, update);
#endif
  log_develop_trace(gc)("Setting forwardee to "PTR_FORMAT" = "PTR_FORMAT, p2i(holder), p2i(update));
  *brooks_ptr_addr(holder) = (HeapWord*) update;
}

inline void BrooksPointer::set_raw(oop holder, HeapWord* update) {
  log_develop_trace(gc)("Setting RAW forwardee for "PTR_FORMAT" = "PTR_FORMAT, p2i(holder), p2i(update));
  assert(UseShenandoahGC, "must only be called when Shenandoah is used.");
  *brooks_ptr_addr(holder) = update;
}

inline HeapWord* BrooksPointer::get_raw(oop holder) {
  assert(UseShenandoahGC, "must only be called when Shenandoah is used.");
  HeapWord* res = *brooks_ptr_addr(holder);
  log_develop_trace(gc)("Getting RAW forwardee for "PTR_FORMAT" = "PTR_FORMAT, p2i(holder), p2i(res));
  return res;
}

inline oop BrooksPointer::forwardee(oop obj) {
#ifdef ASSERT
  ShenandoahVerifier::verify_oop(obj);
#endif
  oop forwardee = oop(*brooks_ptr_addr(obj));
  log_develop_trace(gc)("Forwardee for "PTR_FORMAT" = "PTR_FORMAT, p2i(obj), p2i(forwardee));
  return forwardee;
}

inline oop BrooksPointer::try_update_forwardee(oop holder, oop update) {
#ifdef ASSERT
  ShenandoahVerifier::verify_oop_fwdptr(holder, update);
#endif

  oop result = (oop) Atomic::cmpxchg_ptr(update, brooks_ptr_addr(holder), holder);

#ifdef ASSERT
  assert(result != NULL, "CAS result is not NULL");
  assert(ShenandoahHeap::heap()->is_in(result), "CAS result must point to a heap address");

  if (oopDesc::unsafe_equals(result, holder)) {
    log_develop_trace(gc)("Updated forwardee for "PTR_FORMAT" to "PTR_FORMAT, p2i(holder), p2i(update));
  } else {
    log_develop_trace(gc)("Failed to set forwardee for "PTR_FORMAT" to "PTR_FORMAT", was already "PTR_FORMAT, p2i(holder), p2i(update), p2i(result));
  }
#endif

  return result;
}

#endif // SHARE_VM_GC_SHENANDOAH_BROOKSPOINTER_INLINE_HPP
