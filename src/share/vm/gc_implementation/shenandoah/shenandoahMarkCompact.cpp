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
#include "gc_implementation/shenandoah/shenandoahMarkCompact.hpp"
#include "gc_implementation/shenandoah/shenandoahBarrierSet.hpp"
#include "gc_implementation/shenandoah/shenandoahHeapRegionSet.hpp"
#include "gc_implementation/shenandoah/shenandoahHeap.hpp"
#include "gc_implementation/shenandoah/shenandoahHeap.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahRootProcessor.hpp"
#include "gc_implementation/shenandoah/shenandoahUtils.hpp"
#include "gc_implementation/shenandoah/shenandoahVerifier.hpp"
#include "gc_implementation/shenandoah/vm_operations_shenandoah.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/biasedLocking.hpp"
#include "runtime/thread.hpp"
#include "utilities/copy.hpp"
#include "utilities/taskqueue.hpp"
#include "utilities/workgroup.hpp"

class ShenandoahMarkCompactBarrierSet : public ShenandoahBarrierSet {
public:
  ShenandoahMarkCompactBarrierSet(ShenandoahHeap* heap) : ShenandoahBarrierSet(heap) {
  }
  oop read_barrier(oop src) {
    return src;
  }
#ifdef ASSERT
  bool is_safe(oop o) {
    if (o == NULL) return true;
    if (! oopDesc::unsafe_equals(o, read_barrier(o))) {
      return false;
    }
    return true;
  }
  bool is_safe(narrowOop o) {
    oop obj = oopDesc::decode_heap_oop(o);
    return is_safe(obj);
  }
#endif
};

class ShenandoahClearRegionStatusClosure: public ShenandoahHeapRegionClosure {
private:
  ShenandoahHeap* _heap;

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
  ShenandoahHeap* _heap;

public:
  ShenandoahEnsureHeapActiveClosure() : _heap(ShenandoahHeap::heap()) {}
  bool heap_region_do(ShenandoahHeapRegion* r) {
    if (r->is_trash()) {
      _heap->immediate_recycle(r);
    }
    if (r->is_empty()) {
      r->make_regular_bypass();
    }
    assert (r->is_active(), "only active regions in heap now");
    return false;
  }
};

STWGCTimer* ShenandoahMarkCompact::_gc_timer = NULL;

GCTimer* ShenandoahMarkCompact::gc_timer() {
  assert(_gc_timer != NULL, "Timer not yet initialized");
  return _gc_timer;
}

void ShenandoahMarkCompact::initialize() {
  _gc_timer = new (ResourceObj::C_HEAP, mtGC) STWGCTimer();
}

