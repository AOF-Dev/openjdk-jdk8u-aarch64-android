/*
 * Copyright (c) 2014, 2015, Red Hat, Inc. and/or its affiliates.
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
#include "code/codeCache.hpp"
#include "gc_implementation/shenandoah/shenandoahGCTraceTime.hpp"
#include "gc_implementation/shared/gcTimer.hpp"
#include "gc_implementation/shenandoah/brooksPointer.hpp"
#include "gc_implementation/shenandoah/shenandoahCollectorPolicy.hpp"
#include "gc_implementation/shenandoah/shenandoahConcurrentMark.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahCollectionSet.hpp"
#include "gc_implementation/shenandoah/shenandoahPhaseTimings.hpp"
#include "gc_implementation/shenandoah/shenandoahMarkCompact.hpp"
#include "gc_implementation/shenandoah/shenandoahBarrierSet.hpp"
#include "gc_implementation/shenandoah/shenandoahHeapRegionSet.hpp"
#include "gc_implementation/shenandoah/shenandoahHeap.hpp"
#include "gc_implementation/shenandoah/shenandoahHeap.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahRootProcessor.hpp"
#include "gc_implementation/shenandoah/shenandoahUtils.hpp"
#include "gc_implementation/shenandoah/shenandoahVerifier.hpp"
#include "gc_implementation/shenandoah/shenandoahWorkerPolicy.hpp"
#include "gc_implementation/shenandoah/vm_operations_shenandoah.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/biasedLocking.hpp"
#include "runtime/thread.hpp"
#include "utilities/copy.hpp"
#include "utilities/taskqueue.hpp"
#include "utilities/workgroup.hpp"

class ShenandoahMarkCompactBarrierSet : public ShenandoahBarrierSet {
public:
  ShenandoahMarkCompactBarrierSet(ShenandoahHeap* heap) : ShenandoahBarrierSet(heap) {}

  oop read_barrier(oop src) {
    return src;
  }

#ifdef ASSERT
  bool is_safe(oop o) {
    if (o == NULL) return true;
    return oopDesc::unsafe_equals(o, read_barrier(o));
  }

  bool is_safe(narrowOop o) {
    oop obj = oopDesc::decode_heap_oop(o);
    return is_safe(obj);
  }
#endif
};

class ShenandoahClearRegionStatusClosure: public ShenandoahHeapRegionClosure {
private:
  ShenandoahHeap* const _heap;

public:
  ShenandoahClearRegionStatusClosure() : _heap(ShenandoahHeap::heap()) {}

  bool heap_region_do(ShenandoahHeapRegion *r) {
    _heap->set_next_top_at_mark_start(r->bottom(), r->top());
    r->clear_live_data();
    r->set_concurrent_iteration_safe_limit(r->top());
    return false;
  }
};

class ShenandoahEnsureHeapActiveClosure: public ShenandoahHeapRegionClosure {
private:
  ShenandoahHeap* const _heap;

public:
  ShenandoahEnsureHeapActiveClosure() : _heap(ShenandoahHeap::heap()) {}
  bool heap_region_do(ShenandoahHeapRegion* r) {
    if (r->is_trash()) {
      r->recycle();
    }
    if (r->is_empty() || r->is_cset()) {
      r->make_regular_bypass();
    }
    assert (r->is_active(), "only active regions in heap now");

    // Record current region occupancy: this communicates empty regions are free
    // to the rest of Full GC code.
    r->set_new_top(r->top());
    return false;
  }
};

void ShenandoahMarkCompact::initialize() {
  _gc_timer = new (ResourceObj::C_HEAP, mtGC) STWGCTimer();
}

void ShenandoahMarkCompact::do_it(GCCause::Cause gc_cause) {
  ShenandoahHeap* heap = ShenandoahHeap::heap();

  {
    if (ShenandoahVerify) {
      heap->verifier()->verify_before_fullgc();
    }

    heap->set_full_gc_in_progress(true);

    assert(ShenandoahSafepoint::is_at_shenandoah_safepoint(), "must be at a safepoint");
    assert(Thread::current()->is_VM_thread(), "Do full GC only while world is stopped");

    {
      ShenandoahGCPhase phase(ShenandoahPhaseTimings::full_gc_heapdumps);
      heap->pre_full_gc_dump(_gc_timer);
    }

    {
      ShenandoahGCPhase phase(ShenandoahPhaseTimings::full_gc_prepare);
      // Full GC is supposed to recover from any GC state:

      // a. Cancel concurrent mark, if in progress
      if (heap->is_concurrent_mark_in_progress()) {
        heap->concurrentMark()->cancel();
        heap->stop_concurrent_marking();
      }
      assert(!heap->is_concurrent_mark_in_progress(), "sanity");

      // b1. Cancel evacuation, if in progress
      if (heap->is_evacuation_in_progress()) {
        heap->set_evacuation_in_progress_at_safepoint(false);
      }
      assert(!heap->is_evacuation_in_progress(), "sanity");

      // b2. Cancel update-refs, if in progress
      if (heap->is_update_refs_in_progress()) {
        heap->set_update_refs_in_progress(false);
      }
      assert(!heap->is_update_refs_in_progress(), "sanity");

      // c. Reset the bitmaps for new marking
      heap->reset_next_mark_bitmap();
      assert(heap->is_next_bitmap_clear(), "sanity");

      // d. Abandon reference discovery and clear all discovered references.
      ReferenceProcessor *rp = heap->ref_processor();
      rp->disable_discovery();
      rp->abandon_partial_discovery();
      rp->verify_no_references_recorded();

      {
        ShenandoahHeapLocker lock(heap->lock());

        // f. Make sure all regions are active. This is needed because we are potentially
        // sliding the data through them
        ShenandoahEnsureHeapActiveClosure ecl;
        heap->heap_region_iterate(&ecl, false, false);

        // g. Clear region statuses, including collection set status
        ShenandoahClearRegionStatusClosure cl;
        heap->heap_region_iterate(&cl, false, false);
      }
    }

    BarrierSet *old_bs = oopDesc::bs();
    ShenandoahMarkCompactBarrierSet bs(heap);
    oopDesc::set_bs(&bs);

    {
      if (UseTLAB) {
        heap->make_tlabs_parsable(true);
      }

      CodeCache::gc_prologue();

      // We should save the marks of the currently locked biased monitors.
      // The marking doesn't preserve the marks of biased objects.
      //BiasedLocking::preserve_marks();

      heap->set_has_forwarded_objects(true);

      OrderAccess::fence();

      phase1_mark_heap();

      heap->set_full_gc_move_in_progress(true);

      // Setup workers for the rest
      {
        OrderAccess::fence();

        // Initialize worker slices
        ShenandoahHeapRegionSet** worker_slices = NEW_C_HEAP_ARRAY(ShenandoahHeapRegionSet*, heap->max_workers(), mtGC);
        for (uint i = 0; i < heap->max_workers(); i++) {
          worker_slices[i] = new ShenandoahHeapRegionSet(heap->num_regions());
        }

        phase2_calculate_target_addresses(worker_slices);

        OrderAccess::fence();

        phase3_update_references();

        phase4_compact_objects(worker_slices);

        // Free worker slices
        for (uint i = 0; i < heap->max_workers(); i++) {
          delete worker_slices[i];
        }
        FREE_C_HEAP_ARRAY(ShenandoahHeapRegionSet*, worker_slices, mtGC);

        CodeCache::gc_epilogue();
        JvmtiExport::gc_epilogue();
      }

      heap->set_bytes_allocated_since_cm(0);

      heap->set_has_forwarded_objects(false);
      heap->set_full_gc_move_in_progress(false);
      heap->set_full_gc_in_progress(false);

      if (ShenandoahVerify) {
        heap->verifier()->verify_after_fullgc();
      }
    }

    {
      ShenandoahGCPhase phase(ShenandoahPhaseTimings::full_gc_heapdumps);
      heap->post_full_gc_dump(_gc_timer);
    }

    if (UseTLAB) {
      ShenandoahGCPhase phase(ShenandoahPhaseTimings::full_gc_resize_tlabs);
      heap->resize_all_tlabs();
    }

    oopDesc::set_bs(old_bs);
  }
}

void ShenandoahMarkCompact::phase1_mark_heap() {
  ShenandoahHeap* heap = ShenandoahHeap::heap();
  GCTraceTime time("Phase 1: Mark live objects", ShenandoahLogDebug, _gc_timer, heap->tracer()->gc_id());
  ShenandoahGCPhase mark_phase(ShenandoahPhaseTimings::full_gc_mark);

  ShenandoahConcurrentMark* cm = heap->concurrentMark();

  // Do not trust heuristics, because this can be our last resort collection.
  // Only ignore processing references and class unloading if explicitly disabled.
  cm->set_process_references(ShenandoahRefProcFrequency != 0);
  cm->set_unload_classes(ShenandoahUnloadClassesFrequency != 0);

  ReferenceProcessor* rp = heap->ref_processor();
  // enable ("weak") refs discovery
  rp->enable_discovery(true /*verify_no_refs*/, true);
  rp->setup_policy(true); // snapshot the soft ref policy to be used in this cycle
  rp->set_active_mt_degree(heap->workers()->active_workers());

  cm->update_roots(ShenandoahPhaseTimings::full_gc_roots);
  cm->mark_roots(ShenandoahPhaseTimings::full_gc_roots);
  cm->shared_finish_mark_from_roots(/* full_gc = */ true);

  heap->swap_mark_bitmaps();
}

