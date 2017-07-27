/*
 * Copyright (c) 2013, 2015, Red Hat, Inc. and/or its affiliates.
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
#include "memory/allocation.hpp"
#include "gc_implementation/shenandoah/brooksPointer.hpp"
#include "gc_implementation/shenandoah/shenandoahHeap.hpp"
#include "gc_implementation/shenandoah/shenandoahHeap.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahHeapRegion.hpp"
#include "memory/space.inline.hpp"
#include "memory/universe.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/java.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"
#include "runtime/safepoint.hpp"

size_t ShenandoahHeapRegion::RegionSizeShift = 0;
size_t ShenandoahHeapRegion::RegionSizeBytes = 0;
size_t ShenandoahHeapRegion::RegionSizeWords = 0;

ShenandoahHeapRegion::ShenandoahHeapRegion(ShenandoahHeap* heap, HeapWord* start,
                                           size_t regionSizeWords, size_t index) :
  _heap(heap),
  _region_number(index),
  _live_data(0),
  _tlab_allocs(0),
  _gclab_allocs(0),
  _shared_allocs(0),
  reserved(MemRegion(start, regionSizeWords)),
  _humongous_start(false),
  _humongous_continuation(false),
  _new_top(NULL),
  _critical_pins(0) {

  ContiguousSpace::initialize(reserved, true, true);
}

size_t ShenandoahHeapRegion::region_number() const {
  return _region_number;
}

bool ShenandoahHeapRegion::rollback_allocation(uint size) {
  set_top(top() - size);
  return true;
}

void ShenandoahHeapRegion::clear_live_data() {
  assert(Thread::current()->is_VM_thread(), "by VM thread");
  _live_data = 0;
}

void ShenandoahHeapRegion::reset_alloc_stats() {
  _tlab_allocs = 0;
  _gclab_allocs = 0;
  _shared_allocs = 0;
}

void ShenandoahHeapRegion::reset_alloc_stats_to_shared() {
  _tlab_allocs = 0;
  _gclab_allocs = 0;
  _shared_allocs = used() >> LogHeapWordSize;
}

size_t ShenandoahHeapRegion::get_shared_allocs() const {
  return _shared_allocs * HeapWordSize;
}

size_t ShenandoahHeapRegion::get_tlab_allocs() const {
  return _tlab_allocs * HeapWordSize;
}

size_t ShenandoahHeapRegion::get_gclab_allocs() const {
  return _gclab_allocs * HeapWordSize;
}

void ShenandoahHeapRegion::set_live_data(size_t s) {
  assert(Thread::current()->is_VM_thread(), "by VM thread");
  _live_data = (jint) (s >> LogHeapWordSize);
}

size_t ShenandoahHeapRegion::get_live_data_words() const {
  if (is_humongous()) {
    if (is_humongous_start()) {
      size_t live_data = (size_t)OrderAccess::load_acquire((volatile jint*)&_live_data);
      return (live_data == 0) ? 0 : (used() >> LogHeapWordSize);
    } else {
      const ShenandoahHeapRegion* start = humongous_start_region();
      return start->get_live_data_words() == 0 ? 0 : (used() >> LogHeapWordSize);
    }
  } else {
    return (size_t)OrderAccess::load_acquire((volatile jint*)&_live_data);
  }
}

size_t ShenandoahHeapRegion::get_live_data_bytes() const {
  return get_live_data_words() * HeapWordSize;
}

bool ShenandoahHeapRegion::has_live() const {
  return get_live_data_words() != 0;
}

size_t ShenandoahHeapRegion::garbage() const {
  assert(used() >= get_live_data_bytes(), err_msg("Live Data must be a subset of used() live: "SIZE_FORMAT" used: "SIZE_FORMAT,
         get_live_data_bytes(), used()));
  size_t result = used() - get_live_data_bytes();
  return result;
}

bool ShenandoahHeapRegion::in_collection_set() const {
  return _heap->region_in_collection_set(_region_number);
}

void ShenandoahHeapRegion::print_on(outputStream* st) const {
  st->print("|" PTR_FORMAT, p2i(this));
  st->print("|" SIZE_FORMAT_W(5), this->_region_number);
  st->print("|BTE " PTR_FORMAT ", " PTR_FORMAT ", " PTR_FORMAT,
            p2i(bottom()), p2i(top()), p2i(end()));
  st->print("|U %3d%%", (int) ((double) used() * 100 / capacity()));
  st->print("|T %3d%%", (int) ((double) get_tlab_allocs() * 100 / capacity()));
  st->print("|G %3d%%", (int) ((double) get_gclab_allocs() * 100 / capacity()));
  st->print("|S %3d%%", (int) ((double) get_shared_allocs() * 100 / capacity()));
  st->print("|L %3d%%", (int) ((double) get_live_data_bytes() * 100 / capacity()));
  if (is_humongous_start()) {
    st->print("|H ");
  } else if (is_humongous_continuation()) {
    st->print("|HC");
  } else {
    st->print("|  ");
  }
  if (in_collection_set()) {
    st->print("|CS");
  } else {
    st->print("|  ");
  }
  st->print("|CP %3d", _critical_pins);

  st->print_cr("|TAMS " PTR_FORMAT ", " PTR_FORMAT "|",
               p2i(ShenandoahHeap::heap()->complete_top_at_mark_start(_bottom)),
               p2i(ShenandoahHeap::heap()->next_top_at_mark_start(_bottom)));
}


class SkipUnreachableObjectToOopClosure: public ObjectClosure {
  ExtendedOopClosure* _cl;
  bool _skip_unreachable_objects;
  ShenandoahHeap* _heap;

public:
  SkipUnreachableObjectToOopClosure(ExtendedOopClosure* cl, bool skip_unreachable_objects) :
    _cl(cl), _skip_unreachable_objects(skip_unreachable_objects), _heap(ShenandoahHeap::heap()) {}

  void do_object(oop obj) {

    if ((! _skip_unreachable_objects) || _heap->is_marked_complete(obj)) {
#ifdef ASSERT
      if (_skip_unreachable_objects) {
        assert(_heap->is_marked_complete(obj), "obj must be live");
      }
#endif
      obj->oop_iterate(_cl);
    }

  }
};

void ShenandoahHeapRegion::object_iterate_interruptible(ObjectClosure* blk, bool allow_cancel) {
  HeapWord* p = bottom() + BrooksPointer::word_size();
  while (p < top() && !(allow_cancel && _heap->cancelled_concgc())) {
    blk->do_object(oop(p));
    p += oop(p)->size() + BrooksPointer::word_size();
  }
}

HeapWord* ShenandoahHeapRegion::object_iterate_careful(ObjectClosureCareful* blk) {
  HeapWord * limit = concurrent_iteration_safe_limit();
  assert(limit <= top(), "sanity check");
  for (HeapWord* p = bottom() + BrooksPointer::word_size(); p < limit;) {
    size_t size = blk->do_object_careful(oop(p));
    if (size == 0) {
      return p;  // failed at p
    }
    p += size + BrooksPointer::word_size();
  }
  return NULL; // all done
}

void ShenandoahHeapRegion::oop_iterate_skip_unreachable(ExtendedOopClosure* cl, bool skip_unreachable_objects) {
  SkipUnreachableObjectToOopClosure cl2(cl, skip_unreachable_objects);
  object_iterate_interruptible(&cl2, false);
}

void ShenandoahHeapRegion::fill_region() {
  ShenandoahHeap* sh = ShenandoahHeap::heap();

  if (free() > (BrooksPointer::word_size() + CollectedHeap::min_fill_size())) {
    HeapWord* filler = allocate(BrooksPointer::word_size(), ShenandoahHeap::_alloc_shared);
    HeapWord* obj = allocate(end() - top(), ShenandoahHeap::_alloc_shared);
    sh->fill_with_object(obj, end() - obj);
    BrooksPointer::initialize(oop(obj));
  }
}

void ShenandoahHeapRegion::set_humongous_start(bool start) {
  _humongous_start = start;
}

void ShenandoahHeapRegion::set_humongous_continuation(bool continuation) {
  _humongous_continuation = continuation;
}

bool ShenandoahHeapRegion::is_humongous() const {
  return _humongous_start || _humongous_continuation;
}

bool ShenandoahHeapRegion::is_humongous_start() const {
  return _humongous_start;
}

bool ShenandoahHeapRegion::is_humongous_continuation() const {
  return _humongous_continuation;
}

ShenandoahHeapRegion* ShenandoahHeapRegion::humongous_start_region() const {
  assert(is_humongous(), "Must be a part of the humongous region");
  size_t reg_num = region_number();
  ShenandoahHeapRegion* r = const_cast<ShenandoahHeapRegion*>(this);
  while (!r->is_humongous_start()) {
    assert(reg_num > 0, "Sanity");
    reg_num --;
    r = _heap->regions()->get(reg_num);
    assert(r->is_humongous(), "Must be a part of the humongous region");
  }
  assert(r->is_humongous_start(), "Must be");
  return r;
}

void ShenandoahHeapRegion::recycle() {
  ContiguousSpace::clear(false);
  if (ZapUnusedHeapArea) {
    ContiguousSpace::mangle_unused_area_complete();
  }
  clear_live_data();
  _humongous_start = false;
  _humongous_continuation = false;
  reset_alloc_stats();
  // Reset C-TAMS pointer to ensure size-based iteration, everything
  // in that regions is going to be new objects.
  _heap->set_complete_top_at_mark_start(bottom(), bottom());
  // We can only safely reset the C-TAMS pointer if the bitmap is clear for that region.
  assert(_heap->is_complete_bitmap_clear_range(bottom(), end()), "must be clear");
}

HeapWord* ShenandoahHeapRegion::block_start_const(const void* p) const {
  assert(MemRegion(bottom(), end()).contains(p),
         err_msg("p ("PTR_FORMAT") not in space ["PTR_FORMAT", "PTR_FORMAT")",
		 p2i(p), p2i(bottom()), p2i(end())));
  if (p >= top()) {
    return top();
  } else {
    HeapWord* last = bottom() + BrooksPointer::word_size();
    HeapWord* cur = last;
    while (cur <= p) {
      last = cur;
      cur += oop(cur)->size() + BrooksPointer::word_size();
    }
    assert(oop(last)->is_oop(),
           err_msg(PTR_FORMAT" should be an object start", p2i(last)));
    return last;
  }
}

void ShenandoahHeapRegion::setup_heap_region_size(size_t initial_heap_size, size_t max_heap_size) {
  // Absolute minimums we should not ever break:
  static const size_t MIN_REGION_SIZE = 256*K;
  static const size_t MIN_NUM_REGIONS = 10;

  uintx region_size;
  if (FLAG_IS_DEFAULT(ShenandoahHeapRegionSize)) {
    if (ShenandoahMinRegionSize > initial_heap_size / MIN_NUM_REGIONS) {
      err_msg message("Initial heap size (" SIZE_FORMAT "K) is too low to afford the minimum number "
                      "of regions (" SIZE_FORMAT ") of minimum region size (" SIZE_FORMAT "K).",
                      initial_heap_size/K, MIN_NUM_REGIONS, ShenandoahMinRegionSize/K);
      vm_exit_during_initialization("Invalid -XX:ShenandoahMinRegionSize option", message);
    }
    if (ShenandoahMinRegionSize < MIN_REGION_SIZE) {
      err_msg message("" SIZE_FORMAT "K should not be lower than minimum region size (" SIZE_FORMAT "K).",
                      ShenandoahMinRegionSize/K,  MIN_REGION_SIZE/K);
      vm_exit_during_initialization("Invalid -XX:ShenandoahMinRegionSize option", message);
    }
    if (ShenandoahMinRegionSize < MinTLABSize) {
      err_msg message("" SIZE_FORMAT "K should not be lower than TLAB size size (" SIZE_FORMAT "K).",
                      ShenandoahMinRegionSize/K,  MinTLABSize/K);
      vm_exit_during_initialization("Invalid -XX:ShenandoahMinRegionSize option", message);
    }
    if (ShenandoahMaxRegionSize < MIN_REGION_SIZE) {
      err_msg message("" SIZE_FORMAT "K should not be lower than min region size (" SIZE_FORMAT "K).",
                      ShenandoahMaxRegionSize/K,  MIN_REGION_SIZE/K);
      vm_exit_during_initialization("Invalid -XX:ShenandoahMaxRegionSize option", message);
    }
    if (ShenandoahMinRegionSize > ShenandoahMaxRegionSize) {
      err_msg message("Minimum (" SIZE_FORMAT "K) should be larger than maximum (" SIZE_FORMAT "K).",
                      ShenandoahMinRegionSize/K, ShenandoahMaxRegionSize/K);
      vm_exit_during_initialization("Invalid -XX:ShenandoahMinRegionSize or -XX:ShenandoahMaxRegionSize", message);
    }
    size_t average_heap_size = (initial_heap_size + max_heap_size) / 2;
    region_size = MAX2(average_heap_size / ShenandoahTargetNumRegions,
                       ShenandoahMinRegionSize);

    // Now make sure that we don't go over or under our limits.
    region_size = MAX2(ShenandoahMinRegionSize, region_size);
    region_size = MIN2(ShenandoahMaxRegionSize, region_size);

  } else {
    if (ShenandoahHeapRegionSize > initial_heap_size / MIN_NUM_REGIONS) {
      err_msg message("Initial heap size (" SIZE_FORMAT "K) is too low to afford the minimum number "
                              "of regions (" SIZE_FORMAT ") of requested size (" SIZE_FORMAT "K).",
                      initial_heap_size/K, MIN_NUM_REGIONS, ShenandoahHeapRegionSize/K);
      vm_exit_during_initialization("Invalid -XX:ShenandoahHeapRegionSize option", message);
    }
    if (ShenandoahHeapRegionSize < ShenandoahMinRegionSize) {
      err_msg message("Heap region size (" SIZE_FORMAT "K) should be larger than min region size (" SIZE_FORMAT "K).",
                      ShenandoahHeapRegionSize/K, ShenandoahMinRegionSize/K);
      vm_exit_during_initialization("Invalid -XX:ShenandoahHeapRegionSize option", message);
    }
    if (ShenandoahHeapRegionSize > ShenandoahMaxRegionSize) {
      err_msg message("Heap region size (" SIZE_FORMAT "K) should be lower than max region size (" SIZE_FORMAT "K).",
                      ShenandoahHeapRegionSize/K, ShenandoahMaxRegionSize/K);
      vm_exit_during_initialization("Invalid -XX:ShenandoahHeapRegionSize option", message);
    }
    region_size = ShenandoahHeapRegionSize;
  }

  // Make sure region size is at least one large page, if enabled.
  // Otherwise, mem-protecting one region may falsely protect the adjacent
  // regions too.
  if (UseLargePages) {
    region_size = MAX2(region_size, os::large_page_size());
  }

  int region_size_log = log2_long((jlong) region_size);
  // Recalculate the region size to make sure it's a power of
  // 2. This means that region_size is the largest power of 2 that's
  // <= what we've calculated so far.
  region_size = ((uintx)1 << region_size_log);

  // Now, set up the globals.
  guarantee(RegionSizeShift == 0, "we should only set it once");
  RegionSizeShift = (size_t)region_size_log;

  guarantee(RegionSizeBytes == 0, "we should only set it once");
  RegionSizeBytes = (size_t)region_size;
  RegionSizeWords = RegionSizeBytes >> LogHeapWordSize;
  assert (RegionSizeWords*HeapWordSize == RegionSizeBytes, "sanity");

  log_info(gc, heap)("Heap region size: " SIZE_FORMAT "M", RegionSizeBytes / M);
  log_info(gc, init)("Region size in bytes: "SIZE_FORMAT, RegionSizeBytes);
  log_info(gc, init)("Region size shift: "SIZE_FORMAT, RegionSizeShift);
  log_info(gc, init)("Initial number of regions: "SIZE_FORMAT, initial_heap_size / RegionSizeBytes);
  log_info(gc, init)("Maximum number of regions: "SIZE_FORMAT, max_heap_size / RegionSizeBytes);
}

void ShenandoahHeapRegion::pin() {
  assert(! SafepointSynchronize::is_at_safepoint(), "only outside safepoints");
  assert(_critical_pins >= 0, "sanity");
  Atomic::inc(&_critical_pins);
}

void ShenandoahHeapRegion::unpin() {
  assert(! SafepointSynchronize::is_at_safepoint(), "only outside safepoints");
  Atomic::dec(&_critical_pins);
  assert(_critical_pins >= 0, "sanity");
}

bool ShenandoahHeapRegion::is_pinned() {
  jint v = OrderAccess::load_acquire(&_critical_pins);
  assert(v >= 0, "sanity");
  return v > 0;
}