void ShenandoahMarkCompact::do_mark_compact(GCCause::Cause gc_cause) {

  ShenandoahHeap* _heap = ShenandoahHeap::heap();
  {
    ShenandoahGCSession session(/* is_full_gc */true);

    GCTracer *_gc_tracer = _heap->tracer();
    if (_gc_tracer->has_reported_gc_start()) {
      _gc_tracer->report_gc_end(_gc_timer->gc_end(), _gc_timer->time_partitions());
    }
    _gc_tracer->report_gc_start(gc_cause, _gc_timer->gc_start());

    _heap->set_full_gc_in_progress(true);

    assert(SafepointSynchronize::is_at_safepoint(), "must be at a safepoint");
    assert(Thread::current()->is_VM_thread(), "Do full GC only while world is stopped");

    {
      ShenandoahGCPhase phase(ShenandoahCollectorPolicy::full_gc_heapdumps);
      _heap->pre_full_gc_dump(_gc_timer);
    }

    {
      ShenandoahGCPhase phase(ShenandoahCollectorPolicy::full_gc_prepare);
      // Full GC is supposed to recover from any GC state:

      // a. Cancel concurrent mark, if in progress
      if (_heap->concurrent_mark_in_progress()) {
        _heap->concurrentMark()->cancel();
        _heap->stop_concurrent_marking();
      }
      assert(!_heap->concurrent_mark_in_progress(), "sanity");

      // b. Cancel evacuation, if in progress
      if (_heap->is_evacuation_in_progress()) {
        _heap->set_evacuation_in_progress_at_safepoint(false);
      }
      assert(!_heap->is_evacuation_in_progress(), "sanity");

      // c. Reset the bitmaps for new marking
      _heap->reset_next_mark_bitmap(_heap->workers());
      assert(_heap->is_next_bitmap_clear(), "sanity");

      // d. Abandon reference discovery and clear all discovered references.
      ReferenceProcessor *rp = _heap->ref_processor();
      rp->disable_discovery();
      rp->abandon_partial_discovery();
      rp->verify_no_references_recorded();

      {
        ShenandoahHeapLocker lock(_heap->lock());

        // e. Make sure all regions are active. This is needed because we are potentially
        // sliding the data through them
        ShenandoahEnsureHeapActiveClosure ecl;
        _heap->heap_region_iterate(&ecl, false, false);

        // f. Clear region statuses, including collection set status
        ShenandoahClearRegionStatusClosure cl;
        _heap->heap_region_iterate(&cl, false, false);
      }

      if (ShenandoahVerify) {
        // Full GC should only be called between regular concurrent cycles, therefore
        // those verifications should be valid.
        _heap->verifier()->verify_before_fullgc();
      }
    }

    BarrierSet *old_bs = oopDesc::bs();
    ShenandoahMarkCompactBarrierSet bs(_heap);
    oopDesc::set_bs(&bs);

    {
      GCTraceTime time("Pause Full", PrintGC, _gc_timer, _gc_tracer->gc_id(), true);

      if (UseTLAB) {
        _heap->ensure_parsability(true);
      }

      CodeCache::gc_prologue();

      // We should save the marks of the currently locked biased monitors.
      // The marking doesn't preserve the marks of biased objects.
      //BiasedLocking::preserve_marks();

      _heap->set_need_update_refs(true);
      FlexibleWorkGang* workers = _heap->workers();

      // Setup workers for phase 1
      {
        ShenandoahGCPhase phase(ShenandoahCollectorPolicy::full_gc_mark);

        uint nworkers = ShenandoahCollectorPolicy::calc_workers_for_init_marking(
                workers->active_workers(), (uint) Threads::number_of_non_daemon_threads());
        workers->set_active_workers(nworkers);
        ShenandoahWorkerScope scope(workers, nworkers);

        OrderAccess::fence();

        phase1_mark_heap();
      }

      // Setup workers for the rest
      {
        uint nworkers = ShenandoahCollectorPolicy::calc_workers_for_parallel_evacuation(
                workers->active_workers(), (uint) Threads::number_of_non_daemon_threads());
        ShenandoahWorkerScope scope(workers, nworkers);

        OrderAccess::fence();

        ShenandoahHeapRegionSet **copy_queues = NEW_C_HEAP_ARRAY(ShenandoahHeapRegionSet*, _heap->max_workers(), mtGC);

        {
          ShenandoahGCPhase phase(ShenandoahCollectorPolicy::full_gc_calculate_addresses);
          phase2_calculate_target_addresses(copy_queues);
        }

        OrderAccess::fence();

        {
          ShenandoahGCPhase phase(ShenandoahCollectorPolicy::full_gc_adjust_pointers);
          phase3_update_references();
        }

        {
          ShenandoahGCPhase phase(ShenandoahCollectorPolicy::full_gc_copy_objects);
          phase4_compact_objects(copy_queues);
        }

        FREE_C_HEAP_ARRAY(ShenandoahHeapRegionSet*, copy_queues, mtGC);

        CodeCache::gc_epilogue();
        JvmtiExport::gc_epilogue();
      }

      // refs processing: clean slate
      // rp.enqueue_discovered_references();

      if (ShenandoahVerify) {
        _heap->verifier()->verify_after_fullgc();
      }

      _heap->set_bytes_allocated_since_cm(0);

      _heap->set_need_update_refs(false);

      _heap->set_full_gc_in_progress(false);
    }

    _gc_tracer->report_gc_end(_gc_timer->gc_end(), _gc_timer->time_partitions());

    {
      ShenandoahGCPhase phase(ShenandoahCollectorPolicy::full_gc_heapdumps);
      _heap->post_full_gc_dump(_gc_timer);
    }

    if (UseTLAB) {
      ShenandoahGCPhase phase(ShenandoahCollectorPolicy::full_gc_resize_tlabs);
      _heap->resize_all_tlabs();
    }

    oopDesc::set_bs(old_bs);
  }
}