class ShenandoahMCReclaimHumongousRegionClosure : public ShenandoahHeapRegionClosure {
private:
  ShenandoahHeap* const _heap;
public:
  ShenandoahMCReclaimHumongousRegionClosure() : _heap(ShenandoahHeap::heap()) {}

  bool heap_region_do(ShenandoahHeapRegion* r) {
    if (r->is_humongous_start()) {
      oop humongous_obj = oop(r->bottom() + BrooksPointer::word_size());
      if (!_heap->is_marked_complete(humongous_obj)) {
        _heap->trash_humongous_region_at(r);
      }
    }
    return false;
  }
};

class ShenandoahPrepareForCompactionObjectClosure : public ObjectClosure {
private:
  ShenandoahHeap*          const _heap;
  ShenandoahHeapRegionSet* const _empty_regions;
  ShenandoahHeapRegion*          _to_region;
  ShenandoahHeapRegion*          _from_region;
  HeapWord* _compact_point;

public:
  ShenandoahPrepareForCompactionObjectClosure(ShenandoahHeapRegionSet* empty_regions, ShenandoahHeapRegion* to_region) :
    _heap(ShenandoahHeap::heap()),
    _empty_regions(empty_regions),
    _to_region(to_region),
    _from_region(NULL),
    _compact_point(to_region->bottom()) {}

