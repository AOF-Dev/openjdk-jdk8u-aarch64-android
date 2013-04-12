/*
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
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
#include "c1/c1_Compilation.hpp"
#include "c1/c1_FrameMap.hpp"
#include "c1/c1_Instruction.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_LIRGenerator.hpp"
#include "c1/c1_Runtime1.hpp"
#include "c1/c1_ValueStack.hpp"
#include "ci/ciArray.hpp"
#include "ci/ciObjArrayKlass.hpp"
#include "ci/ciTypeArrayKlass.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "vmreg_aarch64.inline.hpp"

#ifdef ASSERT
#define __ gen()->lir(__FILE__, __LINE__)->
#else
#define __ gen()->lir()->
#endif

// Item will be loaded into a byte register; Intel only
void LIRItem::load_byte_item() {
  load_item();
  LIR_Opr res = result();

  if (!res->is_virtual() || !_gen->is_vreg_flag_set(res, LIRGenerator::byte_reg)) {
    // make sure that it is a byte register
    assert(!value()->type()->is_float() && !value()->type()->is_double(),
           "can't load floats in byte register");
    LIR_Opr reg = _gen->rlock_byte(T_BYTE);
    __ move(res, reg);

    _result = reg;
  }
}


void LIRItem::load_nonconstant() {
  LIR_Opr r = value()->operand();
  if (r->is_constant()) {
    _result = r;
  } else {
    load_item();
  }
}

//--------------------------------------------------------------
//               LIRGenerator
//--------------------------------------------------------------


LIR_Opr LIRGenerator::exceptionOopOpr() { return FrameMap::r0_oop_opr; }
LIR_Opr LIRGenerator::exceptionPcOpr()  { return FrameMap::r3_opr; }
LIR_Opr LIRGenerator::divInOpr()        { Unimplemented(); return LIR_OprFact::illegalOpr; }
LIR_Opr LIRGenerator::divOutOpr()       { Unimplemented(); return LIR_OprFact::illegalOpr; }
LIR_Opr LIRGenerator::remOutOpr()       { Unimplemented(); return LIR_OprFact::illegalOpr; }
LIR_Opr LIRGenerator::shiftCountOpr()   { Unimplemented(); return LIR_OprFact::illegalOpr; }
LIR_Opr LIRGenerator::syncTempOpr()     { Unimplemented(); return LIR_OprFact::illegalOpr; }
LIR_Opr LIRGenerator::getThreadTemp()   { return LIR_OprFact::illegalOpr; }


LIR_Opr LIRGenerator::result_register_for(ValueType* type, bool callee) {
  LIR_Opr opr;
  switch (type->tag()) {
    case intTag:     opr = FrameMap::r0_opr;          break;
    case objectTag:  opr = FrameMap::r0_oop_opr;      break;
    case longTag:    opr = FrameMap::long0_opr;        break;
    case floatTag:   opr = FrameMap::fpu0_float_opr;  break;
    case doubleTag:  opr = FrameMap::fpu0_double_opr;  break;

    case addressTag:
    default: ShouldNotReachHere(); return LIR_OprFact::illegalOpr;
  }

  assert(opr->type_field() == as_OprType(as_BasicType(type)), "type mismatch");
  return opr;
}


LIR_Opr LIRGenerator::rlock_byte(BasicType type) { Unimplemented(); return LIR_OprFact::illegalOpr; }


//--------- loading items into registers --------------------------------


// i486 instructions can inline constants
bool LIRGenerator::can_store_as_constant(Value v, BasicType type) const { return false; }


bool LIRGenerator::can_inline_as_constant(Value v) const {
  // FIXME: Just a guess
  if (v->type()->as_IntConstant() != NULL) {
    return Assembler::operand_valid_for_add_sub_immediate(v->type()->as_IntConstant()->value());
  } else if (v->type()->as_LongConstant() != NULL) {
    return v->type()->as_LongConstant()->value() == 0L;
  } else if (v->type()->as_ObjectConstant() != NULL) {
    return v->type()->as_ObjectConstant()->value()->is_null_object();
  } else {
    return false;
  }
}


bool LIRGenerator::can_inline_as_constant(LIR_Const* c) const { return false; }


LIR_Opr LIRGenerator::safepoint_poll_register() {
  return LIR_OprFact::illegalOpr;
}


LIR_Address* LIRGenerator::generate_address(LIR_Opr base, LIR_Opr index,
                                            int shift, int disp, BasicType type) {
  assert(base->is_register(), "must be");

  // accumulate fixed displacements
  if (index->is_constant()) {
    disp += index->as_constant_ptr()->as_jint() << shift;
    index = LIR_OprFact::illegalOpr;
  }

  if (index->is_register()) {
    // apply the shift and accumulate the displacement
    if (shift > 0) {
      LIR_Opr tmp = new_pointer_register();
      __ shift_left(index, shift, tmp);
      index = tmp;
    }
    if (disp != 0) {
      LIR_Opr tmp = new_pointer_register();
      if (Assembler::operand_valid_for_add_sub_immediate(disp)) {
        __ add(tmp, tmp, LIR_OprFact::intptrConst(disp));
	index = tmp;
      } else {
	__ move(tmp, LIR_OprFact::intptrConst(disp));
        __ add(tmp, index, tmp);
        index = tmp;
      }
      disp = 0;
    }
  } else if (disp != 0 && !Address::offset_ok_for_immed(disp, shift)) {
    // index is illegal so replace it with the displacement loaded into a register
    index = new_pointer_register();
    __ move(LIR_OprFact::intptrConst(disp), index);
    disp = 0;
  }

  // at this point we either have base + index or base + displacement
  if (disp == 0) {
    return new LIR_Address(base, index, type);
  } else {
    assert(Address::offset_ok_for_immed(disp, 0), "must be");
    return new LIR_Address(base, disp, type);
  }
}


LIR_Address* LIRGenerator::emit_array_address(LIR_Opr array_opr, LIR_Opr index_opr,
                                              BasicType type, bool needs_card_mark) {
  int offset_in_bytes = arrayOopDesc::base_offset_in_bytes(type);
  int elem_size = type2aelembytes(type);
  int shift = exact_log2(elem_size);

  LIR_Address* addr;
  if (index_opr->is_constant()) {
    addr = new LIR_Address(array_opr,
                           offset_in_bytes + index_opr->as_jint() * elem_size, type);
  } else {
// #ifdef _LP64
//     if (index_opr->type() == T_INT) {
//       LIR_Opr tmp = new_register(T_LONG);
//       __ convert(Bytecodes::_i2l, index_opr, tmp);
//       index_opr = tmp;
//     }
// #endif
    if (offset_in_bytes) {
      LIR_Opr tmp = new_pointer_register();
      __ add(array_opr, LIR_OprFact::intConst(offset_in_bytes), tmp);
      array_opr = tmp;
      offset_in_bytes = 0;
    }
    addr =  new LIR_Address(array_opr,
                            index_opr,
                            LIR_Address::scale(type),
                            offset_in_bytes, type);
  }
  if (needs_card_mark) {
    // This store will need a precise card mark, so go ahead and
    // compute the full adddres instead of computing once for the
    // store and again for the card mark.
    LIR_Opr tmp = new_pointer_register();
    __ leal(LIR_OprFact::address(addr), tmp);
    return new LIR_Address(tmp, type);
  } else {
    return addr;
  }
}

LIR_Opr LIRGenerator::load_immediate(int x, BasicType type) { Unimplemented(); return LIR_OprFact::illegalOpr; }

void LIRGenerator::increment_counter(address counter, BasicType type, int step) {
  LIR_Opr pointer = new_pointer_register();
  __ move(LIR_OprFact::intptrConst(counter), pointer);
  LIR_Address* addr = new LIR_Address(pointer, type);
  increment_counter(addr, step);
}


void LIRGenerator::increment_counter(LIR_Address* addr, int step) {
  LIR_Opr reg = new_register(T_INT);
  __ load(addr, reg);
  __ add(reg, reg, LIR_OprFact::intConst(step));
  __ store(reg, addr);
}

void LIRGenerator::cmp_mem_int(LIR_Condition condition, LIR_Opr base, int disp, int c, CodeEmitInfo* info) { Unimplemented(); }

void LIRGenerator::cmp_reg_mem(LIR_Condition condition, LIR_Opr reg, LIR_Opr base, int disp, BasicType type, CodeEmitInfo* info) {
  __ cmp_reg_mem(condition, reg, new LIR_Address(base, disp, type), info);
}


bool LIRGenerator::strength_reduce_multiply(LIR_Opr left, int c, LIR_Opr result, LIR_Opr tmp) {

  if (is_power_of_2(c - 1)) {
    __ shift_left(left, exact_log2(c - 1), tmp);
    __ add(tmp, left, result);
    return true;
  } else if (is_power_of_2(c + 1)) {
    __ shift_left(left, exact_log2(c + 1), tmp);
    __ sub(tmp, left, result);
    return true;
  } else {
    return false;
  }
}

void LIRGenerator::store_stack_parameter (LIR_Opr item, ByteSize offset_from_sp) { Unimplemented(); }

//----------------------------------------------------------------------
//             visitor functions
//----------------------------------------------------------------------


void LIRGenerator::do_StoreIndexed(StoreIndexed* x) {
  assert(x->is_pinned(),"");
  bool needs_range_check = true;
  bool use_length = x->length() != NULL;
  bool obj_store = x->elt_type() == T_ARRAY || x->elt_type() == T_OBJECT;
  bool needs_store_check = obj_store && (x->value()->as_Constant() == NULL ||
                                         !get_jobject_constant(x->value())->is_null_object() ||
                                         x->should_profile());

  LIRItem array(x->array(), this);
  LIRItem index(x->index(), this);
  LIRItem value(x->value(), this);
  LIRItem length(this);

  array.load_item();
  index.load_nonconstant();

  if (use_length) {
    needs_range_check = x->compute_needs_range_check();
    if (needs_range_check) {
      length.set_instruction(x->length());
      length.load_item();
    }
  }
  if (needs_store_check) {
    value.load_item();
  } else {
    value.load_for_store(x->elt_type());
  }

  set_no_result(x);

  // the CodeEmitInfo must be duplicated for each different
  // LIR-instruction because spilling can occur anywhere between two
  // instructions and so the debug information must be different
  CodeEmitInfo* range_check_info = state_for(x);
  CodeEmitInfo* null_check_info = NULL;
  if (x->needs_null_check()) {
    null_check_info = new CodeEmitInfo(range_check_info);
  }

  // emit array address setup early so it schedules better
  // FIXME?  No harm in this on aarch64, and it might help
  LIR_Address* array_addr = emit_array_address(array.result(), index.result(), x->elt_type(), obj_store);

  if (GenerateRangeChecks && needs_range_check) {
    if (use_length) {
      __ cmp(lir_cond_belowEqual, length.result(), index.result());
      __ branch(lir_cond_belowEqual, T_INT, new RangeCheckStub(range_check_info, index.result()));
    } else {
      array_range_check(array.result(), index.result(), null_check_info, range_check_info);
      // range_check also does the null check
      null_check_info = NULL;
    }
  }

  if (GenerateArrayStoreCheck && needs_store_check) {
    LIR_Opr tmp1 = new_register(objectType);
    LIR_Opr tmp2 = new_register(objectType);
    LIR_Opr tmp3 = new_register(objectType);

    CodeEmitInfo* store_check_info = new CodeEmitInfo(range_check_info);
    __ store_check(value.result(), array.result(), tmp1, tmp2, tmp3, store_check_info, x->profiled_method(), x->profiled_bci());
  }

  if (obj_store) {
    // Needs GC write barriers.
    pre_barrier(LIR_OprFact::address(array_addr), LIR_OprFact::illegalOpr /* pre_val */,
                true /* do_load */, false /* patch */, NULL);
    __ move(value.result(), array_addr, null_check_info);
    // Seems to be a precise
    post_barrier(LIR_OprFact::address(array_addr), value.result());
  } else {
    __ move(value.result(), array_addr, null_check_info);
  }
}

