/*
 * Copyright (c) 2008, 2012, Oracle and/or its affiliates. All rights reserved.
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
#include "compiler/compileBroker.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/oopMapCache.hpp"
#include "memory/allocation.inline.hpp"
#include "memory/oopFactory.hpp"
#include "prims/methodHandles.hpp"
#include "runtime/compilationPolicy.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/reflection.hpp"
#include "runtime/signature.hpp"
#include "runtime/stubRoutines.hpp"


/*
 * JSR 292 reference implementation: method handles
 * The JDK 7 reference implementation represented method handle
 * combinations as chains.  Each link in the chain had a "vmentry"
 * field which pointed at a bit of assembly code which performed
 * one transformation before dispatching to the next link in the chain.
 *
 * The current reference implementation pushes almost all code generation
 * responsibility to (trusted) Java code.  A method handle contains a
 * pointer to its "LambdaForm", which embodies all details of the method
 * handle's behavior.  The LambdaForm is a normal Java object, managed
 * by a runtime coded in Java.
 */

bool MethodHandles::_enabled = false; // set true after successful native linkage
MethodHandlesAdapterBlob* MethodHandles::_adapter_code = NULL;

//------------------------------------------------------------------------------
// MethodHandles::generate_adapters
//
void MethodHandles::generate_adapters() {
  if (!EnableInvokeDynamic || SystemDictionary::MethodHandle_klass() == NULL)  return;

  assert(_adapter_code == NULL, "generate only once");

  ResourceMark rm;
  TraceTime timer("MethodHandles adapters generation", TraceStartupTime);
  _adapter_code = MethodHandlesAdapterBlob::create(adapter_code_size);
  if (_adapter_code == NULL)
    vm_exit_out_of_memory(adapter_code_size, "CodeCache: no room for MethodHandles adapters");
  {
    CodeBuffer code(_adapter_code);
    MethodHandlesAdapterGenerator g(&code);
    g.generate();
    code.log_section_sizes("MethodHandlesAdapterBlob");
  }
}

//------------------------------------------------------------------------------
// MethodHandlesAdapterGenerator::generate
//
void MethodHandlesAdapterGenerator::generate() {
  // Generate generic method handle adapters.
  // Generate interpreter entries
  for (Interpreter::MethodKind mk = Interpreter::method_handle_invoke_FIRST;
       mk <= Interpreter::method_handle_invoke_LAST;
       mk = Interpreter::MethodKind(1 + (int)mk)) {
    vmIntrinsics::ID iid = Interpreter::method_handle_intrinsic(mk);
    StubCodeMark mark(this, "MethodHandle::interpreter_entry", vmIntrinsics::name_at(iid));
    address entry = MethodHandles::generate_method_handle_interpreter_entry(_masm, iid);
    if (entry != NULL) {
      Interpreter::set_entry_for_kind(mk, entry);
    }
    // If the entry is not set, it will throw AbstractMethodError.
  }
}

void MethodHandles::set_enabled(bool z) {
  if (_enabled != z) {
    guarantee(z && EnableInvokeDynamic, "can only enable once, and only if -XX:+EnableInvokeDynamic");
    _enabled = z;
  }
}

// MemberName support

// import java_lang_invoke_MemberName.*
enum {
  IS_METHOD      = java_lang_invoke_MemberName::MN_IS_METHOD,
  IS_CONSTRUCTOR = java_lang_invoke_MemberName::MN_IS_CONSTRUCTOR,
  IS_FIELD       = java_lang_invoke_MemberName::MN_IS_FIELD,
  IS_TYPE        = java_lang_invoke_MemberName::MN_IS_TYPE,
  REFERENCE_KIND_SHIFT = java_lang_invoke_MemberName::MN_REFERENCE_KIND_SHIFT,
  REFERENCE_KIND_MASK  = java_lang_invoke_MemberName::MN_REFERENCE_KIND_MASK,
  SEARCH_SUPERCLASSES = java_lang_invoke_MemberName::MN_SEARCH_SUPERCLASSES,
  SEARCH_INTERFACES   = java_lang_invoke_MemberName::MN_SEARCH_INTERFACES,
  ALL_KINDS      = IS_METHOD | IS_CONSTRUCTOR | IS_FIELD | IS_TYPE
};

Handle MethodHandles::new_MemberName(TRAPS) {
  Handle empty;
  instanceKlassHandle k(THREAD, SystemDictionary::MemberName_klass());
  if (!k->is_initialized())  k->initialize(CHECK_(empty));
  return Handle(THREAD, k->allocate_instance(THREAD));
}

oop MethodHandles::init_MemberName(oop mname_oop, oop target_oop) {
  Klass* target_klass = target_oop->klass();
  if (target_klass == SystemDictionary::reflect_Field_klass()) {
    oop clazz = java_lang_reflect_Field::clazz(target_oop); // fd.field_holder()
    int slot  = java_lang_reflect_Field::slot(target_oop);  // fd.index()
    int mods  = java_lang_reflect_Field::modifiers(target_oop);
    oop type  = java_lang_reflect_Field::type(target_oop);
    oop name  = java_lang_reflect_Field::name(target_oop);
    Klass* k = java_lang_Class::as_Klass(clazz);
    intptr_t offset = InstanceKlass::cast(k)->field_offset(slot);
    return init_field_MemberName(mname_oop, k, accessFlags_from(mods), type, name, offset);
  } else if (target_klass == SystemDictionary::reflect_Method_klass()) {
    oop clazz  = java_lang_reflect_Method::clazz(target_oop);
    int slot   = java_lang_reflect_Method::slot(target_oop);
    Klass* k = java_lang_Class::as_Klass(clazz);
    if (k != NULL && Klass::cast(k)->oop_is_instance()) {
      Method* m = InstanceKlass::cast(k)->method_with_idnum(slot);
      return init_method_MemberName(mname_oop, m, true, k);
    }
  } else if (target_klass == SystemDictionary::reflect_Constructor_klass()) {
    oop clazz  = java_lang_reflect_Constructor::clazz(target_oop);
    int slot   = java_lang_reflect_Constructor::slot(target_oop);
    Klass* k = java_lang_Class::as_Klass(clazz);
    if (k != NULL && Klass::cast(k)->oop_is_instance()) {
      Method* m = InstanceKlass::cast(k)->method_with_idnum(slot);
      return init_method_MemberName(mname_oop, m, false, k);
    }
  } else if (target_klass == SystemDictionary::MemberName_klass()) {
    // Note: This only works if the MemberName has already been resolved.
    oop clazz        = java_lang_invoke_MemberName::clazz(target_oop);
    int flags        = java_lang_invoke_MemberName::flags(target_oop);
    Metadata* vmtarget=java_lang_invoke_MemberName::vmtarget(target_oop);
    intptr_t vmindex = java_lang_invoke_MemberName::vmindex(target_oop);
    Klass* k         = java_lang_Class::as_Klass(clazz);
    int ref_kind     = (flags >> REFERENCE_KIND_SHIFT) & REFERENCE_KIND_MASK;
    if (vmtarget == NULL)  return NULL;  // not resolved
    if ((flags & IS_FIELD) != 0) {
      assert(vmtarget->is_klass(), "field vmtarget is Klass*");
      int basic_mods = (ref_kind_is_static(ref_kind) ? JVM_ACC_STATIC : 0);
      // FIXME:  how does k (receiver_limit) contribute?
      return init_field_MemberName(mname_oop, (Klass*)vmtarget, accessFlags_from(basic_mods), NULL, NULL, vmindex);
    } else if ((flags & (IS_METHOD | IS_CONSTRUCTOR)) != 0) {
      assert(vmtarget->is_method(), "method or constructor vmtarget is Method*");
      return init_method_MemberName(mname_oop, (Method*)vmtarget, ref_kind_does_dispatch(ref_kind), k);
    } else {
      return NULL;
    }
  }
  return NULL;
}