  void set_from_region(ShenandoahHeapRegion* from_region) {
    _from_region = from_region;
  }

  void finish_region() {
    assert(_to_region != NULL, "should not happen");
    _to_region->set_new_top(_compact_point);
  }

  bool is_compact_same_region() {
    return _from_region == _to_region;
  }

  void do_object(oop p) {
    assert(_from_region != NULL, "must set before work");
    assert(_heap->is_marked_complete(p), "must be marked");
    assert(!_heap->allocated_after_complete_mark_start((HeapWord*) p), "must be truly marked");

    size_t obj_size = p->size() + BrooksPointer::word_size();
    if (_compact_point + obj_size > _to_region->end()) {
      finish_region();

      // Object doesn't fit. Pick next empty region and start compacting there.
      ShenandoahHeapRegion* new_to_region = _empty_regions->current_then_next();

      // Out of empty region? Compact within the same region.
      if (new_to_region == NULL) {
        new_to_region = _from_region;
      }

      assert(new_to_region != _to_region, "must not reuse same to-region");
      assert(new_to_region != NULL, "must not be NULL");
      _to_region = new_to_region;
      _compact_point = _to_region->bottom();
    }

    // Object fits into current region, record new location:
    assert(_compact_point + obj_size <= _to_region->end(), "must fit");
    assert(oopDesc::unsafe_equals(p, ShenandoahBarrierSet::resolve_oop_static_not_null(p)),
           "expect forwarded oop");
    BrooksPointer::set_raw(p, _compact_point + BrooksPointer::word_size());
    _compact_point += obj_size;
  }
};