void ShenandoahMarkCompact::phase1_mark_heap() {
  ShenandoahHeap* _heap = ShenandoahHeap::heap();
  GCTraceTime time("Phase 1: Mark live objects", ShenandoahLogDebug, _gc_timer, _heap->tracer()->gc_id());

  ShenandoahConcurrentMark* cm = _heap->concurrentMark();

  // Do not trust heuristics, because this can be our last resort collection.
  // Only ignore processing references and class unloading if explicitly disabled.
  cm->set_process_references(ShenandoahRefProcFrequency != 0);
  cm->set_unload_classes(ShenandoahUnloadClassesFrequency != 0);

  ReferenceProcessor* rp = _heap->ref_processor();
  // enable ("weak") refs discovery
  rp->enable_discovery(true /*verify_no_refs*/, true);
  rp->setup_policy(true); // snapshot the soft ref policy to be used in this cycle
  rp->set_active_mt_degree(_heap->workers()->active_workers());

  cm->update_roots(ShenandoahCollectorPolicy::full_gc_roots);
  cm->mark_roots(ShenandoahCollectorPolicy::full_gc_roots);
  cm->shared_finish_mark_from_roots(/* full_gc = */ true);

  _heap->swap_mark_bitmaps();

  if (VerifyDuringGC) {
    HandleMark hm;  // handle scope
    _heap->prepare_for_verify();
    // Note: we can verify only the heap here. When an object is
    // marked, the previous value of the mark word (including
    // identity hash values, ages, etc) is preserved, and the mark
    // word is set to markOop::marked_value - effectively removing
    // any hash values from the mark word. These hash values are
    // used when verifying the dictionaries and so removing them
    // from the mark word can make verification of the dictionaries
    // fail. At the end of the GC, the original mark word values
    // (including hash values) are restored to the appropriate
    // objects.
    _heap->verify(true, VerifyOption_G1UseMarkWord);
  }
}

class ShenandoahMCReclaimHumongousRegionClosure : public ShenandoahHeapRegionClosure {
private:
  ShenandoahHeap* _heap;
public:
  ShenandoahMCReclaimHumongousRegionClosure() : _heap(ShenandoahHeap::heap()) {
  }

  bool heap_region_do(ShenandoahHeapRegion* r) {
    if (r->is_humongous_start()) {
      oop humongous_obj = oop(r->bottom() + BrooksPointer::word_size());
      if (! _heap->is_marked_complete(humongous_obj)) {
        _heap->reclaim_humongous_region_at(r);
      }
    }
    return false;
  }
};


class ShenandoahPrepareForCompactionObjectClosure : public ObjectClosure {

private:

  ShenandoahHeap* _heap;
  ShenandoahHeapRegionSet* _to_regions;
  ShenandoahHeapRegion* _to_region;
  ShenandoahHeapRegion* _from_region;
  HeapWord* _compact_point;

public:

  ShenandoahPrepareForCompactionObjectClosure(ShenandoahHeapRegionSet* to_regions, ShenandoahHeapRegion* to_region) :
    _heap(ShenandoahHeap::heap()),
    _to_regions(to_regions),
    _to_region(to_region),
    _from_region(NULL),
    _compact_point(to_region->bottom()) {
  }

  void set_from_region(ShenandoahHeapRegion* from_region) {
    _from_region = from_region;
  }

  ShenandoahHeapRegion* to_region() const {
    return _to_region;
  }
  HeapWord* compact_point() const {
    return _compact_point;
  }
  void do_object(oop p) {
    assert(_from_region != NULL, "must set before work");
    assert(_heap->is_marked_complete(p), "must be marked");
    assert(! _heap->allocated_after_complete_mark_start((HeapWord*) p), "must be truly marked");
    size_t obj_size = p->size() + BrooksPointer::word_size();
    if (_compact_point + obj_size > _to_region->end()) {
      // Object doesn't fit. Pick next to-region and start compacting there.
      _to_region->set_new_top(_compact_point);
      ShenandoahHeapRegion* new_to_region = _to_regions->current();
      _to_regions->next();
      if (new_to_region == NULL) {
        new_to_region = _from_region;
      }
      assert(new_to_region != _to_region, "must not reuse same to-region");
      assert(new_to_region != NULL, "must not be NULL");
      _to_region = new_to_region;
      _compact_point = _to_region->bottom();
    }
    assert(_compact_point + obj_size <= _to_region->end(), "must fit");
    assert(oopDesc::unsafe_equals(p, ShenandoahBarrierSet::resolve_oop_static_not_null(p)),
           "expect forwarded oop");
    BrooksPointer::set_raw(p, _compact_point + BrooksPointer::word_size());
    _compact_point += obj_size;
  }
};

class ShenandoahPrepareForCompactionTask : public AbstractGangTask {
private:

  ShenandoahHeapRegionSet** _copy_queues;
  ShenandoahHeapRegionSet* _from_regions;