oop MethodHandles::init_method_MemberName(oop mname_oop, Method* m, bool do_dispatch,
                                          Klass* receiver_limit) {
  AccessFlags mods = m->access_flags();
  int flags = (jushort)( mods.as_short() & JVM_RECOGNIZED_METHOD_MODIFIERS );
  int vmindex = Method::nonvirtual_vtable_index; // implies never any dispatch
  Klass* mklass = m->method_holder();
  if (receiver_limit == NULL)
    receiver_limit = mklass;
  if (m->is_initializer()) {
    flags |= IS_CONSTRUCTOR | (JVM_REF_invokeSpecial << REFERENCE_KIND_SHIFT);
  } else if (mods.is_static()) {
    flags |= IS_METHOD | (JVM_REF_invokeStatic << REFERENCE_KIND_SHIFT);
  } else if (receiver_limit != mklass &&
             !Klass::cast(receiver_limit)->is_subtype_of(mklass)) {
    return NULL;  // bad receiver limit
  } else if (Klass::cast(receiver_limit)->is_interface() &&
             Klass::cast(mklass)->is_interface()) {
    flags |= IS_METHOD | (JVM_REF_invokeInterface << REFERENCE_KIND_SHIFT);
    receiver_limit = mklass;  // ignore passed-in limit; interfaces are interconvertible
    vmindex = klassItable::compute_itable_index(m);
  } else if (mklass != receiver_limit && Klass::cast(mklass)->is_interface()) {
    flags |= IS_METHOD | (JVM_REF_invokeVirtual << REFERENCE_KIND_SHIFT);
    // it is a miranda method, so m->vtable_index is not what we want
    ResourceMark rm;
    klassVtable* vt = InstanceKlass::cast(receiver_limit)->vtable();
    vmindex = vt->index_of_miranda(m->name(), m->signature());
  } else if (!do_dispatch || m->can_be_statically_bound()) {
    flags |= IS_METHOD | (JVM_REF_invokeSpecial << REFERENCE_KIND_SHIFT);
  } else {
    flags |= IS_METHOD | (JVM_REF_invokeVirtual << REFERENCE_KIND_SHIFT);
    vmindex = m->vtable_index();
  }

  java_lang_invoke_MemberName::set_flags(mname_oop,    flags);
  java_lang_invoke_MemberName::set_vmtarget(mname_oop, m);
  java_lang_invoke_MemberName::set_vmindex(mname_oop,  vmindex);   // vtable/itable index
  java_lang_invoke_MemberName::set_clazz(mname_oop,    Klass::cast(receiver_limit)->java_mirror());
  // Note:  name and type can be lazily computed by resolve_MemberName,
  // if Java code needs them as resolved String and MethodType objects.
  // The clazz must be eagerly stored, because it provides a GC
  // root to help keep alive the Method*.
  // If relevant, the vtable or itable value is stored as vmindex.
  // This is done eagerly, since it is readily available without
  // constructing any new objects.
  // TO DO: maybe intern mname_oop
  return mname_oop;
}

Handle MethodHandles::init_method_MemberName(oop mname_oop, CallInfo& info, TRAPS) {
  Handle empty;
  if (info.resolved_appendix().not_null()) {
    // The resolved MemberName must not be accompanied by an appendix argument,
    // since there is no way to bind this value into the MemberName.
    // Caller is responsible to prevent this from happening.
    THROW_MSG_(vmSymbols::java_lang_InternalError(), "appendix", empty);
  }
  methodHandle m = info.resolved_method();
  KlassHandle defc = info.resolved_klass();
  int vmindex = -1;
  if (defc->is_interface() && Klass::cast(m->method_holder())->is_interface()) {
    // LinkResolver does not report itable indexes!  (fix this?)
    vmindex = klassItable::compute_itable_index(m());
  } else if (m->can_be_statically_bound()) {
    // LinkResolver reports vtable index even for final methods!
    vmindex = Method::nonvirtual_vtable_index;
  } else {
    vmindex = info.vtable_index();
  }
  oop res = init_method_MemberName(mname_oop, m(), (vmindex >= 0), defc());
  assert(res == NULL || (java_lang_invoke_MemberName::vmindex(res) == vmindex), "");
  return Handle(THREAD, res);
}

oop MethodHandles::init_field_MemberName(oop mname_oop, Klass* field_holder,
                                         AccessFlags mods, oop type, oop name,
                                         intptr_t offset, bool is_setter) {
  int flags = (jushort)( mods.as_short() & JVM_RECOGNIZED_FIELD_MODIFIERS );
  flags |= IS_FIELD | ((mods.is_static() ? JVM_REF_getStatic : JVM_REF_getField) << REFERENCE_KIND_SHIFT);
  if (is_setter)  flags += ((JVM_REF_putField - JVM_REF_getField) << REFERENCE_KIND_SHIFT);
  Metadata* vmtarget = field_holder;
  int vmindex  = offset;  // determines the field uniquely when combined with static bit
  java_lang_invoke_MemberName::set_flags(mname_oop,    flags);
  java_lang_invoke_MemberName::set_vmtarget(mname_oop, vmtarget);
  java_lang_invoke_MemberName::set_vmindex(mname_oop,  vmindex);
  java_lang_invoke_MemberName::set_clazz(mname_oop,    Klass::cast(field_holder)->java_mirror());
  if (name != NULL)
    java_lang_invoke_MemberName::set_name(mname_oop,   name);
  if (type != NULL)
    java_lang_invoke_MemberName::set_type(mname_oop,   type);
  // Note:  name and type can be lazily computed by resolve_MemberName,
  // if Java code needs them as resolved String and Class objects.
  // Note that the incoming type oop might be pre-resolved (non-null).
  // The base clazz and field offset (vmindex) must be eagerly stored,
  // because they unambiguously identify the field.
  // Although the fieldDescriptor::_index would also identify the field,
  // we do not use it, because it is harder to decode.
  // TO DO: maybe intern mname_oop
  return mname_oop;
}

Handle MethodHandles::init_field_MemberName(oop mname_oop, FieldAccessInfo& info, TRAPS) {
  return Handle();
#if 0 // FIXME
  KlassHandle field_holder = info.klass();
  intptr_t    field_offset = info.field_offset();
  return init_field_MemberName(mname_oop, field_holder(),
                               info.access_flags(),
                               type, name,
                               field_offset, false /*is_setter*/);
#endif
}