void LIRGenerator::do_MonitorEnter(MonitorEnter* x) { Unimplemented(); }

void LIRGenerator::do_MonitorExit(MonitorExit* x) { Unimplemented(); }

// _ineg, _lneg, _fneg, _dneg
void LIRGenerator::do_NegateOp(NegateOp* x) {
  
  LIRItem from(x->x(), this);
  from.load_item();
  LIR_Opr result = rlock_result(x);
  __ negate (from.result(), result);

}

// for  _fadd, _fmul, _fsub, _fdiv, _frem
//      _dadd, _dmul, _dsub, _ddiv, _drem
void LIRGenerator::do_ArithmeticOp_FPU(ArithmeticOp* x) {

  if (x->op() == Bytecodes::_frem || x->op() == Bytecodes::_drem) {
    // float remainder is implemented as a direct call into the runtime
    LIRItem right(x->x(), this);
    LIRItem left(x->y(), this);

    BasicTypeList signature(2);
    if (x->op() == Bytecodes::_frem) {
      signature.append(T_FLOAT);
      signature.append(T_FLOAT);
    } else {
      signature.append(T_DOUBLE);
      signature.append(T_DOUBLE);
    }
    CallingConvention* cc = frame_map()->c_calling_convention(&signature);

    const LIR_Opr result_reg = result_register_for(x->type());
    left.load_item_force(cc->at(1));
    right.load_item();

    __ move(right.result(), cc->at(0));

    address entry;
    if (x->op() == Bytecodes::_frem) {
      entry = CAST_FROM_FN_PTR(address, SharedRuntime::frem);
    } else {
      entry = CAST_FROM_FN_PTR(address, SharedRuntime::drem);
    }

    LIR_Opr result = rlock_result(x);
    __ call_runtime_leaf(entry, getThreadTemp(), result_reg, cc->args());
    __ move(result_reg, result);

    return;
  }

  LIRItem left(x->x(),  this);
  LIRItem right(x->y(), this);
  LIRItem* left_arg  = &left;
  LIRItem* right_arg = &right;

  // Always load right hand side.
  right.load_item();

  if (!left.is_register())
    left.load_item();

  LIR_Opr reg = rlock(x);
  LIR_Opr tmp = LIR_OprFact::illegalOpr;
  if (x->is_strictfp() && (x->op() == Bytecodes::_dmul || x->op() == Bytecodes::_ddiv)) {
    tmp = new_register(T_DOUBLE);
  }

  arithmetic_op_fpu(x->op(), reg, left.result(), right.result(), NULL);

  set_result(x, round_item(reg));
}

