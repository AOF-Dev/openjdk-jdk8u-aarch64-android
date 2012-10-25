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
#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "classfile/vmSymbols.hpp"
#include "gc_interface/collectedHeap.hpp"
#include "gc_interface/collectedHeap.inline.hpp"
#include "memory/metadataFactory.hpp"
#include "memory/resourceArea.hpp"
#include "memory/universe.hpp"
#include "memory/universe.inline.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/klass.inline.hpp"
#include "oops/objArrayKlass.hpp"
#include "oops/oop.inline.hpp"
#include "oops/typeArrayKlass.hpp"
#include "oops/typeArrayOop.hpp"
#include "runtime/handles.inline.hpp"

bool typeArrayKlass::compute_is_subtype_of(Klass* k) {
  if (!k->oop_is_typeArray()) {
    return arrayKlass::compute_is_subtype_of(k);
  }

  typeArrayKlass* tak = typeArrayKlass::cast(k);
  if (dimension() != tak->dimension()) return false;

  return element_type() == tak->element_type();
}

typeArrayKlass* typeArrayKlass::create_klass(BasicType type,
                                      const char* name_str, TRAPS) {
  Symbol* sym = NULL;
  if (name_str != NULL) {
    sym = SymbolTable::new_permanent_symbol(name_str, CHECK_NULL);
  }

  ClassLoaderData* null_loader_data = ClassLoaderData::the_null_class_loader_data();

  typeArrayKlass* ak = typeArrayKlass::allocate(null_loader_data, type, sym, CHECK_NULL);

  // Add all classes to our internal class loader list here,
  // including classes in the bootstrap (NULL) class loader.
  // GC walks these as strong roots.
  null_loader_data->add_class(ak);

  // Call complete_create_array_klass after all instance variables have been initialized.
  complete_create_array_klass(ak, ak->super(), CHECK_NULL);

  return ak;
}

typeArrayKlass* typeArrayKlass::allocate(ClassLoaderData* loader_data, BasicType type, Symbol* name, TRAPS) {
  assert(typeArrayKlass::header_size() <= InstanceKlass::header_size(),
      "array klasses must be same size as InstanceKlass");

  int size = arrayKlass::static_size(typeArrayKlass::header_size());

  return new (loader_data, size, THREAD) typeArrayKlass(type, name);
}

typeArrayKlass::typeArrayKlass(BasicType type, Symbol* name) : arrayKlass(name) {
  set_layout_helper(array_layout_helper(type));
  assert(oop_is_array(), "sanity");
  assert(oop_is_typeArray(), "sanity");

  set_max_length(arrayOopDesc::max_array_length(type));
  assert(size() >= typeArrayKlass::header_size(), "bad size");

  set_class_loader_data(ClassLoaderData::the_null_class_loader_data());
}

typeArrayOop typeArrayKlass::allocate_common(int length, bool do_zero, TRAPS) {
  assert(log2_element_size() >= 0, "bad scale");
  if (length >= 0) {
    if (length <= max_length()) {
      size_t size = typeArrayOopDesc::object_size(layout_helper(), length);
      KlassHandle h_k(THREAD, this);
      typeArrayOop t;
      CollectedHeap* ch = Universe::heap();
      if (do_zero) {
        t = (typeArrayOop)CollectedHeap::array_allocate(h_k, (int)size, length, CHECK_NULL);
      } else {
        t = (typeArrayOop)CollectedHeap::array_allocate_nozero(h_k, (int)size, length, CHECK_NULL);
      }
      return t;
    } else {
      report_java_out_of_memory("Requested array size exceeds VM limit");
      JvmtiExport::post_array_size_exhausted();
      THROW_OOP_0(Universe::out_of_memory_error_array_size());
    }
  } else {
    THROW_0(vmSymbols::java_lang_NegativeArraySizeException());
  }
}

oop typeArrayKlass::multi_allocate(int rank, jint* last_size, TRAPS) {
  // For typeArrays this is only called for the last dimension
  assert(rank == 1, "just checking");
  int length = *last_size;
  return allocate(length, THREAD);
}