class ShenandoahPrepareForCompactionTask : public AbstractGangTask {
private:
  ShenandoahHeap*           const _heap;
  ShenandoahHeapRegionSet** const _worker_slices;
  ShenandoahHeapRegionSet*  const _heap_regions;

  ShenandoahHeapRegion* next_from_region(ShenandoahHeapRegionSet* slice) {
    ShenandoahHeapRegion* from_region = _heap_regions->claim_next();

    while (from_region != NULL && (!from_region->is_move_allowed() || from_region->is_humongous())) {
      from_region = _heap_regions->claim_next();
    }

    if (from_region != NULL) {
      assert(slice != NULL, "sanity");
      assert(!from_region->is_humongous(), "this path cannot handle humongous regions");
      assert(from_region->is_move_allowed(), "only regions that can be moved in mark-compact");
      slice->add_region(from_region);
    }

    return from_region;
  }

public:
  ShenandoahPrepareForCompactionTask(ShenandoahHeapRegionSet** worker_slices) :
          AbstractGangTask("Shenandoah Prepare For Compaction Task"),
          _heap(ShenandoahHeap::heap()), _heap_regions(_heap->regions()), _worker_slices(worker_slices) {
    _heap_regions->clear_current_index();
  }

  void work(uint worker_id) {
    ShenandoahHeapRegionSet* slice = _worker_slices[worker_id];
    ShenandoahHeapRegion* from_region = next_from_region(slice);

    // No work?
    if (from_region == NULL) {
      return;
    }

    // Sliding compaction. Walk all regions in the slice, and compact them.
    // Remember empty regions and reuse them as needed.
    ShenandoahHeapRegionSet empty_regions(_heap->num_regions());
    ShenandoahPrepareForCompactionObjectClosure cl(&empty_regions, from_region);
    while (from_region != NULL) {
      cl.set_from_region(from_region);
      _heap->marked_object_iterate(from_region, &cl);

      // Compacted the region to somewhere else? From-region is empty then.
      if (!cl.is_compact_same_region()) {
        empty_regions.add_region(from_region);
      }
      from_region = next_from_region(slice);
    }
    cl.finish_region();

    // Mark all remaining regions as empty
    ShenandoahHeapRegion* r = empty_regions.current_then_next();
    while (r != NULL) {
      r->set_new_top(r->bottom());
      r = empty_regions.current_then_next();
    }
  }
};

void ShenandoahMarkCompact::calculate_target_humongous_objects() {
  ShenandoahHeap* heap = ShenandoahHeap::heap();

  // Compute the new addresses for humongous objects. We need to do this after addresses
  // for regular objects are calculated, and we know what regions in heap suffix are
  // available for humongous moves.
  //
  // Scan the heap backwards, because we are compacting humongous regions towards the end.
  // Maintain the contiguous compaction window in [to_begin; to_end), so that we can slide
  // humongous start there.
  //
  // The complication is potential non-movable regions during the scan. If such region is
  // detected, then sliding restarts towards that non-movable region.

  size_t to_begin = heap->num_regions();
  size_t to_end = heap->num_regions();

  for (size_t c = heap->num_regions() - 1; c > 0; c--) {
    ShenandoahHeapRegion *r = heap->regions()->get(c);
    if (r->is_humongous_continuation() || (r->new_top() == r->bottom())) {
      // To-region candidate: record this, and continue scan
      to_begin = r->region_number();
      continue;
    }

    if (r->is_humongous_start() && r->is_move_allowed()) {
      // From-region candidate: movable humongous region
      oop old_obj = oop(r->bottom() + BrooksPointer::word_size());
      size_t words_size = old_obj->size() + BrooksPointer::word_size();
      size_t num_regions = ShenandoahHeapRegion::required_regions(words_size * HeapWordSize);

      size_t start = to_end - num_regions;

      if (start >= to_begin && start != r->region_number()) {
        // Fits into current window, and the move is non-trivial. Record the move then, and continue scan.
        BrooksPointer::set_raw(old_obj, heap->regions()->get(start)->bottom() + BrooksPointer::word_size());
        to_end = start;
        continue;
      }
    }

    // Failed to fit. Scan starting from current region.
    to_begin = r->region_number();
    to_end = r->region_number();
  }
}