// for  _ladd, _lmul, _lsub, _ldiv, _lrem
void LIRGenerator::do_ArithmeticOp_Long(ArithmeticOp* x) {

  // missing test if instr is commutative and if we should swap
  LIRItem left(x->x(), this);
  LIRItem right(x->y(), this);

  if (x->op() == Bytecodes::_ldiv || x->op() == Bytecodes::_lrem) {

    // the check for division by zero destroys the right operand
    right.set_destroys_register();

    // check for division by zero (destroys registers of right operand!)
    CodeEmitInfo* info = state_for(x);

    left.load_item();
    right.load_item();

    __ cmp(lir_cond_equal, right.result(), LIR_OprFact::longConst(0));
    __ branch(lir_cond_equal, T_LONG, new DivByZeroStub(info));

    rlock_result(x);
    switch (x->op()) {
    case Bytecodes::_lrem:
      __ rem (left.result(), right.result(), x->operand());
      break;
    case Bytecodes::_ldiv:
      __ div (left.result(), right.result(), x->operand());
      break;
    default:
      ShouldNotReachHere();
      break;
    }


  } else {
    assert (x->op() == Bytecodes::_lmul || x->op() == Bytecodes::_ladd || x->op() == Bytecodes::_lsub,
	    "expect lmul, ladd or lsub");
    // add, sub, mul
    left.load_item();
    if (! right.is_register()) {
      if (x->op() == Bytecodes::_lmul
          || ! right.is_constant()
	  || ! Assembler::operand_valid_for_add_sub_immediate(right.get_jlong_constant())) {
	right.load_item();
      } else { // add, sub
	assert (x->op() == Bytecodes::_ladd || x->op() == Bytecodes::_lsub, "expect ladd or lsub");
	// don't load constants to save register
	right.load_nonconstant();
      }
    }
    rlock_result(x);
    arithmetic_op_long(x->op(), x->operand(), left.result(), right.result(), NULL);
  }
}

