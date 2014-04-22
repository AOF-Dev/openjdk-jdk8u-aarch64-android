/*
 * Copyright (c) 2013, Red Hat Inc.
 * Copyright (c) 2000, 2010, Oracle and/or its affiliates.
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

#ifndef CPU_AARCH64_VM_REGISTER_AARCH64_HPP
#define CPU_AARCH64_VM_REGISTER_AARCH64_HPP

#include "asm/register.hpp"
#include "vm_version_aarch64.hpp"

class VMRegImpl;
typedef VMRegImpl* VMReg;

// Use Register as shortcut
class RegisterImpl;
typedef RegisterImpl* Register;

inline Register as_Register(int encoding) {
  return (Register)(intptr_t) encoding;
}

class RegisterImpl: public AbstractRegisterImpl {
 public:
  enum {
    number_of_registers      = 32,
    number_of_byte_registers = 32
  };

  // derived registers, offsets, and addresses
  Register successor() const                          { return as_Register(encoding() + 1); }

  // construction
  inline friend Register as_Register(int encoding);

  VMReg as_VMReg();

  // accessors
  int   encoding() const                         { assert(is_valid(), "invalid register"); return (intptr_t)this; }
  bool  is_valid() const                         { return 0 <= (intptr_t)this && (intptr_t)this < number_of_registers; }
  bool  has_byte_register() const                { return 0 <= (intptr_t)this && (intptr_t)this < number_of_byte_registers; }
  const char* name() const;
  int   encoding_nocheck() const                 { return (intptr_t)this; }
  unsigned long bit(bool yes = true) const       { return yes << encoding(); }
};

// The integer registers of the aarch64 architecture

CONSTANT_REGISTER_DECLARATION(Register, noreg, (-1));


CONSTANT_REGISTER_DECLARATION(Register, r0,    (0));
CONSTANT_REGISTER_DECLARATION(Register, r1,    (1));
CONSTANT_REGISTER_DECLARATION(Register, r2,    (2));
CONSTANT_REGISTER_DECLARATION(Register, r3,    (3));
CONSTANT_REGISTER_DECLARATION(Register, r4,    (4));
CONSTANT_REGISTER_DECLARATION(Register, r5,    (5));
CONSTANT_REGISTER_DECLARATION(Register, r6,    (6));
CONSTANT_REGISTER_DECLARATION(Register, r7,    (7));
CONSTANT_REGISTER_DECLARATION(Register, r8,    (8));
CONSTANT_REGISTER_DECLARATION(Register, r9,    (9));
CONSTANT_REGISTER_DECLARATION(Register, r10,  (10));
CONSTANT_REGISTER_DECLARATION(Register, r11,  (11));
CONSTANT_REGISTER_DECLARATION(Register, r12,  (12));
CONSTANT_REGISTER_DECLARATION(Register, r13,  (13));
CONSTANT_REGISTER_DECLARATION(Register, r14,  (14));
CONSTANT_REGISTER_DECLARATION(Register, r15,  (15));
CONSTANT_REGISTER_DECLARATION(Register, r16,  (16));
CONSTANT_REGISTER_DECLARATION(Register, r17,  (17));
CONSTANT_REGISTER_DECLARATION(Register, r18,  (18));
CONSTANT_REGISTER_DECLARATION(Register, r19,  (19));
CONSTANT_REGISTER_DECLARATION(Register, r20,  (20));
CONSTANT_REGISTER_DECLARATION(Register, r21,  (21));
CONSTANT_REGISTER_DECLARATION(Register, r22,  (22));
CONSTANT_REGISTER_DECLARATION(Register, r23,  (23));
CONSTANT_REGISTER_DECLARATION(Register, r24,  (24));
CONSTANT_REGISTER_DECLARATION(Register, r25,  (25));
CONSTANT_REGISTER_DECLARATION(Register, r26,  (26));
CONSTANT_REGISTER_DECLARATION(Register, r27,  (27));
CONSTANT_REGISTER_DECLARATION(Register, r28,  (28));
CONSTANT_REGISTER_DECLARATION(Register, r29,  (29));
CONSTANT_REGISTER_DECLARATION(Register, r30,  (30));

CONSTANT_REGISTER_DECLARATION(Register, r31_sp, (31));
CONSTANT_REGISTER_DECLARATION(Register, zr,  (32));
CONSTANT_REGISTER_DECLARATION(Register, sp,  (33));

// Use FloatRegister as shortcut
class FloatRegisterImpl;
typedef FloatRegisterImpl* FloatRegister;

inline FloatRegister as_FloatRegister(int encoding) {
  return (FloatRegister)(intptr_t) encoding;
}

// The implementation of floating point registers for the architecture
class FloatRegisterImpl: public AbstractRegisterImpl {
 public:
  enum {
    number_of_registers = 32
  };

  // construction
  inline friend FloatRegister as_FloatRegister(int encoding);

  VMReg as_VMReg();

  // derived registers, offsets, and addresses
  FloatRegister successor() const                          { return as_FloatRegister(encoding() + 1); }

  // accessors
  int   encoding() const                          { assert(is_valid(), "invalid register"); return (intptr_t)this; }
  int   encoding_nocheck() const                         { return (intptr_t)this; }
  bool  is_valid() const                          { return 0 <= (intptr_t)this && (intptr_t)this < number_of_registers; }
  const char* name() const;

};

// The float registers of the AARCH64 architecture

CONSTANT_REGISTER_DECLARATION(FloatRegister, fnoreg , (-1));

CONSTANT_REGISTER_DECLARATION(FloatRegister, v0     , ( 0));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v1     , ( 1));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v2     , ( 2));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v3     , ( 3));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v4     , ( 4));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v5     , ( 5));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v6     , ( 6));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v7     , ( 7));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v8     , ( 8));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v9     , ( 9));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v10    , (10));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v11    , (11));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v12    , (12));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v13    , (13));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v14    , (14));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v15    , (15));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v16    , (16));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v17    , (17));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v18    , (18));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v19    , (19));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v20    , (20));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v21    , (21));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v22    , (22));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v23    , (23));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v24    , (24));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v25    , (25));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v26    , (26));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v27    , (27));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v28    , (28));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v29    , (29));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v30    , (30));
CONSTANT_REGISTER_DECLARATION(FloatRegister, v31    , (31));

// #ifndef DONT_USE_REGISTER_DEFINES
#if 0
#define fnoreg ((FloatRegister)(fnoreg_FloatRegisterEnumValue))
#define v0     ((FloatRegister)(    v0_FloatRegisterEnumValue))
#define v1     ((FloatRegister)(    v1_FloatRegisterEnumValue))
#define v2     ((FloatRegister)(    v2_FloatRegisterEnumValue))
#define v3     ((FloatRegister)(    v3_FloatRegisterEnumValue))
#define v4     ((FloatRegister)(    v4_FloatRegisterEnumValue))
#define v5     ((FloatRegister)(    v5_FloatRegisterEnumValue))
#define v6     ((FloatRegister)(    v6_FloatRegisterEnumValue))
#define v7     ((FloatRegister)(    v7_FloatRegisterEnumValue))
#define v8     ((FloatRegister)(    v8_FloatRegisterEnumValue))
#define v9     ((FloatRegister)(    v9_FloatRegisterEnumValue))
#define v10    ((FloatRegister)(   v10_FloatRegisterEnumValue))
#define v11    ((FloatRegister)(   v11_FloatRegisterEnumValue))
#define v12    ((FloatRegister)(   v12_FloatRegisterEnumValue))
#define v13    ((FloatRegister)(   v13_FloatRegisterEnumValue))
#define v14    ((FloatRegister)(   v14_FloatRegisterEnumValue))
#define v15    ((FloatRegister)(   v15_FloatRegisterEnumValue))
#define v16    ((FloatRegister)(   v16_FloatRegisterEnumValue))
#define v17    ((FloatRegister)(   v17_FloatRegisterEnumValue))
#define v18    ((FloatRegister)(   v18_FloatRegisterEnumValue))
#define v19    ((FloatRegister)(   v19_FloatRegisterEnumValue))
#define v20    ((FloatRegister)(   v20_FloatRegisterEnumValue))
#define v21    ((FloatRegister)(   v21_FloatRegisterEnumValue))
#define v22    ((FloatRegister)(   v22_FloatRegisterEnumValue))
#define v23    ((FloatRegister)(   v23_FloatRegisterEnumValue))
#define v24    ((FloatRegister)(   v24_FloatRegisterEnumValue))
#define v25    ((FloatRegister)(   v25_FloatRegisterEnumValue))
#define v26    ((FloatRegister)(   v26_FloatRegisterEnumValue))
#define v27    ((FloatRegister)(   v27_FloatRegisterEnumValue))
#define v28    ((FloatRegister)(   v28_FloatRegisterEnumValue))
#define v29    ((FloatRegister)(   v29_FloatRegisterEnumValue))
#define v30    ((FloatRegister)(   v30_FloatRegisterEnumValue))
#define v31    ((FloatRegister)(   v31_FloatRegisterEnumValue))
#endif // 0
//#endif // DONT_USE_REGISTER_DEFINES

// Need to know the total number of registers of all sorts for SharedInfo.
// Define a class that exports it.
class ConcreteRegisterImpl : public AbstractRegisterImpl {
 public:
  enum {
  // A big enough number for C2: all the registers plus flags
  // This number must be large enough to cover REG_COUNT (defined by c2) registers.
  // There is no requirement that any ordering here matches any ordering c2 gives
  // it's optoregs.

    number_of_registers = (2 * RegisterImpl::number_of_registers +
                           2 * FloatRegisterImpl::number_of_registers +
                           1) // flags
  };

  // added to make it compile
  static const int max_gpr;
  static const int max_fpr;
};

// A set of registers
class RegSet {
  uint32_t _bitset;

  RegSet(uint32_t bitset) : _bitset (bitset) { }

public:

  RegSet() : _bitset(0) { }

  RegSet operator+(Register r1) const {
    RegSet result(_bitset | r1->bit());
    return result;
  }

  RegSet operator+(RegSet aSet) const {
    RegSet result(_bitset | aSet._bitset);
    return result;
  }

  RegSet operator-(RegSet aSet) const {
    RegSet result(_bitset & ~aSet._bitset);
    return result;
  }

  static RegSet of(Register r1) {
    return RegSet(r1->bit());
  }

  static RegSet of(Register r1, Register r2) {
    return of(r1) + r2;
  }

  static RegSet of(Register r1, Register r2, Register r3) {
    return of(r1, r2) + r3;
  }

  static RegSet of(Register r1, Register r2, Register r3, Register r4) {
    return of(r1, r2, r3) + r4;
  }

  static RegSet range(Register start, Register end) {
    uint32_t bits = ~0;
    bits <<= start->encoding();
    bits <<= 31 - end->encoding();
    bits >>= 31 - end->encoding();

    return RegSet(bits);
  }

  uint32_t bits() const { return _bitset; }
};

#endif // CPU_AARCH64_VM_REGISTER_AARCH64_HPP
