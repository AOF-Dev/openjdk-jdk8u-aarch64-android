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
#include "classfile/altHashing.hpp"
#include "classfile/javaClasses.hpp"
#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "gc_interface/collectedHeap.inline.hpp"
#include "memory/allocation.inline.hpp"
#include "memory/filemap.hpp"
#include "memory/gcLocker.inline.hpp"
#include "oops/oop.inline.hpp"
#include "oops/oop.inline2.hpp"
#include "runtime/mutexLocker.hpp"
#include "utilities/hashtable.inline.hpp"
#include "utilities/numberSeq.hpp"

// --------------------------------------------------------------------------

SymbolTable* SymbolTable::_the_table = NULL;
// Static arena for symbols that are not deallocated
Arena* SymbolTable::_arena = NULL;
bool SymbolTable::_needs_rehashing = false;

Symbol* SymbolTable::allocate_symbol(const u1* name, int len, bool c_heap, TRAPS) {
  assert (len <= Symbol::max_length(), "should be checked by caller");

  Symbol* sym;

  if (c_heap) {
    // refcount starts as 1
    assert(!DumpSharedSpaces, "never allocate to C heap");
    sym = new (len, THREAD) Symbol(name, len, 1);
    assert(sym != NULL, "new should call vm_exit_out_of_memory if C_HEAP is exhausted");
  } else {
    if (DumpSharedSpaces) {
      sym = new (len, ClassLoaderData::the_null_class_loader_data(), THREAD) Symbol(name, len, -1);
  } else {
    sym = new (len, arena(), THREAD) Symbol(name, len, -1);
  }
  }
  return sym;
}

void SymbolTable::initialize_symbols(int arena_alloc_size) {
  // Initialize the arena for global symbols, size passed in depends on CDS.
  if (arena_alloc_size == 0) {
    _arena = new (mtSymbol) Arena();
  } else {
    _arena = new (mtSymbol) Arena(arena_alloc_size);
  }
}

// Call function for all symbols in the symbol table.
void SymbolTable::symbols_do(SymbolClosure *cl) {
  const int n = the_table()->table_size();
  for (int i = 0; i < n; i++) {
    for (HashtableEntry<Symbol*, mtSymbol>* p = the_table()->bucket(i);
         p != NULL;
         p = p->next()) {
      cl->do_symbol(p->literal_addr());
    }
  }
}

int SymbolTable::symbols_removed = 0;
int SymbolTable::symbols_counted = 0;

// Remove unreferenced symbols from the symbol table
// This is done late during GC.
void SymbolTable::unlink() {
  int removed = 0;
  int total = 0;
  size_t memory_total = 0;
  for (int i = 0; i < the_table()->table_size(); ++i) {
    HashtableEntry<Symbol*, mtSymbol>** p = the_table()->bucket_addr(i);
    HashtableEntry<Symbol*, mtSymbol>* entry = the_table()->bucket(i);
    while (entry != NULL) {
      // Shared entries are normally at the end of the bucket and if we run into
      // a shared entry, then there is nothing more to remove. However, if we
      // have rehashed the table, then the shared entries are no longer at the
      // end of the bucket.
      if (entry->is_shared() && !use_alternate_hashcode()) {
        break;
      }
      Symbol* s = entry->literal();
      memory_total += s->size();
      total++;
      assert(s != NULL, "just checking");
      // If reference count is zero, remove.
      if (s->refcount() == 0) {
        assert(!entry->is_shared(), "shared entries should be kept live");
        delete s;
        removed++;
        *p = entry->next();
        the_table()->free_entry(entry);
      } else {
        p = entry->next_addr();
      }
      // get next entry
      entry = (HashtableEntry<Symbol*, mtSymbol>*)HashtableEntry<Symbol*, mtSymbol>::make_ptr(*p);
    }
  }
  symbols_removed += removed;
  symbols_counted += total;
  // Exclude printing for normal PrintGCDetails because people parse
  // this output.
  if (PrintGCDetails && Verbose && WizardMode) {
    gclog_or_tty->print(" [Symbols=%d size=" SIZE_FORMAT "K] ", total,
                        (memory_total*HeapWordSize)/1024);
  }
}