// for: _iadd, _imul, _isub, _idiv, _irem
void LIRGenerator::do_ArithmeticOp_Int(ArithmeticOp* x) {

  // Test if instr is commutative and if we should swap
  LIRItem left(x->x(),  this);
  LIRItem right(x->y(), this);
  LIRItem* left_arg = &left;
  LIRItem* right_arg = &right;
  if (x->is_commutative() && left.is_stack() && right.is_register()) {
    // swap them if left is real stack (or cached) and right is real register(not cached)
    left_arg = &right;
    right_arg = &left;
  }

  left_arg->load_item();

  // do not need to load right, as we can handle stack and constants
  if (x->op() == Bytecodes::_idiv || x->op() == Bytecodes::_irem) {

    right_arg->load_item();
    rlock_result(x);

    CodeEmitInfo* info = state_for(x);
    LIR_Opr tmp = new_register(T_INT);
    __ cmp(lir_cond_equal, right_arg->result(), LIR_OprFact::longConst(0));
    __ branch(lir_cond_equal, T_INT, new DivByZeroStub(info));
    info = state_for(x);

    if (x->op() == Bytecodes::_irem) {
      __ irem(left_arg->result(), right_arg->result(), x->operand(), tmp, info);
    } else if (x->op() == Bytecodes::_idiv) {
      __ idiv(left_arg->result(), right_arg->result(), x->operand(), tmp, info);
    }

  } else if (x->op() == Bytecodes::_iadd || x->op() == Bytecodes::_isub) {
    if (right.is_constant()
	&& Assembler::operand_valid_for_add_sub_immediate(right.get_jint_constant())) {
      right.load_nonconstant();
    } else {
      right.load_item();
    }
    rlock_result(x);
    arithmetic_op_int(x->op(), x->operand(), left_arg->result(), right_arg->result(), LIR_OprFact::illegalOpr);
  } else {
    assert (x->op() == Bytecodes::_imul, "expect imul");
    if (right.is_constant()) {
      int c = right.get_jint_constant();
      if (! is_power_of_2(c) && ! is_power_of_2(c + 1) && ! is_power_of_2(c - 1)) {
	// Cannot use constant op.
	right.load_item();
      } else {
	right.dont_load_item();
      }
    } else {
      right.load_item();
    }
    rlock_result(x);
    arithmetic_op_int(x->op(), x->operand(), left_arg->result(), right_arg->result(), new_register(T_INT));
  }
}

