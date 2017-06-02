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

#ifndef SHARE_VM_GC_SHENANDOAH_SHENANDOAHHEAPREGIONSET_HPP
#define SHARE_VM_GC_SHENANDOAH_SHENANDOAHHEAPREGIONSET_HPP

#include "memory/allocation.hpp"

#include "utilities/quickSort.hpp"

class ShenandoahHeapRegion;

extern outputStream* tty;

class ShenandoahHeapRegionClosure : public StackObj {
  bool _complete;
  void incomplete() {_complete = false;}

public:

  ShenandoahHeapRegionClosure(): _complete(true) {}

  // typically called on each region until it returns true;
  virtual bool doHeapRegion(ShenandoahHeapRegion* r) = 0;

  bool complete() { return _complete;}
};

// The basic set.
// Implements iteration.
class ShenandoahHeapRegionSet: public CHeapObj<mtGC> {
protected:
  ShenandoahHeapRegion** _regions;
  size_t                 _active_end;
  size_t                 _reserved_end;
  volatile size_t        _current_index;

public:

  ShenandoahHeapRegionSet(size_t max_regions);

  virtual ~ShenandoahHeapRegionSet();

  size_t   max_regions()     const { return _reserved_end;}
  size_t   active_regions()  const { return _active_end;}

  HeapWord* bottom() const;
  HeapWord* end() const;

  void clear();

  size_t count() const;

  ShenandoahHeapRegion* get_or_null(size_t i) const;
  inline ShenandoahHeapRegion* get(size_t i) const {
    assert (i < _active_end, "sanity");
    return _regions[i];
  }

  virtual void add_region(ShenandoahHeapRegion* r);
  virtual void add_region_check_for_duplicates(ShenandoahHeapRegion* r);

  // Advance the iteration pointer to the next region.
  void next();
  // Return the current region, and advance iteration pointer to next one, atomically.
  ShenandoahHeapRegion* claim_next();

  template<class C>
  void sort(C comparator) {
    QuickSort::sort<ShenandoahHeapRegion*>(_regions, (int)_active_end, comparator, false);
  }

  void print(outputStream* out = tty);
public:

  void heap_region_iterate(ShenandoahHeapRegionClosure* blk,
                           bool skip_dirty_regions = false,
                           bool skip_humongous_continuation = false) const;

  size_t current_index()   { return _current_index;}
  void clear_current_index() {_current_index = 0; }

  bool contains(ShenandoahHeapRegion* r);
  ShenandoahHeapRegion* current() const;

protected:

  void active_heap_region_iterate(ShenandoahHeapRegionClosure* blk,
                           bool skip_dirty_regions = false,
                           bool skip_humongous_continuation = false) const;

  void unclaimed_heap_region_iterate(ShenandoahHeapRegionClosure* blk,
                           bool skip_dirty_regions = false,
                           bool skip_humongous_continuation = false) const;

};

#endif //SHARE_VM_GC_SHENANDOAH_SHENANDOAHHEAPREGIONSET_HPP
