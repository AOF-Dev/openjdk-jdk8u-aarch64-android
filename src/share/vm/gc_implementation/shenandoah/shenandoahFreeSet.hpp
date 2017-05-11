
/*
 * Copyright (c) 2016, Red Hat, Inc. and/or its affiliates.
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

#ifndef SHARE_VM_GC_SHENANDOAH_SHENANDOAHFREESET_HPP
#define SHARE_VM_GC_SHENANDOAH_SHENANDOAHFREESET_HPP

#include "gc_implementation/shenandoah/shenandoahHeapRegionSet.hpp"

class ShenandoahFreeSet : public ShenandoahHeapRegionSet {

private:
  size_t _capacity;
  size_t _used;

  size_t is_contiguous(size_t start, size_t num);
  size_t find_contiguous(size_t start, size_t num);
  void initialize_humongous_regions(size_t first, size_t num);
  ShenandoahHeapRegion* skip_humongous(ShenandoahHeapRegion* r);

  void assert_heaplock_owned_by_current_thread() PRODUCT_RETURN;

public:
  ShenandoahFreeSet(size_t max_regions);
  ~ShenandoahFreeSet();

  void add_region(ShenandoahHeapRegion* r);

  size_t capacity();

  size_t used();

  size_t unsafe_peek_next_no_humongous() const;

  ShenandoahHeapRegion* allocate_contiguous(size_t num);
  void clear();

  void increase_used(size_t amount);
  ShenandoahHeapRegion* current_no_humongous();
  ShenandoahHeapRegion* next_no_humongous();
};

#endif //SHARE_VM_GC_SHENANDOAH_SHENANDOAHFREESET_HPP
