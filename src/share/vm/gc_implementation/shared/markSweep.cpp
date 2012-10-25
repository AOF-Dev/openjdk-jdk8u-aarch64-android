/*
 * Copyright (c) 1997, 2012, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
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
#include "compiler/compileBroker.hpp"
#include "gc_implementation/shared/markSweep.inline.hpp"
#include "gc_interface/collectedHeap.inline.hpp"
#include "oops/methodData.hpp"
#include "oops/objArrayKlass.inline.hpp"
#include "oops/oop.inline.hpp"

unsigned int            MarkSweep::_total_invocations = 0;

Stack<oop, mtGC>              MarkSweep::_marking_stack;
Stack<ObjArrayTask, mtGC>     MarkSweep::_objarray_stack;

Stack<oop, mtGC>              MarkSweep::_preserved_oop_stack;
Stack<markOop, mtGC>          MarkSweep::_preserved_mark_stack;
size_t                  MarkSweep::_preserved_count = 0;
size_t                  MarkSweep::_preserved_count_max = 0;
PreservedMark*          MarkSweep::_preserved_marks = NULL;
ReferenceProcessor*     MarkSweep::_ref_processor   = NULL;

#ifdef VALIDATE_MARK_SWEEP
GrowableArray<void*>*   MarkSweep::_root_refs_stack = NULL;
GrowableArray<oop> *    MarkSweep::_live_oops = NULL;
GrowableArray<oop> *    MarkSweep::_live_oops_moved_to = NULL;
GrowableArray<size_t>*  MarkSweep::_live_oops_size = NULL;
size_t                  MarkSweep::_live_oops_index = 0;
size_t                  MarkSweep::_live_oops_index_at_perm = 0;
GrowableArray<void*>*   MarkSweep::_other_refs_stack = NULL;
GrowableArray<void*>*   MarkSweep::_adjusted_pointers = NULL;
bool                         MarkSweep::_pointer_tracking = false;
bool                         MarkSweep::_root_tracking = true;

GrowableArray<HeapWord*>* MarkSweep::_cur_gc_live_oops = NULL;
GrowableArray<HeapWord*>* MarkSweep::_cur_gc_live_oops_moved_to = NULL;
GrowableArray<size_t>   * MarkSweep::_cur_gc_live_oops_size = NULL;
GrowableArray<HeapWord*>* MarkSweep::_last_gc_live_oops = NULL;
GrowableArray<HeapWord*>* MarkSweep::_last_gc_live_oops_moved_to = NULL;
GrowableArray<size_t>   * MarkSweep::_last_gc_live_oops_size = NULL;
#endif

MarkSweep::FollowRootClosure  MarkSweep::follow_root_closure;
CodeBlobToOopClosure MarkSweep::follow_code_root_closure(&MarkSweep::follow_root_closure, /*do_marking=*/ true);

void MarkSweep::FollowRootClosure::do_oop(oop* p)       { follow_root(p); }
void MarkSweep::FollowRootClosure::do_oop(narrowOop* p) { follow_root(p); }

MarkSweep::MarkAndPushClosure MarkSweep::mark_and_push_closure;
MarkSweep::FollowKlassClosure MarkSweep::follow_klass_closure;
MarkSweep::AdjustKlassClosure MarkSweep::adjust_klass_closure;

void MarkSweep::MarkAndPushClosure::do_oop(oop* p)       { mark_and_push(p); }
void MarkSweep::MarkAndPushClosure::do_oop(narrowOop* p) { mark_and_push(p); }

void MarkSweep::FollowKlassClosure::do_klass(Klass* klass) {
  klass->oops_do(&MarkSweep::mark_and_push_closure);
}
void MarkSweep::AdjustKlassClosure::do_klass(Klass* klass) {
  klass->oops_do(&MarkSweep::adjust_pointer_closure);
}

void MarkSweep::follow_klass(Klass* klass) {
  ClassLoaderData* cld = klass->class_loader_data();
  // The actual processing of the klass is done when we
  // traverse the list of Klasses in the class loader data.
  MarkSweep::follow_class_loader(cld);
}

void MarkSweep::adjust_klass(Klass* klass) {
  ClassLoaderData* cld = klass->class_loader_data();
  // The actual processing of the klass is done when we
  // traverse the list of Klasses in the class loader data.
  MarkSweep::adjust_class_loader(cld);
}

void MarkSweep::follow_class_loader(ClassLoaderData* cld) {
  cld->oops_do(&MarkSweep::mark_and_push_closure, &MarkSweep::follow_klass_closure, true);
}

void MarkSweep::adjust_class_loader(ClassLoaderData* cld) {
  cld->oops_do(&MarkSweep::adjust_root_pointer_closure, &MarkSweep::adjust_klass_closure, true);
}