// Create a new table and using alternate hash code, populate the new table
// with the existing strings.   Set flag to use the alternate hash code afterwards.
void SymbolTable::rehash_table() {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint");
  // This should never happen with -Xshare:dump but it might in testing mode.
  if (DumpSharedSpaces) return;
  // Create a new symbol table
  SymbolTable* new_table = new SymbolTable();

  the_table()->move_to(new_table);

  // Delete the table and buckets (entries are reused in new table).
  delete _the_table;
  // Don't check if we need rehashing until the table gets unbalanced again.
  // Then rehash with a new global seed.
  _needs_rehashing = false;
  _the_table = new_table;
}

// Lookup a symbol in a bucket.

Symbol* SymbolTable::lookup(int index, const char* name,
                              int len, unsigned int hash) {
  int count = 0;
  for (HashtableEntry<Symbol*, mtSymbol>* e = bucket(index); e != NULL; e = e->next()) {
    count++;  // count all entries in this bucket, not just ones with same hash
    if (e->hash() == hash) {
      Symbol* sym = e->literal();
      if (sym->equals(name, len)) {
        // something is referencing this symbol now.
        sym->increment_refcount();
        return sym;
      }
    }
  }
  // If the bucket size is too deep check if this hash code is insufficient.
  if (count >= BasicHashtable<mtSymbol>::rehash_count && !needs_rehashing()) {
    _needs_rehashing = check_rehash_table(count);
  }
  return NULL;
}

// Pick hashing algorithm.
unsigned int SymbolTable::hash_symbol(const char* s, int len) {
  return use_alternate_hashcode() ?
           AltHashing::murmur3_32(seed(), (const jbyte*)s, len) :
           java_lang_String::to_hash(s, len);
}


// We take care not to be blocking while holding the
// SymbolTable_lock. Otherwise, the system might deadlock, since the
// symboltable is used during compilation (VM_thread) The lock free
// synchronization is simplified by the fact that we do not delete
// entries in the symbol table during normal execution (only during
// safepoints).

Symbol* SymbolTable::lookup(const char* name, int len, TRAPS) {
  unsigned int hashValue = hash_symbol(name, len);
  int index = the_table()->hash_to_index(hashValue);

  Symbol* s = the_table()->lookup(index, name, len, hashValue);

  // Found
  if (s != NULL) return s;

  // Grab SymbolTable_lock first.
  MutexLocker ml(SymbolTable_lock, THREAD);

  // Otherwise, add to symbol to table
  return the_table()->basic_add(index, (u1*)name, len, hashValue, true, CHECK_NULL);
}

Symbol* SymbolTable::lookup(const Symbol* sym, int begin, int end, TRAPS) {
  char* buffer;
  int index, len;
  unsigned int hashValue;
  char* name;
  {
    debug_only(No_Safepoint_Verifier nsv;)

    name = (char*)sym->base() + begin;
    len = end - begin;
    hashValue = hash_symbol(name, len);
    index = the_table()->hash_to_index(hashValue);
    Symbol* s = the_table()->lookup(index, name, len, hashValue);

    // Found
    if (s != NULL) return s;
  }

  // Otherwise, add to symbol to table. Copy to a C string first.
  char stack_buf[128];
  ResourceMark rm(THREAD);
  if (len <= 128) {
    buffer = stack_buf;
  } else {
    buffer = NEW_RESOURCE_ARRAY_IN_THREAD(THREAD, char, len);
  }
  for (int i=0; i<len; i++) {
    buffer[i] = name[i];
  }
  // Make sure there is no safepoint in the code above since name can't move.
  // We can't include the code in No_Safepoint_Verifier because of the
  // ResourceMark.

  // Grab SymbolTable_lock first.
  MutexLocker ml(SymbolTable_lock, THREAD);

  return the_table()->basic_add(index, (u1*)buffer, len, hashValue, true, CHECK_NULL);
}

Symbol* SymbolTable::lookup_only(const char* name, int len,
                                   unsigned int& hash) {
  hash = hash_symbol(name, len);
  int index = the_table()->hash_to_index(hash);

  Symbol* s = the_table()->lookup(index, name, len, hash);
  return s;
}

// Look up the address of the literal in the SymbolTable for this Symbol*
// Do not create any new symbols
// Do not increment the reference count to keep this alive
Symbol** SymbolTable::lookup_symbol_addr(Symbol* sym){
  unsigned int hash = hash_symbol((char*)sym->bytes(), sym->utf8_length());
  int index = the_table()->hash_to_index(hash);

  for (HashtableEntry<Symbol*, mtSymbol>* e = the_table()->bucket(index); e != NULL; e = e->next()) {
    if (e->hash() == hash) {
      Symbol* literal_sym = e->literal();
      if (sym == literal_sym) {
        return e->literal_addr();
      }
    }
  }
  return NULL;
}

