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

#include "gc_implementation/shared/gcTimer.hpp"
#include "gc_implementation/shenandoah/shenandoahGCTraceTime.hpp"
#include "gc_implementation/shared/parallelCleaning.hpp"

#include "gc_implementation/shenandoah/brooksPointer.hpp"
#include "gc_implementation/shenandoah/shenandoahBarrierSet.hpp"
#include "gc_implementation/shenandoah/shenandoahCollectionSet.hpp"
#include "gc_implementation/shenandoah/shenandoahCollectorPolicy.hpp"
#include "gc_implementation/shenandoah/shenandoahConcurrentMark.hpp"
#include "gc_implementation/shenandoah/shenandoahConcurrentMark.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahConcurrentThread.hpp"
#include "gc_implementation/shenandoah/shenandoahFreeSet.hpp"
#include "gc_implementation/shenandoah/shenandoahHeap.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahHeapRegion.hpp"
#include "gc_implementation/shenandoah/shenandoahHeapRegionSet.hpp"
#include "gc_implementation/shenandoah/shenandoahMarkCompact.hpp"
#include "gc_implementation/shenandoah/shenandoahMonitoringSupport.hpp"
#include "gc_implementation/shenandoah/shenandoahOopClosures.inline.hpp"
#include "gc_implementation/shenandoah/shenandoahRootProcessor.hpp"
#include "gc_implementation/shenandoah/shenandoahUtils.hpp"
#include "gc_implementation/shenandoah/shenandoahVerifier.hpp"
#include "gc_implementation/shenandoah/shenandoahCodeRoots.hpp"
#include "gc_implementation/shenandoah/vm_operations_shenandoah.hpp"

#include "runtime/vmThread.hpp"
#include "services/mallocTracker.hpp"

ShenandoahUpdateRefsClosure::ShenandoahUpdateRefsClosure() : _heap(ShenandoahHeap::heap()) {}

#ifdef ASSERT
template <class T>
void ShenandoahAssertToSpaceClosure::do_oop_nv(T* p) {
  T o = oopDesc::load_heap_oop(p);
  if (! oopDesc::is_null(o)) {
    oop obj = oopDesc::decode_heap_oop_not_null(o);
    assert(oopDesc::unsafe_equals(obj, ShenandoahBarrierSet::resolve_oop_static_not_null(obj)),
           err_msg("need to-space object here obj: "PTR_FORMAT" , rb(obj): "PTR_FORMAT", p: "PTR_FORMAT,
		   p2i(obj), p2i(ShenandoahBarrierSet::resolve_oop_static_not_null(obj)), p2i(p)));
  }
}

void ShenandoahAssertToSpaceClosure::do_oop(narrowOop* p) { do_oop_nv(p); }
void ShenandoahAssertToSpaceClosure::do_oop(oop* p)       { do_oop_nv(p); }
#endif

const char* ShenandoahHeap::name() const {
  return "Shenandoah";
}

class ShenandoahPretouchTask : public AbstractGangTask {
private:
  ShenandoahHeapRegionSet* _regions;
  const size_t _bitmap_size;
  const size_t _page_size;
  char* _bitmap0_base;
  char* _bitmap1_base;
public:
  ShenandoahPretouchTask(ShenandoahHeapRegionSet* regions,
                         char* bitmap0_base, char* bitmap1_base, size_t bitmap_size,
                         size_t page_size) :
    AbstractGangTask("Shenandoah PreTouch"),
    _bitmap0_base(bitmap0_base),
    _bitmap1_base(bitmap1_base),
    _regions(regions),
    _bitmap_size(bitmap_size),
    _page_size(page_size) {
    _regions->clear_current_index();
  };

  virtual void work(uint worker_id) {
    ShenandoahHeapRegion* r = _regions->claim_next();
    while (r != NULL) {
      log_trace(gc, heap)("Pretouch region " SIZE_FORMAT ": " PTR_FORMAT " -> " PTR_FORMAT,
                          r->region_number(), p2i(r->bottom()), p2i(r->end()));
      os::pretouch_memory((char*) r->bottom(), (char*) r->end());

      size_t start = r->region_number()       * ShenandoahHeapRegion::region_size_bytes() / MarkBitMap::heap_map_factor();
      size_t end   = (r->region_number() + 1) * ShenandoahHeapRegion::region_size_bytes() / MarkBitMap::heap_map_factor();
      assert (end <= _bitmap_size, err_msg("end is sane: " SIZE_FORMAT " < " SIZE_FORMAT, end, _bitmap_size));

      log_trace(gc, heap)("Pretouch bitmap under region " SIZE_FORMAT ": " PTR_FORMAT " -> " PTR_FORMAT,
                          r->region_number(), p2i(_bitmap0_base + start), p2i(_bitmap0_base + end));
      os::pretouch_memory(_bitmap0_base + start, _bitmap0_base + end);

      log_trace(gc, heap)("Pretouch bitmap under region " SIZE_FORMAT ": " PTR_FORMAT " -> " PTR_FORMAT,
                          r->region_number(), p2i(_bitmap1_base + start), p2i(_bitmap1_base + end));
      os::pretouch_memory(_bitmap1_base + start, _bitmap1_base + end);

      r = _regions->claim_next();
    }
  }
};

jint ShenandoahHeap::initialize() {
  CollectedHeap::pre_initialize();

  BrooksPointer::initial_checks();

  size_t init_byte_size = collector_policy()->initial_heap_byte_size();
  size_t max_byte_size = collector_policy()->max_heap_byte_size();
  size_t heap_alignment = collector_policy()->heap_alignment();

  if (ShenandoahAlwaysPreTouch) {
    // Enabled pre-touch means the entire heap is committed right away.
    init_byte_size = max_byte_size;
  }

  Universe::check_alignment(max_byte_size,
                            ShenandoahHeapRegion::region_size_bytes(),
                            "shenandoah heap");
  Universe::check_alignment(init_byte_size,
                            ShenandoahHeapRegion::region_size_bytes(),
                            "shenandoah heap");

  ReservedSpace heap_rs = Universe::reserve_heap(max_byte_size,
                                                 heap_alignment);

  _reserved.set_word_size(0);
  _reserved.set_start((HeapWord*)heap_rs.base());
  _reserved.set_end((HeapWord*)(heap_rs.base() + heap_rs.size()));

  set_barrier_set(new ShenandoahBarrierSet(this));
  ReservedSpace pgc_rs = heap_rs.first_part(max_byte_size);

  _num_regions = max_byte_size / ShenandoahHeapRegion::region_size_bytes();
  size_t num_committed_regions = init_byte_size / ShenandoahHeapRegion::region_size_bytes();
  _initial_size = num_committed_regions * ShenandoahHeapRegion::region_size_bytes();
  _committed = _initial_size;

  log_info(gc, heap)("Initialize Shenandoah heap with initial size " SIZE_FORMAT " bytes", init_byte_size);
  if (!os::commit_memory(pgc_rs.base(), _initial_size, false)) {
    vm_exit_out_of_memory(_initial_size, OOM_MMAP_ERROR, "Shenandoah failed to initialize heap");
  }

  size_t reg_size_words = ShenandoahHeapRegion::region_size_words();

  _ordered_regions = new ShenandoahHeapRegionSet(_num_regions);
  _free_regions = new ShenandoahFreeSet(_num_regions);

  _collection_set = new ShenandoahCollectionSet(this, (HeapWord*)pgc_rs.base());

  _next_top_at_mark_starts_base = NEW_C_HEAP_ARRAY(HeapWord*, _num_regions, mtGC);
  _next_top_at_mark_starts = _next_top_at_mark_starts_base -
               ((uintx) pgc_rs.base() >> ShenandoahHeapRegion::region_size_bytes_shift());

  _complete_top_at_mark_starts_base = NEW_C_HEAP_ARRAY(HeapWord*, _num_regions, mtGC);
  _complete_top_at_mark_starts = _complete_top_at_mark_starts_base -
               ((uintx) pgc_rs.base() >> ShenandoahHeapRegion::region_size_bytes_shift());

  {
    ShenandoahHeapLocker locker(lock());
    for (size_t i = 0; i < _num_regions; i++) {
      ShenandoahHeapRegion* r = new ShenandoahHeapRegion(this,
                                                         (HeapWord*) pgc_rs.base() + reg_size_words * i,
                                                         reg_size_words,
                                                         i,
                                                         i < num_committed_regions);

      _complete_top_at_mark_starts_base[i] = r->bottom();
      _next_top_at_mark_starts_base[i] = r->bottom();

      // Add to ordered regions first.
      // We use the active size of ordered regions as the number of active regions in heap,
      // free set and collection set use the number to assert the correctness of incoming regions.
      _ordered_regions->add_region(r);
      _free_regions->add_region(r);
      assert(!collection_set()->is_in(i), "New region should not be in collection set");
    }
  }

  assert(_ordered_regions->active_regions() == _num_regions, "Must match");
  assert((((size_t) base()) & ShenandoahHeapRegion::region_size_bytes_mask()) == 0,
         err_msg("misaligned heap: "PTR_FORMAT, p2i(base())));

  if (ShenandoahLogTrace) {
    ResourceMark rm;
    outputStream* out = gclog_or_tty;
    log_trace(gc, region)("All Regions");
    _ordered_regions->print_on(out);
    log_trace(gc, region)("Free Regions");
    _free_regions->print_on(out);
  }

  _recycled_regions = NEW_C_HEAP_ARRAY(size_t, _num_regions, mtGC);
  _recycled_region_count = 0;

  // The call below uses stuff (the SATB* things) that are in G1, but probably
  // belong into a shared location.
  JavaThread::satb_mark_queue_set().initialize(SATB_Q_CBL_mon,
                                               SATB_Q_FL_lock,
                                               20 /*G1SATBProcessCompletedThreshold */,
                                               Shared_SATB_Q_lock);

  // Reserve space for prev and next bitmap.
  _bitmap_size = MarkBitMap::compute_size(heap_rs.size());
  _heap_region = MemRegion((HeapWord*) heap_rs.base(), heap_rs.size() / HeapWordSize);

  size_t page_size = UseLargePages ? (size_t)os::large_page_size() : (size_t)os::vm_page_size();

  ReservedSpace bitmap0(_bitmap_size, page_size);
  os::commit_memory_or_exit(bitmap0.base(), bitmap0.size(), false, "couldn't allocate mark bitmap");
  MemTracker::record_virtual_memory_type(bitmap0.base(), mtGC);
  MemRegion bitmap_region0 = MemRegion((HeapWord*) bitmap0.base(), bitmap0.size() / HeapWordSize);

  ReservedSpace bitmap1(_bitmap_size, page_size);
  os::commit_memory_or_exit(bitmap1.base(), bitmap1.size(), false, "couldn't allocate mark bitmap");
  MemTracker::record_virtual_memory_type(bitmap1.base(), mtGC);
  MemRegion bitmap_region1 = MemRegion((HeapWord*) bitmap1.base(), bitmap1.size() / HeapWordSize);

  if (ShenandoahVerify) {
    ReservedSpace verify_bitmap(_bitmap_size, page_size);
    os::commit_memory_or_exit(verify_bitmap.base(), verify_bitmap.size(), false,
                              "couldn't allocate verification bitmap");
    MemTracker::record_virtual_memory_type(verify_bitmap.base(), mtGC);
    MemRegion verify_bitmap_region = MemRegion((HeapWord *) verify_bitmap.base(), verify_bitmap.size() / HeapWordSize);
    _verification_bit_map.initialize(_heap_region, verify_bitmap_region);
    _verifier = new ShenandoahVerifier(this, &_verification_bit_map);
  }

  if (ShenandoahAlwaysPreTouch) {
    assert (!AlwaysPreTouch, "Should have been overridden");

    // For NUMA, it is important to pre-touch the storage under bitmaps with worker threads,
    // before initialize() below zeroes it with initializing thread. For any given region,
    // we touch the region and the corresponding bitmaps from the same thread.

    log_info(gc, heap)("Parallel pretouch " SIZE_FORMAT " regions with " SIZE_FORMAT " byte pages",
                       _ordered_regions->count(), page_size);
    ShenandoahPretouchTask cl(_ordered_regions, bitmap0.base(), bitmap1.base(), _bitmap_size, page_size);
    _workers->run_task(&cl);
  }

  _mark_bit_map0.initialize(_heap_region, bitmap_region0);
  _complete_mark_bit_map = &_mark_bit_map0;

  _mark_bit_map1.initialize(_heap_region, bitmap_region1);
  _next_mark_bit_map = &_mark_bit_map1;

  _monitoring_support = new ShenandoahMonitoringSupport(this);

  _concurrent_gc_thread = new ShenandoahConcurrentThread();

  ShenandoahMarkCompact::initialize();

  ShenandoahCodeRoots::initialize();

  return JNI_OK;
}

