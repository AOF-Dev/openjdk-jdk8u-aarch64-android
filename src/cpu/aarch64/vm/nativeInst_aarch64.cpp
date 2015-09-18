/*
 * Copyright (c) 2013, Red Hat Inc.
 * Copyright (c) 1997, 2010, Oracle and/or its affiliates.
 * All rights reserved.
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
#include "asm/macroAssembler.hpp"
#include "memory/resourceArea.hpp"
#include "nativeInst_aarch64.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/handles.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "utilities/ostream.hpp"
#ifdef COMPILER1
#include "c1/c1_Runtime1.hpp"
#endif

void NativeCall::verify() { ; }

address NativeCall::destination() const {
  return instruction_address() + displacement();
}

// Inserts a native call instruction at a given pc
void NativeCall::insert(address code_pos, address entry) { Unimplemented(); }

//-------------------------------------------------------------------

void NativeMovConstReg::verify() {
  // make sure code pattern is actually mov reg64, imm64 instructions
}


intptr_t NativeMovConstReg::data() const {
  // das(uint64_t(instruction_address()),2);
  address addr = MacroAssembler::pd_call_destination(instruction_address());
  if (maybe_cpool_ref(instruction_address())) {
    return *(intptr_t*)addr;
  } else {
    return (intptr_t)addr;
  }
}

void NativeMovConstReg::set_data(intptr_t x) {
  if (maybe_cpool_ref(instruction_address())) {
    address addr = MacroAssembler::pd_call_destination(instruction_address());
    *(intptr_t*)addr = x;
  } else {
    MacroAssembler::pd_patch_instruction(instruction_address(), (address)x);
    ICache::invalidate_range(instruction_address(), instruction_size);
  }
};

void NativeMovConstReg::print() {
  tty->print_cr(PTR_FORMAT ": mov reg, " INTPTR_FORMAT,
                p2i(instruction_address()), data());
}

//-------------------------------------------------------------------

address NativeMovRegMem::instruction_address() const      { return addr_at(instruction_offset); }

int NativeMovRegMem::offset() const  {
  address pc = instruction_address();
  unsigned insn = *(unsigned*)pc;
  if (Instruction_aarch64::extract(insn, 28, 24) == 0b10000) {
    address addr = MacroAssembler::pd_call_destination(pc);
    return *addr;
  } else {
    return (int)(intptr_t)MacroAssembler::pd_call_destination(instruction_address());
  }
}

void NativeMovRegMem::set_offset(int x) {
  address pc = instruction_address();
  unsigned insn = *(unsigned*)pc;
  if (maybe_cpool_ref(pc)) {
    address addr = MacroAssembler::pd_call_destination(pc);
    *(long*)addr = x;
  } else {
    MacroAssembler::pd_patch_instruction(pc, (address)intptr_t(x));
    ICache::invalidate_range(instruction_address(), instruction_size);
  }
}

void NativeMovRegMem::verify() {
#ifdef ASSERT
  address dest = MacroAssembler::pd_call_destination(instruction_address());
#endif
}

//--------------------------------------------------------------------------------

void NativeJump::verify() { ; }


void NativeJump::check_verified_entry_alignment(address entry, address verified_entry) {
}


address NativeJump::jump_destination() const          {
  address dest = MacroAssembler::pd_call_destination(instruction_address());

  // We use jump to self as the unresolved address which the inline
  // cache code (and relocs) know about

  // return -1 if jump to self
  dest = (dest == (address) this) ? (address) -1 : dest;
  return dest;
}

void NativeJump::set_jump_destination(address dest) {
  // We use jump to self as the unresolved address which the inline
  // cache code (and relocs) know about
  if (dest == (address) -1)
    dest = instruction_address();

  MacroAssembler::pd_patch_instruction(instruction_address(), dest);
  ICache::invalidate_range(instruction_address(), instruction_size);
};

//-------------------------------------------------------------------

bool NativeInstruction::is_safepoint_poll() {
  // a safepoint_poll is implemented in two steps as either
  //
  // adrp(reg, polling_page);
  // ldr(zr, [reg, #offset]);
  //
  // or
  //
  // mov(reg, polling_page);
  // ldr(zr, [reg, #offset]);
  //
  // however, we cannot rely on the polling page address load always
  // directly preceding the read from the page. C1 does that but C2
  // has to do the load and read as two independent instruction
  // generation steps. that's because with a single macro sequence the
  // generic C2 code can only add the oop map before the mov/adrp and
  // the trap handler expects an oop map to be associated with the
  // load. with the load scheuled as a prior step the oop map goes
  // where it is needed.
  //
  // so all we can do here is check that marked instruction is a load
  // word to zr
  return is_ldrw_to_zr(address(this));
}

bool NativeInstruction::is_adrp_at(address instr) {
  unsigned insn = *(unsigned*)instr;
  return (Instruction_aarch64::extract(insn, 31, 24) & 0b10011111) == 0b10010000;
}

bool NativeInstruction::is_ldr_literal_at(address instr) {
  unsigned insn = *(unsigned*)instr;
  return (Instruction_aarch64::extract(insn, 29, 24) & 0b011011) == 0b00011000;
}

bool NativeInstruction::is_ldrw_to_zr(address instr) {
  unsigned insn = *(unsigned*)instr;
  return (Instruction_aarch64::extract(insn, 31, 22) == 0b1011100101 &&
          Instruction_aarch64::extract(insn, 4, 0) == 0b11111);
}

bool NativeInstruction::is_movz() {
  return Instruction_aarch64::extract(int_at(0), 30, 23) == 0b10100101;
}

bool NativeInstruction::is_movk() {
  return Instruction_aarch64::extract(int_at(0), 30, 23) == 0b11100101;
}

//-------------------------------------------------------------------

// MT safe inserting of a jump over a jump or a nop (used by nmethod::makeZombie)

void NativeJump::patch_verified_entry(address entry, address verified_entry, address dest) {
  ptrdiff_t disp = dest - verified_entry;
  guarantee(disp < 1 << 27 && disp > - (1 << 27), "branch overflow");

  unsigned int insn = (0b000101 << 26) | ((disp >> 2) & 0x3ffffff);

  assert(nativeInstruction_at(verified_entry)->is_jump_or_nop(),
	 "Aarch64 cannot replace non-jump with jump");
  *(unsigned int*)verified_entry = insn;
  ICache::invalidate_range(verified_entry, instruction_size);
}

void NativeGeneralJump::verify() {  }

void NativeGeneralJump::insert_unconditional(address code_pos, address entry) {
  NativeGeneralJump* n_jump = (NativeGeneralJump*)code_pos;
  ptrdiff_t disp = entry - code_pos;
  guarantee(disp < 1 << 27 && disp > - (1 << 27), "branch overflow");

  unsigned int insn = (0b000101 << 26) | ((disp >> 2) & 0x3ffffff);
  *(unsigned int*)code_pos = insn;
  ICache::invalidate_range(code_pos, instruction_size);
}

// MT-safe patching of a long jump instruction.
void NativeGeneralJump::replace_mt_safe(address instr_addr, address code_buffer) {
  NativeGeneralJump* n_jump = (NativeGeneralJump*)instr_addr;
  assert(n_jump->is_jump_or_nop(),
	 "Aarch64 cannot replace non-jump with jump");
  uint32_t instr = *(uint32_t*)code_buffer;
  *(uint32_t*)instr_addr = instr;
  ICache::invalidate_range(instr_addr, instruction_size);
}

bool NativeInstruction::is_dtrace_trap() { return false; }