void LIRGenerator::do_ArithmeticOp(ArithmeticOp* x) {
  // when an operand with use count 1 is the left operand, then it is
  // likely that no move for 2-operand-LIR-form is necessary
  if (x->is_commutative() && x->y()->as_Constant() == NULL && x->x()->use_count() > x->y()->use_count()) {
    x->swap_operands();
  }

  ValueTag tag = x->type()->tag();
  assert(x->x()->type()->tag() == tag && x->y()->type()->tag() == tag, "wrong parameters");
  switch (tag) {
    case floatTag:
    case doubleTag:  do_ArithmeticOp_FPU(x);  return;
    case longTag:    do_ArithmeticOp_Long(x); return;
    case intTag:     do_ArithmeticOp_Int(x);  return;
  }
  ShouldNotReachHere();
}

// _ishl, _lshl, _ishr, _lshr, _iushr, _lushr
void LIRGenerator::do_ShiftOp(ShiftOp* x) {

  LIRItem left(x->x(),  this);
  LIRItem right(x->y(), this);

  left.load_item();

  rlock_result(x);
  if (right.is_constant()) {
    right.dont_load_item();
    
    switch (x->op()) {
    case Bytecodes::_ishl: {
      int c = right.get_jint_constant() & 0x1f;
      __ shift_left(left.result(), c, x->operand());
      break;
    }
    case Bytecodes::_ishr: {
      int c = right.get_jint_constant() & 0x1f;
      __ shift_right(left.result(), c, x->operand());
      break;
    }
    case Bytecodes::_iushr: {
      int c = right.get_jint_constant() & 0x1f;
      __ unsigned_shift_right(left.result(), c, x->operand());
      break;
    }
    case Bytecodes::_lshl: {
      int c = right.get_jint_constant() & 0x3f;
      __ shift_left(left.result(), c, x->operand());
      break;
    }
    case Bytecodes::_lshr: {
      int c = right.get_jint_constant() & 0x3f;
      __ shift_right(left.result(), c, x->operand());
      break;
    }
    case Bytecodes::_lushr: {
      int c = right.get_jint_constant() & 0x3f;
      __ unsigned_shift_right(left.result(), c, x->operand());
      break;
    }
    default:
      ShouldNotReachHere();
    }
  } else {
    right.load_item();
    LIR_Opr tmp = new_register(T_INT);
    switch (x->op()) {
    case Bytecodes::_ishl: {
      __ logical_and(right.result(), LIR_OprFact::intConst(0x1f), tmp);
      __ shift_left(left.result(), tmp, x->operand(), tmp);
      break;
    }
    case Bytecodes::_ishr: {
      __ logical_and(right.result(), LIR_OprFact::intConst(0x1f), tmp);
      __ shift_right(left.result(), tmp, x->operand(), tmp);
      break;
    }
    case Bytecodes::_iushr: {
      __ logical_and(right.result(), LIR_OprFact::intConst(0x1f), tmp);
      __ unsigned_shift_right(left.result(), tmp, x->operand(), tmp);
      break;
    }
    case Bytecodes::_lshl: {
      __ logical_and(right.result(), LIR_OprFact::intConst(0x3f), tmp);
      __ shift_left(left.result(), tmp, x->operand(), tmp);
      break;
    }
    case Bytecodes::_lshr: {
      __ logical_and(right.result(), LIR_OprFact::intConst(0x3f), tmp);
      __ shift_right(left.result(), tmp, x->operand(), tmp);
      break;
    }
    case Bytecodes::_lushr: {
      __ logical_and(right.result(), LIR_OprFact::intConst(0x3f), tmp);
      __ unsigned_shift_right(left.result(), tmp, x->operand(), tmp);
      break;
    }
    default:
      ShouldNotReachHere();
    }
  }
}