ShenandoahHeap::ShenandoahHeap(ShenandoahCollectorPolicy* policy) :
  SharedHeap(policy),
  _shenandoah_policy(policy),
  _concurrent_mark_in_progress(0),
  _evacuation_in_progress(0),
  _full_gc_in_progress(false),
  _update_refs_in_progress(false),
  _free_regions(NULL),
  _collection_set(NULL),
  _bytes_allocated_since_cm(0),
  _bytes_allocated_during_cm(0),
  _allocated_last_gc(0),
  _used_start_gc(0),
  _max_workers((uint)MAX2(ConcGCThreads, ParallelGCThreads)),
  _ref_processor(NULL),
  _next_top_at_mark_starts(NULL),
  _next_top_at_mark_starts_base(NULL),
  _complete_top_at_mark_starts(NULL),
  _complete_top_at_mark_starts_base(NULL),
  _mark_bit_map0(),
  _mark_bit_map1(),
  _cancelled_concgc(0),
  _need_update_refs(false),
  _need_reset_bitmaps(false),
  _verifier(NULL),
  _heap_lock(0),
#ifdef ASSERT
  _heap_lock_owner(NULL),
  _heap_expansion_count(0),
#endif
  _gc_timer(new (ResourceObj::C_HEAP, mtGC) ConcurrentGCTimer())

{
  log_info(gc, init)("Parallel GC threads: "UINTX_FORMAT, ParallelGCThreads);
  log_info(gc, init)("Concurrent GC threads: "UINTX_FORMAT, ConcGCThreads);
  log_info(gc, init)("Parallel reference processing enabled: %s", BOOL_TO_STR(ParallelRefProcEnabled));

  _scm = new ShenandoahConcurrentMark();
  _used = 0;

  _max_workers = MAX2(_max_workers, 1U);
  _workers = new FlexibleWorkGang("Shenandoah GC Threads", _max_workers,
                            /* are_GC_task_threads */true,
                            /* are_ConcurrentGC_threads */false);
  if (_workers == NULL) {
    vm_exit_during_initialization("Failed necessary allocation.");
  } else {
    _workers->initialize_workers();
  }
}

class ShenandoahResetNextBitmapTask : public AbstractGangTask {
private:
  ShenandoahHeapRegionSet* _regions;

public:
  ShenandoahResetNextBitmapTask(ShenandoahHeapRegionSet* regions) :
    AbstractGangTask("Parallel Reset Bitmap Task"),
    _regions(regions) {
    _regions->clear_current_index();
  }

  void work(uint worker_id) {
    ShenandoahHeapRegion* region = _regions->claim_next();
    ShenandoahHeap* heap = ShenandoahHeap::heap();
    while (region != NULL) {
      HeapWord* bottom = region->bottom();
      HeapWord* top = heap->next_top_at_mark_start(region->bottom());
      if (top > bottom) {
        heap->next_mark_bit_map()->clear_range_large(MemRegion(bottom, top));
      }
      assert(heap->is_next_bitmap_clear_range(bottom, region->end()), "must be clear");
      region = _regions->claim_next();
    }
  }
};

void ShenandoahHeap::reset_next_mark_bitmap(WorkGang* workers) {
  ShenandoahResetNextBitmapTask task = ShenandoahResetNextBitmapTask(_ordered_regions);
  workers->run_task(&task);
}

class ShenandoahResetCompleteBitmapTask : public AbstractGangTask {
private:
  ShenandoahHeapRegionSet* _regions;

public:
  ShenandoahResetCompleteBitmapTask(ShenandoahHeapRegionSet* regions) :
    AbstractGangTask("Parallel Reset Bitmap Task"),
    _regions(regions) {
    _regions->clear_current_index();
  }

  void work(uint worker_id) {
    ShenandoahHeapRegion* region = _regions->claim_next();
    ShenandoahHeap* heap = ShenandoahHeap::heap();
    while (region != NULL) {
      HeapWord* bottom = region->bottom();
      HeapWord* top = heap->complete_top_at_mark_start(region->bottom());
      if (top > bottom) {
        heap->complete_mark_bit_map()->clear_range_large(MemRegion(bottom, top));
      }
      assert(heap->is_complete_bitmap_clear_range(bottom, region->end()), "must be clear");
      region = _regions->claim_next();
    }
  }
};

void ShenandoahHeap::reset_complete_mark_bitmap(WorkGang* workers) {
  ShenandoahResetCompleteBitmapTask task = ShenandoahResetCompleteBitmapTask(_ordered_regions);
  workers->run_task(&task);
}

bool ShenandoahHeap::is_next_bitmap_clear() {
  HeapWord* start = _ordered_regions->bottom();
  HeapWord* end = _ordered_regions->end();
  return _next_mark_bit_map->getNextMarkedWordAddress(start, end) == end;
}

bool ShenandoahHeap::is_next_bitmap_clear_range(HeapWord* start, HeapWord* end) {
  return _next_mark_bit_map->getNextMarkedWordAddress(start, end) == end;
}

bool ShenandoahHeap::is_complete_bitmap_clear_range(HeapWord* start, HeapWord* end) {
  return _complete_mark_bit_map->getNextMarkedWordAddress(start, end) == end;
}

void ShenandoahHeap::print_on(outputStream* st) const {
  st->print_cr("Shenandoah Heap");
  st->print_cr(" " SIZE_FORMAT "K total, " SIZE_FORMAT "K committed, " SIZE_FORMAT "K used",
               capacity() / K, committed() / K, used() / K);
  st->print_cr(" " SIZE_FORMAT " x " SIZE_FORMAT"K regions",
               num_regions(), ShenandoahHeapRegion::region_size_bytes() / K);

  st->print("Status: ");
  if (concurrent_mark_in_progress()) {
    st->print("marking ");
  } else if (is_evacuation_in_progress()) {
    st->print("evacuating ");
  } else if (is_update_refs_in_progress()) {
    st->print("updating refs ");
  } else {
    st->print("idle ");
  }
  if (cancelled_concgc()) {
    st->print("cancelled ");
  }
  st->cr();

  st->print_cr("Reserved region:");
  st->print_cr(" - [" PTR_FORMAT ", " PTR_FORMAT ") ",
               p2i(reserved_region().start()),
               p2i(reserved_region().end()));

  if (Verbose) {
    print_heap_regions_on(st);
  }
}

class ShenandoahInitGCLABClosure : public ThreadClosure {
public:
  void do_thread(Thread* thread) {
    thread->gclab().initialize(true);
  }
};

void ShenandoahHeap::post_initialize() {
  if (UseTLAB) {
    MutexLocker ml(Threads_lock);

    ShenandoahInitGCLABClosure init_gclabs;
    Threads::java_threads_do(&init_gclabs);
    gc_threads_do(&init_gclabs);
  }

  _scm->initialize(_max_workers);

  ref_processing_init();
}

