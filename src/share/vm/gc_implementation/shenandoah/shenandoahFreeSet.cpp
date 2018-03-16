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

#include "precompiled.hpp"
#include "gc_implementation/shenandoah/shenandoahFreeSet.hpp"
#include "gc_implementation/shenandoah/shenandoahHeap.inline.hpp"

ShenandoahFreeSet::ShenandoahFreeSet(ShenandoahHeapRegionSet* regions, size_t max_regions) :
        _regions(regions),
        _mutator_free_bitmap(max_regions, /* in_resource_area = */ false),
        _collector_free_bitmap(max_regions, /* in_resource_area = */ false),
        _max(max_regions)
{
  clear_internal();
}

void ShenandoahFreeSet::increase_used(size_t num_bytes) {
  assert_heaplock_owned_by_current_thread();
  _used += num_bytes;

  assert(_used <= _capacity, err_msg("must not use more than we have: used: "SIZE_FORMAT
                                     ", capacity: "SIZE_FORMAT", num_bytes: "SIZE_FORMAT,
                                     _used, _capacity, num_bytes));
}

bool ShenandoahFreeSet::is_mutator_free(size_t idx) const {
  assert (idx < _max,
          err_msg("index is sane: " SIZE_FORMAT " < " SIZE_FORMAT " (left: " SIZE_FORMAT ", right: " SIZE_FORMAT ")",
                  idx, _max, _mutator_leftmost, _mutator_rightmost));
  return _mutator_free_bitmap.at(idx);
}

bool ShenandoahFreeSet::is_collector_free(size_t idx) const {
  assert (idx < _max,
          err_msg("index is sane: " SIZE_FORMAT " < " SIZE_FORMAT " (left: " SIZE_FORMAT ", right: " SIZE_FORMAT ")",
                  idx, _max, _collector_leftmost, _collector_rightmost));
  return _collector_free_bitmap.at(idx);
}

HeapWord* ShenandoahFreeSet::allocate_single(size_t word_size, ShenandoahHeap::AllocType type, bool& in_new_region) {
  // Scan the bitmap looking for a first fit.
  //
  // Leftmost and rightmost bounds provide enough caching to walk bitmap efficiently. Normally,
  // we would find the region to allocate at right away.
  //
  // Allocations are biased: new application allocs go to beginning of the heap, and GC allocs
  // go to the end. This makes application allocation faster, because we would clear lots
  // of regions from the beginning most of the time.
  //
  // Free set maintains mutator and collector views, and normally they allocate in their views only,
  // unless we special cases for stealing and mixed allocations.

  switch (type) {
    case ShenandoahHeap::_alloc_tlab:
    case ShenandoahHeap::_alloc_shared: {

      // Fast-path: try to allocate in the mutator view first:
      for (size_t idx = _mutator_leftmost; idx <= _mutator_rightmost; idx++) {
        if (is_mutator_free(idx)) {
          HeapWord* result = try_allocate_in(_regions->get(idx), word_size, type, in_new_region);
          if (result != NULL) {
            return result;
          }
        }
      }

      // Recovery: try to steal the empty region from the collector view:
      for (size_t idx = _collector_leftmost; idx <= _collector_rightmost; idx++) {
        if (is_collector_free(idx)) {
          ShenandoahHeapRegion* r = _regions->get(idx);
          if (r->is_empty()) {
            HeapWord *result = try_allocate_in(r, word_size, type, in_new_region);
            if (result != NULL) {
              flip_to_mutator(idx);
              return result;
            }
          }
        }
      }

      // Recovery: try to mix the allocation into the collector view:
      if (ShenandoahAllowMixedAllocs) {
        for (size_t idx = _collector_leftmost; idx <= _collector_rightmost; idx++) {
          if (is_collector_free(idx)) {
            HeapWord* result = try_allocate_in(_regions->get(idx), word_size, type, in_new_region);
            if (result != NULL) {
              return result;
            }
          }
        }
      }

      break;
    }
    case ShenandoahHeap::_alloc_gclab:
    case ShenandoahHeap::_alloc_shared_gc: {
      // size_t is unsigned, need to dodge underflow when _leftmost = 0

      // Fast-path: try to allocate in the collector view first:
      for (size_t c = _collector_rightmost + 1; c > _collector_leftmost; c--) {
        size_t idx = c - 1;
        if (is_collector_free(idx)) {
          HeapWord* result = try_allocate_in(_regions->get(idx), word_size, type, in_new_region);
          if (result != NULL) {
            return result;
          }
        }
      }

      // Recovery: try to steal the empty region from the mutator view:
      for (size_t c = _mutator_rightmost + 1; c > _mutator_leftmost; c--) {
        size_t idx = c - 1;
        if (is_mutator_free(idx)) {
          ShenandoahHeapRegion* r = _regions->get(idx);
          if (r->is_empty()) {
            HeapWord *result = try_allocate_in(r, word_size, type, in_new_region);
            if (result != NULL) {
              flip_to_gc(idx);
              return result;
            }
          }
        }
      }

      // Recovery: try to mix the allocation into the mutator view:
      if (ShenandoahAllowMixedAllocs) {
        for (size_t c = _mutator_rightmost + 1; c > _mutator_leftmost; c--) {
          size_t idx = c - 1;
          if (is_mutator_free(idx)) {
            HeapWord* result = try_allocate_in(_regions->get(idx), word_size, type, in_new_region);
            if (result != NULL) {
              return result;
            }
          }
        }
      }

      break;
    }
    default:
      ShouldNotReachHere();
  }

  return NULL;
}