void typeArrayKlass::copy_array(arrayOop s, int src_pos, arrayOop d, int dst_pos, int length, TRAPS) {
  assert(s->is_typeArray(), "must be type array");

  // Check destination
  if (!d->is_typeArray() || element_type() != typeArrayKlass::cast(d->klass())->element_type()) {
    THROW(vmSymbols::java_lang_ArrayStoreException());
  }

  // Check is all offsets and lengths are non negative
  if (src_pos < 0 || dst_pos < 0 || length < 0) {
    THROW(vmSymbols::java_lang_ArrayIndexOutOfBoundsException());
  }
  // Check if the ranges are valid
  if  ( (((unsigned int) length + (unsigned int) src_pos) > (unsigned int) s->length())
     || (((unsigned int) length + (unsigned int) dst_pos) > (unsigned int) d->length()) ) {
    THROW(vmSymbols::java_lang_ArrayIndexOutOfBoundsException());
  }
  // Check zero copy
  if (length == 0)
    return;

  // This is an attempt to make the copy_array fast.
  int l2es = log2_element_size();
  int ihs = array_header_in_bytes() / wordSize;
  char* src = (char*) ((oop*)s + ihs) + ((size_t)src_pos << l2es);
  char* dst = (char*) ((oop*)d + ihs) + ((size_t)dst_pos << l2es);
  Copy::conjoint_memory_atomic(src, dst, (size_t)length << l2es);
}


// create a klass of array holding typeArrays
Klass* typeArrayKlass::array_klass_impl(bool or_null, int n, TRAPS) {
  int dim = dimension();
  assert(dim <= n, "check order of chain");
    if (dim == n)
      return this;

  if (higher_dimension() == NULL) {
    if (or_null)  return NULL;

    ResourceMark rm;
    JavaThread *jt = (JavaThread *)THREAD;
    {
      MutexLocker mc(Compile_lock, THREAD);   // for vtables
      // Atomic create higher dimension and link into list
      MutexLocker mu(MultiArray_lock, THREAD);

      if (higher_dimension() == NULL) {
        Klass* oak = objArrayKlass::allocate_objArray_klass(
              class_loader_data(), dim + 1, this, CHECK_NULL);
        objArrayKlass* h_ak = objArrayKlass::cast(oak);
        h_ak->set_lower_dimension(this);
        OrderAccess::storestore();
        set_higher_dimension(h_ak);
        assert(h_ak->oop_is_objArray(), "incorrect initialization of objArrayKlass");
      }
    }
  } else {
    CHECK_UNHANDLED_OOPS_ONLY(Thread::current()->clear_unhandled_oops());
  }
  objArrayKlass* h_ak = objArrayKlass::cast(higher_dimension());
  if (or_null) {
    return h_ak->array_klass_or_null(n);
  }
  return h_ak->array_klass(n, CHECK_NULL);
}

Klass* typeArrayKlass::array_klass_impl(bool or_null, TRAPS) {
  return array_klass_impl(or_null, dimension() +  1, THREAD);
}

int typeArrayKlass::oop_size(oop obj) const {
  assert(obj->is_typeArray(),"must be a type array");
  typeArrayOop t = typeArrayOop(obj);
  return t->object_size();
}

void typeArrayKlass::oop_follow_contents(oop obj) {
  assert(obj->is_typeArray(),"must be a type array");
  // Performance tweak: We skip iterating over the klass pointer since we
  // know that Universe::typeArrayKlass never moves.
}

#ifndef SERIALGC
void typeArrayKlass::oop_follow_contents(ParCompactionManager* cm, oop obj) {
  assert(obj->is_typeArray(),"must be a type array");
  // Performance tweak: We skip iterating over the klass pointer since we
  // know that Universe::typeArrayKlass never moves.
}
#endif // SERIALGC

int typeArrayKlass::oop_adjust_pointers(oop obj) {
  assert(obj->is_typeArray(),"must be a type array");
  typeArrayOop t = typeArrayOop(obj);
  // Performance tweak: We skip iterating over the klass pointer since we
  // know that Universe::typeArrayKlass never moves.
  return t->object_size();
}

int typeArrayKlass::oop_oop_iterate(oop obj, ExtendedOopClosure* blk) {
  assert(obj->is_typeArray(),"must be a type array");
  typeArrayOop t = typeArrayOop(obj);
  // Performance tweak: We skip iterating over the klass pointer since we
  // know that Universe::typeArrayKlass never moves.
  return t->object_size();
}

int typeArrayKlass::oop_oop_iterate_m(oop obj, ExtendedOopClosure* blk, MemRegion mr) {
  assert(obj->is_typeArray(),"must be a type array");
  typeArrayOop t = typeArrayOop(obj);
  // Performance tweak: We skip iterating over the klass pointer since we
  // know that Universe::typeArrayKlass never moves.
  return t->object_size();
}

#ifndef SERIALGC
void typeArrayKlass::oop_push_contents(PSPromotionManager* pm, oop obj) {
  ShouldNotReachHere();
  assert(obj->is_typeArray(),"must be a type array");
}

int
typeArrayKlass::oop_update_pointers(ParCompactionManager* cm, oop obj) {
  assert(obj->is_typeArray(),"must be a type array");
  return typeArrayOop(obj)->object_size();
}
#endif // SERIALGC

void typeArrayKlass::initialize(TRAPS) {
  // Nothing to do. Having this function is handy since objArrayKlasses can be
  // initialized by calling initialize on their bottom_klass, see objArrayKlass::initialize
}