// Suggestion: Push unicode-based lookup all the way into the hashing
// and probing logic, so there is no need for convert_to_utf8 until
// an actual new Symbol* is created.
Symbol* SymbolTable::lookup_unicode(const jchar* name, int utf16_length, TRAPS) {
  int utf8_length = UNICODE::utf8_length((jchar*) name, utf16_length);
  char stack_buf[128];
  if (utf8_length < (int) sizeof(stack_buf)) {
    char* chars = stack_buf;
    UNICODE::convert_to_utf8(name, utf16_length, chars);
    return lookup(chars, utf8_length, THREAD);
  } else {
    ResourceMark rm(THREAD);
    char* chars = NEW_RESOURCE_ARRAY(char, utf8_length + 1);;
    UNICODE::convert_to_utf8(name, utf16_length, chars);
    return lookup(chars, utf8_length, THREAD);
  }
}

Symbol* SymbolTable::lookup_only_unicode(const jchar* name, int utf16_length,
                                           unsigned int& hash) {
  int utf8_length = UNICODE::utf8_length((jchar*) name, utf16_length);
  char stack_buf[128];
  if (utf8_length < (int) sizeof(stack_buf)) {
    char* chars = stack_buf;
    UNICODE::convert_to_utf8(name, utf16_length, chars);
    return lookup_only(chars, utf8_length, hash);
  } else {
    ResourceMark rm;
    char* chars = NEW_RESOURCE_ARRAY(char, utf8_length + 1);;
    UNICODE::convert_to_utf8(name, utf16_length, chars);
    return lookup_only(chars, utf8_length, hash);
  }
}

void SymbolTable::add(ClassLoaderData* loader_data, constantPoolHandle cp,
                      int names_count,
                      const char** names, int* lengths, int* cp_indices,
                      unsigned int* hashValues, TRAPS) {
  // Grab SymbolTable_lock first.
  MutexLocker ml(SymbolTable_lock, THREAD);

  SymbolTable* table = the_table();
  bool added = table->basic_add(loader_data, cp, names_count, names, lengths,
                                cp_indices, hashValues, CHECK);
  if (!added) {
    // do it the hard way
    for (int i=0; i<names_count; i++) {
      int index = table->hash_to_index(hashValues[i]);
      bool c_heap = !loader_data->is_the_null_class_loader_data();
      Symbol* sym = table->basic_add(index, (u1*)names[i], lengths[i], hashValues[i], c_heap, CHECK);
      cp->symbol_at_put(cp_indices[i], sym);
    }
  }
}

Symbol* SymbolTable::new_permanent_symbol(const char* name, TRAPS) {
  unsigned int hash;
  Symbol* result = SymbolTable::lookup_only((char*)name, (int)strlen(name), hash);
  if (result != NULL) {
    return result;
  }
  // Grab SymbolTable_lock first.
  MutexLocker ml(SymbolTable_lock, THREAD);

  SymbolTable* table = the_table();
  int index = table->hash_to_index(hash);
  return table->basic_add(index, (u1*)name, (int)strlen(name), hash, false, THREAD);
}

Symbol* SymbolTable::basic_add(int index_arg, u1 *name, int len,
                               unsigned int hashValue_arg, bool c_heap, TRAPS) {
  assert(!Universe::heap()->is_in_reserved(name) || GC_locker::is_active(),
         "proposed name of symbol must be stable");

  // Don't allow symbols to be created which cannot fit in a Symbol*.
  if (len > Symbol::max_length()) {
    THROW_MSG_0(vmSymbols::java_lang_InternalError(),
                "name is too long to represent");
  }

  // Cannot hit a safepoint in this function because the "this" pointer can move.
  No_Safepoint_Verifier nsv;

  // Check if the symbol table has been rehashed, if so, need to recalculate
  // the hash value and index.
  unsigned int hashValue;
  int index;
  if (use_alternate_hashcode()) {
    hashValue = hash_symbol((const char*)name, len);
    index = hash_to_index(hashValue);
  } else {
    hashValue = hashValue_arg;
    index = index_arg;
  }

  // Since look-up was done lock-free, we need to check if another
  // thread beat us in the race to insert the symbol.
  Symbol* test = lookup(index, (char*)name, len, hashValue);
  if (test != NULL) {
    // A race occurred and another thread introduced the symbol.
    assert(test->refcount() != 0, "lookup should have incremented the count");
    return test;
  }

  // Create a new symbol.
  Symbol* sym = allocate_symbol(name, len, c_heap, CHECK_NULL);
  assert(sym->equals((char*)name, len), "symbol must be properly initialized");

  HashtableEntry<Symbol*, mtSymbol>* entry = new_entry(hashValue, sym);
  add_entry(index, entry);
  return sym;
}