// JVM 2.9 Special Methods:
// A method is signature polymorphic if and only if all of the following conditions hold :
// * It is declared in the java.lang.invoke.MethodHandle class.
// * It has a single formal parameter of type Object[].
// * It has a return type of Object.
// * It has the ACC_VARARGS and ACC_NATIVE flags set.
bool MethodHandles::is_method_handle_invoke_name(Klass* klass, Symbol* name) {
  if (klass == NULL)
    return false;
  // The following test will fail spuriously during bootstrap of MethodHandle itself:
  //    if (klass != SystemDictionary::MethodHandle_klass())
  // Test the name instead:
  if (Klass::cast(klass)->name() != vmSymbols::java_lang_invoke_MethodHandle())
    return false;
  Symbol* poly_sig = vmSymbols::object_array_object_signature();
  Method* m = InstanceKlass::cast(klass)->find_method(name, poly_sig);
  if (m == NULL)  return false;
  int required = JVM_ACC_NATIVE | JVM_ACC_VARARGS;
  int flags = m->access_flags().as_int();
  return (flags & required) == required;
}


Symbol* MethodHandles::signature_polymorphic_intrinsic_name(vmIntrinsics::ID iid) {
  assert(is_signature_polymorphic_intrinsic(iid), err_msg("iid=%d", iid));
  switch (iid) {
  case vmIntrinsics::_invokeBasic:      return vmSymbols::invokeBasic_name();
  case vmIntrinsics::_linkToVirtual:    return vmSymbols::linkToVirtual_name();
  case vmIntrinsics::_linkToStatic:     return vmSymbols::linkToStatic_name();
  case vmIntrinsics::_linkToSpecial:    return vmSymbols::linkToSpecial_name();
  case vmIntrinsics::_linkToInterface:  return vmSymbols::linkToInterface_name();
  }
  assert(false, "");
  return 0;
}

int MethodHandles::signature_polymorphic_intrinsic_ref_kind(vmIntrinsics::ID iid) {
  switch (iid) {
  case vmIntrinsics::_invokeBasic:      return 0;
  case vmIntrinsics::_linkToVirtual:    return JVM_REF_invokeVirtual;
  case vmIntrinsics::_linkToStatic:     return JVM_REF_invokeStatic;
  case vmIntrinsics::_linkToSpecial:    return JVM_REF_invokeSpecial;
  case vmIntrinsics::_linkToInterface:  return JVM_REF_invokeInterface;
  }
  assert(false, err_msg("iid=%d", iid));
  return 0;
}

vmIntrinsics::ID MethodHandles::signature_polymorphic_name_id(Symbol* name) {
  vmSymbols::SID name_id = vmSymbols::find_sid(name);
  switch (name_id) {
  // The ID _invokeGeneric stands for all non-static signature-polymorphic methods, except built-ins.
  case vmSymbols::VM_SYMBOL_ENUM_NAME(invoke_name):           return vmIntrinsics::_invokeGeneric;
  // The only built-in non-static signature-polymorphic method is MethodHandle.invokeBasic:
  case vmSymbols::VM_SYMBOL_ENUM_NAME(invokeBasic_name):      return vmIntrinsics::_invokeBasic;

  // There is one static signature-polymorphic method for each JVM invocation mode.
  case vmSymbols::VM_SYMBOL_ENUM_NAME(linkToVirtual_name):    return vmIntrinsics::_linkToVirtual;
  case vmSymbols::VM_SYMBOL_ENUM_NAME(linkToStatic_name):     return vmIntrinsics::_linkToStatic;
  case vmSymbols::VM_SYMBOL_ENUM_NAME(linkToSpecial_name):    return vmIntrinsics::_linkToSpecial;
  case vmSymbols::VM_SYMBOL_ENUM_NAME(linkToInterface_name):  return vmIntrinsics::_linkToInterface;
  }

  // Cover the case of invokeExact and any future variants of invokeFoo.
  Klass* mh_klass = SystemDictionary::well_known_klass(
                              SystemDictionary::WK_KLASS_ENUM_NAME(MethodHandle_klass) );
  if (mh_klass != NULL && is_method_handle_invoke_name(mh_klass, name))
    return vmIntrinsics::_invokeGeneric;

  // Note: The pseudo-intrinsic _compiledLambdaForm is never linked against.
  // Instead it is used to mark lambda forms bound to invokehandle or invokedynamic.
  return vmIntrinsics::_none;
}

vmIntrinsics::ID MethodHandles::signature_polymorphic_name_id(Klass* klass, Symbol* name) {
  if (klass != NULL &&
      Klass::cast(klass)->name() == vmSymbols::java_lang_invoke_MethodHandle()) {
    vmIntrinsics::ID iid = signature_polymorphic_name_id(name);
    if (iid != vmIntrinsics::_none)
      return iid;
    if (is_method_handle_invoke_name(klass, name))
      return vmIntrinsics::_invokeGeneric;
  }
  return vmIntrinsics::_none;
}


// convert the external string or reflective type to an internal signature
Symbol* MethodHandles::lookup_signature(oop type_str, bool intern_if_not_found, TRAPS) {
  if (java_lang_invoke_MethodType::is_instance(type_str)) {
    return java_lang_invoke_MethodType::as_signature(type_str, intern_if_not_found, CHECK_NULL);
  } else if (java_lang_Class::is_instance(type_str)) {
    return java_lang_Class::as_signature(type_str, false, CHECK_NULL);
  } else if (java_lang_String::is_instance(type_str)) {
    if (intern_if_not_found) {
      return java_lang_String::as_symbol(type_str, CHECK_NULL);
    } else {
      return java_lang_String::as_symbol_or_null(type_str);
    }
  } else {
    THROW_MSG_(vmSymbols::java_lang_InternalError(), "unrecognized type", NULL);
  }
}

static const char OBJ_SIG[] = "Ljava/lang/Object;";
enum { OBJ_SIG_LEN = 18 };

bool MethodHandles::is_basic_type_signature(Symbol* sig) {
  assert(vmSymbols::object_signature()->utf8_length() == (int)OBJ_SIG_LEN, "");
  assert(vmSymbols::object_signature()->equals(OBJ_SIG), "");
  const int len = sig->utf8_length();
  for (int i = 0; i < len; i++) {
    switch (sig->byte_at(i)) {
    case 'L':
      // only java/lang/Object is valid here
      if (sig->index_of_at(i, OBJ_SIG, OBJ_SIG_LEN) != i)
        return false;
      i += OBJ_SIG_LEN-1;  //-1 because of i++ in loop
      continue;
    case '(': case ')': case 'V':
    case 'I': case 'J': case 'F': case 'D':
      continue;
    //case '[':
    //case 'Z': case 'B': case 'C': case 'S':
    default:
      return false;
    }
  }
  return true;
}

