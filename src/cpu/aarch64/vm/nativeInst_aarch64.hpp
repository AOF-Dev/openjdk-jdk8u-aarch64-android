/*
 * Copyright (c) 1997, 2011, Oracle and/or its affiliates. All rights reserved.
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

#ifndef CPU_AARCH64_VM_NATIVEINST_AARCH64_HPP
#define CPU_AARCH64_VM_NATIVEINST_AARCH64_HPP

#include "asm/assembler.hpp"
#include "memory/allocation.hpp"
#include "runtime/icache.hpp"
#include "runtime/os.hpp"
#include "utilities/top.hpp"

// We have interfaces for the following instructions:
// - NativeInstruction
// - - NativeCall
// - - NativeMovConstReg
// - - NativeMovConstRegPatching
// - - NativeMovRegMem
// - - NativeMovRegMemPatching
// - - NativeJump
// - - NativeIllegalOpCode
// - - NativeGeneralJump
// - - NativeReturn
// - - NativeReturnX (return with argument)
// - - NativePushConst
// - - NativeTstRegMem

// The base class for different kinds of native instruction abstractions.
// Provides the primitive operations to manipulate code relative to this.

class NativeInstruction VALUE_OBJ_CLASS_SPEC {
  friend class Relocation;
 public:
  enum { instruction_size = BytesPerWord };
  bool is_nop();
  bool is_dtrace_trap();
  inline bool is_call();
  inline bool is_illegal();
  inline bool is_return();
  inline bool is_jump();
  inline bool is_cond_jump();
  inline bool is_safepoint_poll();
  inline bool is_mov_literal64();

 protected:
  address addr_at(int offset) const    { return address(this) + offset; }

  s_char sbyte_at(int offset) const    { return *(s_char*) addr_at(offset); }
  u_char ubyte_at(int offset) const    { return *(u_char*) addr_at(offset); }

  jint int_at(int offset) const         { return *(jint*) addr_at(offset); }

  intptr_t ptr_at(int offset) const    { return *(intptr_t*) addr_at(offset); }

  oop  oop_at (int offset) const       { return *(oop*) addr_at(offset); }


  void set_char_at(int offset, char c)        { *addr_at(offset) = (u_char)c; wrote(offset); }
  void set_int_at(int offset, jint  i)        { *(jint*)addr_at(offset) = i;  wrote(offset); }
  void set_ptr_at (int offset, intptr_t  ptr) { *(intptr_t*) addr_at(offset) = ptr;  wrote(offset); }
  void set_oop_at (int offset, oop  o)        { *(oop*) addr_at(offset) = o;  wrote(offset); }

  // This doesn't really do anything on Intel, but it is the place where
  // cache invalidation belongs, generically:
  void wrote(int offset);

 public:

  // unit test stuff
  static void test() {}                 // override for testing

  inline friend NativeInstruction* nativeInstruction_at(address address);
};

inline NativeInstruction* nativeInstruction_at(address address) {
  NativeInstruction* inst = (NativeInstruction*)address;
#ifdef ASSERT
  //inst->verify();
#endif
  return inst;
}

inline NativeCall* nativeCall_at(address address);
// The NativeCall is an abstraction for accessing/manipulating native call imm32/rel32off
// instructions (used to manipulate inline caches, primitive & dll calls, etc.).

class NativeCall: public NativeInstruction {
 public:
  enum Aarch64_specific_constants {
    instruction_size            =    4,
    instruction_offset          =    0,
    displacement_offset         =    0,
    return_address_offset       =    4
  };

  enum { cache_line_size = BytesPerWord };  // conservative estimate!
  address instruction_address() const       { return addr_at(instruction_offset); }
  address next_instruction_address() const  { return addr_at(return_address_offset); }
  int   displacement() const                { return (int_at(displacement_offset) << 7) >> 5; }
  address displacement_address() const      { return addr_at(displacement_offset); }
  address return_address() const            { return addr_at(return_address_offset); }
  address destination() const;
  void  set_destination(address dest)       {
    int offset = dest - instruction_address();
    unsigned int insn = 0b100101 << 26;
    assert((offset & 3) == 0, "should be");
    offset >>= 2;
    offset &= (1 << 26) - 1; // mask off insn part
    insn |= offset;
    set_int_at(displacement_offset, insn);

  }

  // Similar to replace_mt_safe, but just changes the destination.  The
  // important thing is that free-running threads are able to execute
  // this call instruction at all times.  If the call is an immediate BL
  // instruction we can simply rely on atomicity of 32-bit writes to
  // make sure other threads will see no intermediate states.

  // We cannot rely on locks here, since the free-running threads must run at
  // full speed.
  //
  // Used in the runtime linkage of calls; see class CompiledIC.
  // (Cf. 4506997 and 4479829, where threads witnessed garbage displacements.)
  void  set_destination_mt_safe(address dest) { set_destination(dest); }

  void  verify_alignment()                       { ; }
  void  verify();
  void  print();

  // Creation
  inline friend NativeCall* nativeCall_at(address address);
  inline friend NativeCall* nativeCall_before(address return_address);

  static bool is_call_at(address instr) {
    const uint32_t insn = (*(uint32_t*)instr);
    return (insn & 0b1111110000000000000000000000000u) == 0b10010100000000000000000000000000u;
  }

  static bool is_call_before(address return_address) {
    return is_call_at(return_address - NativeCall::return_address_offset);
  }

  static bool is_call_to(address instr, address target) {
    return nativeInstruction_at(instr)->is_call() &&
      nativeCall_at(instr)->destination() == target;
  }

  // MT-safe patching of a call instruction.
  static void insert(address code_pos, address entry);

  static void replace_mt_safe(address instr_addr, address code_buffer);
};

inline NativeCall* nativeCall_at(address address) {
  NativeCall* call = (NativeCall*)(address - NativeCall::instruction_offset);
#ifdef ASSERT
  call->verify();
#endif
  return call;
}

inline NativeCall* nativeCall_before(address return_address) {
  NativeCall* call = (NativeCall*)(return_address - NativeCall::return_address_offset);
#ifdef ASSERT
  call->verify();
#endif
  return call;
}

// An interface for accessing/manipulating native mov reg, imm32 instructions.
// (used to manipulate inlined 32bit data dll calls, etc.)
class NativeMovConstReg: public NativeInstruction {
 public:
  address instruction_address() const { Unimplemented(); return 0; }
  address next_instruction_address() const { Unimplemented(); return 0; }
  intptr_t data() const { Unimplemented(); return 0; }
  void  set_data(intptr_t x) { Unimplemented(); };

  void  verify();
  void  print();

  // unit test stuff
  static void test() {}

  // Creation
  inline friend NativeMovConstReg* nativeMovConstReg_at(address address);
  inline friend NativeMovConstReg* nativeMovConstReg_before(address address);
};
inline NativeMovConstReg* nativeMovConstReg_at(address address) { Unimplemented(); return 0; }

class NativeMovConstRegPatching: public NativeMovConstReg {
 private:
  friend NativeMovConstRegPatching* nativeMovConstRegPatching_at(address address) { Unimplemented(); return 0; }
};

// An interface for accessing/manipulating native moves of the form:
//      mov[b/w/l/q] [reg + offset], reg   (instruction_code_reg2mem)
//      mov[b/w/l/q] reg, [reg+offset]     (instruction_code_mem2reg
//      mov[s/z]x[w/b/q] [reg + offset], reg
//      fld_s  [reg+offset]
//      fld_d  [reg+offset]
//      fstp_s [reg + offset]
//      fstp_d [reg + offset]
//      mov_literal64  scratch,<pointer> ; mov[b/w/l/q] 0(scratch),reg | mov[b/w/l/q] reg,0(scratch)
//
// Warning: These routines must be able to handle any instruction sequences
// that are generated as a result of the load/store byte,word,long
// macros.  For example: The load_unsigned_byte instruction generates
// an xor reg,reg inst prior to generating the movb instruction.  This
// class must skip the xor instruction.

class NativeMovRegMem: public NativeInstruction {
 public:
  // helper
  int instruction_start() const;

  address instruction_address() const;

  address next_instruction_address() const;

  int   offset() const;

  void  set_offset(int x);

  void  add_offset_in_bytes(int add_offset) { Unimplemented(); }

  void verify();
  void print ();

  // unit test stuff
  static void test() {}

 private:
  inline friend NativeMovRegMem* nativeMovRegMem_at (address address);
};

inline NativeMovRegMem* nativeMovRegMem_at (address address) { Unimplemented(); return 0; }

class NativeMovRegMemPatching: public NativeMovRegMem {
 private:
  friend NativeMovRegMemPatching* nativeMovRegMemPatching_at (address address) {Unimplemented(); return 0;  }
};

// An interface for accessing/manipulating native leal instruction of form:
//        leal reg, [reg + offset]

class NativeLoadAddress: public NativeMovRegMem {
  static const bool has_rex = true;
  static const int rex_size = 1;
 public:

  void verify();
  void print ();

  // unit test stuff
  static void test() {}

 private:
  friend NativeLoadAddress* nativeLoadAddress_at (address address) { Unimplemented(); return 0; }
};

// jump rel32off

class NativeJump: public NativeInstruction {
 public:

  address instruction_address() const { Unimplemented(); return 0; }
  address next_instruction_address() const { Unimplemented(); return 0; }
  address jump_destination() const { Unimplemented(); return 0; }

  void  set_jump_destination(address dest) { Unimplemented(); }

  // Creation
  inline friend NativeJump* nativeJump_at(address address);

  void verify();

  // Unit testing stuff
  static void test() {}

  // Insertion of native jump instruction
  static void insert(address code_pos, address entry);
  // MT-safe insertion of native jump at verified method entry
  static void check_verified_entry_alignment(address entry, address verified_entry);
  static void patch_verified_entry(address entry, address verified_entry, address dest);
};

inline NativeJump* nativeJump_at(address address) { Unimplemented(); return 0; };

// Handles all kinds of jump on Intel. Long/far, conditional/unconditional
class NativeGeneralJump: public NativeInstruction {
 public:
  address instruction_address() const { Unimplemented(); return 0; }
  address jump_destination()    const;

  // Creation
  inline friend NativeGeneralJump* nativeGeneralJump_at(address address);

  // Insertion of native general jump instruction
  static void insert_unconditional(address code_pos, address entry);
  static void replace_mt_safe(address instr_addr, address code_buffer);

  void verify();
};

inline NativeGeneralJump* nativeGeneralJump_at(address address) { Unimplemented(); return 0; }

class NativePopReg : public NativeInstruction {
 public:
  // Insert a pop instruction
  static void insert(address code_pos, Register reg);
};


class NativeIllegalInstruction: public NativeInstruction {
 public:
  // Insert illegal opcode as specific address
  static void insert(address code_pos);
};

// return instruction that does not pop values of the stack
class NativeReturn: public NativeInstruction {
 public:
};

// return instruction that does pop values of the stack
class NativeReturnX: public NativeInstruction {
 public:
};

// Simple test vs memory
class NativeTstRegMem: public NativeInstruction {
 public:
};

inline bool NativeInstruction::is_illegal()      { Unimplemented(); return false; }
inline bool NativeInstruction::is_call()         { Unimplemented(); return false; }
inline bool NativeInstruction::is_return()       { Unimplemented(); return false; }
inline bool NativeInstruction::is_jump()         { Unimplemented(); return false; }
inline bool NativeInstruction::is_cond_jump()    { Unimplemented(); return false; }
inline bool NativeInstruction::is_safepoint_poll() { Unimplemented(); return false; }

inline bool NativeInstruction::is_mov_literal64() { Unimplemented(); return false; }

#endif // CPU_AARCH64_VM_NATIVEINST_AARCH64_HPP