// This version of basic_add adds symbols in batch from the constant pool
// parsing.
bool SymbolTable::basic_add(ClassLoaderData* loader_data, constantPoolHandle cp,
                            int names_count,
                            const char** names, int* lengths,
                            int* cp_indices, unsigned int* hashValues,
                            TRAPS) {

  // Check symbol names are not too long.  If any are too long, don't add any.
  for (int i = 0; i< names_count; i++) {
    if (lengths[i] > Symbol::max_length()) {
      THROW_MSG_0(vmSymbols::java_lang_InternalError(),
                  "name is too long to represent");
    }
  }

  // Cannot hit a safepoint in this function because the "this" pointer can move.
  No_Safepoint_Verifier nsv;

  for (int i=0; i<names_count; i++) {
    // Check if the symbol table has been rehashed, if so, need to recalculate
    // the hash value.
    unsigned int hashValue;
    if (use_alternate_hashcode()) {
      hashValue = hash_symbol(names[i], lengths[i]);
    } else {
      hashValue = hashValues[i];
    }
    // Since look-up was done lock-free, we need to check if another
    // thread beat us in the race to insert the symbol.
    int index = hash_to_index(hashValue);
    Symbol* test = lookup(index, names[i], lengths[i], hashValue);
    if (test != NULL) {
      // A race occurred and another thread introduced the symbol, this one
      // will be dropped and collected. Use test instead.
      cp->symbol_at_put(cp_indices[i], test);
      assert(test->refcount() != 0, "lookup should have incremented the count");
    } else {
      // Create a new symbol.  The null class loader is never unloaded so these
      // are allocated specially in a permanent arena.
      bool c_heap = !loader_data->is_the_null_class_loader_data();
      Symbol* sym = allocate_symbol((const u1*)names[i], lengths[i], c_heap, CHECK_(false));
      assert(sym->equals(names[i], lengths[i]), "symbol must be properly initialized");  // why wouldn't it be???
      HashtableEntry<Symbol*, mtSymbol>* entry = new_entry(hashValue, sym);
      add_entry(index, entry);
      cp->symbol_at_put(cp_indices[i], sym);
    }
  }
  return true;
}


void SymbolTable::verify() {
  for (int i = 0; i < the_table()->table_size(); ++i) {
    HashtableEntry<Symbol*, mtSymbol>* p = the_table()->bucket(i);
    for ( ; p != NULL; p = p->next()) {
      Symbol* s = (Symbol*)(p->literal());
      guarantee(s != NULL, "symbol is NULL");
      unsigned int h = hash_symbol((char*)s->bytes(), s->utf8_length());
      guarantee(p->hash() == h, "broken hash in symbol table entry");
      guarantee(the_table()->hash_to_index(h) == i,
                "wrong index in symbol table");
    }
  }
}

void SymbolTable::dump(outputStream* st) {
  NumberSeq summary;
  for (int i = 0; i < the_table()->table_size(); ++i) {
    int count = 0;
    for (HashtableEntry<Symbol*, mtSymbol>* e = the_table()->bucket(i);
       e != NULL; e = e->next()) {
      count++;
    }
    summary.add((double)count);
  }
  st->print_cr("SymbolTable statistics:");
  st->print_cr("Number of buckets       : %7d", summary.num());
  st->print_cr("Average bucket size     : %7.0f", summary.avg());
  st->print_cr("Variance of bucket size : %7.0f", summary.variance());
  st->print_cr("Std. dev. of bucket size: %7.0f", summary.sd());
  st->print_cr("Maximum bucket size     : %7.0f", summary.maximum());
}


//---------------------------------------------------------------------------
// Non-product code

#ifndef PRODUCT