size_t ShenandoahHeap::used() const {
  OrderAccess::acquire();
  return _used;
}

size_t ShenandoahHeap::committed() const {
  OrderAccess::acquire();
  return _committed;
}

void ShenandoahHeap::increase_committed(size_t bytes) {
  assert_heaplock_or_safepoint();
  _committed += bytes;
}

void ShenandoahHeap::decrease_committed(size_t bytes) {
  assert_heaplock_or_safepoint();
  _committed -= bytes;
}

void ShenandoahHeap::increase_used(size_t bytes) {
  assert_heaplock_or_safepoint();
  _used += bytes;
}

void ShenandoahHeap::set_used(size_t bytes) {
  assert_heaplock_or_safepoint();
  _used = bytes;
}

void ShenandoahHeap::decrease_used(size_t bytes) {
  assert_heaplock_or_safepoint();
  assert(_used >= bytes, "never decrease heap size by more than we've left");
  _used -= bytes;
}

size_t ShenandoahHeap::capacity() const {
  return num_regions() * ShenandoahHeapRegion::region_size_bytes();
}

bool ShenandoahHeap::is_maximal_no_gc() const {
  Unimplemented();
  return true;
}

size_t ShenandoahHeap::max_capacity() const {
  return _num_regions * ShenandoahHeapRegion::region_size_bytes();
}

size_t ShenandoahHeap::initial_capacity() const {
  return _initial_size;
}

bool ShenandoahHeap::is_in(const void* p) const {
  HeapWord* heap_base = (HeapWord*) base();
  HeapWord* last_region_end = heap_base + ShenandoahHeapRegion::region_size_words() * num_regions();
  return p >= heap_base && p < last_region_end;
}

bool ShenandoahHeap::is_in_partial_collection(const void* p ) {
  Unimplemented();
  return false;
}

bool ShenandoahHeap::is_scavengable(const void* p) {
  return true;
}

void ShenandoahHeap::handle_heap_shrinkage() {
  ShenandoahHeapLocker locker(lock());

  ShenandoahHeapRegionSet* set = regions();

  size_t count = 0;
  double current = os::elapsedTime();
  for (size_t i = 0; i < num_regions(); i++) {
    ShenandoahHeapRegion* r = set->get(i);
    if (r->is_empty_committed() &&
            (current - r->empty_time()) * 1000 > ShenandoahUncommitDelay &&
            r->make_empty_uncommitted()) {
      count++;
    }
  }

  if (count > 0) {
    log_info(gc)("Uncommitted " SIZE_FORMAT "M. Heap: " SIZE_FORMAT "M reserved, " SIZE_FORMAT "M committed, " SIZE_FORMAT "M used",
                 count * ShenandoahHeapRegion::region_size_bytes() / M, capacity() / M, committed() / M, used() / M);
  }
}

HeapWord* ShenandoahHeap::allocate_from_gclab_slow(Thread* thread, size_t size) {
  // Retain tlab and allocate object in shared space if
  // the amount free in the tlab is too large to discard.
  if (thread->gclab().free() > thread->gclab().refill_waste_limit()) {
    thread->gclab().record_slow_allocation(size);
    return NULL;
  }

  // Discard gclab and allocate a new one.
  // To minimize fragmentation, the last GCLAB may be smaller than the rest.
  size_t new_gclab_size = thread->gclab().compute_size(size);

  thread->gclab().clear_before_allocation();

  if (new_gclab_size == 0) {
    return NULL;
  }

  // Allocate a new GCLAB...
  HeapWord* obj = allocate_new_gclab(new_gclab_size);
  if (obj == NULL) {
    return NULL;
  }

  if (ZeroTLAB) {
    // ..and clear it.
    Copy::zero_to_words(obj, new_gclab_size);
  } else {
    // ...and zap just allocated object.
#ifdef ASSERT
    // Skip mangling the space corresponding to the object header to
    // ensure that the returned space is not considered parsable by
    // any concurrent GC thread.
    size_t hdr_size = oopDesc::header_size();
    Copy::fill_to_words(obj + hdr_size, new_gclab_size - hdr_size, badHeapWordVal);
#endif // ASSERT
  }
  thread->gclab().fill(obj, obj + size, new_gclab_size);
  return obj;
}

HeapWord* ShenandoahHeap::allocate_new_tlab(size_t word_size) {
#ifdef ASSERT
  log_debug(gc, alloc)("Allocate new tlab, requested size = " SIZE_FORMAT " bytes", word_size * HeapWordSize);
#endif
  return allocate_new_lab(word_size, _alloc_tlab);
}

HeapWord* ShenandoahHeap::allocate_new_gclab(size_t word_size) {
#ifdef ASSERT
  log_debug(gc, alloc)("Allocate new gclab, requested size = " SIZE_FORMAT " bytes", word_size * HeapWordSize);
#endif
  return allocate_new_lab(word_size, _alloc_gclab);
}

HeapWord* ShenandoahHeap::allocate_new_lab(size_t word_size, AllocType type) {
  HeapWord* result = allocate_memory(word_size, type);

  if (result != NULL) {
    assert(! in_collection_set(result), "Never allocate in collection set");
    _bytes_allocated_since_cm += word_size * HeapWordSize;

    log_develop_trace(gc, tlab)("allocating new tlab of size "SIZE_FORMAT" at addr "PTR_FORMAT, word_size, p2i(result));

  }
  return result;
}

ShenandoahHeap* ShenandoahHeap::heap() {
  CollectedHeap* heap = Universe::heap();
  assert(heap != NULL, "Unitialized access to ShenandoahHeap::heap()");
  assert(heap->kind() == CollectedHeap::ShenandoahHeap, "not a shenandoah heap");
  return (ShenandoahHeap*) heap;
}

ShenandoahHeap* ShenandoahHeap::heap_no_check() {
  CollectedHeap* heap = Universe::heap();
  return (ShenandoahHeap*) heap;
}

HeapWord* ShenandoahHeap::allocate_memory(size_t word_size, AllocType type) {
  ShenandoahAllocTrace trace_alloc(word_size, type);

  bool in_new_region = false;
  HeapWord* result = allocate_memory_under_lock(word_size, type, in_new_region);

  if (type == _alloc_tlab || type == _alloc_shared) {
    // Allocation failed, try full-GC, then retry allocation.
    //
    // It might happen that one of the threads requesting allocation would unblock
    // way later after full-GC happened, only to fail the second allocation, because
    // other threads have already depleted the free storage. In this case, a better
    // strategy would be to try full-GC again.
    //
    // Lacking the way to detect progress from "collect" call, we are left with blindly
    // retrying for some bounded number of times.
    // TODO: Poll if Full GC made enough progress to warrant retry.
    int tries = 0;
    while ((result == NULL) && (tries++ < ShenandoahFullGCTries)) {
      log_debug(gc)("[" PTR_FORMAT " Failed to allocate " SIZE_FORMAT " bytes, doing full GC, try %d",
                    p2i(Thread::current()), word_size * HeapWordSize, tries);
      collect(GCCause::_allocation_failure);
      result = allocate_memory_under_lock(word_size, type, in_new_region);
    }
  }

  if (type == _alloc_tlab || (type == _alloc_shared && in_new_region)) {
    // Update monitoring counters when either (large) TLAB allocation happened,
    // or we did the shared allocation that took a new region. This amortizes the
    // update costs on slow path.
    //
    // Do not update monitoring counters when calling from a write-barrier.
    // Otherwise we might attempt to grab the Service_lock, which we must
    // not do when coming from a write-barrier (because the thread might
    // already hold the Compile_lock).
    monitoring_support()->update_counters();
  }

  log_develop_trace(gc, alloc)("allocate memory chunk of size "SIZE_FORMAT" at addr "PTR_FORMAT " by thread %d ",
                               word_size, p2i(result), Thread::current()->osthread()->thread_id());

  return result;
}

HeapWord* ShenandoahHeap::allocate_memory_under_lock(size_t word_size, AllocType type, bool& in_new_region) {
  ShenandoahHeapLocker locker(lock());

  if (word_size > ShenandoahHeapRegion::humongous_threshold_words()) {
    switch (type) {
      case _alloc_shared:
      case _alloc_shared_gc:
        in_new_region = true;
        return allocate_large_memory(word_size);
      case _alloc_gclab:
      case _alloc_tlab:
        log_warning(gc)("Trying to allocate TLAB larger than the humongous threshold: " SIZE_FORMAT " > " SIZE_FORMAT,
                        word_size, ShenandoahHeapRegion::humongous_threshold_words());
        return NULL;
      default:
        ShouldNotReachHere();
    }
  }

  in_new_region = false;

  // Not enough memory in free region set. Coming out of full GC, it is possible that
  // there are no free regions available, so current_index may be invalid. Have to
  // poll capacity as the precaution here.
  if (word_size * HeapWordSize > _free_regions->capacity()) return NULL;

  ShenandoahHeapRegion* current = _free_regions->current_no_humongous();

  if (current == NULL) {
    // No more room to make a new region. OOM.
    return NULL;
  }

  HeapWord* result = current->allocate(word_size, type);

  while (result == NULL) {
    in_new_region = true;

    // Retire the current region:
    _free_regions->increase_used(current->free());

    // Try next region:
    current = _free_regions->next_no_humongous();
    if (current == NULL) {
      // No more room to make a new region. OOM.
      return NULL;
    }
    result = current->allocate(word_size, type);
  }

  // Allocation successful, bump up used/live data:
  current->increase_live_data_words(word_size);
  increase_used(word_size * HeapWordSize);
  _free_regions->increase_used(word_size * HeapWordSize);

  return result;
}

