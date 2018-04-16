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

size_t ShenandoahHeapRegion::RegionSizeBytes = 0;
size_t ShenandoahHeapRegion::RegionSizeWords = 0;
size_t ShenandoahHeapRegion::RegionSizeBytesShift = 0;
size_t ShenandoahHeapRegion::RegionSizeWordsShift = 0;
size_t ShenandoahHeapRegion::RegionSizeBytesMask = 0;
size_t ShenandoahHeapRegion::RegionSizeWordsMask = 0;
size_t ShenandoahHeapRegion::HumongousThresholdBytes = 0;
size_t ShenandoahHeapRegion::HumongousThresholdWords = 0;
size_t ShenandoahHeapRegion::MaxTLABSizeBytes = 0;

ShenandoahHeapRegion::ShenandoahHeapRegion(ShenandoahHeap* heap, HeapWord* start,
                                           size_t size_words, size_t index, bool committed) :
  _heap(heap),
  _pacer(ShenandoahPacing ? heap->pacer() : NULL),
  _region_number(index),
  _live_data(0),
  _tlab_allocs(0),
  _gclab_allocs(0),
  _shared_allocs(0),
  _reserved(MemRegion(start, size_words)),
  _new_top(NULL),
  _state(committed ? _empty_committed : _empty_uncommitted),
  _empty_time(os::elapsedTime()),
  _critical_pins(0) {

  ContiguousSpace::initialize(_reserved, true, committed);
}

size_t ShenandoahHeapRegion::region_number() const {
  return _region_number;
}

void ShenandoahHeapRegion::report_illegal_transition(const char *method) {
  ResourceMark rm;
  stringStream ss;
  ss.print("Illegal region state transition from \"%s\", at %s\n  ", region_state_to_string(_state), method);
  print_on(&ss);
  fatal(ss.as_string());
}

void ShenandoahHeapRegion::make_regular_allocation() {
  _heap->assert_heaplock_owned_by_current_thread();
  switch (_state) {
    case _empty_uncommitted:
      do_commit();
    case _empty_committed:
      _state = _regular;
    case _regular:
    case _pinned:
      return;
    default:
      report_illegal_transition("regular allocation");
  }
}

void ShenandoahHeapRegion::make_regular_bypass() {
  _heap->assert_heaplock_owned_by_current_thread();
  assert (_heap->is_full_gc_in_progress(), "only for full GC");

  switch (_state) {
    case _empty_uncommitted:
      do_commit();
    case _empty_committed:
    case _cset:
    case _humongous_start:
    case _humongous_cont:
      _state = _regular;
      return;
    case _pinned_cset:
      _state = _pinned;
      return;
    case _regular:
    case _pinned:
      return;
    default:
      report_illegal_transition("regular bypass");
  }
}

void ShenandoahHeapRegion::make_humongous_start() {
  _heap->assert_heaplock_owned_by_current_thread();
  switch (_state) {
    case _empty_uncommitted:
      do_commit();
    case _empty_committed:
      _state = _humongous_start;
      return;
    default:
      report_illegal_transition("humongous start allocation");
  }
}

void ShenandoahHeapRegion::make_humongous_start_bypass() {
  _heap->assert_heaplock_owned_by_current_thread();
  assert (_heap->is_full_gc_in_progress(), "only for full GC");

  switch (_state) {
    case _empty_committed:
    case _regular:
    case _humongous_start:
    case _humongous_cont:
      _state = _humongous_start;
      return;
    default:
      report_illegal_transition("humongous start bypass");
  }
}

void ShenandoahHeapRegion::make_humongous_cont() {
  _heap->assert_heaplock_owned_by_current_thread();
  switch (_state) {
    case _empty_uncommitted:
      do_commit();
    case _empty_committed:
      _state = _humongous_cont;
      return;
    default:
      report_illegal_transition("humongous continuation allocation");
  }
}

void ShenandoahHeapRegion::make_humongous_cont_bypass() {
  _heap->assert_heaplock_owned_by_current_thread();
  assert (_heap->is_full_gc_in_progress(), "only for full GC");

  switch (_state) {
    case _empty_committed:
    case _regular:
    case _humongous_start:
    case _humongous_cont:
      _state = _humongous_cont;
      return;
    default:
      report_illegal_transition("humongous continuation bypass");
  }
}