void SymbolTable::print_histogram() {
  MutexLocker ml(SymbolTable_lock);
  const int results_length = 100;
  int results[results_length];
  int i,j;

  // initialize results to zero
  for (j = 0; j < results_length; j++) {
    results[j] = 0;
  }

  int total = 0;
  int max_symbols = 0;
  int out_of_range = 0;
  int memory_total = 0;
  int count = 0;
  for (i = 0; i < the_table()->table_size(); i++) {
    HashtableEntry<Symbol*, mtSymbol>* p = the_table()->bucket(i);
    for ( ; p != NULL; p = p->next()) {
      memory_total += p->literal()->size();
      count++;
      int counter = p->literal()->utf8_length();
      total += counter;
      if (counter < results_length) {
        results[counter]++;
      } else {
        out_of_range++;
      }
      max_symbols = MAX2(max_symbols, counter);
    }
  }
  tty->print_cr("Symbol Table:");
  tty->print_cr("Total number of symbols  %5d", count);
  tty->print_cr("Total size in memory     %5dK",
          (memory_total*HeapWordSize)/1024);
  tty->print_cr("Total counted            %5d", symbols_counted);
  tty->print_cr("Total removed            %5d", symbols_removed);
  if (symbols_counted > 0) {
    tty->print_cr("Percent removed          %3.2f",
          ((float)symbols_removed/(float)symbols_counted)* 100);
  }
  tty->print_cr("Reference counts         %5d", Symbol::_total_count);
  tty->print_cr("Symbol arena size        %5d used %5d",
                 arena()->size_in_bytes(), arena()->used());
  tty->print_cr("Histogram of symbol length:");
  tty->print_cr("%8s %5d", "Total  ", total);
  tty->print_cr("%8s %5d", "Maximum", max_symbols);
  tty->print_cr("%8s %3.2f", "Average",
          ((float) total / (float) the_table()->table_size()));
  tty->print_cr("%s", "Histogram:");
  tty->print_cr(" %s %29s", "Length", "Number chains that length");
  for (i = 0; i < results_length; i++) {
    if (results[i] > 0) {
      tty->print_cr("%6d %10d", i, results[i]);
    }
  }
  if (Verbose) {
    int line_length = 70;
    tty->print_cr("%s %30s", " Length", "Number chains that length");
    for (i = 0; i < results_length; i++) {
      if (results[i] > 0) {
        tty->print("%4d", i);
        for (j = 0; (j < results[i]) && (j < line_length);  j++) {
          tty->print("%1s", "*");
        }
        if (j == line_length) {
          tty->print("%1s", "+");
        }
        tty->cr();
      }
    }
  }
  tty->print_cr(" %s %d: %d\n", "Number chains longer than",
                    results_length, out_of_range);
}

void SymbolTable::print() {
  for (int i = 0; i < the_table()->table_size(); ++i) {
    HashtableEntry<Symbol*, mtSymbol>** p = the_table()->bucket_addr(i);
    HashtableEntry<Symbol*, mtSymbol>* entry = the_table()->bucket(i);
    if (entry != NULL) {
      while (entry != NULL) {
        tty->print(PTR_FORMAT " ", entry->literal());
        entry->literal()->print();
        tty->print(" %d", entry->literal()->refcount());
        p = entry->next_addr();
        entry = (HashtableEntry<Symbol*, mtSymbol>*)HashtableEntry<Symbol*, mtSymbol>::make_ptr(*p);
      }
      tty->cr();
    }
  }
}
#endif // PRODUCT

// --------------------------------------------------------------------------

#ifdef ASSERT
class StableMemoryChecker : public StackObj {
  enum { _bufsize = wordSize*4 };

  address _region;
  jint    _size;
  u1      _save_buf[_bufsize];

  int sample(u1* save_buf) {
    if (_size <= _bufsize) {
      memcpy(save_buf, _region, _size);
      return _size;
    } else {
      // copy head and tail
      memcpy(&save_buf[0],          _region,                      _bufsize/2);
      memcpy(&save_buf[_bufsize/2], _region + _size - _bufsize/2, _bufsize/2);
      return (_bufsize/2)*2;
    }
  }

 public:
  StableMemoryChecker(const void* region, jint size) {
    _region = (address) region;
    _size   = size;
    sample(_save_buf);
  }