  ShenandoahHeapRegion* next_from_region(ShenandoahHeapRegionSet* copy_queue) {
    ShenandoahHeapRegion* from_region = _from_regions->claim_next();

    // Note: During Full GC after cancelled conc GC, we might have incoming regions
    // in the collection set. Otherwise we would have just taken care of regular regions.
    while (from_region != NULL && !(from_region->is_regular() || from_region->is_cset())) {
      from_region = _from_regions->claim_next();
    }
    if (from_region != NULL) {
      assert(copy_queue != NULL, "sanity");
      assert(from_region->is_regular() || from_region->is_cset(), "only regular/cset regions in mark-compact");
      copy_queue->add_region(from_region);
    }
    return from_region;
  }

public:
  ShenandoahPrepareForCompactionTask(ShenandoahHeapRegionSet* from_regions, ShenandoahHeapRegionSet** copy_queues) :
    AbstractGangTask("Shenandoah Prepare For Compaction Task"),
    _from_regions(from_regions), _copy_queues(copy_queues) {
  }

  void work(uint worker_id) {
    ShenandoahHeap* heap = ShenandoahHeap::heap();
    ShenandoahHeapRegionSet* copy_queue = _copy_queues[worker_id];
    ShenandoahHeapRegion* from_region = next_from_region(copy_queue);
    if (from_region == NULL) return;
    ShenandoahHeapRegionSet* to_regions = new ShenandoahHeapRegionSet(ShenandoahHeap::heap()->num_regions());
    ShenandoahPrepareForCompactionObjectClosure cl(to_regions, from_region);
    while (from_region != NULL) {
      assert(from_region != NULL, "sanity");
      cl.set_from_region(from_region);
      heap->marked_object_iterate(from_region, &cl);
      if (from_region != cl.to_region()) {
        assert(from_region != NULL, "sanity");
        to_regions->add_region(from_region);
      }
      from_region = next_from_region(copy_queue);
    }
    assert(cl.to_region() != NULL, "should not happen");
    cl.to_region()->set_new_top(cl.compact_point());
    while (to_regions->count() > 0) {
      ShenandoahHeapRegion* r = to_regions->current();
      to_regions->next();
      assert(r != NULL, "should not happen");
      r->set_new_top(r->bottom());
    }
    delete to_regions;
  }
};

void ShenandoahMarkCompact::phase2_calculate_target_addresses(ShenandoahHeapRegionSet** copy_queues) {
  ShenandoahHeap* heap = ShenandoahHeap::heap();
  GCTraceTime time("Phase 2: Compute new object addresses", ShenandoahLogDebug, _gc_timer, heap->tracer()->gc_id());

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

  // Initialize copy queues.
  for (uint i = 0; i < heap->max_workers(); i++) {
    copy_queues[i] = new ShenandoahHeapRegionSet(heap->num_regions());
  }

  ShenandoahHeapRegionSet* from_regions = heap->regions();
  from_regions->clear_current_index();
  ShenandoahPrepareForCompactionTask prepare_task(from_regions, copy_queues);
  heap->workers()->run_task(&prepare_task);
}

class ShenandoahAdjustPointersClosure : public MetadataAwareOopClosure {
private:
  ShenandoahHeap* _heap;

public:

  ShenandoahAdjustPointersClosure() :
          _heap(ShenandoahHeap::heap())
  {}

private:
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
  void do_oop(oop* p) {
    do_oop_work(p);
  }
  void do_oop(narrowOop* p) {
    do_oop_work(p);
  }
};

class ShenandoahAdjustPointersObjectClosure : public ObjectClosure {
private:
  ShenandoahAdjustPointersClosure* _cl;
  ShenandoahHeap* _heap;
public:
  ShenandoahAdjustPointersObjectClosure(ShenandoahAdjustPointersClosure* cl) :
    _cl(cl), _heap(ShenandoahHeap::heap()) {
  }
  void do_object(oop p) {
    assert(_heap->is_marked_complete(p), "must be marked");
    p->oop_iterate(_cl);
  }
};

class ShenandoahAdjustPointersTask : public AbstractGangTask {
private:
  ShenandoahHeapRegionSet* _regions;
public:

  ShenandoahAdjustPointersTask(ShenandoahHeapRegionSet* regions) :
    AbstractGangTask("Shenandoah Adjust Pointers Task"),
    _regions(regions) {
  }