void ShenandoahHeapRegion::make_pinned() {
  _heap->assert_heaplock_owned_by_current_thread();
  switch (_state) {
    case _regular:
      assert (_critical_pins == 0, "sanity");
      _state = _pinned;
    case _pinned_cset:
    case _pinned:
      _critical_pins++;
      return;
    case _humongous_start:
      assert (_critical_pins == 0, "sanity");
      _state = _pinned_humongous_start;
    case _pinned_humongous_start:
      _critical_pins++;
      return;
    case _cset:
      guarantee(_heap->cancelled_concgc(), "only valid when evac has been cancelled");
      assert (_critical_pins == 0, "sanity");
      _state = _pinned_cset;
      _critical_pins++;
      return;
    default:
      report_illegal_transition("pinning");
  }
}

void ShenandoahHeapRegion::make_unpinned() {
  _heap->assert_heaplock_owned_by_current_thread();
  switch (_state) {
    case _pinned:
      assert (_critical_pins > 0, "sanity");
      _critical_pins--;
      if (_critical_pins == 0) {
        _state = _regular;
      }
      return;
    case _regular:
    case _humongous_start:
      assert (_critical_pins == 0, "sanity");
      return;
    case _pinned_cset:
      guarantee(_heap->cancelled_concgc(), "only valid when evac has been cancelled");
      assert (_critical_pins > 0, "sanity");
      _critical_pins--;
      if (_critical_pins == 0) {
        _state = _cset;
      }
      return;
    case _pinned_humongous_start:
      assert (_critical_pins > 0, "sanity");
      _critical_pins--;
      if (_critical_pins == 0) {
        _state = _humongous_start;
      }
      return;
    default:
      report_illegal_transition("unpinning");
  }
}

void ShenandoahHeapRegion::make_cset() {
  _heap->assert_heaplock_owned_by_current_thread();
  switch (_state) {
    case _regular:
      _state = _cset;
    case _cset:
      return;
    default:
      report_illegal_transition("cset");
  }
}

void ShenandoahHeapRegion::make_trash() {
  _heap->assert_heaplock_owned_by_current_thread();
  switch (_state) {
    case _cset:
      // Reclaiming cset regions
    case _humongous_start:
    case _humongous_cont:
      // Reclaiming humongous regions
    case _regular:
      // Immediate region reclaim
      _state = _trash;
      return;
    default:
      report_illegal_transition("trashing");
  }
}

void ShenandoahHeapRegion::make_empty() {
  _heap->assert_heaplock_owned_by_current_thread();
  switch (_state) {
    case _trash:
      _state = _empty_committed;
      _empty_time = os::elapsedTime();
      return;
    default:
      report_illegal_transition("emptying");
  }
}

void ShenandoahHeapRegion::make_uncommitted() {
  _heap->assert_heaplock_owned_by_current_thread();
  switch (_state) {
    case _empty_committed:
      do_uncommit();
      _state = _empty_uncommitted;
      return;
    default:
      report_illegal_transition("uncommiting");
  }
}

void ShenandoahHeapRegion::make_committed_bypass() {
  _heap->assert_heaplock_owned_by_current_thread();
  assert (_heap->is_full_gc_in_progress(), "only for full GC");

  switch (_state) {
    case _empty_uncommitted:
      do_commit();
      _state = _empty_committed;
      return;
    default:
      report_illegal_transition("commit bypass");
  }
}

bool ShenandoahHeapRegion::rollback_allocation(uint size) {
  set_top(top() - size);
  return true;
}

void ShenandoahHeapRegion::clear_live_data() {
  OrderAccess::release_store_fence((volatile jint*)&_live_data, 0);
}

void ShenandoahHeapRegion::reset_alloc_metadata() {
  _tlab_allocs = 0;
  _gclab_allocs = 0;
  _shared_allocs = 0;
}

void ShenandoahHeapRegion::reset_alloc_metadata_to_shared() {
  if (used() > 0) {
    _tlab_allocs = 0;
    _gclab_allocs = 0;
    _shared_allocs = used() >> LogHeapWordSize;
  } else {
    reset_alloc_metadata();
  }
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
  size_t v = s >> LogHeapWordSize;
  assert(v < max_jint, "sanity");
  _live_data = (jint)v;
}

