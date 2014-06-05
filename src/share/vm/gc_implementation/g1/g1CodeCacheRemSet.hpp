/*
 * Copyright (c) 2014, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_GC_IMPLEMENTATION_G1_G1CODECACHEREMSET_HPP
#define SHARE_VM_GC_IMPLEMENTATION_G1_G1CODECACHEREMSET_HPP

#include "memory/allocation.hpp"
#include "memory/freeList.hpp"
#include "runtime/globals.hpp"

class CodeBlobClosure;

class G1CodeRootChunk : public CHeapObj<mtGC> {
 private:
  static const int NUM_ENTRIES = 32;
 public:
  G1CodeRootChunk*     _next;
  G1CodeRootChunk*     _prev;

  nmethod** _top;

  nmethod* _data[NUM_ENTRIES];

  nmethod** bottom() const {
    return (nmethod**) &(_data[0]);
  }

  nmethod** end() const {
    return (nmethod**) &(_data[NUM_ENTRIES]);
  }

 public:
  G1CodeRootChunk();
  ~G1CodeRootChunk() {}

  static size_t word_size() { return (size_t)(align_size_up_(sizeof(G1CodeRootChunk), HeapWordSize) / HeapWordSize); }

  // FreeList "interface" methods

  G1CodeRootChunk* next() const         { return _next; }
  G1CodeRootChunk* prev() const         { return _prev; }
  void set_next(G1CodeRootChunk* v)     { _next = v; assert(v != this, "Boom");}
  void set_prev(G1CodeRootChunk* v)     { _prev = v; assert(v != this, "Boom");}
  void clear_next()       { set_next(NULL); }
  void clear_prev()       { set_prev(NULL); }

  size_t size() const { return word_size(); }

  void link_next(G1CodeRootChunk* ptr)  { set_next(ptr); }
  void link_prev(G1CodeRootChunk* ptr)  { set_prev(ptr); }
  void link_after(G1CodeRootChunk* ptr) {
    link_next(ptr);
    if (ptr != NULL) ptr->link_prev((G1CodeRootChunk*)this);
  }

  bool is_free()                 { return true; }

  // New G1CodeRootChunk routines

  void reset();

  bool is_empty() const {
    return _top == bottom();
  }

  bool is_full() const {
    return _top == (nmethod**)end();
  }

  bool contains(nmethod* method) {
    nmethod** cur = bottom();
    while (cur != _top) {
      if (*cur == method) return true;
      cur++;
    }
    return false;
  }

  bool add(nmethod* method) {
    if (is_full()) return false;
    *_top = method;
    _top++;
    return true;
  }

  bool remove(nmethod* method) {
    nmethod** cur = bottom();
    while (cur != _top) {
      if (*cur == method) {
        memmove(cur, cur + 1, (_top - (cur + 1)) * sizeof(nmethod**));
        _top--;
        return true;
      }
      cur++;
    }
    return false;
  }

  void nmethods_do(CodeBlobClosure* blk);

  nmethod* pop() {
    if (is_empty()) {
      return NULL;
    }
    _top--;
    return *_top;
  }
};

// Implements storage for a set of code roots.
// All methods that modify the set are not thread-safe except if otherwise noted.
class G1CodeRootSet VALUE_OBJ_CLASS_SPEC {
 private:
  // Global free chunk list management
  static FreeList<G1CodeRootChunk> _free_list;
  // Total number of chunks handed out
  static size_t _num_chunks_handed_out;

  static G1CodeRootChunk* new_chunk();
  static void free_chunk(G1CodeRootChunk* chunk);
  // Free all elements of the given list.
  static void free_all_chunks(FreeList<G1CodeRootChunk>* list);

  // Return the chunk that contains the given nmethod, NULL otherwise.
  // Scans the list of chunks backwards, as this method is used to add new
  // entries, which are typically added in bulk for a single nmethod.
  G1CodeRootChunk* find(nmethod* method);
  void free(G1CodeRootChunk* chunk);

  size_t _length;
  FreeList<G1CodeRootChunk> _list;

 public:
  G1CodeRootSet();
  ~G1CodeRootSet();

  static void initialize();
  static void purge_chunks(size_t keep_ratio);

  static size_t static_mem_size();
  static size_t fl_mem_size();

  // Search for the code blob from the recently allocated ones to find duplicates more quickly, as this
  // method is likely to be repeatedly called with the same nmethod.
  void add(nmethod* method);

  void remove(nmethod* method);
  nmethod* pop();

  bool contains(nmethod* method);

  void clear();

  void nmethods_do(CodeBlobClosure* blk) const;

  bool is_empty() { return length() == 0; }

  // Length in elements
  size_t length() const { return _length; }

  // Memory size in bytes taken by this set.
  size_t mem_size();

  static void test() PRODUCT_RETURN;
};

#endif // SHARE_VM_GC_IMPLEMENTATION_G1_G1CODECACHEREMSET_HPP