// _iand, _land, _ior, _lor, _ixor, _lxor
void LIRGenerator::do_LogicOp(LogicOp* x) {

  LIRItem left(x->x(),  this);
  LIRItem right(x->y(), this);

  left.load_item();

  rlock_result(x);
  if (right.is_constant()
      && ((right.type()->tag() == intTag
	   && Assembler::operand_valid_for_logical_immediate(true, right.get_jint_constant()))
	  || (right.type()->tag() == longTag
	      && Assembler::operand_valid_for_logical_immediate(false, right.get_jlong_constant()))))  {
    right.dont_load_item();
  } else {
    right.load_item();
  }
  switch (x->op()) {
  case Bytecodes::_iand:
  case Bytecodes::_land:
    __ logical_and(left.result(), right.result(), x->operand()); break;
  case Bytecodes::_ior:
  case Bytecodes::_lor:
    __ logical_or (left.result(), right.result(), x->operand()); break;
  case Bytecodes::_ixor:
  case Bytecodes::_lxor:
    __ logical_xor(left.result(), right.result(), x->operand()); break;
  default: Unimplemented();
  }
}

// _lcmp, _fcmpl, _fcmpg, _dcmpl, _dcmpg
void LIRGenerator::do_CompareOp(CompareOp* x) { Unimplemented(); }

void LIRGenerator::do_CompareAndSwap(Intrinsic* x, ValueType* type) { Unimplemented(); }

void LIRGenerator::do_MathIntrinsic(Intrinsic* x) { Unimplemented(); }

void LIRGenerator::do_ArrayCopy(Intrinsic* x) { Unimplemented(); }

// _i2l, _i2f, _i2d, _l2i, _l2f, _l2d, _f2i, _f2l, _f2d, _d2i, _d2l, _d2f
// _i2b, _i2c, _i2s
LIR_Opr fixed_register_for(BasicType type) { Unimplemented(); }