Symbol* MethodHandles::lookup_basic_type_signature(Symbol* sig, bool keep_last_arg, TRAPS) {
  Symbol* bsig = NULL;
  if (sig == NULL) {
    return sig;
  } else if (is_basic_type_signature(sig)) {
    sig->increment_refcount();
    return sig;  // that was easy
  } else if (sig->byte_at(0) != '(') {
    BasicType bt = char2type(sig->byte_at(0));
    if (is_subword_type(bt)) {
      bsig = vmSymbols::int_signature();
    } else {
      assert(bt == T_OBJECT || bt == T_ARRAY, "is_basic_type_signature was false");
      bsig = vmSymbols::object_signature();
    }
  } else {
    ResourceMark rm;
    stringStream buffer(128);
    buffer.put('(');
    int arg_pos = 0, keep_arg_pos = -1;
    if (keep_last_arg)
      keep_arg_pos = ArgumentCount(sig).size() - 1;
    for (SignatureStream ss(sig); !ss.is_done(); ss.next()) {
      BasicType bt = ss.type();
      size_t this_arg_pos = buffer.size();
      if (ss.at_return_type()) {
        buffer.put(')');
      }
      if (arg_pos == keep_arg_pos) {
        buffer.write((char*) ss.raw_bytes(),
                     (int)   ss.raw_length());
      } else if (bt == T_OBJECT || bt == T_ARRAY) {
        buffer.write(OBJ_SIG, OBJ_SIG_LEN);
      } else {
        if (is_subword_type(bt))
          bt = T_INT;
        buffer.put(type2char(bt));
      }
      arg_pos++;
    }
    const char* sigstr =       buffer.base();
    int         siglen = (int) buffer.size();
    bsig = SymbolTable::new_symbol(sigstr, siglen, THREAD);
  }
  assert(is_basic_type_signature(bsig) ||
         // detune assert in case the injected argument is not a basic type:
         keep_last_arg, "");
  return bsig;
}

void MethodHandles::print_as_basic_type_signature_on(outputStream* st,
                                                     Symbol* sig,
                                                     bool keep_arrays,
                                                     bool keep_basic_names) {
  st = st ? st : tty;
  int len  = sig->utf8_length();
  int array = 0;
  bool prev_type = false;
  for (int i = 0; i < len; i++) {
    char ch = sig->byte_at(i);
    switch (ch) {
    case '(': case ')':
      prev_type = false;
      st->put(ch);
      continue;
    case '[':
      if (!keep_basic_names && keep_arrays)
        st->put(ch);
      array++;
      continue;
    case 'L':
      {
        if (prev_type)  st->put(',');
        int start = i+1, slash = start;
        while (++i < len && (ch = sig->byte_at(i)) != ';') {
          if (ch == '/' || ch == '.' || ch == '$')  slash = i+1;
        }
        if (slash < i)  start = slash;
        if (!keep_basic_names) {
          st->put('L');
        } else {
          for (int j = start; j < i; j++)
            st->put(sig->byte_at(j));
          prev_type = true;
        }
        break;
      }
    default:
      {
        if (array && char2type(ch) != T_ILLEGAL && !keep_arrays) {
          ch = '[';
          array = 0;
        }
        if (prev_type)  st->put(',');
        const char* n = NULL;
        if (keep_basic_names)
          n = type2name(char2type(ch));
        if (n == NULL) {
          // unknown letter, or we don't want to know its name
          st->put(ch);
        } else {
          st->print(n);
          prev_type = true;
        }
        break;
      }
    }
    // Switch break goes here to take care of array suffix:
    if (prev_type) {
      while (array > 0) {
        st->print("[]");
        --array;
      }
    }
    array = 0;
  }
}



static oop object_java_mirror() {
  return Klass::cast(SystemDictionary::Object_klass())->java_mirror();
}

static oop field_name_or_null(Symbol* s) {
  if (s == NULL)  return NULL;
  return StringTable::lookup(s);
}

static oop field_signature_type_or_null(Symbol* s) {
  if (s == NULL)  return NULL;
  BasicType bt = FieldType::basic_type(s);
  if (is_java_primitive(bt)) {
    assert(s->utf8_length() == 1, "");
    return java_lang_Class::primitive_mirror(bt);
  }
  // Here are some more short cuts for common types.
  // They are optional, since reference types can be resolved lazily.
  if (bt == T_OBJECT) {
    if (s == vmSymbols::object_signature()) {
      return object_java_mirror();
    } else if (s == vmSymbols::class_signature()) {
      return Klass::cast(SystemDictionary::Class_klass())->java_mirror();
    } else if (s == vmSymbols::string_signature()) {
      return Klass::cast(SystemDictionary::String_klass())->java_mirror();
    } else {
      int len = s->utf8_length();
      if (s->byte_at(0) == 'L' && s->byte_at(len-1) == ';') {
        TempNewSymbol cname = SymbolTable::probe((const char*)&s->bytes()[1], len-2);
        if (cname == NULL)  return NULL;
        Klass* wkk = SystemDictionary::find_well_known_klass(cname);
        if (wkk == NULL)  return NULL;
        return Klass::cast(wkk)->java_mirror();
      }
    }
  }
  return NULL;
}