void ShenandoahMarkCompact::phase2_calculate_target_addresses(ShenandoahHeapRegionSet** worker_slices) {
  ShenandoahHeap* heap = ShenandoahHeap::heap();
  GCTraceTime time("Phase 2: Compute new object addresses", ShenandoahLogDebug, _gc_timer, heap->tracer()->gc_id());
  ShenandoahGCPhase calculate_address_phase(ShenandoahPhaseTimings::full_gc_calculate_addresses);

  {
    ShenandoahGCPhase phase(ShenandoahPhaseTimings::full_gc_calculate_addresses_regular);

    {
      ShenandoahHeapLocker lock(heap->lock());

      ShenandoahMCReclaimHumongousRegionClosure cl;
      heap->heap_region_iterate(&cl);

      // After some humongous regions were reclaimed, we need to ensure their
      // backing storage is active. This is needed because we are potentially
      // sliding the data through them.
      ShenandoahEnsureHeapActiveClosure ecl;
      heap->heap_region_iterate(&ecl, false, false);
    }

    // Compute the new addresses for regular objects
    ShenandoahPrepareForCompactionTask prepare_task(worker_slices);
    heap->workers()->run_task(&prepare_task);
  }

  // Compute the new addresses for humongous objects
  {
    ShenandoahGCPhase phase(ShenandoahPhaseTimings::full_gc_calculate_addresses_humong);
    calculate_target_humongous_objects();
  }
}

class ShenandoahAdjustPointersClosure : public MetadataAwareOopClosure {
private:
  ShenandoahHeap* const _heap;

  template <class T>
  inline void do_oop_work(T* p) {
    T o = oopDesc::load_heap_oop(p);
    if (! oopDesc::is_null(o)) {
      oop obj = oopDesc::decode_heap_oop_not_null(o);
      assert(_heap->is_marked_complete(obj), "must be marked");
      oop forw = oop(BrooksPointer::get_raw(obj));
      oopDesc::encode_store_heap_oop(p, forw);
    }
  }

public:
  ShenandoahAdjustPointersClosure() : _heap(ShenandoahHeap::heap()) {}

  void do_oop(oop* p)       { do_oop_work(p); }
  void do_oop(narrowOop* p) { do_oop_work(p); }
};

class ShenandoahAdjustPointersObjectClosure : public ObjectClosure {
private:
  ShenandoahHeap* const _heap;
  ShenandoahAdjustPointersClosure _cl;

public:
  ShenandoahAdjustPointersObjectClosure() :
    _heap(ShenandoahHeap::heap()) {
  }
  void do_object(oop p) {
    assert(_heap->is_marked_complete(p), "must be marked");
    p->oop_iterate(&_cl);
  }
};

class ShenandoahAdjustPointersTask : public AbstractGangTask {
private:
  ShenandoahHeap*          const _heap;
  ShenandoahHeapRegionSet* const _regions;

public:
  ShenandoahAdjustPointersTask() :
    AbstractGangTask("Shenandoah Adjust Pointers Task"),
    _heap(ShenandoahHeap::heap()), _regions(_heap->regions()) {
    _regions->clear_current_index();
  }

  void work(uint worker_id) {
    ShenandoahAdjustPointersObjectClosure obj_cl;
    ShenandoahHeapRegion* r = _regions->claim_next();
    while (r != NULL) {
      if (!r->is_humongous_continuation()) {
        _heap->marked_object_iterate(r, &obj_cl);
      }
      r = _regions->claim_next();
    }
  }
};

class ShenandoahAdjustRootPointersTask : public AbstractGangTask {
private:
  ShenandoahRootProcessor* _rp;

public:
  ShenandoahAdjustRootPointersTask(ShenandoahRootProcessor* rp) :
    AbstractGangTask("Shenandoah Adjust Root Pointers Task"),
    _rp(rp) {}