void LIRGenerator::do_Convert(Convert* x) {
  bool fixed_input, fixed_result, round_result, needs_stub;

  switch (x->op()) {
    case Bytecodes::_i2l: // fall through
    case Bytecodes::_l2i: // fall through
    case Bytecodes::_i2b: // fall through
    case Bytecodes::_i2c: // fall through
    case Bytecodes::_i2s: fixed_input = false;       fixed_result = false;       round_result = false;      needs_stub = false; break;

    case Bytecodes::_f2d: fixed_input = false;       fixed_result = false;       round_result = false;      needs_stub = false; break;
    case Bytecodes::_d2f: fixed_input = false;       fixed_result = UseSSE == 1; round_result = UseSSE < 1; needs_stub = false; break;
    case Bytecodes::_i2f: fixed_input = false;       fixed_result = false;       round_result = UseSSE < 1; needs_stub = false; break;
    case Bytecodes::_i2d: fixed_input = false;       fixed_result = false;       round_result = false;      needs_stub = false; break;
    case Bytecodes::_f2i: fixed_input = false;       fixed_result = false;       round_result = false;      needs_stub = true;  break;
    case Bytecodes::_d2i: fixed_input = false;       fixed_result = false;       round_result = false;      needs_stub = true;  break;
    case Bytecodes::_l2f: fixed_input = false;       fixed_result = false;       round_result = UseSSE < 1; needs_stub = false; break;
    case Bytecodes::_l2d: fixed_input = false;       fixed_result = false;       round_result = UseSSE < 2; needs_stub = false; break;
    case Bytecodes::_f2l: fixed_input = true;        fixed_result = true;        round_result = false;      needs_stub = false; break;
    case Bytecodes::_d2l: fixed_input = true;        fixed_result = true;        round_result = false;      needs_stub = false; break;
    default: ShouldNotReachHere();
  }

  LIRItem value(x->value(), this);
  value.load_item();
  LIR_Opr input = value.result();
  LIR_Opr result = rlock(x);

  // arguments of lir_convert
  LIR_Opr conv_input = input;
  LIR_Opr conv_result = result;
  ConversionStub* stub = NULL;

  if (fixed_input) {
    conv_input = fixed_register_for(input->type());
    __ move(input, conv_input);
  }

  assert(fixed_result == false || round_result == false, "cannot set both");
  if (fixed_result) {
    conv_result = fixed_register_for(result->type());
  } else if (round_result) {
    result = new_register(result->type());
    set_vreg_flag(result, must_start_in_memory);
  }

  if (needs_stub) {
    stub = new ConversionStub(x->op(), conv_input, conv_result);
  }

  __ convert(x->op(), conv_input, conv_result, stub, new_register(T_INT));

  if (result != conv_result) {
    __ move(conv_result, result);
  }

  assert(result->is_virtual(), "result must be virtual register");
  set_result(x, result);
}

void LIRGenerator::do_NewInstance(NewInstance* x) {
#ifndef PRODUCT
  if (PrintNotLoaded && !x->klass()->is_loaded()) {
    tty->print_cr("   ###class not loaded at new bci %d", x->printable_bci());
  }
#endif
  CodeEmitInfo* info = state_for(x, x->state());
  LIR_Opr reg = result_register_for(x->type());
  new_instance(reg, x->klass(),
                       FrameMap::r2_oop_opr,
                       FrameMap::r5_oop_opr,
                       FrameMap::r4_oop_opr,
                       LIR_OprFact::illegalOpr,
                       FrameMap::r3_metadata_opr, info);
  LIR_Opr result = rlock_result(x);
  __ move(reg, result);
}

void LIRGenerator::do_NewTypeArray(NewTypeArray* x) {
  CodeEmitInfo* info = state_for(x, x->state());

  LIRItem length(x->length(), this);
  length.load_item_force(FrameMap::r19_opr);

  LIR_Opr reg = result_register_for(x->type());
  LIR_Opr tmp1 = FrameMap::r2_oop_opr;
  LIR_Opr tmp2 = FrameMap::r4_oop_opr;
  LIR_Opr tmp3 = FrameMap::r5_oop_opr;
  LIR_Opr tmp4 = reg;
  LIR_Opr klass_reg = FrameMap::r3_metadata_opr;
  LIR_Opr len = length.result();
  BasicType elem_type = x->elt_type();

  __ metadata2reg(ciTypeArrayKlass::make(elem_type)->constant_encoding(), klass_reg);

  CodeStub* slow_path = new NewTypeArrayStub(klass_reg, len, reg, info);
  __ allocate_array(reg, len, tmp1, tmp2, tmp3, tmp4, elem_type, klass_reg, slow_path);

  LIR_Opr result = rlock_result(x);
  __ move(reg, result);
}