// An unresolved member name is a mere symbolic reference.
// Resolving it plants a vmtarget/vmindex in it,
// which refers directly to JVM internals.
Handle MethodHandles::resolve_MemberName(Handle mname, TRAPS) {
  Handle empty;
  assert(java_lang_invoke_MemberName::is_instance(mname()), "");

  if (java_lang_invoke_MemberName::vmtarget(mname()) != NULL) {
    // Already resolved.
    DEBUG_ONLY(int vmindex = java_lang_invoke_MemberName::vmindex(mname()));
    assert(vmindex >= Method::nonvirtual_vtable_index, "");
    return mname;
  }

  Handle defc_oop(THREAD, java_lang_invoke_MemberName::clazz(mname()));
  Handle name_str(THREAD, java_lang_invoke_MemberName::name( mname()));
  Handle type_str(THREAD, java_lang_invoke_MemberName::type( mname()));
  int    flags    =       java_lang_invoke_MemberName::flags(mname());
  int    ref_kind =       (flags >> REFERENCE_KIND_SHIFT) & REFERENCE_KIND_MASK;
  if (!ref_kind_is_valid(ref_kind)) {
    THROW_MSG_(vmSymbols::java_lang_InternalError(), "obsolete MemberName format", empty);
  }

  DEBUG_ONLY(int old_vmindex);
  assert((old_vmindex = java_lang_invoke_MemberName::vmindex(mname())) == 0, "clean input");

  if (defc_oop.is_null() || name_str.is_null() || type_str.is_null()) {
    THROW_MSG_(vmSymbols::java_lang_IllegalArgumentException(), "nothing to resolve", empty);
  }

  instanceKlassHandle defc;
  {
    Klass* defc_klass = java_lang_Class::as_Klass(defc_oop());
    if (defc_klass == NULL)  return empty;  // a primitive; no resolution possible
    if (!Klass::cast(defc_klass)->oop_is_instance()) {
      if (!Klass::cast(defc_klass)->oop_is_array())  return empty;
      defc_klass = SystemDictionary::Object_klass();
    }
    defc = instanceKlassHandle(THREAD, defc_klass);
  }
  if (defc.is_null()) {
    THROW_MSG_(vmSymbols::java_lang_InternalError(), "primitive class", empty);
  }
  defc->link_class(CHECK_(empty));  // possible safepoint

  // convert the external string name to an internal symbol
  TempNewSymbol name = java_lang_String::as_symbol_or_null(name_str());
  if (name == NULL)  return empty;  // no such name
  if (name == vmSymbols::class_initializer_name())
    return empty; // illegal name

  vmIntrinsics::ID mh_invoke_id = vmIntrinsics::_none;
  if ((flags & ALL_KINDS) == IS_METHOD &&
      (defc() == SystemDictionary::MethodHandle_klass()) &&
      (ref_kind == JVM_REF_invokeVirtual ||
       ref_kind == JVM_REF_invokeSpecial ||
       // static invocation mode is required for _linkToVirtual, etc.:
       ref_kind == JVM_REF_invokeStatic)) {
    vmIntrinsics::ID iid = signature_polymorphic_name_id(name);
    if (iid != vmIntrinsics::_none &&
        ((ref_kind == JVM_REF_invokeStatic) == is_signature_polymorphic_static(iid))) {
      // Virtual methods invoke and invokeExact, plus internal invokers like _invokeBasic.
      // For a static reference it could an internal linkage routine like _linkToVirtual, etc.
      mh_invoke_id = iid;
    }
  }

  // convert the external string or reflective type to an internal signature
  TempNewSymbol type = lookup_signature(type_str(), (mh_invoke_id != vmIntrinsics::_none), CHECK_(empty));
  if (type == NULL)  return empty;  // no such signature exists in the VM

  // Time to do the lookup.
  switch (flags & ALL_KINDS) {
  case IS_METHOD:
    {
      CallInfo result;
      bool do_dispatch = true;  // default, neutral setting
      {
        assert(!HAS_PENDING_EXCEPTION, "");
        if (ref_kind == JVM_REF_invokeStatic) {
          //do_dispatch = false;  // no need, since statics are never dispatched
          LinkResolver::resolve_static_call(result,
                        defc, name, type, KlassHandle(), false, false, THREAD);
        } else if (ref_kind == JVM_REF_invokeInterface) {
          LinkResolver::resolve_interface_call(result, Handle(), defc,
                        defc, name, type, KlassHandle(), false, false, THREAD);
        } else if (mh_invoke_id != vmIntrinsics::_none) {
          assert(!is_signature_polymorphic_static(mh_invoke_id), "");
          LinkResolver::resolve_handle_call(result,
                        defc, name, type, KlassHandle(), THREAD);
        } else if (ref_kind == JVM_REF_invokeSpecial) {
          do_dispatch = false;  // force non-virtual linkage
          LinkResolver::resolve_special_call(result,
                        defc, name, type, KlassHandle(), false, THREAD);
        } else if (ref_kind == JVM_REF_invokeVirtual) {
          LinkResolver::resolve_virtual_call(result, Handle(), defc,
                        defc, name, type, KlassHandle(), false, false, THREAD);
        } else {
          assert(false, err_msg("ref_kind=%d", ref_kind));
        }
        if (HAS_PENDING_EXCEPTION) {
          return empty;
        }
      }
      return init_method_MemberName(mname(), result, THREAD);
    }
  case IS_CONSTRUCTOR:
    {
      CallInfo result;
      {
        assert(!HAS_PENDING_EXCEPTION, "");
        if (name == vmSymbols::object_initializer_name()) {
          LinkResolver::resolve_special_call(result,
                        defc, name, type, KlassHandle(), false, THREAD);
        } else {
          break;                // will throw after end of switch
        }
        if (HAS_PENDING_EXCEPTION) {
          return empty;
        }
      }
      assert(result.is_statically_bound(), "");
      return init_method_MemberName(mname(), result, THREAD);
    }
  case IS_FIELD:
    {
      // This is taken from LinkResolver::resolve_field, sans access checks.
      fieldDescriptor fd; // find_field initializes fd if found
      KlassHandle sel_klass(THREAD, InstanceKlass::cast(defc())->find_field(name, type, &fd));
      // check if field exists; i.e., if a klass containing the field def has been selected
      if (sel_klass.is_null())  return empty;  // should not happen
      oop type = field_signature_type_or_null(fd.signature());
      oop name = field_name_or_null(fd.name());
      bool is_setter = (ref_kind_is_valid(ref_kind) && ref_kind_is_setter(ref_kind));
      mname = Handle(THREAD,
                     init_field_MemberName(mname(), sel_klass(),
                                           fd.access_flags(), type, name, fd.offset(), is_setter));
      return mname;
    }
  default:
    THROW_MSG_(vmSymbols::java_lang_InternalError(), "unrecognized MemberName format", empty);
  }

  return empty;
}

// Conversely, a member name which is only initialized from JVM internals
// may have null defc, name, and type fields.
// Resolving it plants a vmtarget/vmindex in it,
// which refers directly to JVM internals.
void MethodHandles::expand_MemberName(Handle mname, int suppress, TRAPS) {
  assert(java_lang_invoke_MemberName::is_instance(mname()), "");
  Metadata* vmtarget = java_lang_invoke_MemberName::vmtarget(mname());
  int vmindex  = java_lang_invoke_MemberName::vmindex(mname());
  if (vmtarget == NULL) {
    THROW_MSG(vmSymbols::java_lang_IllegalArgumentException(), "nothing to expand");
  }

  bool have_defc = (java_lang_invoke_MemberName::clazz(mname()) != NULL);
  bool have_name = (java_lang_invoke_MemberName::name(mname()) != NULL);
  bool have_type = (java_lang_invoke_MemberName::type(mname()) != NULL);
  int flags      = java_lang_invoke_MemberName::flags(mname());

  if (suppress != 0) {
    if (suppress & _suppress_defc)  have_defc = true;
    if (suppress & _suppress_name)  have_name = true;
    if (suppress & _suppress_type)  have_type = true;
  }

  if (have_defc && have_name && have_type)  return;  // nothing needed

  switch (flags & ALL_KINDS) {
  case IS_METHOD:
  case IS_CONSTRUCTOR:
    {
      assert(vmtarget->is_method(), "method or constructor vmtarget is Method*");
      methodHandle m(THREAD, (Method*)vmtarget);
      DEBUG_ONLY(vmtarget = NULL);  // safety
      if (m.is_null())  break;
      if (!have_defc) {
        Klass* defc = m->method_holder();
        java_lang_invoke_MemberName::set_clazz(mname(), Klass::cast(defc)->java_mirror());
      }
      if (!have_name) {
        //not java_lang_String::create_from_symbol; let's intern member names
        Handle name = StringTable::intern(m->name(), CHECK);
        java_lang_invoke_MemberName::set_name(mname(), name());
      }
      if (!have_type) {
        Handle type = java_lang_String::create_from_symbol(m->signature(), CHECK);
        java_lang_invoke_MemberName::set_type(mname(), type());
      }
      return;
    }
  case IS_FIELD:
    {
      // This is taken from LinkResolver::resolve_field, sans access checks.
      assert(vmtarget->is_klass(), "field vmtarget is Klass*");
      if (!Klass::cast((Klass*) vmtarget)->oop_is_instance())  break;
      instanceKlassHandle defc(THREAD, (Klass*) vmtarget);
      DEBUG_ONLY(vmtarget = NULL);  // safety
      bool is_static = ((flags & JVM_ACC_STATIC) != 0);
      fieldDescriptor fd; // find_field initializes fd if found
      if (!defc->find_field_from_offset(vmindex, is_static, &fd))
        break;                  // cannot expand
      if (!have_defc) {
        java_lang_invoke_MemberName::set_clazz(mname(), defc->java_mirror());
      }
      if (!have_name) {
        //not java_lang_String::create_from_symbol; let's intern member names
        Handle name = StringTable::intern(fd.name(), CHECK);
        java_lang_invoke_MemberName::set_name(mname(), name());
      }
      if (!have_type) {
        // If it is a primitive field type, don't mess with short strings like "I".
        Handle type = field_signature_type_or_null(fd.signature());
        if (type.is_null()) {
          java_lang_String::create_from_symbol(fd.signature(), CHECK);
        }
        java_lang_invoke_MemberName::set_type(mname(), type());
      }
      return;
    }
  }
  THROW_MSG(vmSymbols::java_lang_InternalError(), "unrecognized MemberName format");
}