  bool verify() {
    u1 check_buf[sizeof(_save_buf)];
    int check_size = sample(check_buf);
    return (0 == memcmp(_save_buf, check_buf, check_size));
  }

  void set_region(const void* region) { _region = (address) region; }
};
#endif


// --------------------------------------------------------------------------
StringTable* StringTable::_the_table = NULL;

bool StringTable::_needs_rehashing = false;

// Pick hashing algorithm
unsigned int StringTable::hash_string(const jchar* s, int len) {
  return use_alternate_hashcode() ? AltHashing::murmur3_32(seed(), s, len) :
                                    java_lang_String::to_hash(s, len);
}

oop StringTable::lookup(int index, jchar* name,
                        int len, unsigned int hash) {
  int count = 0;
  for (HashtableEntry<oop, mtSymbol>* l = bucket(index); l != NULL; l = l->next()) {
    count++;
    if (l->hash() == hash) {
      if (java_lang_String::equals(l->literal(), name, len)) {
        return l->literal();
      }
    }
  }
  // If the bucket size is too deep check if this hash code is insufficient.
  if (count >= BasicHashtable<mtSymbol>::rehash_count && !needs_rehashing()) {
    _needs_rehashing = check_rehash_table(count);
  }
  return NULL;
}


oop StringTable::basic_add(int index_arg, Handle string, jchar* name,
                           int len, unsigned int hashValue_arg, TRAPS) {

  assert(java_lang_String::equals(string(), name, len),
         "string must be properly initialized");
  // Cannot hit a safepoint in this function because the "this" pointer can move.
  No_Safepoint_Verifier nsv;

  // Check if the symbol table has been rehashed, if so, need to recalculate
  // the hash value and index before second lookup.
  unsigned int hashValue;
  int index;
  if (use_alternate_hashcode()) {
    hashValue = hash_string(name, len);
    index = hash_to_index(hashValue);
  } else {
    hashValue = hashValue_arg;
    index = index_arg;
  }

  // Since look-up was done lock-free, we need to check if another
  // thread beat us in the race to insert the symbol.

  oop test = lookup(index, name, len, hashValue); // calls lookup(u1*, int)
  if (test != NULL) {
    // Entry already added
    return test;
  }

  HashtableEntry<oop, mtSymbol>* entry = new_entry(hashValue, string());
  add_entry(index, entry);
  return string();
}


oop StringTable::lookup(Symbol* symbol) {
  ResourceMark rm;
  int length;
  jchar* chars = symbol->as_unicode(length);
  unsigned int hashValue = hash_string(chars, length);
  int index = the_table()->hash_to_index(hashValue);
  return the_table()->lookup(index, chars, length, hashValue);
}


oop StringTable::intern(Handle string_or_null, jchar* name,
                        int len, TRAPS) {
  unsigned int hashValue = hash_string(name, len);
  int index = the_table()->hash_to_index(hashValue);
  oop found_string = the_table()->lookup(index, name, len, hashValue);

  // Found
  if (found_string != NULL) return found_string;

  debug_only(StableMemoryChecker smc(name, len * sizeof(name[0])));
  assert(!Universe::heap()->is_in_reserved(name) || GC_locker::is_active(),
         "proposed name of symbol must be stable");

  Handle string;
  // try to reuse the string if possible
  if (!string_or_null.is_null()) {
    string = string_or_null;
  } else {
    string = java_lang_String::create_from_unicode(name, len, CHECK_NULL);
  }

  // Grab the StringTable_lock before getting the_table() because it could
  // change at safepoint.
  MutexLocker ml(StringTable_lock, THREAD);

  // Otherwise, add to symbol to table
  return the_table()->basic_add(index, string, name, len,
                                hashValue, CHECK_NULL);
}

oop StringTable::intern(Symbol* symbol, TRAPS) {
  if (symbol == NULL) return NULL;
  ResourceMark rm(THREAD);
  int length;
  jchar* chars = symbol->as_unicode(length);
  Handle string;
  oop result = intern(string, chars, length, CHECK_NULL);
  return result;
}


oop StringTable::intern(oop string, TRAPS)
{
  if (string == NULL) return NULL;
  ResourceMark rm(THREAD);
  int length;
  Handle h_string (THREAD, string);
  jchar* chars = java_lang_String::as_unicode_string(string, length);
  oop result = intern(h_string, chars, length, CHECK_NULL);
  return result;
}


