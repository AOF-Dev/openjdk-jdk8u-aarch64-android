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

#ifndef SHARE_VM_GC_SHENANDOAH_SHENANDOAHCONCURRENTMARK_HPP
#define SHARE_VM_GC_SHENANDOAH_SHENANDOAHCONCURRENTMARK_HPP

#include "utilities/taskqueue.hpp"
#include "utilities/workgroup.hpp"
#include "gc_implementation/shenandoah/shenandoahTaskqueue.hpp"
#include "gc_implementation/shenandoah/shenandoahCollectorPolicy.hpp"

class ShenandoahConcurrentMark;

class ShenandoahConcurrentMark: public CHeapObj<mtGC> {

private:
  ShenandoahHeap* _heap;

  // The per-worker-thread work queues
  ShenandoahObjToScanQueueSet* _task_queues;

  bool _process_references;
  bool _unload_classes;

  volatile jbyte _claimed_codecache;

  // Used for buffering per-region liveness data.
  // Needed since ShenandoahHeapRegion uses atomics to update liveness.
  //
  // The array has max-workers elements, each of which is an array of
  // jushort * max_regions. The choice of jushort is not accidental:
  // there is a tradeoff between static/dynamic footprint that translates
  // into cache pressure (which is already high during marking), and
  // too many atomic updates. size_t/jint is too large, jbyte is too small.
  jushort** _liveness_local;

private:
  template <class T, bool COUNT_LIVENESS>
  inline void do_task(ShenandoahObjToScanQueue* q, T* cl, jushort* live_data, ShenandoahMarkTask* task);

  template <class T>
  inline void do_chunked_array_start(ShenandoahObjToScanQueue* q, T* cl, oop array);

  template <class T>
  inline void do_chunked_array(ShenandoahObjToScanQueue* q, T* cl, oop array, int chunk, int pow);

  inline void count_liveness(jushort* live_data, oop obj);

  // Actual mark loop with closures set up
  template <class T, bool CANCELLABLE, bool DRAIN_SATB, bool COUNT_LIVENESS>
  void mark_loop_work(T* cl, jushort* live_data, uint worker_id, ParallelTaskTerminator *t);

  template <bool CANCELLABLE, bool DRAIN_SATB, bool COUNT_LIVENESS, bool CLASS_UNLOAD, bool UPDATE_REFS>
  void mark_loop_prework(uint worker_id, ParallelTaskTerminator *terminator, ReferenceProcessor *rp);

  // ------------------------ Currying dynamic arguments to template args ----------------------------

  template <bool B1, bool B2, bool B3, bool B4>
  void mark_loop_4(uint w, ParallelTaskTerminator* t, ReferenceProcessor* rp, bool b5) {
    if (b5) {
      mark_loop_prework<B1, B2, B3, B4, true>(w, t, rp);
    } else {
      mark_loop_prework<B1, B2, B3, B4, false>(w, t, rp);
    }
  };

  template <bool B1, bool B2, bool B3>
  void mark_loop_3(uint w, ParallelTaskTerminator* t, ReferenceProcessor* rp, bool b4, bool b5) {
    if (b4) {
      mark_loop_4<B1, B2, B3, true>(w, t, rp, b5);
    } else {
      mark_loop_4<B1, B2, B3, false>(w, t, rp, b5);
    }
  };

  template <bool B1, bool B2>
  void mark_loop_2(uint w, ParallelTaskTerminator* t, ReferenceProcessor* rp, bool b3, bool b4, bool b5) {
    if (b3) {
      mark_loop_3<B1, B2, true>(w, t, rp, b4, b5);
    } else {
      mark_loop_3<B1, B2, false>(w, t, rp, b4, b5);
    }
  };

  template <bool B1>
  void mark_loop_1(uint w, ParallelTaskTerminator* t, ReferenceProcessor* rp, bool b2, bool b3, bool b4, bool b5) {
    if (b2) {
      mark_loop_2<B1, true>(w, t, rp, b3, b4, b5);
    } else {
      mark_loop_2<B1, false>(w, t, rp, b3, b4, b5);
    }
  };

  // ------------------------ END: Currying dynamic arguments to template args ----------------------------

public:
  // We need to do this later when the heap is already created.
  void initialize(uint workers);

  void set_process_references(bool pr);
  bool process_references() const;

  void set_unload_classes(bool uc);
  bool unload_classes() const;

  bool claim_codecache();
  void clear_claim_codecache();

  template<class T, UpdateRefsMode UPDATE_REFS>
  static inline void mark_through_ref(T* p, ShenandoahHeap* heap, ShenandoahObjToScanQueue* q);

  void mark_from_roots();

  // Prepares unmarked root objects by marking them and putting
  // them into the marking task queue.
  void init_mark_roots();
  void mark_roots(ShenandoahCollectorPolicy::TimingPhase root_phase);
  void update_roots(ShenandoahCollectorPolicy::TimingPhase root_phase);

  void shared_finish_mark_from_roots(bool full_gc);
  void finish_mark_from_roots();
  // Those are only needed public because they're called from closures.

  // Mark loop entry.
  // Translates dynamic arguments to template parameters with progressive currying.
  void mark_loop(uint worker_id, ParallelTaskTerminator* terminator, ReferenceProcessor *rp,
                 bool cancellable, bool drain_satb, bool count_liveness, bool class_unload, bool update_refs) {
    if (cancellable) {
      mark_loop_1<true>(worker_id, terminator, rp, drain_satb, count_liveness, class_unload, update_refs);
    } else {
      mark_loop_1<false>(worker_id, terminator, rp, drain_satb, count_liveness, class_unload, update_refs);
    }
  }

  inline bool try_queue(ShenandoahObjToScanQueue* q, ShenandoahMarkTask &task);

  ShenandoahObjToScanQueue* get_queue(uint worker_id);
  void clear_queue(ShenandoahObjToScanQueue *q);

  inline bool try_draining_satb_buffer(ShenandoahObjToScanQueue *q, ShenandoahMarkTask &task);
  void drain_satb_buffers(uint worker_id, bool remark = false);
  ShenandoahObjToScanQueueSet* task_queues() { return _task_queues;}

  jushort* get_liveness(uint worker_id);

  void cancel();

  void preclean_weak_refs();

private:

  void weak_refs_work(bool full_gc);
  void weak_refs_work_doit(bool full_gc);

#if TASKQUEUE_STATS
  static void print_taskqueue_stats_hdr(outputStream* const st);
  void print_taskqueue_stats() const;
  void reset_taskqueue_stats();
#endif // TASKQUEUE_STATS

};

#endif // SHARE_VM_GC_SHENANDOAH_SHENANDOAHCONCURRENTMARK_HPP