int MethodHandles::find_MemberNames(Klass* k,
                                    Symbol* name, Symbol* sig,
                                    int mflags, Klass* caller,
                                    int skip, objArrayOop results) {
  DEBUG_ONLY(No_Safepoint_Verifier nsv);
  // this code contains no safepoints!

  // %%% take caller into account!

  if (k == NULL || !Klass::cast(k)->oop_is_instance())  return -1;

  int rfill = 0, rlimit = results->length(), rskip = skip;
  // overflow measurement:
  int overflow = 0, overflow_limit = MAX2(1000, rlimit);

  int match_flags = mflags;
  bool search_superc = ((match_flags & SEARCH_SUPERCLASSES) != 0);
  bool search_intfc  = ((match_flags & SEARCH_INTERFACES)   != 0);
  bool local_only = !(search_superc | search_intfc);
  bool classes_only = false;

  if (name != NULL) {
    if (name->utf8_length() == 0)  return 0; // a match is not possible
  }
  if (sig != NULL) {
    if (sig->utf8_length() == 0)  return 0; // a match is not possible
    if (sig->byte_at(0) == '(')
      match_flags &= ~(IS_FIELD | IS_TYPE);
    else
      match_flags &= ~(IS_CONSTRUCTOR | IS_METHOD);
  }

  if ((match_flags & IS_TYPE) != 0) {
    // NYI, and Core Reflection works quite well for this query
  }

  if ((match_flags & IS_FIELD) != 0) {
    for (FieldStream st(k, local_only, !search_intfc); !st.eos(); st.next()) {
      if (name != NULL && st.name() != name)
          continue;
      if (sig != NULL && st.signature() != sig)
        continue;
      // passed the filters
      if (rskip > 0) {
        --rskip;
      } else if (rfill < rlimit) {
        oop result = results->obj_at(rfill++);
        if (!java_lang_invoke_MemberName::is_instance(result))
          return -99;  // caller bug!
        oop type = field_signature_type_or_null(st.signature());
        oop name = field_name_or_null(st.name());
        oop saved = MethodHandles::init_field_MemberName(result, st.klass()(),
                                                         st.access_flags(), type, name,
                                                         st.offset());
        if (saved != result)
          results->obj_at_put(rfill-1, saved);  // show saved instance to user
      } else if (++overflow >= overflow_limit) {
        match_flags = 0; break; // got tired of looking at overflow
      }
    }
  }

  if ((match_flags & (IS_METHOD | IS_CONSTRUCTOR)) != 0) {
    // watch out for these guys:
    Symbol* init_name   = vmSymbols::object_initializer_name();
    Symbol* clinit_name = vmSymbols::class_initializer_name();
    if (name == clinit_name)  clinit_name = NULL; // hack for exposing <clinit>
    bool negate_name_test = false;
    // fix name so that it captures the intention of IS_CONSTRUCTOR
    if (!(match_flags & IS_METHOD)) {
      // constructors only
      if (name == NULL) {
        name = init_name;
      } else if (name != init_name) {
        return 0;               // no constructors of this method name
      }
    } else if (!(match_flags & IS_CONSTRUCTOR)) {
      // methods only
      if (name == NULL) {
        name = init_name;
        negate_name_test = true; // if we see the name, we *omit* the entry
      } else if (name == init_name) {
        return 0;               // no methods of this constructor name
      }
    } else {
      // caller will accept either sort; no need to adjust name
    }
    for (MethodStream st(k, local_only, !search_intfc); !st.eos(); st.next()) {
      Method* m = st.method();
      Symbol* m_name = m->name();
      if (m_name == clinit_name)
        continue;
      if (name != NULL && ((m_name != name) ^ negate_name_test))
          continue;
      if (sig != NULL && m->signature() != sig)
        continue;
      // passed the filters
      if (rskip > 0) {
        --rskip;
      } else if (rfill < rlimit) {
        oop result = results->obj_at(rfill++);
        if (!java_lang_invoke_MemberName::is_instance(result))
          return -99;  // caller bug!
        oop saved = MethodHandles::init_method_MemberName(result, m, true, NULL);
        if (saved != result)
          results->obj_at_put(rfill-1, saved);  // show saved instance to user
      } else if (++overflow >= overflow_limit) {
        match_flags = 0; break; // got tired of looking at overflow
      }
    }
  }

  // return number of elements we at leasted wanted to initialize
  return rfill + overflow;
}
//
// Here are the native methods in java.lang.invoke.MethodHandleNatives
// They are the private interface between this JVM and the HotSpot-specific
// Java code that implements JSR 292 method handles.
//
// Note:  We use a JVM_ENTRY macro to define each of these, for this is the way
// that intrinsic (non-JNI) native methods are defined in HotSpot.
//

JVM_ENTRY(jint, MHN_getConstant(JNIEnv *env, jobject igcls, jint which)) {
  switch (which) {
  case MethodHandles::GC_COUNT_GWT:
#ifdef COMPILER2
    return true;
#else
    return false;
#endif
  }
  return 0;
}
JVM_END

#ifndef PRODUCT
#define EACH_NAMED_CON(template, requirement) \
    template(MethodHandles,GC_COUNT_GWT) \
    template(java_lang_invoke_MemberName,MN_IS_METHOD) \
    template(java_lang_invoke_MemberName,MN_IS_CONSTRUCTOR) \
    template(java_lang_invoke_MemberName,MN_IS_FIELD) \
    template(java_lang_invoke_MemberName,MN_IS_TYPE) \
    template(java_lang_invoke_MemberName,MN_SEARCH_SUPERCLASSES) \
    template(java_lang_invoke_MemberName,MN_SEARCH_INTERFACES) \
    template(java_lang_invoke_MemberName,MN_REFERENCE_KIND_SHIFT) \
    template(java_lang_invoke_MemberName,MN_REFERENCE_KIND_MASK) \
    template(MethodHandles,GC_LAMBDA_SUPPORT) \
    /*end*/

#define IGNORE_REQ(req_expr) /* req_expr */
#define ONE_PLUS(scope,value) 1+
static const int con_value_count = EACH_NAMED_CON(ONE_PLUS, IGNORE_REQ) 0;
#define VALUE_COMMA(scope,value) scope::value,
static const int con_values[con_value_count+1] = { EACH_NAMED_CON(VALUE_COMMA, IGNORE_REQ) 0 };
#define STRING_NULL(scope,value) #value "\0"
static const char con_names[] = { EACH_NAMED_CON(STRING_NULL, IGNORE_REQ) };

static bool advertise_con_value(int which) {
  if (which < 0)  return false;
  bool ok = true;
  int count = 0;
#define INC_COUNT(scope,value) \
  ++count;
#define CHECK_REQ(req_expr) \
  if (which < count)  return ok; \
  ok = (req_expr);
  EACH_NAMED_CON(INC_COUNT, CHECK_REQ);
#undef INC_COUNT
#undef CHECK_REQ
  assert(count == con_value_count, "");
  if (which < count)  return ok;
  return false;
}

#undef ONE_PLUS
#undef VALUE_COMMA
#undef STRING_NULL
#undef EACH_NAMED_CON
#endif // PRODUCT