  void work(uint worker_id) {
    ShenandoahAdjustPointersClosure cl;
    CLDToOopClosure adjust_cld_closure(&cl, true);
    MarkingCodeBlobClosure adjust_code_closure(&cl,
                                             CodeBlobToOopClosure::FixRelocations);

    _rp->process_all_roots(&cl, &cl,
                           &adjust_cld_closure,
                           &adjust_code_closure, worker_id);
  }
};

void ShenandoahMarkCompact::phase3_update_references() {
  ShenandoahHeap* heap = ShenandoahHeap::heap();
  GCTraceTime time("Phase 3: Adjust pointers", ShenandoahLogDebug, _gc_timer, heap->tracer()->gc_id());
  ShenandoahGCPhase adjust_pointer_phase(ShenandoahPhaseTimings::full_gc_adjust_pointers);

  WorkGang* workers = heap->workers();
  uint nworkers = workers->active_workers();
  {
    COMPILER2_PRESENT(DerivedPointerTable::clear());
    ShenandoahRootProcessor rp(heap, nworkers, ShenandoahPhaseTimings::full_gc_roots);
    ShenandoahAdjustRootPointersTask task(&rp);
    workers->run_task(&task);
    COMPILER2_PRESENT(DerivedPointerTable::update_pointers());
  }

  ShenandoahAdjustPointersTask adjust_pointers_task;
  workers->run_task(&adjust_pointers_task);
}

class ShenandoahCompactObjectsClosure : public ObjectClosure {
private:
  ShenandoahHeap* const _heap;

public:
  ShenandoahCompactObjectsClosure() : _heap(ShenandoahHeap::heap()) {}

  void do_object(oop p) {
    assert(_heap->is_marked_complete(p), "must be marked");
    size_t size = (size_t)p->size();
    HeapWord* compact_to = BrooksPointer::get_raw(p);
    HeapWord* compact_from = (HeapWord*) p;
    if (compact_from != compact_to) {
      Copy::aligned_conjoint_words(compact_from, compact_to, size);
    }
    oop new_obj = oop(compact_to);
    BrooksPointer::initialize(new_obj);
  }
};

class ShenandoahCompactObjectsTask : public AbstractGangTask {
private:
  ShenandoahHeap* const _heap;
  ShenandoahHeapRegionSet** const _worker_slices;

public:
  ShenandoahCompactObjectsTask(ShenandoahHeapRegionSet** worker_slices) :
    AbstractGangTask("Shenandoah Compact Objects Task"),
    _heap(ShenandoahHeap::heap()),
    _worker_slices(worker_slices) {
  }

  void work(uint worker_id) {
    ShenandoahHeapRegionSet* slice = _worker_slices[worker_id];
    slice->clear_current_index();

    ShenandoahCompactObjectsClosure cl;
    ShenandoahHeapRegion* r = slice->current_then_next();
    while (r != NULL) {
      assert(!r->is_humongous(), "must not get humongous regions here");
      _heap->marked_object_iterate(r, &cl);
      r->set_top(r->new_top());
      r = slice->current_then_next();
    }
  }
};

class ShenandoahPostCompactClosure : public ShenandoahHeapRegionClosure {
private:
  ShenandoahHeap* const _heap;
  size_t _live;

public:
  ShenandoahPostCompactClosure() : _live(0), _heap(ShenandoahHeap::heap()) {
    _heap->clear_free_regions();
  }

  bool heap_region_do(ShenandoahHeapRegion* r) {
    assert (!r->is_cset(), "cset regions should have been demoted already");

    // Need to reset the complete-top-at-mark-start pointer here because
    // the complete marking bitmap is no longer valid. This ensures
    // size-based iteration in marked_object_iterate().
    _heap->set_complete_top_at_mark_start(r->bottom(), r->bottom());

    size_t live = r->used();

    // Reclaim regular regions that became empty
    if (r->is_regular() && live == 0) {
      r->make_trash();
    }

    // Recycle all trash regions
    if (r->is_trash()) {
      live = 0;
      r->recycle();
    }

    // Finally, add all suitable regions into the free set
    if (r->is_alloc_allowed()) {
      if (_heap->collection_set()->is_in(r)) {
        _heap->collection_set()->remove_region(r);
      }
      _heap->add_free_region(r);
    }

    r->set_live_data(live);
    r->reset_alloc_stats_to_shared();
    _live += live;
    return false;
  }