HeapWord* ShenandoahHeap::allocate_large_memory(size_t words) {
  assert_heaplock_owned_by_current_thread();

  ShenandoahHeapRegion* r = _free_regions->allocate_contiguous(words);
  if (r != NULL)  {
    HeapWord* result = r->bottom();
    log_debug(gc, humongous)("allocating humongous object of size: "SIZE_FORMAT" KB at location "PTR_FORMAT" in start region "SIZE_FORMAT,
                             (words * HeapWordSize) / K, p2i(result), r->region_number());
    return result;
  } else {
    log_debug(gc, humongous)("allocating humongous object of size: "SIZE_FORMAT" KB failed",
                             (words * HeapWordSize) / K);
    return NULL;
  }
}

HeapWord*  ShenandoahHeap::mem_allocate(size_t size,
                                        bool*  gc_overhead_limit_was_exceeded) {
  HeapWord* filler = allocate_memory(size + BrooksPointer::word_size(), _alloc_shared);
  HeapWord* result = filler + BrooksPointer::word_size();
  if (filler != NULL) {
    BrooksPointer::initialize(oop(result));
    _bytes_allocated_since_cm += size * HeapWordSize;

    assert(! in_collection_set(result), "never allocate in targetted region");
    return result;
  } else {
    return NULL;
  }
}