HeapWord* ShenandoahFreeSet::try_allocate_in(ShenandoahHeapRegion* r, size_t word_size, ShenandoahHeap::AllocType type, bool& in_new_region) {
  in_new_region = r->is_empty();

  HeapWord* result = r->allocate(word_size, type);

  if (result != NULL) {
    // Allocation successful, bump live data stats:
    r->increase_live_data_alloc_words(word_size);
    increase_used(word_size * HeapWordSize);
    ShenandoahHeap::heap()->increase_used(word_size * HeapWordSize);
  } else {
    // Region cannot afford allocation. Retire it.
    // While this seems a bit harsh, especially in the case when this large allocation does not
    // fit, but the next small one would, we are risking to inflate scan times when lots of
    // almost-full regions precede the fully-empty region where we want allocate the entire TLAB.
    // TODO: Record first fully-empty region, and use that for large allocations
    size_t num = r->region_number();
    increase_used(r->free());

    _collector_free_bitmap.clear_bit(num);
    _mutator_free_bitmap.clear_bit(num);
    // Touched the bounds? Need to update:
    if (touches_bounds(num)) {
      adjust_bounds();
    }
    assert_bounds();
  }
  return result;
}

bool ShenandoahFreeSet::touches_bounds(size_t num) const {
  return num == _collector_leftmost || num == _collector_rightmost || num == _mutator_leftmost || num == _mutator_rightmost;
}

void ShenandoahFreeSet::adjust_bounds() {
  // Rewind both mutator bounds until the next bit.
  while (_mutator_leftmost < _max && !is_mutator_free(_mutator_leftmost)) {
    _mutator_leftmost++;
  }
  while (_mutator_rightmost > 0 && !is_mutator_free(_mutator_rightmost)) {
    _mutator_rightmost--;
  }
  // Rewind both collector bounds until the next bit.
  while (_collector_leftmost < _max && !is_collector_free(_collector_leftmost)) {
    _collector_leftmost++;
  }
  while (_collector_rightmost > 0 && !is_collector_free(_collector_rightmost)) {
    _collector_rightmost--;
  }
}

HeapWord* ShenandoahFreeSet::allocate_contiguous(size_t words_size) {
  assert_heaplock_owned_by_current_thread();

  size_t num = ShenandoahHeapRegion::required_regions(words_size * HeapWordSize);

  // No regions left to satisfy allocation, bye.
  if (num > mutator_count()) {
    return NULL;
  }

  // Find the continuous interval of $num regions, starting from $beg and ending in $end,
  // inclusive. Contiguous allocations are biased to the beginning.

  size_t beg = _mutator_leftmost;
  size_t end = beg;

  while (true) {
    if (end >= _max) {
      // Hit the end, goodbye
      return NULL;
    }

    // If regions are not adjacent, then current [beg; end] is useless, and we may fast-forward.
    // If region is not empty, the current [beg; end] is useless, and we may fast-forward.
    if (!is_mutator_free(end) || !_regions->get(end)->is_empty()) {
      end++;
      beg = end;
      continue;
    }

    if ((end - beg + 1) == num) {
      // found the match
      break;
    }

    end++;
  };

#ifdef ASSERT
  assert ((end - beg + 1) == num, "Found just enough regions");
  for (size_t i = beg; i <= end; i++) {
    assert(_regions->get(i)->is_empty(), "Should be empty");
    assert(i == beg || _regions->get(i-1)->region_number() + 1 == _regions->get(i)->region_number(), "Should be contiguous");
  }
#endif

  ShenandoahHeap* sh = ShenandoahHeap::heap();

  // Initialize regions:
  for (size_t i = beg; i <= end; i++) {
    ShenandoahHeapRegion* r = _regions->get(i);
    if (i == beg) {
      r->make_humongous_start();
    } else {
      r->make_humongous_cont();
    }

    // Trailing region may be non-full, record the remainder there
    size_t remainder = words_size & ShenandoahHeapRegion::region_size_words_mask();
    size_t used_words;
    if ((i == end) && (remainder != 0)) {
      used_words = remainder;
    } else {
      used_words = ShenandoahHeapRegion::region_size_words();
    }

    r->set_top(r->bottom() + used_words);
    r->reset_alloc_metadata_to_shared();

    r->increase_live_data_alloc_words(used_words);
    sh->increase_used(used_words * HeapWordSize);

    _mutator_free_bitmap.clear_bit(r->region_number());
  }

  // While individual regions report their true use, all humongous regions are
  // marked used in the free set.
  increase_used(ShenandoahHeapRegion::region_size_bytes() * num);

  // Allocated at left/rightmost? Move the bounds appropriately.
  if (beg == _mutator_leftmost || end == _mutator_rightmost) {
    adjust_bounds();
  }
  assert_bounds();

  return _regions->get(beg)->bottom();
}