size_t ShenandoahHeapRegion::get_live_data_words() const {
  jint v = OrderAccess::load_acquire((volatile jint*)&_live_data);
  assert(v >= 0, "sanity");
  return (size_t)v;
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
  st->print("|");
  st->print(SIZE_FORMAT_W(5), this->_region_number);

  switch (_state) {
    case _empty_uncommitted:
      st->print("|EU ");
      break;
    case _empty_committed:
      st->print("|EC ");
      break;
    case _regular:
      st->print("|R  ");
      break;
    case _humongous_start:
      st->print("|H  ");
      break;
    case _pinned_humongous_start:
      st->print("|HP ");
      break;
    case _humongous_cont:
      st->print("|HC ");
      break;
    case _cset:
      st->print("|CS ");
      break;
    case _trash:
      st->print("|T  ");
      break;
    case _pinned:
      st->print("|P  ");
      break;
    case _pinned_cset:
      st->print("|CSP");
      break;
    default:
      ShouldNotReachHere();
  }
  st->print("|BTE " INTPTR_FORMAT_W(12) ", " INTPTR_FORMAT_W(12) ", " INTPTR_FORMAT_W(12),
            p2i(bottom()), p2i(top()), p2i(end()));
  st->print("|TAMS " INTPTR_FORMAT_W(12) ", " INTPTR_FORMAT_W(12),
            p2i(_heap->complete_top_at_mark_start(_bottom)),
            p2i(_heap->next_top_at_mark_start(_bottom)));
  st->print("|U %3d%%", (int) ((double) used() * 100 / capacity()));
  st->print("|T %3d%%", (int) ((double) get_tlab_allocs() * 100 / capacity()));
  st->print("|G %3d%%", (int) ((double) get_gclab_allocs() * 100 / capacity()));
  st->print("|S %3d%%", (int) ((double) get_shared_allocs() * 100 / capacity()));
  st->print("|L %3d%%", (int) ((double) get_live_data_bytes() * 100 / capacity()));
  st->print("|CP " SIZE_FORMAT_W(3), _critical_pins);

  st->cr();
}