class ShenandoahEvacuateUpdateRootsClosure: public ExtendedOopClosure {
private:
  ShenandoahHeap* _heap;
  Thread* _thread;
public:
  ShenandoahEvacuateUpdateRootsClosure() :
          _heap(ShenandoahHeap::heap()), _thread(Thread::current()) {
  }

private:
  template <class T>
  void do_oop_work(T* p) {
    assert(_heap->is_evacuation_in_progress(), "Only do this when evacuation is in progress");

    T o = oopDesc::load_heap_oop(p);
    if (! oopDesc::is_null(o)) {
      oop obj = oopDesc::decode_heap_oop_not_null(o);
      if (_heap->in_collection_set(obj)) {
        assert(_heap->is_marked_complete(obj), err_msg("only evacuate marked objects %d %d",
               _heap->is_marked_complete(obj), _heap->is_marked_complete(ShenandoahBarrierSet::resolve_oop_static_not_null(obj))));
        oop resolved = ShenandoahBarrierSet::resolve_oop_static_not_null(obj);
        if (oopDesc::unsafe_equals(resolved, obj)) {
          bool evac;
          resolved = _heap->evacuate_object(obj, _thread, evac);
        }
        oopDesc::encode_store_heap_oop(p, resolved);
      }
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

class ShenandoahEvacuateRootsClosure: public ExtendedOopClosure {
private:
  ShenandoahHeap* _heap;
  Thread* _thread;
public:
  ShenandoahEvacuateRootsClosure() :
          _heap(ShenandoahHeap::heap()), _thread(Thread::current()) {
  }

private:
  template <class T>
  void do_oop_work(T* p) {
    T o = oopDesc::load_heap_oop(p);
    if (! oopDesc::is_null(o)) {
      oop obj = oopDesc::decode_heap_oop_not_null(o);
      if (_heap->in_collection_set(obj)) {
        oop resolved = ShenandoahBarrierSet::resolve_oop_static_not_null(obj);
        if (oopDesc::unsafe_equals(resolved, obj)) {
          bool evac;
          _heap->evacuate_object(obj, _thread, evac);
        }
      }
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

class ShenandoahParallelEvacuateRegionObjectClosure : public ObjectClosure {
private:
  ShenandoahHeap* const _heap;
  Thread* const _thread;
public:
  ShenandoahParallelEvacuateRegionObjectClosure(ShenandoahHeap* heap) :
    _heap(heap), _thread(Thread::current()) {}

  void do_object(oop p) {
    assert(_heap->is_marked_complete(p), "expect only marked objects");
    if (oopDesc::unsafe_equals(p, ShenandoahBarrierSet::resolve_oop_static_not_null(p))) {
      bool evac;
      _heap->evacuate_object(p, _thread, evac);
    }
  }
};

class ShenandoahParallelEvacuationTask : public AbstractGangTask {
private:
  ShenandoahHeap* const _sh;
  ShenandoahCollectionSet* const _cs;
  volatile jbyte _claimed_codecache;

  bool claim_codecache() {
    jbyte old = Atomic::cmpxchg(1, &_claimed_codecache, 0);
    return old == 0;
  }
public:
  ShenandoahParallelEvacuationTask(ShenandoahHeap* sh,
                         ShenandoahCollectionSet* cs) :
    AbstractGangTask("Parallel Evacuation Task"),
    _cs(cs),
    _sh(sh),
    _claimed_codecache(0)
  {}

  void work(uint worker_id) {

    // If concurrent code cache evac is enabled, evacuate it here.
    // Note we cannot update the roots here, because we risk non-atomic stores to the alive
    // nmethods. The update would be handled elsewhere.
    if (ShenandoahConcurrentEvacCodeRoots && claim_codecache()) {
      ShenandoahEvacuateRootsClosure cl;
      MutexLockerEx mu(CodeCache_lock, Mutex::_no_safepoint_check_flag);
      CodeBlobToOopClosure blobs(&cl, !CodeBlobToOopClosure::FixRelocations);
      CodeCache::blobs_do(&blobs);
    }

    ShenandoahParallelEvacuateRegionObjectClosure cl(_sh);
    ShenandoahHeapRegion* r;
    while ((r =_cs->claim_next()) != NULL) {
      log_develop_trace(gc, region)("Thread "INT32_FORMAT" claimed Heap Region "SIZE_FORMAT,
                                    worker_id,
                                    r->region_number());

      assert(r->has_live(), "all-garbage regions are reclaimed early");
      _sh->marked_object_iterate(r, &cl);

      if (_sh->cancelled_concgc()) {
        log_develop_trace(gc, region)("Cancelled concgc while evacuating region " SIZE_FORMAT, r->region_number());
        break;
      }
    }
  }
};

void ShenandoahHeap::recycle_cset_regions() {
  ShenandoahHeapLocker locker(lock());

  size_t bytes_reclaimed = 0;

  ShenandoahCollectionSet* set = collection_set();

  start_deferred_recycling();

  ShenandoahHeapRegion* r;
  set->clear_current_index();
  while ((r = set->next()) != NULL) {
    decrease_used(r->used());
    bytes_reclaimed += r->used();
    defer_recycle(r);
  }

  finish_deferred_recycle();

  _shenandoah_policy->record_bytes_reclaimed(bytes_reclaimed);
  collection_set()->clear();
}

void ShenandoahHeap::print_heap_regions_on(outputStream* st) const {
  st->print_cr("Heap Regions:");
  st->print_cr("EU=empty-uncommitted, EC=empty-committed, R=regular, H=humongous start, HC=humongous continuation, CS=collection set, T=trash, P=pinned");
  st->print_cr("BTE=bottom/top/end, U=used, T=TLAB allocs, G=GCLAB allocs, S=shared allocs, L=live data");
  st->print_cr("R=root, CP=critical pins, TAMS=top-at-mark-start (previous, next)");

  _ordered_regions->print_on(st);
}

size_t ShenandoahHeap::reclaim_humongous_region_at(ShenandoahHeapRegion* start) {
  assert(start->is_humongous_start(), "reclaim regions starting with the first one");

  oop humongous_obj = oop(start->bottom() + BrooksPointer::word_size());
  size_t size = humongous_obj->size() + BrooksPointer::word_size();
  size_t required_regions = ShenandoahHeapRegion::required_regions(size * HeapWordSize);
  size_t index = start->region_number() + required_regions - 1;

  assert(!start->has_live(), "liveness must be zero");
  log_trace(gc, humongous)("Reclaiming "SIZE_FORMAT" humongous regions for object of size: "SIZE_FORMAT" words", required_regions, size);

  for(size_t i = 0; i < required_regions; i++) {
     // Reclaim from tail. Otherwise, assertion fails when printing region to trace log,
     // as it expects that every region belongs to a humongous region starting with a humongous start region.
     ShenandoahHeapRegion* region = _ordered_regions->get(index --);

    if (ShenandoahLogDebug) {
      ResourceMark rm;
      outputStream* out = gclog_or_tty;
      region->print_on(out);
    }

    assert(region->is_humongous(), "expect correct humongous start or continuation");
    assert(!in_collection_set(region), "Humongous region should not be in collection set");

    decrease_used(region->used());
    immediate_recycle(region);
  }
  return required_regions;
}

#ifdef ASSERT
class ShenandoahCheckCollectionSetClosure: public ShenandoahHeapRegionClosure {
  bool heap_region_do(ShenandoahHeapRegion* r) {
    assert(! ShenandoahHeap::heap()->in_collection_set(r), "Should have been cleared by now");
    return false;
  }
};
#endif

void ShenandoahHeap::prepare_for_concurrent_evacuation() {
  assert(_ordered_regions->get(0)->region_number() == 0, "FIXME CHF. FIXME CHF!");

  log_develop_trace(gc)("Thread %d started prepare_for_concurrent_evacuation", Thread::current()->osthread()->thread_id());

  if (!cancelled_concgc()) {
    // Allocations might have happened before we STWed here, record peak:
    shenandoahPolicy()->record_peak_occupancy();

    ensure_parsability(true);

    if (ShenandoahVerify) {
      verifier()->verify_after_concmark();
    }

    recycle_cset_regions();

    // NOTE: This needs to be done during a stop the world pause, because
    // putting regions into the collection set concurrently with Java threads
    // will create a race. In particular, acmp could fail because when we
    // resolve the first operand, the containing region might not yet be in
    // the collection set, and thus return the original oop. When the 2nd
    // operand gets resolved, the region could be in the collection set
    // and the oop gets evacuated. If both operands have originally been
    // the same, we get false negatives.

    {
      ShenandoahHeapLocker locker(lock());
      _collection_set->clear();
      _free_regions->clear();

#ifdef ASSERT
      ShenandoahCheckCollectionSetClosure ccsc;
      _ordered_regions->heap_region_iterate(&ccsc);
#endif

      _shenandoah_policy->choose_collection_set(_collection_set);
      _shenandoah_policy->choose_free_set(_free_regions);
    }

    if (UseShenandoahMatrix) {
      _collection_set->print_on(tty);
    }

    _bytes_allocated_since_cm = 0;

    Universe::update_heap_info_at_gc();

    if (ShenandoahVerify) {
      verifier()->verify_before_evacuation();
    }
  }
}


class ShenandoahRetireTLABClosure : public ThreadClosure {
private:
  bool _retire;

public:
  ShenandoahRetireTLABClosure(bool retire) : _retire(retire) {}

  void do_thread(Thread* thread) {
    assert(thread->gclab().is_initialized(), err_msg("GCLAB should be initialized for %s", thread->name()));
    thread->gclab().make_parsable(_retire);
  }
};

void ShenandoahHeap::ensure_parsability(bool retire_tlabs) {
  if (UseTLAB) {
    CollectedHeap::ensure_parsability(retire_tlabs);
    ShenandoahRetireTLABClosure cl(retire_tlabs);
    Threads::java_threads_do(&cl);
    gc_threads_do(&cl);
  }
}

class ShenandoahEvacuateUpdateRootsTask : public AbstractGangTask {
  ShenandoahRootEvacuator* _rp;
public:

  ShenandoahEvacuateUpdateRootsTask(ShenandoahRootEvacuator* rp) :
    AbstractGangTask("Shenandoah evacuate and update roots"),
    _rp(rp)
  {
    // Nothing else to do.
  }

  void work(uint worker_id) {
    ShenandoahEvacuateUpdateRootsClosure cl;

    if (ShenandoahConcurrentEvacCodeRoots) {
      _rp->process_evacuate_roots(&cl, NULL, worker_id);
    } else {
      MarkingCodeBlobClosure blobsCl(&cl, CodeBlobToOopClosure::FixRelocations);
      _rp->process_evacuate_roots(&cl, &blobsCl, worker_id);
    }
  }
};

class ShenandoahFixRootsTask : public AbstractGangTask {
  ShenandoahRootEvacuator* _rp;
public:

  ShenandoahFixRootsTask(ShenandoahRootEvacuator* rp) :
    AbstractGangTask("Shenandoah update roots"),
    _rp(rp)
  {
    // Nothing else to do.
  }

  void work(uint worker_id) {
    ShenandoahUpdateRefsClosure cl;
    MarkingCodeBlobClosure blobsCl(&cl, CodeBlobToOopClosure::FixRelocations);

    _rp->process_evacuate_roots(&cl, &blobsCl, worker_id);
  }
};
void ShenandoahHeap::evacuate_and_update_roots() {

  COMPILER2_PRESENT(DerivedPointerTable::clear());

  assert(SafepointSynchronize::is_at_safepoint(), "Only iterate roots while world is stopped");

  {
    ShenandoahRootEvacuator rp(this, workers()->active_workers(), ShenandoahCollectorPolicy::init_evac);
    ShenandoahEvacuateUpdateRootsTask roots_task(&rp);
    workers()->run_task(&roots_task);
  }

  COMPILER2_PRESENT(DerivedPointerTable::update_pointers());

  if (cancelled_concgc()) {
    // If initial evacuation has been cancelled, we need to update all references
    // after all workers have finished. Otherwise we might run into the following problem:
    // GC thread 1 cannot allocate anymore, thus evacuation fails, leaves from-space ptr of object X.
    // GC thread 2 evacuates the same object X to to-space
    // which leaves a truly dangling from-space reference in the first root oop*. This must not happen.
    // clear() and update_pointers() must always be called in pairs,
    // cannot nest with above clear()/update_pointers().
    COMPILER2_PRESENT(DerivedPointerTable::clear());
    ShenandoahRootEvacuator rp(this, workers()->active_workers(), ShenandoahCollectorPolicy::init_evac);
    ShenandoahFixRootsTask update_roots_task(&rp);
    workers()->run_task(&update_roots_task);
    COMPILER2_PRESENT(DerivedPointerTable::update_pointers());
  }
}


void ShenandoahHeap::do_evacuation() {
  ShenandoahGCPhase conc_evac_phase(ShenandoahCollectorPolicy::conc_evac);

  if (ShenandoahLogTrace) {
    ResourceMark rm;
    outputStream* out = gclog_or_tty;
    out->print_cr("All available regions:");
    print_heap_regions_on(out);
  }

  if (ShenandoahLogTrace) {
    ResourceMark rm;
    outputStream* out = gclog_or_tty;
    out->print_cr("Collection set ("SIZE_FORMAT" regions):", _collection_set->count());
    _collection_set->print_on(out);

    out->print_cr("Free set ("SIZE_FORMAT" regions):", _free_regions->count());
    _free_regions->print_on(out);
  }

  ShenandoahParallelEvacuationTask task(this, _collection_set);
  workers()->run_task(&task);

  if (ShenandoahLogTrace) {
    ResourceMark rm;
    outputStream* out = gclog_or_tty;
    out->print_cr("After evacuation collection set ("SIZE_FORMAT" regions):",
                  _collection_set->count());
    _collection_set->print_on(out);

    out->print_cr("After evacuation free set ("SIZE_FORMAT" regions):",
                  _free_regions->count());
    _free_regions->print_on(out);
  }

  if (ShenandoahLogTrace) {
    ResourceMark rm;
    outputStream* out = gclog_or_tty;
    out->print_cr("All regions after evacuation:");
    print_heap_regions_on(out);
  }
}

void ShenandoahHeap::roots_iterate(OopClosure* cl) {
  assert(SafepointSynchronize::is_at_safepoint(), "Only iterate roots while world is stopped");

  CodeBlobToOopClosure blobsCl(cl, false);
  CLDToOopClosure cldCl(cl);

  ShenandoahRootProcessor rp(this, 1, ShenandoahCollectorPolicy::_num_phases);
  rp.process_all_roots(cl, NULL, &cldCl, &blobsCl, 0);
}

bool ShenandoahHeap::supports_tlab_allocation() const {
  return true;
}

size_t  ShenandoahHeap::unsafe_max_tlab_alloc(Thread *thread) const {
  return MIN2(_free_regions->unsafe_peek_free(), max_tlab_size());
}

size_t ShenandoahHeap::max_tlab_size() const {
  return ShenandoahHeapRegion::humongous_threshold_bytes();
}

class ShenandoahResizeGCLABClosure : public ThreadClosure {
public:
  void do_thread(Thread* thread) {
    assert(thread->gclab().is_initialized(), err_msg("GCLAB should be initialized for %s", thread->name()));
    thread->gclab().resize();
  }
};

void ShenandoahHeap::resize_all_tlabs() {
  CollectedHeap::resize_all_tlabs();

  ShenandoahResizeGCLABClosure cl;
  Threads::java_threads_do(&cl);
  gc_threads_do(&cl);
}

class ShenandoahAccumulateStatisticsGCLABClosure : public ThreadClosure {
public:
  void do_thread(Thread* thread) {
    assert(thread->gclab().is_initialized(), err_msg("GCLAB should be initialized for %s", thread->name()));
    thread->gclab().accumulate_statistics();
    thread->gclab().initialize_statistics();
  }
};

void ShenandoahHeap::accumulate_statistics_all_gclabs() {
  ShenandoahAccumulateStatisticsGCLABClosure cl;
  Threads::java_threads_do(&cl);
  gc_threads_do(&cl);
}

bool  ShenandoahHeap::can_elide_tlab_store_barriers() const {
  return true;
}

oop ShenandoahHeap::new_store_pre_barrier(JavaThread* thread, oop new_obj) {
  // Overridden to do nothing.
  return new_obj;
}

bool  ShenandoahHeap::can_elide_initializing_store_barrier(oop new_obj) {
  return true;
}

bool ShenandoahHeap::card_mark_must_follow_store() const {
  return false;
}

bool ShenandoahHeap::supports_heap_inspection() const {
  return false;
}

void ShenandoahHeap::collect(GCCause::Cause cause) {
  assert(cause != GCCause::_gc_locker, "no JNI critical callback");
  if (GCCause::is_user_requested_gc(cause)) {
    if (!DisableExplicitGC) {
      if (ExplicitGCInvokesConcurrent) {
        _concurrent_gc_thread->do_conc_gc();
      } else {
        _concurrent_gc_thread->do_full_gc(cause);
      }
    }
  } else if (cause == GCCause::_allocation_failure) {
    collector_policy()->set_should_clear_all_soft_refs(true);
    _concurrent_gc_thread->do_full_gc(cause);
  }
}

void ShenandoahHeap::do_full_collection(bool clear_all_soft_refs) {
  //assert(false, "Shouldn't need to do full collections");
}

AdaptiveSizePolicy* ShenandoahHeap::size_policy() {
  Unimplemented();
  return NULL;

}

CollectorPolicy* ShenandoahHeap::collector_policy() const {
  return _shenandoah_policy;
}


HeapWord* ShenandoahHeap::block_start(const void* addr) const {
  Space* sp = heap_region_containing(addr);
  if (sp != NULL) {
    return sp->block_start(addr);
  }
  return NULL;
}

size_t ShenandoahHeap::block_size(const HeapWord* addr) const {
  Space* sp = heap_region_containing(addr);
  assert(sp != NULL, "block_size of address outside of heap");
  return sp->block_size(addr);
}

bool ShenandoahHeap::block_is_obj(const HeapWord* addr) const {
  Space* sp = heap_region_containing(addr);
  return sp->block_is_obj(addr);
}

jlong ShenandoahHeap::millis_since_last_gc() {
  return 0;
}

void ShenandoahHeap::prepare_for_verify() {
  if (SafepointSynchronize::is_at_safepoint() || ! UseTLAB) {
    ensure_parsability(false);
  }
}

void ShenandoahHeap::print_gc_threads_on(outputStream* st) const {
  workers()->print_worker_threads_on(st);
}

void ShenandoahHeap::gc_threads_do(ThreadClosure* tcl) const {
  workers()->threads_do(tcl);
}

void ShenandoahHeap::print_tracing_info() const {
  if (PrintGC || TraceGen0Time || TraceGen1Time) {
    ResourceMark rm;
    outputStream* out = gclog_or_tty;
    _shenandoah_policy->print_tracing_info(out);
  }
}

void ShenandoahHeap::verify(bool silent, VerifyOption vo) {
  if (SafepointSynchronize::is_at_safepoint() || ! UseTLAB) {
    if (ShenandoahVerify) {
      verifier()->verify_generic(vo);
    } else {
      // TODO: Consider allocating verification bitmaps on demand,
      // and turn this on unconditionally.
    }
  }
}
size_t ShenandoahHeap::tlab_capacity(Thread *thr) const {
  return _free_regions->capacity();
}

class ShenandoahIterateObjectClosureRegionClosure: public ShenandoahHeapRegionClosure {
  ObjectClosure* _cl;
public:
  ShenandoahIterateObjectClosureRegionClosure(ObjectClosure* cl) : _cl(cl) {}
  bool heap_region_do(ShenandoahHeapRegion* r) {
    ShenandoahHeap::heap()->marked_object_iterate(r, _cl);
    return false;
  }
};

void ShenandoahHeap::object_iterate(ObjectClosure* cl) {
  ShenandoahIterateObjectClosureRegionClosure blk(cl);
  heap_region_iterate(&blk, false, true);
}

class ShenandoahSafeObjectIterateAdjustPtrsClosure : public MetadataAwareOopClosure {
private:
  ShenandoahHeap* _heap;

public:
  ShenandoahSafeObjectIterateAdjustPtrsClosure() : _heap(ShenandoahHeap::heap()) {}

private:
  template <class T>
  inline void do_oop_work(T* p) {
    T o = oopDesc::load_heap_oop(p);
    if (!oopDesc::is_null(o)) {
      oop obj = oopDesc::decode_heap_oop_not_null(o);
      oopDesc::encode_store_heap_oop(p, BrooksPointer::forwardee(obj));
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

class ShenandoahSafeObjectIterateAndUpdate : public ObjectClosure {
private:
  ObjectClosure* _cl;
public:
  ShenandoahSafeObjectIterateAndUpdate(ObjectClosure *cl) : _cl(cl) {}

  virtual void do_object(oop obj) {
    assert (oopDesc::unsafe_equals(obj, BrooksPointer::forwardee(obj)),
            "avoid double-counting: only non-forwarded objects here");

    // Fix up the ptrs.
    ShenandoahSafeObjectIterateAdjustPtrsClosure adjust_ptrs;
    obj->oop_iterate(&adjust_ptrs);

    // Can reply the object now:
    _cl->do_object(obj);
  }
};

void ShenandoahHeap::safe_object_iterate(ObjectClosure* cl) {
  assert(SafepointSynchronize::is_at_safepoint(), "safe iteration is only available during safepoints");

  // Safe iteration does objects only with correct references.
  // This is why we skip collection set regions that have stale copies of objects,
  // and fix up the pointers in the returned objects.

  ShenandoahSafeObjectIterateAndUpdate safe_cl(cl);
  ShenandoahIterateObjectClosureRegionClosure blk(&safe_cl);
  heap_region_iterate(&blk,
                      /* skip_cset_regions = */ true,
                      /* skip_humongous_continuations = */ true);

  _need_update_refs = false; // already updated the references
}

class ShenandoahIterateOopClosureRegionClosure : public ShenandoahHeapRegionClosure {
  MemRegion _mr;
  ExtendedOopClosure* _cl;
  bool _skip_unreachable_objects;
public:
  ShenandoahIterateOopClosureRegionClosure(ExtendedOopClosure* cl, bool skip_unreachable_objects) :
    _cl(cl), _skip_unreachable_objects(skip_unreachable_objects) {}
  ShenandoahIterateOopClosureRegionClosure(MemRegion mr, ExtendedOopClosure* cl)
    :_mr(mr), _cl(cl) {}
  bool heap_region_do(ShenandoahHeapRegion* r) {
    r->oop_iterate_skip_unreachable(_cl, _skip_unreachable_objects);
    return false;
  }
};

void ShenandoahHeap::oop_iterate(ExtendedOopClosure* cl, bool skip_cset_regions, bool skip_unreachable_objects) {
  ShenandoahIterateOopClosureRegionClosure blk(cl, skip_unreachable_objects);
  heap_region_iterate(&blk, skip_cset_regions, true);
}

class ShenandoahSpaceClosureRegionClosure: public ShenandoahHeapRegionClosure {
  SpaceClosure* _cl;
public:
  ShenandoahSpaceClosureRegionClosure(SpaceClosure* cl) : _cl(cl) {}
  bool heap_region_do(ShenandoahHeapRegion* r) {
    _cl->do_space(r);
    return false;
  }
};

void  ShenandoahHeap::space_iterate(SpaceClosure* cl) {
  ShenandoahSpaceClosureRegionClosure blk(cl);
  heap_region_iterate(&blk);
}

Space*  ShenandoahHeap::space_containing(const void* oop) const {
  Space* res = heap_region_containing(oop);
  return res;
}

void  ShenandoahHeap::gc_prologue(bool b) {
  Unimplemented();
}

void  ShenandoahHeap::gc_epilogue(bool b) {
  Unimplemented();
}

// Apply blk->heap_region_do() on all committed regions in address order,
// terminating the iteration early if heap_region_do() returns true.
void ShenandoahHeap::heap_region_iterate(ShenandoahHeapRegionClosure* blk, bool skip_cset_regions, bool skip_humongous_continuation) const {
  for (size_t i = 0; i < num_regions(); i++) {
    ShenandoahHeapRegion* current  = _ordered_regions->get(i);
    if (skip_humongous_continuation && current->is_humongous_continuation()) {
      continue;
    }
    if (skip_cset_regions && in_collection_set(current)) {
      continue;
    }
    if (blk->heap_region_do(current)) {
      return;
    }
  }
}

class ShenandoahClearLivenessClosure : public ShenandoahHeapRegionClosure {
private:
  ShenandoahHeap* sh;
public:
  ShenandoahClearLivenessClosure(ShenandoahHeap* heap) : sh(heap) {}

  bool heap_region_do(ShenandoahHeapRegion* r) {
    r->clear_live_data();
    sh->set_next_top_at_mark_start(r->bottom(), r->top());
    return false;
  }
};


void ShenandoahHeap::start_concurrent_marking() {
  if (ShenandoahVerify) {
    verifier()->verify_before_concmark();
  }

  {
    ShenandoahGCPhase phase(ShenandoahCollectorPolicy::accumulate_stats);
    accumulate_statistics_all_tlabs();
  }

  set_concurrent_mark_in_progress(true);
  // We need to reset all TLABs because we'd lose marks on all objects allocated in them.
  if (UseTLAB) {
    ShenandoahGCPhase phase(ShenandoahCollectorPolicy::make_parsable);
    ensure_parsability(true);
  }

  _shenandoah_policy->record_bytes_allocated(_bytes_allocated_since_cm);
  _used_start_gc = used();

  {
    ShenandoahGCPhase phase(ShenandoahCollectorPolicy::clear_liveness);
    ShenandoahClearLivenessClosure clc(this);
    heap_region_iterate(&clc);
  }

  // Make above changes visible to worker threads
  OrderAccess::fence();

  concurrentMark()->init_mark_roots();

  if (UseTLAB) {
    ShenandoahGCPhase phase(ShenandoahCollectorPolicy::resize_tlabs);
    resize_all_tlabs();
  }
}

void ShenandoahHeap::swap_mark_bitmaps() {
  // Swap bitmaps.
  MarkBitMap* tmp1 = _complete_mark_bit_map;
  _complete_mark_bit_map = _next_mark_bit_map;
  _next_mark_bit_map = tmp1;

  // Swap top-at-mark-start pointers
  HeapWord** tmp2 = _complete_top_at_mark_starts;
  _complete_top_at_mark_starts = _next_top_at_mark_starts;
  _next_top_at_mark_starts = tmp2;

  HeapWord** tmp3 = _complete_top_at_mark_starts_base;
  _complete_top_at_mark_starts_base = _next_top_at_mark_starts_base;
  _next_top_at_mark_starts_base = tmp3;
}

void ShenandoahHeap::stop_concurrent_marking() {
  assert(concurrent_mark_in_progress(), "How else could we get here?");
  if (! cancelled_concgc()) {
    // If we needed to update refs, and concurrent marking has been cancelled,
    // we need to finish updating references.
    set_need_update_refs(false);
    swap_mark_bitmaps();
  }
  set_concurrent_mark_in_progress(false);

  if (ShenandoahLogTrace) {
    ResourceMark rm;
    outputStream* out = gclog_or_tty;
    out->print_cr("Regions at stopping the concurrent mark:");
    print_heap_regions_on(out);
  }
}

void ShenandoahHeap::set_concurrent_mark_in_progress(bool in_progress) {
  _concurrent_mark_in_progress = in_progress ? 1 : 0;
  JavaThread::satb_mark_queue_set().set_active_all_threads(in_progress, !in_progress);
}

void ShenandoahHeap::set_evacuation_in_progress_concurrently(bool in_progress) {
  // Note: it is important to first release the _evacuation_in_progress flag here,
  // so that Java threads can get out of oom_during_evacuation() and reach a safepoint,
  // in case a VM task is pending.
  set_evacuation_in_progress(in_progress);
  MutexLocker mu(Threads_lock);
  JavaThread::set_evacuation_in_progress_all_threads(in_progress);
}

void ShenandoahHeap::set_evacuation_in_progress_at_safepoint(bool in_progress) {
  assert(SafepointSynchronize::is_at_safepoint(), "Only call this at safepoint");
  set_evacuation_in_progress(in_progress);
  JavaThread::set_evacuation_in_progress_all_threads(in_progress);
}

void ShenandoahHeap::set_evacuation_in_progress(bool in_progress) {
  _evacuation_in_progress = in_progress ? 1 : 0;
  OrderAccess::fence();
}

void ShenandoahHeap::oom_during_evacuation() {
  Thread* t = Thread::current();

  log_develop_trace(gc)("Out of memory during evacuation, cancel evacuation, schedule full GC by thread %d",
                        t->osthread()->thread_id());

  // We ran out of memory during evacuation. Cancel evacuation, and schedule a full-GC.
  collector_policy()->set_should_clear_all_soft_refs(true);
  concurrent_thread()->try_set_full_gc();
  cancel_concgc(_oom_evacuation);

  if (!t->is_GC_task_thread() && !t->is_ConcurrentGC_thread() && t != concurrent_thread()->slt()) {
    assert(! Threads_lock->owned_by_self()
           || SafepointSynchronize::is_at_safepoint(), "must not hold Threads_lock here");
    log_warning(gc)("OOM during evacuation. Let Java thread wait until evacuation finishes.");
    while (_evacuation_in_progress) { // wait.
      t->_ParkEvent->park(1);
    }
  }

  // Special case for SurrogateLockerThread that may evacuate in VMOperation prolog:
  // if OOM happened during evacuation in SLT, we ignore it, and let the whole thing
  // slide into Full GC. Dropping evac_in_progress flag helps to avoid another OOME
  // when Full GC VMOperation is executed.
  if (t == concurrent_thread()->slt()) {
    set_evacuation_in_progress_concurrently(false);
  }
}

HeapWord* ShenandoahHeap::tlab_post_allocation_setup(HeapWord* obj) {
  // Initialize Brooks pointer for the next object
  HeapWord* result = obj + BrooksPointer::word_size();
  BrooksPointer::initialize(oop(result));
  return result;
}

uint ShenandoahHeap::oop_extra_words() {
  return BrooksPointer::word_size();
}

ShenandoahForwardedIsAliveClosure::ShenandoahForwardedIsAliveClosure() :
  _heap(ShenandoahHeap::heap_no_check()) {
}

bool ShenandoahForwardedIsAliveClosure::do_object_b(oop obj) {
  assert(_heap != NULL, "sanity");
  obj = ShenandoahBarrierSet::resolve_oop_static_not_null(obj);
#ifdef ASSERT
  if (_heap->concurrent_mark_in_progress()) {
    assert(oopDesc::unsafe_equals(obj, ShenandoahBarrierSet::resolve_oop_static_not_null(obj)), "only query to-space");
  }
#endif
  assert(!oopDesc::is_null(obj), "null");
  return _heap->is_marked_next(obj);
}

ShenandoahIsAliveClosure::ShenandoahIsAliveClosure() :
  _heap(ShenandoahHeap::heap_no_check()) {
}

bool ShenandoahIsAliveClosure::do_object_b(oop obj) {
  assert(_heap != NULL, "sanity");
  assert(!oopDesc::is_null(obj), "null");
  assert(oopDesc::unsafe_equals(obj, ShenandoahBarrierSet::resolve_oop_static_not_null(obj)), "only query to-space");
  return _heap->is_marked_next(obj);
}

BoolObjectClosure* ShenandoahHeap::is_alive_closure() {
  return need_update_refs() ?
         (BoolObjectClosure*) &_forwarded_is_alive :
         (BoolObjectClosure*) &_is_alive;
}

void ShenandoahHeap::ref_processing_init() {
  MemRegion mr = reserved_region();

  _forwarded_is_alive.init(ShenandoahHeap::heap());
  _is_alive.init(ShenandoahHeap::heap());
  assert(_max_workers > 0, "Sanity");

  _ref_processor =
    new ReferenceProcessor(mr,    // span
                           ParallelRefProcEnabled,  // MT processing
                           _max_workers,            // Degree of MT processing
                           true,                    // MT discovery
                           _max_workers,            // Degree of MT discovery
                           false,                   // Reference discovery is not atomic
                           &_forwarded_is_alive);   // Pessimistically assume "forwarded"
}

void ShenandoahHeap::acquire_pending_refs_lock() {
  _concurrent_gc_thread->slt()->manipulatePLL(SurrogateLockerThread::acquirePLL);
}

void ShenandoahHeap::release_pending_refs_lock() {
  _concurrent_gc_thread->slt()->manipulatePLL(SurrogateLockerThread::releaseAndNotifyPLL);
}

GCTracer* ShenandoahHeap::tracer() {
  return shenandoahPolicy()->tracer();
}

size_t ShenandoahHeap::tlab_used(Thread* thread) const {
  return _free_regions->used();
}

void ShenandoahHeap::cancel_concgc(GCCause::Cause cause) {
  if (try_cancel_concgc()) {
    log_info(gc)("Cancelling concurrent GC: %s", GCCause::to_string(cause));
    _shenandoah_policy->report_concgc_cancelled();
  }
}

void ShenandoahHeap::cancel_concgc(ShenandoahCancelCause cause) {
  if (try_cancel_concgc()) {
    log_info(gc)("Cancelling concurrent GC: %s", cancel_cause_to_string(cause));
    _shenandoah_policy->report_concgc_cancelled();
  }
}

const char* ShenandoahHeap::cancel_cause_to_string(ShenandoahCancelCause cause) {
  switch (cause) {
    case _oom_evacuation:
      return "Out of memory for evacuation";
    case _vm_stop:
      return "Stopping VM";
    default:
      return "Unknown";
  }
}

uint ShenandoahHeap::max_workers() {
  return _max_workers;
}

void ShenandoahHeap::stop() {
  // The shutdown sequence should be able to terminate when GC is running.

  // Step 0. Notify policy to disable event recording.
  _shenandoah_policy->record_shutdown();

  // Step 1. Notify control thread that we are in shutdown.
  // Note that we cannot do that with stop(), because stop() is blocking and waits for the actual shutdown.
  // Doing stop() here would wait for the normal GC cycle to complete, never falling through to cancel below.
  _concurrent_gc_thread->prepare_for_graceful_shutdown();

  // Step 2. Notify GC workers that we are cancelling GC.
  cancel_concgc(_vm_stop);

  // Step 3. Wait until GC worker exits normally.
  _concurrent_gc_thread->stop();
}

void ShenandoahHeap::unload_classes_and_cleanup_tables(bool full_gc) {
  ShenandoahCollectorPolicy::TimingPhase phase_root =
          full_gc ?
          ShenandoahCollectorPolicy::full_gc_purge :
          ShenandoahCollectorPolicy::purge;

  ShenandoahCollectorPolicy::TimingPhase phase_unload =
          full_gc ?
          ShenandoahCollectorPolicy::full_gc_purge_class_unload :
          ShenandoahCollectorPolicy::purge_class_unload;

  ShenandoahCollectorPolicy::TimingPhase phase_cldg =
          full_gc ?
          ShenandoahCollectorPolicy::full_gc_purge_cldg :
          ShenandoahCollectorPolicy::purge_cldg;

  ShenandoahCollectorPolicy::TimingPhase phase_par =
          full_gc ?
          ShenandoahCollectorPolicy::full_gc_purge_par :
          ShenandoahCollectorPolicy::purge_par;

  ShenandoahCollectorPolicy::TimingPhase phase_par_classes =
          full_gc ?
          ShenandoahCollectorPolicy::full_gc_purge_par_classes :
          ShenandoahCollectorPolicy::purge_par_classes;

  ShenandoahCollectorPolicy::TimingPhase phase_par_codecache =
          full_gc ?
          ShenandoahCollectorPolicy::full_gc_purge_par_codecache :
          ShenandoahCollectorPolicy::purge_par_codecache;

  ShenandoahCollectorPolicy::TimingPhase phase_par_symbstring =
          full_gc ?
          ShenandoahCollectorPolicy::full_gc_purge_par_symbstring :
          ShenandoahCollectorPolicy::purge_par_symbstring;

  ShenandoahCollectorPolicy::TimingPhase phase_par_sync =
          full_gc ?
          ShenandoahCollectorPolicy::full_gc_purge_par_sync :
          ShenandoahCollectorPolicy::purge_par_sync;

  ShenandoahGCPhase root_phase(phase_root);

  BoolObjectClosure* is_alive = is_alive_closure();

  bool purged_class;

  // Unload classes and purge SystemDictionary.
  {
    ShenandoahGCPhase phase(phase_unload);
    purged_class = SystemDictionary::do_unloading(is_alive, true);
  }

  {
    ShenandoahGCPhase phase(phase_par);
    uint active = _workers->active_workers();
    ParallelCleaningTask unlink_task(is_alive, true, true, active, purged_class);
    _workers->run_task(&unlink_task);

    ShenandoahCollectorPolicy* p = ShenandoahHeap::heap()->shenandoahPolicy();
    ParallelCleaningTimes times = unlink_task.times();

    // "times" report total time, phase_tables_cc reports wall time. Divide total times
    // by active workers to get average time per worker, that would add up to wall time.
    p->record_phase_time(phase_par_classes,    times.klass_work_us() / active);
    p->record_phase_time(phase_par_codecache,  times.codecache_work_us() / active);
    p->record_phase_time(phase_par_symbstring, times.tables_work_us() / active);
    p->record_phase_time(phase_par_sync,       times.sync_us() / active);
  }

  {
    ShenandoahGCPhase phase(phase_cldg);
    ClassLoaderDataGraph::purge();
  }
}

void ShenandoahHeap::set_need_update_refs(bool need_update_refs) {
  _need_update_refs = need_update_refs;
}

//fixme this should be in heapregionset
ShenandoahHeapRegion* ShenandoahHeap::next_compaction_region(const ShenandoahHeapRegion* r) {
  size_t region_idx = r->region_number() + 1;
  ShenandoahHeapRegion* next = _ordered_regions->get(region_idx);
  guarantee(next->region_number() == region_idx, "region number must match");
  while (next->is_humongous()) {
    region_idx = next->region_number() + 1;
    next = _ordered_regions->get(region_idx);
    guarantee(next->region_number() == region_idx, "region number must match");
  }
  return next;
}

ShenandoahMonitoringSupport* ShenandoahHeap::monitoring_support() {
  return _monitoring_support;
}

MarkBitMap* ShenandoahHeap::complete_mark_bit_map() {
  return _complete_mark_bit_map;
}

MarkBitMap* ShenandoahHeap::next_mark_bit_map() {
  return _next_mark_bit_map;
}

void ShenandoahHeap::add_free_region(ShenandoahHeapRegion* r) {
  _free_regions->add_region(r);
}

void ShenandoahHeap::clear_free_regions() {
  _free_regions->clear();
}

address ShenandoahHeap::in_cset_fast_test_addr() {
  ShenandoahHeap* heap = ShenandoahHeap::heap();
  assert(heap->collection_set() != NULL, "Sanity");
  return (address) heap->collection_set()->biased_map_address();
}

address ShenandoahHeap::cancelled_concgc_addr() {
  return (address) &(ShenandoahHeap::heap()->_cancelled_concgc);
}


size_t ShenandoahHeap::conservative_max_heap_alignment() {
  return ShenandoahMaxRegionSize;
}

size_t ShenandoahHeap::bytes_allocated_since_cm() {
  return _bytes_allocated_since_cm;
}

void ShenandoahHeap::set_bytes_allocated_since_cm(size_t bytes) {
  _bytes_allocated_since_cm = bytes;
}

void ShenandoahHeap::set_next_top_at_mark_start(HeapWord* region_base, HeapWord* addr) {
  uintx index = ((uintx) region_base) >> ShenandoahHeapRegion::region_size_bytes_shift();
  _next_top_at_mark_starts[index] = addr;
}

HeapWord* ShenandoahHeap::next_top_at_mark_start(HeapWord* region_base) {
  uintx index = ((uintx) region_base) >> ShenandoahHeapRegion::region_size_bytes_shift();
  return _next_top_at_mark_starts[index];
}

void ShenandoahHeap::set_complete_top_at_mark_start(HeapWord* region_base, HeapWord* addr) {
  uintx index = ((uintx) region_base) >> ShenandoahHeapRegion::region_size_bytes_shift();
  _complete_top_at_mark_starts[index] = addr;
}

HeapWord* ShenandoahHeap::complete_top_at_mark_start(HeapWord* region_base) {
  uintx index = ((uintx) region_base) >> ShenandoahHeapRegion::region_size_bytes_shift();
  return _complete_top_at_mark_starts[index];
}

void ShenandoahHeap::set_full_gc_in_progress(bool in_progress) {
  _full_gc_in_progress = in_progress;
}

bool ShenandoahHeap::is_full_gc_in_progress() const {
  return _full_gc_in_progress;
}

void ShenandoahHeap::set_update_refs_in_progress(bool in_progress) {
  _update_refs_in_progress = in_progress;
}

bool ShenandoahHeap::is_update_refs_in_progress() const {
  return _update_refs_in_progress;
}

void ShenandoahHeap::register_nmethod(nmethod* nm) {
  ShenandoahCodeRoots::add_nmethod(nm);
}

void ShenandoahHeap::unregister_nmethod(nmethod* nm) {
  ShenandoahCodeRoots::remove_nmethod(nm);
}

void ShenandoahHeap::pin_object(oop o) {
  ShenandoahHeapLocker locker(lock());
  heap_region_containing(o)->make_pinned();
}

void ShenandoahHeap::unpin_object(oop o) {
  ShenandoahHeapLocker locker(lock());
  heap_region_containing(o)->make_unpinned();
}

GCTimer* ShenandoahHeap::gc_timer() const {
  return _gc_timer;
}

class ShenandoahCountGarbageClosure : public ShenandoahHeapRegionClosure {
private:
  size_t            _garbage;
public:
  ShenandoahCountGarbageClosure() : _garbage(0) {
  }

  bool heap_region_do(ShenandoahHeapRegion* r) {
    if (r->is_regular()) {
      _garbage += r->garbage();
    }
    return false;
  }

  size_t garbage() {
    return _garbage;
  }
};

size_t ShenandoahHeap::garbage() {
  ShenandoahCountGarbageClosure cl;
  heap_region_iterate(&cl);
  return cl.garbage();
}

ShenandoahUpdateHeapRefsClosure::ShenandoahUpdateHeapRefsClosure() :
  _heap(ShenandoahHeap::heap()) {}

ShenandoahVerifier* ShenandoahHeap::verifier() {
  guarantee(ShenandoahVerify, "Should be enabled");
  assert (_verifier != NULL, "sanity");
  return _verifier;
}

class ShenandoahUpdateHeapRefsTask : public AbstractGangTask {
private:
  ShenandoahHeap* _heap;
  ShenandoahHeapRegionSet* _regions;

public:
  ShenandoahUpdateHeapRefsTask(ShenandoahHeapRegionSet* regions) :
    AbstractGangTask("Concurrent Update References Task"),
    _heap(ShenandoahHeap::heap()),
    _regions(regions) {
  }

  void work(uint worker_id) {
    ShenandoahUpdateHeapRefsClosure cl;
    ShenandoahHeapRegion* r = _regions->claim_next();
    while (r != NULL) {
      if (_heap->in_collection_set(r)) {
        HeapWord* bottom = r->bottom();
        HeapWord* top = _heap->complete_top_at_mark_start(r->bottom());
        if (top > bottom) {
          _heap->complete_mark_bit_map()->clear_range_large(MemRegion(bottom, top));
        }
      } else {
        if (r->is_active()) {
          _heap->marked_object_oop_safe_iterate(r, &cl);
        }
      }
      if (_heap->cancelled_concgc()) {
        return;
      }
      r = _regions->claim_next();
    }
  }
};

void ShenandoahHeap::update_heap_references(ShenandoahHeapRegionSet* update_regions) {
  ShenandoahUpdateHeapRefsTask task(update_regions);
  workers()->run_task(&task);
}

void ShenandoahHeap::concurrent_update_heap_references() {
  ShenandoahGCPhase phase(ShenandoahCollectorPolicy::conc_update_refs);
  ShenandoahHeapRegionSet* update_regions = regions();
  update_regions->clear_current_index();
  update_heap_references(update_regions);
}

void ShenandoahHeap::prepare_update_refs() {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint");

  if (ShenandoahVerify) {
    verifier()->verify_before_updaterefs();
  }

  set_evacuation_in_progress_at_safepoint(false);
  set_update_refs_in_progress(true);
  ensure_parsability(true);
  for (uint i = 0; i < num_regions(); i++) {
    ShenandoahHeapRegion* r = _ordered_regions->get(i);
    r->set_concurrent_iteration_safe_limit(r->top());
  }
}

void ShenandoahHeap::finish_update_refs() {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint");

  if (cancelled_concgc()) {
    ShenandoahGCPhase final_work(ShenandoahCollectorPolicy::final_update_refs_finish_work);

    // Finish updating references where we left off.
    clear_cancelled_concgc();
    ShenandoahHeapRegionSet* update_regions = regions();
    update_heap_references(update_regions);
  }

  assert(! cancelled_concgc(), "Should have been done right before");
  concurrentMark()->update_roots(ShenandoahCollectorPolicy::final_update_refs_roots);

  // Allocations might have happened before we STWed here, record peak:
  shenandoahPolicy()->record_peak_occupancy();

  ShenandoahGCPhase final_update_refs(ShenandoahCollectorPolicy::final_update_refs_recycle);

  recycle_cset_regions();
  set_need_update_refs(false);

  if (ShenandoahVerify) {
    verifier()->verify_after_updaterefs();
  }

  {
    // Rebuild the free set
    ShenandoahHeapLocker locker(lock());
    _free_regions->clear();
    size_t end = _ordered_regions->active_regions();
    for (size_t i = 0; i < end; i++) {
      ShenandoahHeapRegion* r = _ordered_regions->get(i);
      if (!r->is_humongous()) {
        assert (!in_collection_set(r), "collection set should be clear");
        _free_regions->add_region(r);
      }
    }
  }
  set_update_refs_in_progress(false);
}

#ifdef ASSERT
void ShenandoahHeap::assert_heaplock_owned_by_current_thread() {
  _lock.assert_owned_by_current_thread();
}

void ShenandoahHeap::assert_heaplock_or_safepoint() {
  _lock.assert_owned_by_current_thread_or_safepoint();
}
#endif

void ShenandoahHeap::start_deferred_recycling() {
  assert_heaplock_owned_by_current_thread();
  _recycled_region_count = 0;
}

void ShenandoahHeap::immediate_recycle(ShenandoahHeapRegion* r) {
  assert_heaplock_owned_by_current_thread();
  r->make_trash();
  r->recycle();
}

void ShenandoahHeap::defer_recycle(ShenandoahHeapRegion* r) {
  assert_heaplock_owned_by_current_thread();
  r->make_trash();
  _recycled_regions[_recycled_region_count++] = r->region_number();
}

void ShenandoahHeap::finish_deferred_recycle() {
  assert_heaplock_owned_by_current_thread();
  for (size_t i = 0; i < _recycled_region_count; i++) {
    regions()->get(_recycled_regions[i])->recycle();
  }
}


void ShenandoahHeap::print_extended_on(outputStream *st) const {
  print_on(st);
  print_heap_regions_on(st);
}