void ShenandoahFreeSet::add_region(ShenandoahHeapRegion* r) {
  size_t idx = r->region_number();

  assert_heaplock_owned_by_current_thread();
  assert(!r->in_collection_set(), "Shouldn't be adding those to the free set");
  assert(r->is_alloc_allowed(), "Should only add regions that can be allocated at");
  assert(!is_mutator_free(idx), "We are about to add it, it shouldn't be there already");
  assert(!is_collector_free(idx), "We are about to add it, it shouldn't be there already");

  _mutator_free_bitmap.set_bit(idx);
  _mutator_leftmost  = MIN2(_mutator_leftmost, idx);
  _mutator_rightmost = MAX2(_mutator_rightmost, idx);
  _capacity += r->free();
  assert(_used <= _capacity, "must not use more than we have");
}

void ShenandoahFreeSet::flip_to_gc(size_t idx) {
  _mutator_free_bitmap.clear_bit(idx);
  _collector_free_bitmap.set_bit(idx);
  _collector_leftmost = MIN2(idx, _collector_leftmost);
  _collector_rightmost = MAX2(idx, _collector_rightmost);
  if (touches_bounds(idx)) {
    adjust_bounds();
  }
  assert_bounds();
}

void ShenandoahFreeSet::flip_to_mutator(size_t idx) {
  _collector_free_bitmap.clear_bit(idx);
  _mutator_free_bitmap.set_bit(idx);
  _mutator_leftmost = MIN2(idx, _mutator_leftmost);
  _mutator_rightmost = MAX2(idx, _mutator_rightmost);
  if (touches_bounds(idx)) {
    adjust_bounds();
  }
  assert_bounds();
}

void ShenandoahFreeSet::clear() {
  assert_heaplock_owned_by_current_thread();
  clear_internal();
}

void ShenandoahFreeSet::clear_internal() {
  _mutator_free_bitmap.clear();
  _collector_free_bitmap.clear();
  _mutator_leftmost = _max;
  _mutator_rightmost = 0;
  _collector_leftmost = _max;
  _collector_rightmost = 0;
  _capacity = 0;
  _used = 0;
}

HeapWord* ShenandoahFreeSet::allocate(size_t word_size, ShenandoahHeap::AllocType type, bool& in_new_region) {
  assert_heaplock_owned_by_current_thread();
  assert_bounds();

  // Not enough memory in free region set. Coming out of full GC, it is possible that
  // there are no free regions available, so current_index may be invalid. Have to
  // poll capacity as the precaution here.
  if (word_size * HeapWordSize > capacity()) return NULL;

  if (word_size > ShenandoahHeapRegion::humongous_threshold_words()) {
    switch (type) {
      case ShenandoahHeap::_alloc_shared:
      case ShenandoahHeap::_alloc_shared_gc:
        in_new_region = true;
        return allocate_large_memory(word_size);
      case ShenandoahHeap::_alloc_gclab:
      case ShenandoahHeap::_alloc_tlab:
        in_new_region = false;
        log_warning(gc)("Trying to allocate TLAB larger than the humongous threshold: " SIZE_FORMAT " > " SIZE_FORMAT,
                        word_size, ShenandoahHeapRegion::humongous_threshold_words());
        return NULL;
      default:
        ShouldNotReachHere();
        return NULL;
    }
  } else {
    return allocate_small_memory(word_size, type, in_new_region);
  }
}

HeapWord* ShenandoahFreeSet::allocate_small_memory(size_t word_size, ShenandoahHeap::AllocType type, bool& in_new_region) {
  // Try to allocate right away:
  HeapWord* result = allocate_single(word_size, type, in_new_region);

  if (result == NULL) {
    // No free regions? Chances are, we have acquired the lock before the recycler.
    // Ask allocator to recycle some trash and try to allocate again.
    ShenandoahHeap::heap()->recycle_trash_assist(1);
    result = allocate_single(word_size, type, in_new_region);
  }

  return result;
}