class ShenandoahSkipUnreachableObjectToOopClosure: public ObjectClosure {
  ExtendedOopClosure* _cl;
  bool _skip_unreachable_objects;
  ShenandoahHeap* _heap;

public:
  ShenandoahSkipUnreachableObjectToOopClosure(ExtendedOopClosure* cl, bool skip_unreachable_objects) :
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

void ShenandoahHeapRegion::fill_region() {
  if (free() > (BrooksPointer::word_size() + CollectedHeap::min_fill_size())) {
    HeapWord* filler = allocate(BrooksPointer::word_size(), ShenandoahHeap::_alloc_shared);
    HeapWord* obj = allocate(end() - top(), ShenandoahHeap::_alloc_shared);
    _heap->fill_with_object(obj, end() - obj);
    BrooksPointer::initialize(oop(obj));
  }
}

ShenandoahHeapRegion* ShenandoahHeapRegion::humongous_start_region() const {
  assert(is_humongous(), "Must be a part of the humongous region");
  size_t reg_num = region_number();
  ShenandoahHeapRegion* r = const_cast<ShenandoahHeapRegion*>(this);
  while (!r->is_humongous_start()) {
    assert(reg_num > 0, "Sanity");
    reg_num --;
    r = _heap->get_region(reg_num);
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
  reset_alloc_metadata();
  // Reset C-TAMS pointer to ensure size-based iteration, everything
  // in that regions is going to be new objects.
  _heap->set_complete_top_at_mark_start(bottom(), bottom());
  // We can only safely reset the C-TAMS pointer if the bitmap is clear for that region.
  assert(_heap->is_complete_bitmap_clear_range(bottom(), end()), "must be clear");

  make_empty();
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

  if (1 > ShenandoahHumongousThreshold || ShenandoahHumongousThreshold > 100) {
    vm_exit_during_initialization("Invalid -XX:ShenandoahHumongousThreshold option, should be within [1..100]");
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
  guarantee(RegionSizeBytesShift == 0, "we should only set it once");
  RegionSizeBytesShift = (size_t)region_size_log;

  guarantee(RegionSizeWordsShift == 0, "we should only set it once");
  RegionSizeWordsShift = RegionSizeBytesShift - LogHeapWordSize;

  guarantee(RegionSizeBytes == 0, "we should only set it once");
  RegionSizeBytes = (size_t)region_size;
  RegionSizeWords = RegionSizeBytes >> LogHeapWordSize;
  assert (RegionSizeWords*HeapWordSize == RegionSizeBytes, "sanity");

  guarantee(RegionSizeWordsMask == 0, "we should only set it once");
  RegionSizeWordsMask = RegionSizeWords - 1;

  guarantee(RegionSizeBytesMask == 0, "we should only set it once");
  RegionSizeBytesMask = RegionSizeBytes - 1;

  guarantee(HumongousThresholdWords == 0, "we should only set it once");
  HumongousThresholdWords = RegionSizeWords * ShenandoahHumongousThreshold / 100;
  assert (HumongousThresholdWords <= RegionSizeWords, "sanity");

  guarantee(HumongousThresholdBytes == 0, "we should only set it once");
  HumongousThresholdBytes = HumongousThresholdWords * HeapWordSize;
  assert (HumongousThresholdBytes <= RegionSizeBytes, "sanity");

  // The rationale for trimming the TLAB sizes has to do with the raciness in
  // TLAB allocation machinery. It may happen that TLAB sizing policy polls Shenandoah
  // about next free size, gets the answer for region #N, goes away for a while, then
  // tries to allocate in region #N, and fail because some other thread have claimed part
  // of the region #N, and then the freeset allocation code has to retire the region #N,
  // before moving the allocation to region #N+1.
  //
  // The worst case realizes when "answer" is "region size", which means it could
  // prematurely retire an entire region. Having smaller TLABs does not fix that
  // completely, but reduces the probability of too wasteful region retirement.
  // With current divisor, we will waste no more than 1/8 of region size in the worst
  // case. This also has a secondary effect on collection set selection: even under
  // the race, the regions would be at least 7/8 used, which allows relying on
  // "used" - "live" for cset selection. Otherwise, we can get the fragmented region
  // below the garbage threshold that would never be considered for collection.
  guarantee(MaxTLABSizeBytes == 0, "we should only set it once");
  MaxTLABSizeBytes = MIN2(RegionSizeBytes / 8, HumongousThresholdBytes);
  assert (MaxTLABSizeBytes > MinTLABSize, "should be larger");

  log_info(gc, heap)("Heap region size: " SIZE_FORMAT "M", RegionSizeBytes / M);
  log_info(gc, init)("Region size in bytes: "SIZE_FORMAT, RegionSizeBytes);
  log_info(gc, init)("Region size byte shift: "SIZE_FORMAT, RegionSizeBytesShift);
  log_info(gc, init)("Humongous threshold in bytes: "SIZE_FORMAT, HumongousThresholdBytes);
  log_info(gc, init)("Max TLAB size in bytes: "SIZE_FORMAT, MaxTLABSizeBytes);
  log_info(gc, init)("Number of regions: "SIZE_FORMAT, max_heap_size / RegionSizeBytes);
}

void ShenandoahHeapRegion::do_commit() {
  if (!os::commit_memory((char *) _reserved.start(), _reserved.byte_size(), false)) {
    report_java_out_of_memory("Unable to commit region");
  }
  if (!_heap->commit_bitmap_slice(this)) {
    report_java_out_of_memory("Unable to commit bitmaps for region");
  }
  _heap->increase_committed(ShenandoahHeapRegion::region_size_bytes());
}

void ShenandoahHeapRegion::do_uncommit() {
  if (!os::uncommit_memory((char *) _reserved.start(), _reserved.byte_size())) {
    report_java_out_of_memory("Unable to uncommit region");
  }
  if (!_heap->uncommit_bitmap_slice(this)) {
    report_java_out_of_memory("Unable to uncommit bitmaps for region");
  }
  _heap->decrease_committed(ShenandoahHeapRegion::region_size_bytes());
}