JVM_ENTRY(jint, MHN_getNamedCon(JNIEnv *env, jobject igcls, jint which, jobjectArray box_jh)) {
#ifndef PRODUCT
  if (advertise_con_value(which)) {
    assert(which >= 0 && which < con_value_count, "");
    int con = con_values[which];
    objArrayHandle box(THREAD, (objArrayOop) JNIHandles::resolve(box_jh));
    if (box.not_null() && box->klass() == Universe::objectArrayKlassObj() && box->length() > 0) {
      const char* str = &con_names[0];
      for (int i = 0; i < which; i++)
        str += strlen(str) + 1;   // skip name and null
      oop name = java_lang_String::create_oop_from_str(str, CHECK_0);  // possible safepoint
      box->obj_at_put(0, name);
    }
    return con;
  }
#endif
  return 0;
}
JVM_END

// void init(MemberName self, AccessibleObject ref)
JVM_ENTRY(void, MHN_init_Mem(JNIEnv *env, jobject igcls, jobject mname_jh, jobject target_jh)) {
  if (mname_jh == NULL) { THROW_MSG(vmSymbols::java_lang_InternalError(), "mname is null"); }
  if (target_jh == NULL) { THROW_MSG(vmSymbols::java_lang_InternalError(), "target is null"); }
  Handle mname(THREAD, JNIHandles::resolve_non_null(mname_jh));
  oop target_oop = JNIHandles::resolve_non_null(target_jh);
  MethodHandles::init_MemberName(mname(), target_oop);
}
JVM_END

// void expand(MemberName self)
JVM_ENTRY(void, MHN_expand_Mem(JNIEnv *env, jobject igcls, jobject mname_jh)) {
  if (mname_jh == NULL) { THROW_MSG(vmSymbols::java_lang_InternalError(), "mname is null"); }
  Handle mname(THREAD, JNIHandles::resolve_non_null(mname_jh));
  MethodHandles::expand_MemberName(mname, 0, CHECK);
}
JVM_END

// void resolve(MemberName self, Class<?> caller)
JVM_ENTRY(jobject, MHN_resolve_Mem(JNIEnv *env, jobject igcls, jobject mname_jh, jclass caller_jh)) {
  if (mname_jh == NULL) { THROW_MSG_NULL(vmSymbols::java_lang_InternalError(), "mname is null"); }
  Handle mname(THREAD, JNIHandles::resolve_non_null(mname_jh));

  // The trusted Java code that calls this method should already have performed
  // access checks on behalf of the given caller.  But, we can verify this.
  if (VerifyMethodHandles && caller_jh != NULL &&
      java_lang_invoke_MemberName::clazz(mname()) != NULL) {
    Klass* reference_klass = java_lang_Class::as_Klass(java_lang_invoke_MemberName::clazz(mname()));
    if (reference_klass != NULL) {
      // Emulate LinkResolver::check_klass_accessability.
      Klass* caller = java_lang_Class::as_Klass(JNIHandles::resolve_non_null(caller_jh));
      if (!Reflection::verify_class_access(caller,
                                           reference_klass,
                                           true)) {
        THROW_MSG_NULL(vmSymbols::java_lang_InternalError(), Klass::cast(reference_klass)->external_name());
      }
    }
  }

  Handle resolved = MethodHandles::resolve_MemberName(mname, CHECK_NULL);
  if (resolved.is_null()) {
    int flags = java_lang_invoke_MemberName::flags(mname());
    int ref_kind = (flags >> REFERENCE_KIND_SHIFT) & REFERENCE_KIND_MASK;
    if (!MethodHandles::ref_kind_is_valid(ref_kind)) {
      THROW_MSG_NULL(vmSymbols::java_lang_InternalError(), "obsolete MemberName format");
    }
    if ((flags & ALL_KINDS) == IS_FIELD) {
      THROW_MSG_NULL(vmSymbols::java_lang_NoSuchMethodError(), "field resolution failed");
    } else if ((flags & ALL_KINDS) == IS_METHOD ||
               (flags & ALL_KINDS) == IS_CONSTRUCTOR) {
      THROW_MSG_NULL(vmSymbols::java_lang_NoSuchFieldError(), "method resolution failed");
    } else {
      THROW_MSG_NULL(vmSymbols::java_lang_LinkageError(), "resolution failed");
    }
  }

  return JNIHandles::make_local(THREAD, resolved());
}
JVM_END

static jlong find_member_field_offset(oop mname, bool must_be_static, TRAPS) {
  if (mname == NULL ||
      java_lang_invoke_MemberName::vmtarget(mname) == NULL) {
    THROW_MSG_0(vmSymbols::java_lang_InternalError(), "mname not resolved");
  } else {
    int flags = java_lang_invoke_MemberName::flags(mname);
    if ((flags & IS_FIELD) != 0 &&
        (must_be_static
         ? (flags & JVM_ACC_STATIC) != 0
         : (flags & JVM_ACC_STATIC) == 0)) {
      int vmindex = java_lang_invoke_MemberName::vmindex(mname);
      return (jlong) vmindex;
    }
  }
  const char* msg = (must_be_static ? "static field required" : "non-static field required");
  THROW_MSG_0(vmSymbols::java_lang_InternalError(), msg);
  return 0;
}

JVM_ENTRY(jlong, MHN_objectFieldOffset(JNIEnv *env, jobject igcls, jobject mname_jh)) {
  return find_member_field_offset(JNIHandles::resolve(mname_jh), false, THREAD);
}
JVM_END

JVM_ENTRY(jlong, MHN_staticFieldOffset(JNIEnv *env, jobject igcls, jobject mname_jh)) {
  return find_member_field_offset(JNIHandles::resolve(mname_jh), true, THREAD);
}
JVM_END

JVM_ENTRY(jobject, MHN_staticFieldBase(JNIEnv *env, jobject igcls, jobject mname_jh)) {
  // use the other function to perform sanity checks:
  jlong ignore = find_member_field_offset(JNIHandles::resolve(mname_jh), true, CHECK_NULL);
  oop clazz = java_lang_invoke_MemberName::clazz(JNIHandles::resolve_non_null(mname_jh));
  return JNIHandles::make_local(THREAD, clazz);
}
JVM_END

JVM_ENTRY(jobject, MHN_getMemberVMInfo(JNIEnv *env, jobject igcls, jobject mname_jh)) {
  if (mname_jh == NULL)  return NULL;
  Handle mname(THREAD, JNIHandles::resolve_non_null(mname_jh));
  intptr_t vmindex  = java_lang_invoke_MemberName::vmindex(mname());
  Metadata* vmtarget = java_lang_invoke_MemberName::vmtarget(mname());
  objArrayHandle result = oopFactory::new_objArray(SystemDictionary::Object_klass(), 2, CHECK_NULL);
  jvalue vmindex_value; vmindex_value.j = (long)vmindex;
  oop x = java_lang_boxing_object::create(T_LONG, &vmindex_value, CHECK_NULL);
  result->obj_at_put(0, x);
  x = NULL;
  if (vmtarget == NULL) {
    x = NULL;
  } else if (vmtarget->is_klass()) {
    x = Klass::cast((Klass*) vmtarget)->java_mirror();
  } else if (vmtarget->is_method()) {
    Handle mname2 = MethodHandles::new_MemberName(CHECK_NULL);
    x = MethodHandles::init_method_MemberName(mname2(), (Method*)vmtarget, false, NULL);
  }
  result->obj_at_put(1, x);
  return JNIHandles::make_local(env, result());
}
JVM_END