void MarkSweep::follow_stack() {
  do {
    while (!_marking_stack.is_empty()) {
      oop obj = _marking_stack.pop();
      assert (obj->is_gc_marked(), "p must be marked");
      obj->follow_contents();
    }
    // Process ObjArrays one at a time to avoid marking stack bloat.
    if (!_objarray_stack.is_empty()) {
      ObjArrayTask task = _objarray_stack.pop();
      objArrayKlass* const k = (objArrayKlass*)task.obj()->klass();
      k->oop_follow_contents(task.obj(), task.index());
    }
  } while (!_marking_stack.is_empty() || !_objarray_stack.is_empty());
}

MarkSweep::FollowStackClosure MarkSweep::follow_stack_closure;

void MarkSweep::FollowStackClosure::do_void() { follow_stack(); }

// We preserve the mark which should be replaced at the end and the location
// that it will go.  Note that the object that this markOop belongs to isn't
// currently at that address but it will be after phase4
void MarkSweep::preserve_mark(oop obj, markOop mark) {
  // We try to store preserved marks in the to space of the new generation since
  // this is storage which should be available.  Most of the time this should be
  // sufficient space for the marks we need to preserve but if it isn't we fall
  // back to using Stacks to keep track of the overflow.
  if (_preserved_count < _preserved_count_max) {
    _preserved_marks[_preserved_count++].init(obj, mark);
  } else {
    _preserved_mark_stack.push(mark);
    _preserved_oop_stack.push(obj);
  }
}

MarkSweep::AdjustPointerClosure MarkSweep::adjust_root_pointer_closure(true);
MarkSweep::AdjustPointerClosure MarkSweep::adjust_pointer_closure(false);

void MarkSweep::AdjustPointerClosure::do_oop(oop* p)       { adjust_pointer(p, _is_root); }
void MarkSweep::AdjustPointerClosure::do_oop(narrowOop* p) { adjust_pointer(p, _is_root); }

void MarkSweep::adjust_marks() {
  assert( _preserved_oop_stack.size() == _preserved_mark_stack.size(),
         "inconsistent preserved oop stacks");

  // adjust the oops we saved earlier
  for (size_t i = 0; i < _preserved_count; i++) {
    _preserved_marks[i].adjust_pointer();
  }

  // deal with the overflow stack
  StackIterator<oop, mtGC> iter(_preserved_oop_stack);
  while (!iter.is_empty()) {
    oop* p = iter.next_addr();
    adjust_pointer(p);
  }
}

void MarkSweep::restore_marks() {
  assert(_preserved_oop_stack.size() == _preserved_mark_stack.size(),
         "inconsistent preserved oop stacks");
  if (PrintGC && Verbose) {
    gclog_or_tty->print_cr("Restoring %d marks",
                           _preserved_count + _preserved_oop_stack.size());
  }

  // restore the marks we saved earlier
  for (size_t i = 0; i < _preserved_count; i++) {
    _preserved_marks[i].restore();
  }

  // deal with the overflow
  while (!_preserved_oop_stack.is_empty()) {
    oop obj       = _preserved_oop_stack.pop();
    markOop mark  = _preserved_mark_stack.pop();
    obj->set_mark(mark);
  }
}

#ifdef VALIDATE_MARK_SWEEP

void MarkSweep::track_adjusted_pointer(void* p, bool isroot) {
  if (!ValidateMarkSweep)
    return;

  if (!isroot) {
    if (_pointer_tracking) {
      guarantee(_adjusted_pointers->contains(p), "should have seen this pointer");
      _adjusted_pointers->remove(p);
    }
  } else {
    ptrdiff_t index = _root_refs_stack->find(p);
    if (index != -1) {
      int l = _root_refs_stack->length();
      if (l > 0 && l - 1 != index) {
        void* last = _root_refs_stack->pop();
        assert(last != p, "should be different");
        _root_refs_stack->at_put(index, last);
      } else {
        _root_refs_stack->remove(p);
      }
    }
  }
}

void MarkSweep::check_adjust_pointer(void* p) {
  _adjusted_pointers->push(p);
}

class AdjusterTracker: public OopClosure {
 public:
  AdjusterTracker() {}
  void do_oop(oop* o)       { MarkSweep::check_adjust_pointer(o); }
  void do_oop(narrowOop* o) { MarkSweep::check_adjust_pointer(o); }
};

void MarkSweep::track_interior_pointers(oop obj) {
  if (ValidateMarkSweep) {
    _adjusted_pointers->clear();
    _pointer_tracking = true;

    AdjusterTracker checker;
    obj->oop_iterate_no_header(&checker);
  }
}