HeapWord* ShenandoahFreeSet::allocate_large_memory(size_t words) {
  assert_heaplock_owned_by_current_thread();

  // Try to allocate right away:
  HeapWord* r = allocate_contiguous(words);
  if (r != NULL) {
    return r;
  }

  // Try to recycle up enough regions for this allocation:
  ShenandoahHeap::heap()->recycle_trash_assist(ShenandoahHeapRegion::required_regions(words*HeapWordSize));
  r = allocate_contiguous(words);
  if (r != NULL) {
    return r;
  }

  // Try to recycle all regions: it is possible we have cleaned up a fragmented block before:
  ShenandoahHeap::heap()->recycle_trash_assist(_max);
  r = allocate_contiguous(words);
  if (r != NULL) {
    return r;
  }

  return NULL;
}

size_t ShenandoahFreeSet::unsafe_peek_free() const {
  // Deliberately not locked, this method is unsafe when free set is modified.

  for (size_t index = _mutator_leftmost; index <= _mutator_rightmost; index++) {
    if (index < _max && is_mutator_free(index)) {
      ShenandoahHeapRegion* r = _regions->get(index);
      if (r->free() >= MinTLABSize) {
        return r->free();
      }
    }
  }

  // It appears that no regions left
  return 0;
}

void ShenandoahFreeSet::print_on(outputStream* out) const {
  out->print_cr("Mutator Free Set: " SIZE_FORMAT "", mutator_count());
  for (size_t index = _mutator_leftmost; index <= _mutator_rightmost; index++) {
    if (is_mutator_free(index)) {
      _regions->get(index)->print_on(out);
    }
  }
  out->print_cr("Collector Free Set: " SIZE_FORMAT "", collector_count());
  for (size_t index = _collector_leftmost; index <= _collector_rightmost; index++) {
    if (is_collector_free(index)) {
      _regions->get(index)->print_on(out);
    }
  }
}

#ifdef ASSERT
void ShenandoahFreeSet::assert_heaplock_owned_by_current_thread() const {
  ShenandoahHeap::heap()->assert_heaplock_owned_by_current_thread();
}



void ShenandoahFreeSet::assert_bounds() const {
  // Performance invariants. Failing these would not break the free set, but performance
  // would suffer.
  assert (_mutator_leftmost <= _max, err_msg("leftmost in bounds: "  SIZE_FORMAT " < " SIZE_FORMAT, _mutator_leftmost,  _max));
  assert (_mutator_rightmost < _max, err_msg("rightmost in bounds: " SIZE_FORMAT " < " SIZE_FORMAT, _mutator_rightmost, _max));

  assert (_mutator_leftmost == _max || is_mutator_free(_mutator_leftmost),  err_msg("leftmost region should be free: " SIZE_FORMAT,  _mutator_leftmost));
  assert (_mutator_rightmost == 0   || is_mutator_free(_mutator_rightmost), err_msg("rightmost region should be free: " SIZE_FORMAT, _mutator_rightmost));

  size_t beg_off = _mutator_free_bitmap.get_next_one_offset(0);
  size_t end_off = _mutator_free_bitmap.get_next_one_offset(_mutator_rightmost + 1);
  assert (beg_off >= _mutator_leftmost, err_msg("free regions before the leftmost: " SIZE_FORMAT ", bound " SIZE_FORMAT, beg_off, _mutator_leftmost));
  assert (end_off == _max,      err_msg("free regions past the rightmost: " SIZE_FORMAT ", bound " SIZE_FORMAT,  end_off, _mutator_rightmost));

  assert (_collector_leftmost <= _max, err_msg("leftmost in bounds: "  SIZE_FORMAT " < " SIZE_FORMAT, _collector_leftmost,  _max));
  assert (_collector_rightmost < _max, err_msg("rightmost in bounds: " SIZE_FORMAT " < " SIZE_FORMAT, _collector_rightmost, _max));

  assert (_collector_leftmost == _max || is_collector_free(_collector_leftmost),  err_msg("leftmost region should be free: " SIZE_FORMAT,  _collector_leftmost));
  assert (_collector_rightmost == 0   || is_collector_free(_collector_rightmost), err_msg("rightmost region should be free: " SIZE_FORMAT, _collector_rightmost));

  beg_off = _collector_free_bitmap.get_next_one_offset(0);
  end_off = _collector_free_bitmap.get_next_one_offset(_collector_rightmost + 1);
  assert (beg_off >= _collector_leftmost, err_msg("free regions before the leftmost: " SIZE_FORMAT ", bound " SIZE_FORMAT, beg_off, _collector_leftmost));
  assert (end_off == _max,      err_msg("free regions past the rightmost: " SIZE_FORMAT ", bound " SIZE_FORMAT,  end_off, _collector_rightmost));
}
#endif