  void work(uint worker_id) {
    ShenandoahHeap* heap = ShenandoahHeap::heap();
    ShenandoahHeapRegion* r = _regions->claim_next();
    ShenandoahAdjustPointersClosure cl;
    ShenandoahAdjustPointersObjectClosure obj_cl(&cl);
    while (r != NULL) {
      if (! r->is_humongous_continuation()) {
        heap->marked_object_iterate(r, &obj_cl);
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
    _rp(rp) {
  }

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

  WorkGang* workers = heap->workers();
  uint nworkers = workers->active_workers();
  {
    COMPILER2_PRESENT(DerivedPointerTable::clear());
    ShenandoahRootProcessor rp(heap, nworkers, ShenandoahCollectorPolicy::full_gc_roots);
    ShenandoahAdjustRootPointersTask task(&rp);
    workers->run_task(&task);
    COMPILER2_PRESENT(DerivedPointerTable::update_pointers());
  }

  ShenandoahHeapRegionSet* regions = heap->regions();
  regions->clear_current_index();
  ShenandoahAdjustPointersTask adjust_pointers_task(regions);
  workers->run_task(&adjust_pointers_task);
}

class ShenandoahCompactObjectsClosure : public ObjectClosure {
private:
  ShenandoahHeap* _heap;
public:
  ShenandoahCompactObjectsClosure() : _heap(ShenandoahHeap::heap()) {
  }
  void do_object(oop p) {
    assert(_heap->is_marked_complete(p), "must be marked");
    size_t size = (size_t)p->size();
    HeapWord* compact_to = BrooksPointer::get_raw(p);
    HeapWord* compact_from = (HeapWord*) p;
    if (compact_from != compact_to) {
      Copy::aligned_conjoint_words(compact_from, compact_to, size);
    }
    oop new_obj = oop(compact_to);
    // new_obj->init_mark();
    BrooksPointer::initialize(new_obj);
  }
};

class ShenandoahCompactObjectsTask : public AbstractGangTask {
  ShenandoahHeapRegionSet** _regions;
public:
  ShenandoahCompactObjectsTask(ShenandoahHeapRegionSet** regions) :
    AbstractGangTask("Shenandoah Compact Objects Task"),
    _regions(regions) {
  }
  void work(uint worker_id) {
    ShenandoahHeap* heap = ShenandoahHeap::heap();
    ShenandoahHeapRegionSet* copy_queue = _regions[worker_id];
    copy_queue->clear_current_index();
    ShenandoahCompactObjectsClosure cl;
    ShenandoahHeapRegion* r = copy_queue->current();
    copy_queue->next();
    while (r != NULL) {
      assert(! r->is_humongous(), "must not get humongous regions here");
      heap->marked_object_iterate(r, &cl);
      r->set_top(r->new_top());
      r = copy_queue->current();
      copy_queue->next();
    }
  }
};

class ShenandoahPostCompactClosure : public ShenandoahHeapRegionClosure {
  size_t _live;
  ShenandoahHeap* _heap;
public:

  ShenandoahPostCompactClosure() : _live(0), _heap(ShenandoahHeap::heap()) {
    _heap->clear_free_regions();
  }

  bool heap_region_do(ShenandoahHeapRegion* r) {
    // Need to reset the complete-top-at-mark-start pointer here because
    // the complete marking bitmap is no longer valid. This ensures
    // size-based iteration in marked_object_iterate().
    _heap->set_complete_top_at_mark_start(r->bottom(), r->bottom());

    size_t live = r->used();

    // Turn any lingering non-empty cset regions into regular regions.
    // This must be the leftover from the cancelled concurrent GC.
    if (r->is_cset() && live != 0) {
      r->make_regular_bypass();
    }

    // Reclaim regular/cset regions that became empty
    if ((r->is_regular() || r->is_cset()) && live == 0) {
      r->make_trash();
    }

    // Recycle all trash regions
    if (r->is_trash()) {
      _heap->immediate_recycle(r);
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

  size_t get_live() { return _live; }

};

void ShenandoahMarkCompact::phase4_compact_objects(ShenandoahHeapRegionSet** copy_queues) {
  ShenandoahHeap* heap = ShenandoahHeap::heap();
  GCTraceTime time("Phase 4: Move objects", ShenandoahLogDebug, _gc_timer, heap->tracer()->gc_id());
  ShenandoahCompactObjectsTask compact_task(copy_queues);
  heap->workers()->run_task(&compact_task);

  // Reset complete bitmap. We're about to reset the complete-top-at-mark-start pointer
  // and must ensure the bitmap is in sync.
  heap->reset_complete_mark_bitmap(heap->workers());

  {
    ShenandoahHeapLocker lock(heap->lock());
    ShenandoahPostCompactClosure post_compact;
    heap->heap_region_iterate(&post_compact);

    heap->set_used(post_compact.get_live());

  }

  heap->collection_set()->clear();
  heap->clear_cancelled_concgc();

  // Also clear the next bitmap in preparation for next marking.
  heap->reset_next_mark_bitmap(heap->workers());

  for (uint i = 0; i < heap->max_workers(); i++) {
    delete copy_queues[i];
  }

}