oop StringTable::intern(const char* utf8_string, TRAPS) {
  if (utf8_string == NULL) return NULL;
  ResourceMark rm(THREAD);
  int length = UTF8::unicode_length(utf8_string);
  jchar* chars = NEW_RESOURCE_ARRAY(jchar, length);
  UTF8::convert_to_unicode(utf8_string, chars, length);
  Handle string;
  oop result = intern(string, chars, length, CHECK_NULL);
  return result;
}

void StringTable::unlink(BoolObjectClosure* is_alive) {
  // Readers of the table are unlocked, so we should only be removing
  // entries at a safepoint.
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint");
  for (int i = 0; i < the_table()->table_size(); ++i) {
    HashtableEntry<oop, mtSymbol>** p = the_table()->bucket_addr(i);
    HashtableEntry<oop, mtSymbol>* entry = the_table()->bucket(i);
    while (entry != NULL) {
      // Shared entries are normally at the end of the bucket and if we run into
      // a shared entry, then there is nothing more to remove. However, if we
      // have rehashed the table, then the shared entries are no longer at the
      // end of the bucket.
      if (entry->is_shared() && !use_alternate_hashcode()) {
        break;
      }
      assert(entry->literal() != NULL, "just checking");
      if (entry->is_shared() || is_alive->do_object_b(entry->literal())) {
        p = entry->next_addr();
      } else {
        *p = entry->next();
        the_table()->free_entry(entry);
      }
      entry = (HashtableEntry<oop, mtSymbol>*)HashtableEntry<oop, mtSymbol>::make_ptr(*p);
    }
  }
}

void StringTable::oops_do(OopClosure* f) {
  for (int i = 0; i < the_table()->table_size(); ++i) {
    HashtableEntry<oop, mtSymbol>** p = the_table()->bucket_addr(i);
    HashtableEntry<oop, mtSymbol>* entry = the_table()->bucket(i);
    while (entry != NULL) {
      f->do_oop((oop*)entry->literal_addr());

      // Did the closure remove the literal from the table?
      if (entry->literal() == NULL) {
        assert(!entry->is_shared(), "immutable hashtable entry?");
        *p = entry->next();
        the_table()->free_entry(entry);
      } else {
        p = entry->next_addr();
      }
      entry = (HashtableEntry<oop, mtSymbol>*)HashtableEntry<oop, mtSymbol>::make_ptr(*p);
    }
  }
}

void StringTable::verify() {
  for (int i = 0; i < the_table()->table_size(); ++i) {
    HashtableEntry<oop, mtSymbol>* p = the_table()->bucket(i);
    for ( ; p != NULL; p = p->next()) {
      oop s = p->literal();
      guarantee(s != NULL, "interned string is NULL");
      unsigned int h = java_lang_String::hash_string(s);
      guarantee(p->hash() == h, "broken hash in string table entry");
      guarantee(the_table()->hash_to_index(h) == i,
                "wrong index in string table");
    }
  }
}

void StringTable::dump(outputStream* st) {
  NumberSeq summary;
  for (int i = 0; i < the_table()->table_size(); ++i) {
    HashtableEntry<oop, mtSymbol>* p = the_table()->bucket(i);
    int count = 0;
    for ( ; p != NULL; p = p->next()) {
      count++;
    }
    summary.add((double)count);
  }
  st->print_cr("StringTable statistics:");
  st->print_cr("Number of buckets       : %7d", summary.num());
  st->print_cr("Average bucket size     : %7.0f", summary.avg());
  st->print_cr("Variance of bucket size : %7.0f", summary.variance());
  st->print_cr("Std. dev. of bucket size: %7.0f", summary.sd());
  st->print_cr("Maximum bucket size     : %7.0f", summary.maximum());
}


// Create a new table and using alternate hash code, populate the new table
// with the existing strings.   Set flag to use the alternate hash code afterwards.
void StringTable::rehash_table() {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint");
  // This should never happen with -Xshare:dump but it might in testing mode.
  if (DumpSharedSpaces) return;
  StringTable* new_table = new StringTable();

  // Rehash the table
  the_table()->move_to(new_table);

  // Delete the table and buckets (entries are reused in new table).
  delete _the_table;
  // Don't check if we need rehashing until the table gets unbalanced again.
  // Then rehash with a new global seed.
  _needs_rehashing = false;
  _the_table = new_table;
}