void MarkSweep::check_interior_pointers() {
  if (ValidateMarkSweep) {
    _pointer_tracking = false;
    guarantee(_adjusted_pointers->length() == 0, "should have processed the same pointers");
  }
}

void MarkSweep::reset_live_oop_tracking() {
  if (ValidateMarkSweep) {
    guarantee((size_t)_live_oops->length() == _live_oops_index, "should be at end of live oops");
    _live_oops_index = 0;
  }
}

void MarkSweep::register_live_oop(oop p, size_t size) {
  if (ValidateMarkSweep) {
    _live_oops->push(p);
    _live_oops_size->push(size);
    _live_oops_index++;
  }
}

void MarkSweep::validate_live_oop(oop p, size_t size) {
  if (ValidateMarkSweep) {
    oop obj = _live_oops->at((int)_live_oops_index);
    guarantee(obj == p, "should be the same object");
    guarantee(_live_oops_size->at((int)_live_oops_index) == size, "should be the same size");
    _live_oops_index++;
  }
}

void MarkSweep::live_oop_moved_to(HeapWord* q, size_t size,
                                  HeapWord* compaction_top) {
  assert(oop(q)->forwardee() == NULL || oop(q)->forwardee() == oop(compaction_top),
         "should be moved to forwarded location");
  if (ValidateMarkSweep) {
    MarkSweep::validate_live_oop(oop(q), size);
    _live_oops_moved_to->push(oop(compaction_top));
  }
  if (RecordMarkSweepCompaction) {
    _cur_gc_live_oops->push(q);
    _cur_gc_live_oops_moved_to->push(compaction_top);
    _cur_gc_live_oops_size->push(size);
  }
}

void MarkSweep::compaction_complete() {
  if (RecordMarkSweepCompaction) {
    GrowableArray<HeapWord*>* _tmp_live_oops          = _cur_gc_live_oops;
    GrowableArray<HeapWord*>* _tmp_live_oops_moved_to = _cur_gc_live_oops_moved_to;
    GrowableArray<size_t>   * _tmp_live_oops_size     = _cur_gc_live_oops_size;

    _cur_gc_live_oops           = _last_gc_live_oops;
    _cur_gc_live_oops_moved_to  = _last_gc_live_oops_moved_to;
    _cur_gc_live_oops_size      = _last_gc_live_oops_size;
    _last_gc_live_oops          = _tmp_live_oops;
    _last_gc_live_oops_moved_to = _tmp_live_oops_moved_to;
    _last_gc_live_oops_size     = _tmp_live_oops_size;
  }
}

void MarkSweep::print_new_location_of_heap_address(HeapWord* q) {
  if (!RecordMarkSweepCompaction) {
    tty->print_cr("Requires RecordMarkSweepCompaction to be enabled");
    return;
  }

  if (_last_gc_live_oops == NULL) {
    tty->print_cr("No compaction information gathered yet");
    return;
  }

  for (int i = 0; i < _last_gc_live_oops->length(); i++) {
    HeapWord* old_oop = _last_gc_live_oops->at(i);
    size_t    sz      = _last_gc_live_oops_size->at(i);
    if (old_oop <= q && q < (old_oop + sz)) {
      HeapWord* new_oop = _last_gc_live_oops_moved_to->at(i);
      size_t offset = (q - old_oop);
      tty->print_cr("Address " PTR_FORMAT, q);
      tty->print_cr(" Was in oop " PTR_FORMAT ", size " SIZE_FORMAT ", at offset " SIZE_FORMAT, old_oop, sz, offset);
      tty->print_cr(" Now in oop " PTR_FORMAT ", actual address " PTR_FORMAT, new_oop, new_oop + offset);
      return;
    }
  }

  tty->print_cr("Address " PTR_FORMAT " not found in live oop information from last GC", q);
}
#endif //VALIDATE_MARK_SWEEP

MarkSweep::IsAliveClosure   MarkSweep::is_alive;

void MarkSweep::IsAliveClosure::do_object(oop p)   { ShouldNotReachHere(); }
bool MarkSweep::IsAliveClosure::do_object_b(oop p) { return p->is_gc_marked(); }

MarkSweep::KeepAliveClosure MarkSweep::keep_alive;

void MarkSweep::KeepAliveClosure::do_oop(oop* p)       { MarkSweep::KeepAliveClosure::do_oop_work(p); }
void MarkSweep::KeepAliveClosure::do_oop(narrowOop* p) { MarkSweep::KeepAliveClosure::do_oop_work(p); }

void marksweep_init() { /* empty */ }

#ifndef PRODUCT

void MarkSweep::trace(const char* msg) {
  if (TraceMarkSweep)
    gclog_or_tty->print("%s", msg);
}

#endif