const char* typeArrayKlass::external_name(BasicType type) {
  switch (type) {
    case T_BOOLEAN: return "[Z";
    case T_CHAR:    return "[C";
    case T_FLOAT:   return "[F";
    case T_DOUBLE:  return "[D";
    case T_BYTE:    return "[B";
    case T_SHORT:   return "[S";
    case T_INT:     return "[I";
    case T_LONG:    return "[J";
    default: ShouldNotReachHere();
  }
  return NULL;
}


// Printing

void typeArrayKlass::print_on(outputStream* st) const {
#ifndef PRODUCT
  assert(is_klass(), "must be klass");
  print_value_on(st);
  Klass::print_on(st);
#endif //PRODUCT
}

void typeArrayKlass::print_value_on(outputStream* st) const {
  assert(is_klass(), "must be klass");
  st->print("{type array ");
  switch (element_type()) {
    case T_BOOLEAN: st->print("bool");    break;
    case T_CHAR:    st->print("char");    break;
    case T_FLOAT:   st->print("float");   break;
    case T_DOUBLE:  st->print("double");  break;
    case T_BYTE:    st->print("byte");    break;
    case T_SHORT:   st->print("short");   break;
    case T_INT:     st->print("int");     break;
    case T_LONG:    st->print("long");    break;
    default: ShouldNotReachHere();
  }
  st->print("}");
}

#ifndef PRODUCT

static void print_boolean_array(typeArrayOop ta, int print_len, outputStream* st) {
  for (int index = 0; index < print_len; index++) {
    st->print_cr(" - %3d: %s", index, (ta->bool_at(index) == 0) ? "false" : "true");
  }
}


static void print_char_array(typeArrayOop ta, int print_len, outputStream* st) {
  for (int index = 0; index < print_len; index++) {
    jchar c = ta->char_at(index);
    st->print_cr(" - %3d: %x %c", index, c, isprint(c) ? c : ' ');
  }
}


static void print_float_array(typeArrayOop ta, int print_len, outputStream* st) {
  for (int index = 0; index < print_len; index++) {
    st->print_cr(" - %3d: %g", index, ta->float_at(index));
  }
}


static void print_double_array(typeArrayOop ta, int print_len, outputStream* st) {
  for (int index = 0; index < print_len; index++) {
    st->print_cr(" - %3d: %g", index, ta->double_at(index));
  }
}


static void print_byte_array(typeArrayOop ta, int print_len, outputStream* st) {
  for (int index = 0; index < print_len; index++) {
    jbyte c = ta->byte_at(index);
    st->print_cr(" - %3d: %x %c", index, c, isprint(c) ? c : ' ');
  }
}


static void print_short_array(typeArrayOop ta, int print_len, outputStream* st) {
  for (int index = 0; index < print_len; index++) {
    int v = ta->ushort_at(index);
    st->print_cr(" - %3d: 0x%x\t %d", index, v, v);
  }
}


static void print_int_array(typeArrayOop ta, int print_len, outputStream* st) {
  for (int index = 0; index < print_len; index++) {
    jint v = ta->int_at(index);
    st->print_cr(" - %3d: 0x%x %d", index, v, v);
  }
}


static void print_long_array(typeArrayOop ta, int print_len, outputStream* st) {
  for (int index = 0; index < print_len; index++) {
    jlong v = ta->long_at(index);
    st->print_cr(" - %3d: 0x%x 0x%x", index, high(v), low(v));
  }
}


void typeArrayKlass::oop_print_on(oop obj, outputStream* st) {
  arrayKlass::oop_print_on(obj, st);
  typeArrayOop ta = typeArrayOop(obj);
  int print_len = MIN2((intx) ta->length(), MaxElementPrintSize);
  switch (element_type()) {
    case T_BOOLEAN: print_boolean_array(ta, print_len, st); break;
    case T_CHAR:    print_char_array(ta, print_len, st);    break;
    case T_FLOAT:   print_float_array(ta, print_len, st);   break;
    case T_DOUBLE:  print_double_array(ta, print_len, st);  break;
    case T_BYTE:    print_byte_array(ta, print_len, st);    break;
    case T_SHORT:   print_short_array(ta, print_len, st);   break;
    case T_INT:     print_int_array(ta, print_len, st);     break;
    case T_LONG:    print_long_array(ta, print_len, st);    break;
    default: ShouldNotReachHere();
  }
  int remaining = ta->length() - print_len;
  if (remaining > 0) {
    tty->print_cr(" - <%d more elements, increase MaxElementPrintSize to print>", remaining);
  }
}

#endif // PRODUCT

const char* typeArrayKlass::internal_name() const {
  return Klass::external_name();
}