  size_t get_live() {
    return _live;
  }
};

void ShenandoahMarkCompact::compact_humongous_objects() {
  // Compact humongous regions, based on their fwdptr objects.
  //
  // This code is serial, because doing the in-slice parallel sliding is tricky. In most cases,
  // humongous regions are already compacted, and do not require further moves, which alleviates
  // sliding costs. We may consider doing this in parallel in future.

  ShenandoahHeap* heap = ShenandoahHeap::heap();

  for (size_t c = heap->num_regions() - 1; c > 0; c--) {
    ShenandoahHeapRegion* r = heap->regions()->get(c);
    if (r->is_humongous_start()) {
      oop old_obj = oop(r->bottom() + BrooksPointer::word_size());
      size_t words_size = old_obj->size() + BrooksPointer::word_size();
      size_t num_regions = ShenandoahHeapRegion::required_regions(words_size * HeapWordSize);

      size_t old_start = r->region_number();
      size_t old_end   = old_start + num_regions - 1;
      size_t new_start = heap->heap_region_index_containing(BrooksPointer::get_raw(old_obj));
      size_t new_end   = new_start + num_regions - 1;

      if (old_start == new_start) {
        // No need to move the object, it stays at the same slot
        continue;
      }

      assert (r->is_move_allowed(), "should be movable");

      Copy::aligned_conjoint_words(heap->regions()->get(old_start)->bottom(),
                                   heap->regions()->get(new_start)->bottom(),
                                   ShenandoahHeapRegion::region_size_words()*num_regions);

      oop new_obj = oop(heap->regions()->get(new_start)->bottom() + BrooksPointer::word_size());
      BrooksPointer::initialize(new_obj);

      {
        ShenandoahHeapLocker lock(heap->lock());

        for (size_t c = old_start; c <= old_end; c++) {
          ShenandoahHeapRegion* r = heap->regions()->get(c);
          r->make_regular_bypass();
          r->set_top(r->bottom());
        }

        for (size_t c = new_start; c <= new_end; c++) {
          ShenandoahHeapRegion* r = heap->regions()->get(c);
          if (c == new_start) {
            r->make_humongous_start_bypass();
          } else {
            r->make_humongous_cont_bypass();
          }

          // Trailing region may be non-full, record the remainder there
          size_t remainder = words_size & ShenandoahHeapRegion::region_size_words_mask();
          if ((c == new_end) && (remainder != 0)) {
            r->set_top(r->bottom() + remainder);
          } else {
            r->set_top(r->end());
          }
        }
      }
    }
  }
}


void ShenandoahMarkCompact::phase4_compact_objects(ShenandoahHeapRegionSet** worker_slices) {
  ShenandoahHeap* heap = ShenandoahHeap::heap();
  GCTraceTime time("Phase 4: Move objects", ShenandoahLogDebug, _gc_timer, heap->tracer()->gc_id());
  ShenandoahGCPhase compaction_phase(ShenandoahPhaseTimings::full_gc_copy_objects);

  // Compact regular objects first
  {
    ShenandoahGCPhase phase(ShenandoahPhaseTimings::full_gc_copy_objects_regular);
    ShenandoahCompactObjectsTask compact_task(worker_slices);
    heap->workers()->run_task(&compact_task);
  }

  // Compact humongous objects after regular object moves
  {
    ShenandoahGCPhase phase(ShenandoahPhaseTimings::full_gc_copy_objects_humong);
    compact_humongous_objects();
  }

  // Reset complete bitmap. We're about to reset the complete-top-at-mark-start pointer
  // and must ensure the bitmap is in sync.
  heap->reset_complete_mark_bitmap();

  // Bring regions in proper states after the collection, and set heap properties.
  {
    ShenandoahHeapLocker lock(heap->lock());
    ShenandoahPostCompactClosure post_compact;
    heap->heap_region_iterate(&post_compact);
    heap->set_used(post_compact.get_live());
  }

  heap->collection_set()->clear();
  heap->clear_cancelled_concgc();

  // Also clear the next bitmap in preparation for next marking.
  heap->reset_next_mark_bitmap();
}