//  static native int getMembers(Class<?> defc, String matchName, String matchSig,
//          int matchFlags, Class<?> caller, int skip, MemberName[] results);
JVM_ENTRY(jint, MHN_getMembers(JNIEnv *env, jobject igcls,
                               jclass clazz_jh, jstring name_jh, jstring sig_jh,
                               int mflags, jclass caller_jh, jint skip, jobjectArray results_jh)) {
  if (clazz_jh == NULL || results_jh == NULL)  return -1;
  KlassHandle k(THREAD, java_lang_Class::as_Klass(JNIHandles::resolve_non_null(clazz_jh)));

  objArrayHandle results(THREAD, (objArrayOop) JNIHandles::resolve(results_jh));
  if (results.is_null() || !results->is_objArray())  return -1;

  TempNewSymbol name = NULL;
  TempNewSymbol sig = NULL;
  if (name_jh != NULL) {
    name = java_lang_String::as_symbol_or_null(JNIHandles::resolve_non_null(name_jh));
    if (name == NULL)  return 0; // a match is not possible
  }
  if (sig_jh != NULL) {
    sig = java_lang_String::as_symbol_or_null(JNIHandles::resolve_non_null(sig_jh));
    if (sig == NULL)  return 0; // a match is not possible
  }

  KlassHandle caller;
  if (caller_jh != NULL) {
    oop caller_oop = JNIHandles::resolve_non_null(caller_jh);
    if (!java_lang_Class::is_instance(caller_oop))  return -1;
    caller = KlassHandle(THREAD, java_lang_Class::as_Klass(caller_oop));
  }

  if (name != NULL && sig != NULL && results.not_null()) {
    // try a direct resolve
    // %%% TO DO
  }

  int res = MethodHandles::find_MemberNames(k(), name, sig, mflags,
                                            caller(), skip, results());
  // TO DO: expand at least some of the MemberNames, to avoid massive callbacks
  return res;
}
JVM_END

JVM_ENTRY(void, MHN_setCallSiteTargetNormal(JNIEnv* env, jobject igcls, jobject call_site_jh, jobject target_jh)) {
  Handle call_site(THREAD, JNIHandles::resolve_non_null(call_site_jh));
  Handle target   (THREAD, JNIHandles::resolve(target_jh));
  {
    // Walk all nmethods depending on this call site.
    MutexLocker mu(Compile_lock, thread);
    Universe::flush_dependents_on(call_site, target);
  }
  java_lang_invoke_CallSite::set_target(call_site(), target());
}
JVM_END

JVM_ENTRY(void, MHN_setCallSiteTargetVolatile(JNIEnv* env, jobject igcls, jobject call_site_jh, jobject target_jh)) {
  Handle call_site(THREAD, JNIHandles::resolve_non_null(call_site_jh));
  Handle target   (THREAD, JNIHandles::resolve(target_jh));
  {
    // Walk all nmethods depending on this call site.
    MutexLocker mu(Compile_lock, thread);
    Universe::flush_dependents_on(call_site, target);
  }
  java_lang_invoke_CallSite::set_target_volatile(call_site(), target());
}
JVM_END

/// JVM_RegisterMethodHandleMethods

#undef CS  // Solaris builds complain

#define LANG "Ljava/lang/"
#define JLINV "Ljava/lang/invoke/"

#define OBJ   LANG"Object;"
#define CLS   LANG"Class;"
#define STRG  LANG"String;"
#define CS    JLINV"CallSite;"
#define MT    JLINV"MethodType;"
#define MH    JLINV"MethodHandle;"
#define MEM   JLINV"MemberName;"

#define CC (char*)  /*cast a literal from (const char*)*/
#define FN_PTR(f) CAST_FROM_FN_PTR(void*, &f)

// These are the native methods on java.lang.invoke.MethodHandleNatives.
static JNINativeMethod required_methods_JDK8[] = {
  {CC"init",                      CC"("MEM""OBJ")V",                     FN_PTR(MHN_init_Mem)},
  {CC"expand",                    CC"("MEM")V",                          FN_PTR(MHN_expand_Mem)},
  {CC"resolve",                   CC"("MEM""CLS")"MEM,                   FN_PTR(MHN_resolve_Mem)},
  {CC"getConstant",               CC"(I)I",                              FN_PTR(MHN_getConstant)},
  //  static native int getNamedCon(int which, Object[] name)
  {CC"getNamedCon",               CC"(I["OBJ")I",                        FN_PTR(MHN_getNamedCon)},
  //  static native int getMembers(Class<?> defc, String matchName, String matchSig,
  //          int matchFlags, Class<?> caller, int skip, MemberName[] results);
  {CC"getMembers",                CC"("CLS""STRG""STRG"I"CLS"I["MEM")I", FN_PTR(MHN_getMembers)},
  {CC"objectFieldOffset",         CC"("MEM")J",                          FN_PTR(MHN_objectFieldOffset)},
  {CC"setCallSiteTargetNormal",   CC"("CS""MH")V",                       FN_PTR(MHN_setCallSiteTargetNormal)},
  {CC"setCallSiteTargetVolatile", CC"("CS""MH")V",                       FN_PTR(MHN_setCallSiteTargetVolatile)},
  {CC"staticFieldOffset",         CC"("MEM")J",                          FN_PTR(MHN_staticFieldOffset)},
  {CC"staticFieldBase",           CC"("MEM")"OBJ,                        FN_PTR(MHN_staticFieldBase)},
  {CC"getMemberVMInfo",           CC"("MEM")"OBJ,                        FN_PTR(MHN_getMemberVMInfo)}
};

// This one function is exported, used by NativeLookup.

JVM_ENTRY(void, JVM_RegisterMethodHandleMethods(JNIEnv *env, jclass MHN_class)) {
  if (!EnableInvokeDynamic) {
    warning("JSR 292 is disabled in this JVM.  Use -XX:+UnlockDiagnosticVMOptions -XX:+EnableInvokeDynamic to enable.");
    return;  // bind nothing
  }

  assert(!MethodHandles::enabled(), "must not be enabled");
  bool enable_MH = true;

  jclass MH_class = NULL;
  if (SystemDictionary::MethodHandle_klass() == NULL) {
    enable_MH = false;
  } else {
    oop mirror = Klass::cast(SystemDictionary::MethodHandle_klass())->java_mirror();
    MH_class = (jclass) JNIHandles::make_local(env, mirror);
  }

  int status;

  if (enable_MH) {
    ThreadToNativeFromVM ttnfv(thread);

    status = env->RegisterNatives(MHN_class, required_methods_JDK8, sizeof(required_methods_JDK8)/sizeof(JNINativeMethod));
    if (status != JNI_OK || env->ExceptionOccurred()) {
      warning("JSR 292 method handle code is mismatched to this JVM.  Disabling support.");
      enable_MH = false;
      env->ExceptionClear();
    }
  }

  if (TraceInvokeDynamic) {
    tty->print_cr("MethodHandle support loaded (using LambdaForms)");
  }

  if (enable_MH) {
    MethodHandles::generate_adapters();
    MethodHandles::set_enabled(true);
  }
}
JVM_END