void LIRGenerator::do_NewObjectArray(NewObjectArray* x) { Unimplemented(); }

void LIRGenerator::do_NewMultiArray(NewMultiArray* x) { Unimplemented(); }

void LIRGenerator::do_BlockBegin(BlockBegin* x) {
  // nothing to do for now
}

void LIRGenerator::do_CheckCast(CheckCast* x) { Unimplemented(); }

void LIRGenerator::do_InstanceOf(InstanceOf* x) { Unimplemented(); }

void LIRGenerator::do_If(If* x) {
  assert(x->number_of_sux() == 2, "inconsistency");
  ValueTag tag = x->x()->type()->tag();
  bool is_safepoint = x->is_safepoint();

  If::Condition cond = x->cond();

  LIRItem xitem(x->x(), this);
  LIRItem yitem(x->y(), this);
  LIRItem* xin = &xitem;
  LIRItem* yin = &yitem;

  if (tag == longTag) {
    // for longs, only conditions "eql", "neq", "lss", "geq" are valid;
    // mirror for other conditions
    if (cond == If::gtr || cond == If::leq) {
      cond = Instruction::mirror(cond);
      xin = &yitem;
      yin = &xitem;
    }
    xin->set_destroys_register();
  }
  xin->load_item();
  if (tag == longTag && yin->is_constant()
      &&  Assembler::operand_valid_for_add_sub_immediate(yin->get_jlong_constant())) {
    yin->dont_load_item();
  } else if (tag == longTag || tag == floatTag || tag == doubleTag) {
    yin->load_item();
  } else {
    yin->dont_load_item();
  }

  // add safepoint before generating condition code so it can be recomputed
  if (x->is_safepoint()) {
    // increment backedge counter if needed
    increment_backedge_counter(state_for(x, x->state_before()), x->profiled_bci());
    __ safepoint(LIR_OprFact::illegalOpr, state_for(x, x->state_before()));
  }
  set_no_result(x);

  if (tag == intTag) {
    if (yin->is_constant()
	&& ! Assembler::operand_valid_for_add_sub_immediate(yin->get_jint_constant())) {
      yin->load_item();
    }
  }

  LIR_Opr left = xin->result();
  LIR_Opr right = yin->result();

  __ cmp(lir_cond(cond), left, right);
  // Generate branch profiling. Profiling code doesn't kill flags.
  profile_branch(x, cond);
  move_to_phi(x->state());
  if (x->x()->type()->is_float_kind()) {
    __ branch(lir_cond(cond), right->type(), x->tsux(), x->usux());
  } else {
    __ branch(lir_cond(cond), right->type(), x->tsux());
  }
  assert(x->default_sux() == x->fsux(), "wrong destination above");
  __ jump(x->default_sux());
}

LIR_Opr LIRGenerator::getThreadPointer() {
   return FrameMap::as_pointer_opr(rthread);
}

void LIRGenerator::trace_block_entry(BlockBegin* block) { Unimplemented(); }

void LIRGenerator::volatile_field_store(LIR_Opr value, LIR_Address* address,
                                        CodeEmitInfo* info) { Unimplemented(); }

void LIRGenerator::volatile_field_load(LIR_Address* address, LIR_Opr result,
                                       CodeEmitInfo* info) { Unimplemented(); }
void LIRGenerator::get_Object_unsafe(LIR_Opr dst, LIR_Opr src, LIR_Opr offset,
                                     BasicType type, bool is_volatile) { Unimplemented(); }

void LIRGenerator::put_Object_unsafe(LIR_Opr src, LIR_Opr offset, LIR_Opr data,
                                     BasicType type, bool is_volatile) { Unimplemented(); }

void LIRGenerator::do_UnsafeGetAndSetObject(UnsafeGetAndSetObject* x) { Unimplemented(); }
