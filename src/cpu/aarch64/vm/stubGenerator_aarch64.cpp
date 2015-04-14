/*
 * Copyright (c) 2013, Red Hat Inc.
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates.
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
#include "asm/macroAssembler.inline.hpp"
#include "interpreter/interpreter.hpp"
#include "nativeInst_aarch64.hpp"
#include "oops/instanceOop.hpp"
#include "oops/method.hpp"
#include "oops/objArrayKlass.hpp"
#include "oops/oop.inline.hpp"
#include "prims/methodHandles.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubCodeGenerator.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/thread.inline.hpp"
#include "utilities/top.hpp"
#ifdef COMPILER2
#include "opto/runtime.hpp"
#endif

#ifdef BUILTIN_SIM
#include "../../../../../../simulator/simulator.hpp"
#endif

// Declaration and definition of StubGenerator (no .hpp file).
// For a more detailed description of the stub routine structure
// see the comment in stubRoutines.hpp

#undef __
#define __ _masm->
#define TIMES_OOP Address::sxtw(exact_log2(UseCompressedOops ? 4 : 8))

#ifdef PRODUCT
#define BLOCK_COMMENT(str) /* nothing */
#else
#define BLOCK_COMMENT(str) __ block_comment(str)
#endif

#define BIND(label) bind(label); BLOCK_COMMENT(#label ":")

// Stub Code definitions

class StubGenerator: public StubCodeGenerator {
 private:

#ifdef PRODUCT
#define inc_counter_np(counter) ((void)0)
#else
  void inc_counter_np_(int& counter) {
    __ lea(rscratch2, ExternalAddress((address)&counter));
    __ ldrw(rscratch1, Address(rscratch2));
    __ addw(rscratch1, rscratch1, 1);
    __ strw(rscratch1, Address(rscratch2));
  }
#define inc_counter_np(counter) \
  BLOCK_COMMENT("inc_counter " #counter); \
  inc_counter_np_(counter);
#endif

  // Call stubs are used to call Java from C
  //
  // Arguments:
  //    c_rarg0:   call wrapper address                   address
  //    c_rarg1:   result                                 address
  //    c_rarg2:   result type                            BasicType
  //    c_rarg3:   method                                 Method*
  //    c_rarg4:   (interpreter) entry point              address
  //    c_rarg5:   parameters                             intptr_t*
  //    c_rarg6:   parameter size (in words)              int
  //    c_rarg7:   thread                                 Thread*
  //
  // There is no return from the stub itself as any Java result
  // is written to result
  //
  // we save r30 (lr) as the return PC at the base of the frame and
  // link r29 (fp) below it as the frame pointer installing sp (r31)
  // into fp.
  //
  // we save r0-r7, which accounts for all the c arguments.
  //
  // TODO: strictly do we need to save them all? they are treated as
  // volatile by C so could we omit saving the ones we are going to
  // place in global registers (thread? method?) or those we only use
  // during setup of the Java call?
  //
  // we don't need to save r8 which C uses as an indirect result location
  // return register.
  //
  // we don't need to save r9-r15 which both C and Java treat as
  // volatile
  //
  // we don't need to save r16-18 because Java does not use them
  //
  // we save r19-r28 which Java uses as scratch registers and C
  // expects to be callee-save
  //
  // we don't save any FP registers since only v8-v15 are callee-save
  // (strictly only the f and d components) and Java uses them as
  // callee-save. v0-v7 are arg registers and C treats v16-v31 as
  // volatile (as does Java?)
  //
  // so the stub frame looks like this when we enter Java code
  //
  //     [ return_from_Java     ] <--- sp
  //     [ argument word n      ]
  //      ...
  // -27 [ argument word 1      ]
  // -26 [ saved d15            ] <--- sp_after_call
  // -25 [ saved d14            ]
  // -24 [ saved d13            ]
  // -23 [ saved d12            ]
  // -22 [ saved d11            ]
  // -21 [ saved d10            ]
  // -20 [ saved d9             ]
  // -19 [ saved d8             ]
  // -18 [ saved r28            ]
  // -17 [ saved r27            ]
  // -16 [ saved r26            ]
  // -15 [ saved r25            ]
  // -14 [ saved r24            ]
  // -13 [ saved r23            ]
  // -12 [ saved r22            ]
  // -11 [ saved r21            ]
  // -10 [ saved r20            ]
  //  -9 [ saved r19            ]
  //  -8 [ call wrapper    (r0) ]
  //  -7 [ result          (r1) ]
  //  -6 [ result type     (r2) ]
  //  -5 [ method          (r3) ]
  //  -4 [ entry point     (r4) ]
  //  -3 [ parameters      (r5) ]
  //  -2 [ parameter size  (r6) ]
  //  -1 [ thread (r7)          ]
  //   0 [ saved fp       (r29) ] <--- fp == saved sp (r31)
  //   1 [ saved lr       (r30) ]

  // Call stub stack layout word offsets from fp
  enum call_stub_layout {
    sp_after_call_off = -26,

    d15_off            = -26,
    d14_off            = -25,
    d13_off            = -24,
    d12_off            = -23,
    d11_off            = -22,
    d10_off            = -21,
    d9_off             = -20,
    d8_off             = -19,

    r28_off            = -18,
    r27_off            = -17,
    r26_off            = -16,
    r25_off            = -15,
    r24_off            = -14,
    r23_off            = -13,
    r22_off            = -12,
    r21_off            = -11,
    r20_off            = -10,
    r19_off            =  -9,
    call_wrapper_off   =  -8,
    result_off         =  -7,
    result_type_off    =  -6,
    method_off         =  -5,
    entry_point_off    =  -4,
    parameters_off     =  -3,
    parameter_size_off =  -2,
    thread_off         =  -1,
    fp_f               =   0,
    retaddr_off        =   1,
  };

  address generate_call_stub(address& return_address) {
    assert((int)frame::entry_frame_after_call_words == -(int)sp_after_call_off + 1 &&
           (int)frame::entry_frame_call_wrapper_offset == (int)call_wrapper_off,
           "adjust this code");

    StubCodeMark mark(this, "StubRoutines", "call_stub");
    address start = __ pc();

    const Address sp_after_call(rfp, sp_after_call_off * wordSize);

    const Address call_wrapper  (rfp, call_wrapper_off   * wordSize);
    const Address result        (rfp, result_off         * wordSize);
    const Address result_type   (rfp, result_type_off    * wordSize);
    const Address method        (rfp, method_off         * wordSize);
    const Address entry_point   (rfp, entry_point_off    * wordSize);
    const Address parameters    (rfp, parameters_off     * wordSize);
    const Address parameter_size(rfp, parameter_size_off * wordSize);

    const Address thread        (rfp, thread_off         * wordSize);

    const Address d15_save      (rfp, d15_off * wordSize);
    const Address d14_save      (rfp, d14_off * wordSize);
    const Address d13_save      (rfp, d13_off * wordSize);
    const Address d12_save      (rfp, d12_off * wordSize);
    const Address d11_save      (rfp, d11_off * wordSize);
    const Address d10_save      (rfp, d10_off * wordSize);
    const Address d9_save       (rfp, d9_off * wordSize);
    const Address d8_save       (rfp, d8_off * wordSize);

    const Address r28_save      (rfp, r28_off * wordSize);
    const Address r27_save      (rfp, r27_off * wordSize);
    const Address r26_save      (rfp, r26_off * wordSize);
    const Address r25_save      (rfp, r25_off * wordSize);
    const Address r24_save      (rfp, r24_off * wordSize);
    const Address r23_save      (rfp, r23_off * wordSize);
    const Address r22_save      (rfp, r22_off * wordSize);
    const Address r21_save      (rfp, r21_off * wordSize);
    const Address r20_save      (rfp, r20_off * wordSize);
    const Address r19_save      (rfp, r19_off * wordSize);

    // stub code

    // we need a C prolog to bootstrap the x86 caller into the sim
    __ c_stub_prolog(8, 0, MacroAssembler::ret_type_void);

    address aarch64_entry = __ pc();

#ifdef BUILTIN_SIM
    // Save sender's SP for stack traces.
    __ mov(rscratch1, sp);
    __ str(rscratch1, Address(__ pre(sp, -2 * wordSize)));
#endif
    // set up frame and move sp to end of save area
    __ enter();
    __ sub(sp, rfp, -sp_after_call_off * wordSize);

    // save register parameters and Java scratch/global registers
    // n.b. we save thread even though it gets installed in
    // rthread because we want to sanity check rthread later
    __ str(c_rarg7,  thread);
    __ strw(c_rarg6, parameter_size);
    __ str(c_rarg5,  parameters);
    __ str(c_rarg4,  entry_point);
    __ str(c_rarg3,  method);
    __ str(c_rarg2,  result_type);
    __ str(c_rarg1,  result);
    __ str(c_rarg0,  call_wrapper);
    __ str(r19,      r19_save);
    __ str(r20,      r20_save);
    __ str(r21,      r21_save);
    __ str(r22,      r22_save);
    __ str(r23,      r23_save);
    __ str(r24,      r24_save);
    __ str(r25,      r25_save);
    __ str(r26,      r26_save);
    __ str(r27,      r27_save);
    __ str(r28,      r28_save);

    __ strd(v8,      d8_save);
    __ strd(v9,      d9_save);
    __ strd(v10,     d10_save);
    __ strd(v11,     d11_save);
    __ strd(v12,     d12_save);
    __ strd(v13,     d13_save);
    __ strd(v14,     d14_save);
    __ strd(v15,     d15_save);

    // install Java thread in global register now we have saved
    // whatever value it held
    __ mov(rthread, c_rarg7);
    // And method
    __ mov(rmethod, c_rarg3);

    // set up the heapbase register
    __ reinit_heapbase();

#ifdef ASSERT
    // make sure we have no pending exceptions
    {
      Label L;
      __ ldr(rscratch1, Address(rthread, in_bytes(Thread::pending_exception_offset())));
      __ cmp(rscratch1, (unsigned)NULL_WORD);
      __ br(Assembler::EQ, L);
      __ stop("StubRoutines::call_stub: entered with pending exception");
      __ BIND(L);
    }
#endif
    // pass parameters if any
    __ mov(esp, sp);
    __ sub(rscratch1, sp, c_rarg6, ext::uxtw, LogBytesPerWord); // Move SP out of the way
    __ andr(sp, rscratch1, -2 * wordSize);

    BLOCK_COMMENT("pass parameters if any");
    Label parameters_done;
    // parameter count is still in c_rarg6
    // and parameter pointer identifying param 1 is in c_rarg5
    __ cbzw(c_rarg6, parameters_done);

    address loop = __ pc();
    __ ldr(rscratch1, Address(__ post(c_rarg5, wordSize)));
    __ subsw(c_rarg6, c_rarg6, 1);
    __ push(rscratch1);
    __ br(Assembler::GT, loop);

    __ BIND(parameters_done);

    // call Java entry -- passing methdoOop, and current sp
    //      rmethod: Method*
    //      r13: sender sp
    BLOCK_COMMENT("call Java function");
    __ mov(r13, sp);
    __ blr(c_rarg4);

    // tell the simulator we have returned to the stub

    // we do this here because the notify will already have been done
    // if we get to the next instruction via an exception
    //
    // n.b. adding this instruction here affects the calculation of
    // whether or not a routine returns to the call stub (used when
    // doing stack walks) since the normal test is to check the return
    // pc against the address saved below. so we may need to allow for
    // this extra instruction in the check.

    if (NotifySimulator) {
      __ notify(Assembler::method_reentry);
    }
    // save current address for use by exception handling code

    return_address = __ pc();

    // store result depending on type (everything that is not
    // T_OBJECT, T_LONG, T_FLOAT or T_DOUBLE is treated as T_INT)
    // n.b. this assumes Java returns an integral result in r0
    // and a floating result in j_farg0
    __ ldr(j_rarg2, result);
    Label is_long, is_float, is_double, exit;
    __ ldr(j_rarg1, result_type);
    __ cmp(j_rarg1, T_OBJECT);
    __ br(Assembler::EQ, is_long);
    __ cmp(j_rarg1, T_LONG);
    __ br(Assembler::EQ, is_long);
    __ cmp(j_rarg1, T_FLOAT);
    __ br(Assembler::EQ, is_float);
    __ cmp(j_rarg1, T_DOUBLE);
    __ br(Assembler::EQ, is_double);

    // handle T_INT case
    __ strw(r0, Address(j_rarg2));

    __ BIND(exit);

    // pop parameters
    __ sub(esp, rfp, -sp_after_call_off * wordSize);

#ifdef ASSERT
    // verify that threads correspond
    {
      Label L, S;
      __ ldr(rscratch1, thread);
      __ cmp(rthread, rscratch1);
      __ br(Assembler::NE, S);
      __ get_thread(rscratch1);
      __ cmp(rthread, rscratch1);
      __ br(Assembler::EQ, L);
      __ BIND(S);
      __ stop("StubRoutines::call_stub: threads must correspond");
      __ BIND(L);
    }
#endif

    // restore callee-save registers
    __ ldrd(v15,      d15_save);
    __ ldrd(v14,      d14_save);
    __ ldrd(v13,      d13_save);
    __ ldrd(v12,      d12_save);
    __ ldrd(v11,      d11_save);
    __ ldrd(v10,      d10_save);
    __ ldrd(v9,       d9_save);
    __ ldrd(v8,       d8_save);

    __ ldr(r28,      r28_save);
    __ ldr(r27,      r27_save);
    __ ldr(r26,      r26_save);
    __ ldr(r25,      r25_save);
    __ ldr(r24,      r24_save);
    __ ldr(r23,      r23_save);
    __ ldr(r22,      r22_save);
    __ ldr(r21,      r21_save);
    __ ldr(r20,      r20_save);
    __ ldr(r19,      r19_save);
    __ ldr(c_rarg0,  call_wrapper);
    __ ldr(c_rarg1,  result);
    __ ldrw(c_rarg2, result_type);
    __ ldr(c_rarg3,  method);
    __ ldr(c_rarg4,  entry_point);
    __ ldr(c_rarg5,  parameters);
    __ ldr(c_rarg6,  parameter_size);
    __ ldr(c_rarg7,  thread);

#ifndef PRODUCT
    // tell the simulator we are about to end Java execution
    if (NotifySimulator) {
      __ notify(Assembler::method_exit);
    }
#endif
    // leave frame and return to caller
    __ leave();
    __ ret(lr);

    // handle return types different from T_INT

    __ BIND(is_long);
    __ str(r0, Address(j_rarg2, 0));
    __ br(Assembler::AL, exit);

    __ BIND(is_float);
    __ strs(j_farg0, Address(j_rarg2, 0));
    __ br(Assembler::AL, exit);

    __ BIND(is_double);
    __ strd(j_farg0, Address(j_rarg2, 0));
    __ br(Assembler::AL, exit);

    return start;
  }

  // Return point for a Java call if there's an exception thrown in
  // Java code.  The exception is caught and transformed into a
  // pending exception stored in JavaThread that can be tested from
  // within the VM.
  //
  // Note: Usually the parameters are removed by the callee. In case
  // of an exception crossing an activation frame boundary, that is
  // not the case if the callee is compiled code => need to setup the
  // rsp.
  //
  // r0: exception oop

  // NOTE: this is used as a target from the signal handler so it
  // needs an x86 prolog which returns into the current simulator
  // executing the generated catch_exception code. so the prolog
  // needs to install rax in a sim register and adjust the sim's
  // restart pc to enter the generated code at the start position
  // then return from native to simulated execution.

  address generate_catch_exception() {
    StubCodeMark mark(this, "StubRoutines", "catch_exception");
    address start = __ pc();

    // same as in generate_call_stub():
    const Address sp_after_call(rfp, sp_after_call_off * wordSize);
    const Address thread        (rfp, thread_off         * wordSize);

#ifdef ASSERT
    // verify that threads correspond
    {
      Label L, S;
      __ ldr(rscratch1, thread);
      __ cmp(rthread, rscratch1);
      __ br(Assembler::NE, S);
      __ get_thread(rscratch1);
      __ cmp(rthread, rscratch1);
      __ br(Assembler::EQ, L);
      __ bind(S);
      __ stop("StubRoutines::catch_exception: threads must correspond");
      __ bind(L);
    }
#endif

    // set pending exception
    __ verify_oop(r0);

    __ str(r0, Address(rthread, Thread::pending_exception_offset()));
    __ mov(rscratch1, (address)__FILE__);
    __ str(rscratch1, Address(rthread, Thread::exception_file_offset()));
    __ movw(rscratch1, (int)__LINE__);
    __ strw(rscratch1, Address(rthread, Thread::exception_line_offset()));

    // complete return to VM
    assert(StubRoutines::_call_stub_return_address != NULL,
           "_call_stub_return_address must have been generated before");
    __ b(StubRoutines::_call_stub_return_address);

    return start;
  }

  // Continuation point for runtime calls returning with a pending
  // exception.  The pending exception check happened in the runtime
  // or native call stub.  The pending exception in Thread is
  // converted into a Java-level exception.
  //
  // Contract with Java-level exception handlers:
  // r0: exception
  // r3: throwing pc
  //
  // NOTE: At entry of this stub, exception-pc must be in LR !!

  // NOTE: this is always used as a jump target within generated code
  // so it just needs to be generated code wiht no x86 prolog

  address generate_forward_exception() {
    StubCodeMark mark(this, "StubRoutines", "forward exception");
    address start = __ pc();

    // Upon entry, LR points to the return address returning into
    // Java (interpreted or compiled) code; i.e., the return address
    // becomes the throwing pc.
    //
    // Arguments pushed before the runtime call are still on the stack
    // but the exception handler will reset the stack pointer ->
    // ignore them.  A potential result in registers can be ignored as
    // well.

#ifdef ASSERT
    // make sure this code is only executed if there is a pending exception
    {
      Label L;
      __ ldr(rscratch1, Address(rthread, Thread::pending_exception_offset()));
      __ cbnz(rscratch1, L);
      __ stop("StubRoutines::forward exception: no pending exception (1)");
      __ bind(L);
    }
#endif

    // compute exception handler into r19

    // call the VM to find the handler address associated with the
    // caller address. pass thread in r0 and caller pc (ret address)
    // in r1. n.b. the caller pc is in lr, unlike x86 where it is on
    // the stack.
    __ mov(c_rarg1, lr);
    // lr will be trashed by the VM call so we move it to R19
    // (callee-saved) because we also need to pass it to the handler
    // returned by this call.
    __ mov(r19, lr);
    BLOCK_COMMENT("call exception_handler_for_return_address");
    __ call_VM_leaf(CAST_FROM_FN_PTR(address,
                         SharedRuntime::exception_handler_for_return_address),
                    rthread, c_rarg1);
    // we should not really care that lr is no longer the callee
    // address. we saved the value the handler needs in r19 so we can
    // just copy it to r3. however, the C2 handler will push its own
    // frame and then calls into the VM and the VM code asserts that
    // the PC for the frame above the handler belongs to a compiled
    // Java method. So, we restore lr here to satisfy that assert.
    __ mov(lr, r19);
    // setup r0 & r3 & clear pending exception
    __ mov(r3, r19);
    __ mov(r19, r0);
    __ ldr(r0, Address(rthread, Thread::pending_exception_offset()));
    __ str(zr, Address(rthread, Thread::pending_exception_offset()));

#ifdef ASSERT
    // make sure exception is set
    {
      Label L;
      __ cbnz(r0, L);
      __ stop("StubRoutines::forward exception: no pending exception (2)");
      __ bind(L);
    }
#endif

    // continue at exception handler
    // r0: exception
    // r3: throwing pc
    // r19: exception handler
    __ verify_oop(r0);
    __ br(r19);

    return start;
  }

  // Non-destructive plausibility checks for oops
  //
  // Arguments:
  //    r0: oop to verify
  //    rscratch1: error message
  //
  // Stack after saving c_rarg3:
  //    [tos + 0]: saved c_rarg3
  //    [tos + 1]: saved c_rarg2
  //    [tos + 2]: saved lr
  //    [tos + 3]: saved rscratch2
  //    [tos + 4]: saved r0
  //    [tos + 5]: saved rscratch1
  address generate_verify_oop() {

    StubCodeMark mark(this, "StubRoutines", "verify_oop");
    address start = __ pc();

    Label exit, error;

    // save c_rarg2 and c_rarg3
    __ stp(c_rarg3, c_rarg2, Address(__ pre(sp, -16)));

    // __ incrementl(ExternalAddress((address) StubRoutines::verify_oop_count_addr()));
    __ lea(c_rarg2, ExternalAddress((address) StubRoutines::verify_oop_count_addr()));
    __ ldr(c_rarg3, Address(c_rarg2));
    __ add(c_rarg3, c_rarg3, 1);
    __ str(c_rarg3, Address(c_rarg2));

    // object is in r0
    // make sure object is 'reasonable'
    __ cbz(r0, exit); // if obj is NULL it is OK

    // Check if the oop is in the right area of memory
    __ mov(c_rarg3, (intptr_t) Universe::verify_oop_mask());
    __ andr(c_rarg2, r0, c_rarg3);
    __ mov(c_rarg3, (intptr_t) Universe::verify_oop_bits());

    // Compare c_rarg2 and c_rarg3.  We don't use a compare
    // instruction here because the flags register is live.
    __ eor(c_rarg2, c_rarg2, c_rarg3);
    __ cbnz(c_rarg2, error);

    // make sure klass is 'reasonable', which is not zero.
    __ load_klass(r0, r0);  // get klass
    __ cbz(r0, error);      // if klass is NULL it is broken

    // return if everything seems ok
    __ bind(exit);

    __ ldp(c_rarg3, c_rarg2, Address(__ post(sp, 16)));
    __ ret(lr);

    // handle errors
    __ bind(error);
    __ ldp(c_rarg3, c_rarg2, Address(__ post(sp, 16)));

    __ push(RegSet::range(r0, r29), sp);
    // debug(char* msg, int64_t pc, int64_t regs[])
    __ mov(c_rarg0, rscratch1);      // pass address of error message
    __ mov(c_rarg1, lr);             // pass return address
    __ mov(c_rarg2, sp);             // pass address of regs on stack
#ifndef PRODUCT
    assert(frame::arg_reg_save_area_bytes == 0, "not expecting frame reg save area");
#endif
    BLOCK_COMMENT("call MacroAssembler::debug");
    __ mov(rscratch1, CAST_FROM_FN_PTR(address, MacroAssembler::debug64));
    __ blrt(rscratch1, 3, 0, 1);

    return start;
  }

  void array_overlap_test(Label& L_no_overlap, Address::sxtw sf) { __ b(L_no_overlap); }

  // Generate code for an array write pre barrier
  //
  //     addr    -  starting address
  //     count   -  element count
  //     tmp     - scratch register
  //
  //     Destroy no registers!
  //
  void  gen_write_ref_array_pre_barrier(Register addr, Register count, bool dest_uninitialized) {
    BarrierSet* bs = Universe::heap()->barrier_set();
    switch (bs->kind()) {
    case BarrierSet::G1SATBCT:
    case BarrierSet::G1SATBCTLogging:
      // With G1, don't generate the call if we statically know that the target in uninitialized
      if (!dest_uninitialized) {
	__ push(RegSet::range(r0, r29), sp);         // integer registers except lr & sp
	if (count == c_rarg0) {
	  if (addr == c_rarg1) {
	    // exactly backwards!!
	    __ stp(c_rarg0, c_rarg1, __ pre(sp, -2 * wordSize));
	    __ ldp(c_rarg1, c_rarg0, __ post(sp, -2 * wordSize));
	  } else {
	    __ mov(c_rarg1, count);
	    __ mov(c_rarg0, addr);
	  }
	} else {
	  __ mov(c_rarg0, addr);
	  __ mov(c_rarg1, count);
	}
	__ call_VM_leaf(CAST_FROM_FN_PTR(address, BarrierSet::static_write_ref_array_pre), 2);
	__ pop(RegSet::range(r0, r29), sp);         // integer registers except lr & sp        }
	break;
      case BarrierSet::CardTableModRef:
      case BarrierSet::CardTableExtension:
      case BarrierSet::ModRef:
        break;
      default:
        ShouldNotReachHere();

      }
    }
  }

  //
  // Generate code for an array write post barrier
  //
  //  Input:
  //     start    - register containing starting address of destination array
  //     end      - register containing ending address of destination array
  //     scratch  - scratch register
  //
  //  The input registers are overwritten.
  //  The ending address is inclusive.
  void gen_write_ref_array_post_barrier(Register start, Register end, Register scratch) {
    assert_different_registers(start, end, scratch);
    BarrierSet* bs = Universe::heap()->barrier_set();
    switch (bs->kind()) {
      case BarrierSet::G1SATBCT:
      case BarrierSet::G1SATBCTLogging:

        {
	  __ push(RegSet::range(r0, r29), sp);         // integer registers except lr & sp
          // must compute element count unless barrier set interface is changed (other platforms supply count)
          assert_different_registers(start, end, scratch);
          __ lea(scratch, Address(end, BytesPerHeapOop));
          __ sub(scratch, scratch, start);               // subtract start to get #bytes
          __ lsr(scratch, scratch, LogBytesPerHeapOop);  // convert to element count
          __ mov(c_rarg0, start);
          __ mov(c_rarg1, scratch);
          __ call_VM_leaf(CAST_FROM_FN_PTR(address, BarrierSet::static_write_ref_array_post), 2);
	  __ pop(RegSet::range(r0, r29), sp);         // integer registers except lr & sp        }
        }
        break;
      case BarrierSet::CardTableModRef:
      case BarrierSet::CardTableExtension:
        {
          CardTableModRefBS* ct = (CardTableModRefBS*)bs;
          assert(sizeof(*ct->byte_map_base) == sizeof(jbyte), "adjust this code");

          Label L_loop;

           __ lsr(start, start, CardTableModRefBS::card_shift);
           __ lsr(end, end, CardTableModRefBS::card_shift);
           __ sub(end, end, start); // number of bytes to copy

          const Register count = end; // 'end' register contains bytes count now
	  __ mov(scratch, (address)ct->byte_map_base);
          __ add(start, start, scratch);
	  __ BIND(L_loop);
	  __ strb(zr, Address(start, count));
          __ subs(count, count, 1);
          __ br(Assembler::HS, L_loop);
        }
        break;
      default:
        ShouldNotReachHere();

    }
  }

  typedef enum {
    copy_forwards = 1,
    copy_backwards = -1
  } copy_direction;

  // Bulk copy of blocks of 8 words.
  //
  // count is a count of words.
  //
  // Precondition: count >= 2
  //
  // Postconditions:
  //
  // The least significant bit of count contains the remaining count
  // of words to copy.  The rest of count is trash.
  //
  // s and d are adjusted to point to the remaining words to copy
  //
  void generate_copy_longs(Label &start, Register s, Register d, Register count,
			   copy_direction direction) {
    int unit = wordSize * direction;

    int offset;
    const Register t0 = r3, t1 = r4, t2 = r5, t3 = r6,
      t4 = r7, t5 = r10, t6 = r11, t7 = r12;

    assert_different_registers(rscratch1, t0, t1, t2, t3, t4, t5, t6, t7);
    assert_different_registers(s, d, count, rscratch1);

    Label again, large, small;
    __ align(6);
    __ bind(start);
    __ cmp(count, 8);
    __ br(Assembler::LO, small);
    if (direction == copy_forwards) {
      __ sub(s, s, 2 * wordSize);
      __ sub(d, d, 2 * wordSize);
    }
    __ subs(count, count, 16);
    __ br(Assembler::GE, large);

    // 8 <= count < 16 words.  Copy 8.
    __ ldp(t0, t1, Address(s, 2 * unit));
    __ ldp(t2, t3, Address(s, 4 * unit));
    __ ldp(t4, t5, Address(s, 6 * unit));
    __ ldp(t6, t7, Address(__ pre(s, 8 * unit)));

    __ stp(t0, t1, Address(d, 2 * unit));
    __ stp(t2, t3, Address(d, 4 * unit));
    __ stp(t4, t5, Address(d, 6 * unit));
    __ stp(t6, t7, Address(__ pre(d, 8 * unit)));

    if (direction == copy_forwards) {
      __ add(s, s, 2 * wordSize);
      __ add(d, d, 2 * wordSize);
    }

    {
      Label L1, L2;
      __ bind(small);
      __ tbz(count, exact_log2(4), L1);
      __ ldp(t0, t1, Address(__ adjust(s, 2 * unit, direction == copy_backwards)));
      __ ldp(t2, t3, Address(__ adjust(s, 2 * unit, direction == copy_backwards)));
      __ stp(t0, t1, Address(__ adjust(d, 2 * unit, direction == copy_backwards)));
      __ stp(t2, t3, Address(__ adjust(d, 2 * unit, direction == copy_backwards)));
      __ bind(L1);

      __ tbz(count, 1, L2);
      __ ldp(t0, t1, Address(__ adjust(s, 2 * unit, direction == copy_backwards)));
      __ stp(t0, t1, Address(__ adjust(d, 2 * unit, direction == copy_backwards)));
      __ bind(L2);
    }

    __ ret(lr);

    __ align(6);
    __ bind(large);

    // Fill 8 registers
    __ ldp(t0, t1, Address(s, 2 * unit));
    __ ldp(t2, t3, Address(s, 4 * unit));
    __ ldp(t4, t5, Address(s, 6 * unit));
    __ ldp(t6, t7, Address(__ pre(s, 8 * unit)));

    __ bind(again);

    if (direction == copy_forwards && PrefetchCopyIntervalInBytes > 0)
      __ prfm(Address(s, PrefetchCopyIntervalInBytes), PLDL1KEEP);

    __ stp(t0, t1, Address(d, 2 * unit));
    __ ldp(t0, t1, Address(s, 2 * unit));
    __ stp(t2, t3, Address(d, 4 * unit));
    __ ldp(t2, t3, Address(s, 4 * unit));
    __ stp(t4, t5, Address(d, 6 * unit));
    __ ldp(t4, t5, Address(s, 6 * unit));
    __ stp(t6, t7, Address(__ pre(d, 8 * unit)));
    __ ldp(t6, t7, Address(__ pre(s, 8 * unit)));

    __ subs(count, count, 8);
    __ br(Assembler::HS, again);

    // Drain
    __ stp(t0, t1, Address(d, 2 * unit));
    __ stp(t2, t3, Address(d, 4 * unit));
    __ stp(t4, t5, Address(d, 6 * unit));
    __ stp(t6, t7, Address(__ pre(d, 8 * unit)));

    if (direction == copy_forwards) {
      __ add(s, s, 2 * wordSize);
      __ add(d, d, 2 * wordSize);
    }

    {
      Label L1, L2;
      __ tbz(count, exact_log2(4), L1);
      __ ldp(t0, t1, Address(__ adjust(s, 2 * unit, direction == copy_backwards)));
      __ ldp(t2, t3, Address(__ adjust(s, 2 * unit, direction == copy_backwards)));
      __ stp(t0, t1, Address(__ adjust(d, 2 * unit, direction == copy_backwards)));
      __ stp(t2, t3, Address(__ adjust(d, 2 * unit, direction == copy_backwards)));
      __ bind(L1);

      __ tbz(count, 1, L2);
      __ ldp(t0, t1, Address(__ adjust(s, 2 * unit, direction == copy_backwards)));
      __ stp(t0, t1, Address(__ adjust(d, 2 * unit, direction == copy_backwards)));
      __ bind(L2);
    }

    __ ret(lr);
  }

  // Small copy: less than 16 bytes.
  //
  // NB: Ignores all of the bits of count which represent more than 15
  // bytes, so a caller doesn't have to mask them.

  void copy_memory_small(Register s, Register d, Register count, Register tmp, int step) {
    bool is_backwards = step < 0;
    size_t granularity = uabs(step);
    int direction = is_backwards ? -1 : 1;
    int unit = wordSize * direction;

    Label Lpair, Lword, Lint, Lshort, Lbyte;

    assert(granularity
	   && granularity <= sizeof (jlong), "Impossible granularity in copy_memory_small");

    const Register t0 = r3, t1 = r4, t2 = r5, t3 = r6;

    // ??? I don't know if this bit-test-and-branch is the right thing
    // to do.  It does a lot of jumping, resulting in several
    // mispredicted branches.  It might make more sense to do this
    // with something like Duff's device with a single computed branch.

    __ tbz(count, 3 - exact_log2(granularity), Lword);
    __ ldr(tmp, Address(__ adjust(s, unit, is_backwards)));
    __ str(tmp, Address(__ adjust(d, unit, is_backwards)));
    __ bind(Lword);

    if (granularity <= sizeof (jint)) {
      __ tbz(count, 2 - exact_log2(granularity), Lint);
      __ ldrw(tmp, Address(__ adjust(s, sizeof (jint) * direction, is_backwards)));
      __ strw(tmp, Address(__ adjust(d, sizeof (jint) * direction, is_backwards)));
      __ bind(Lint);
    }

    if (granularity <= sizeof (jshort)) {
      __ tbz(count, 1 - exact_log2(granularity), Lshort);
      __ ldrh(tmp, Address(__ adjust(s, sizeof (jshort) * direction, is_backwards)));
      __ strh(tmp, Address(__ adjust(d, sizeof (jshort) * direction, is_backwards)));
      __ bind(Lshort);
    }

    if (granularity <= sizeof (jbyte)) {
      __ tbz(count, 0, Lbyte);
      __ ldrb(tmp, Address(__ adjust(s, sizeof (jbyte) * direction, is_backwards)));
      __ strb(tmp, Address(__ adjust(d, sizeof (jbyte) * direction, is_backwards)));
      __ bind(Lbyte);
    }
  }

  Label copy_f, copy_b;

  // All-singing all-dancing memory copy.
  //
  // Copy count units of memory from s to d.  The size of a unit is
  // step, which can be positive or negative depending on the direction
  // of copy.  If is_aligned is false, we align the source address.
  //

  void copy_memory(bool is_aligned, Register s, Register d,
		   Register count, Register tmp, int step) {
    copy_direction direction = step < 0 ? copy_backwards : copy_forwards;
    bool is_backwards = step < 0;
    int granularity = uabs(step);
    const Register t0 = r3, t1 = r4;

    if (is_backwards) {
      __ lea(s, Address(s, count, Address::uxtw(exact_log2(-step))));
      __ lea(d, Address(d, count, Address::uxtw(exact_log2(-step))));
    }

    Label done, tail;

    __ cmp(count, 16/granularity);
    __ br(Assembler::LO, tail);

    // Now we've got the small case out of the way we can align the
    // source address on a 2-word boundary.

    Label aligned;

    if (is_aligned) {
      // We may have to adjust by 1 word to get s 2-word-aligned.
      __ tbz(s, exact_log2(wordSize), aligned);
      __ ldr(tmp, Address(__ adjust(s, direction * wordSize, is_backwards)));
      __ str(tmp, Address(__ adjust(d, direction * wordSize, is_backwards)));
      __ sub(count, count, wordSize/granularity);
    } else {
      if (is_backwards) {
	__ andr(rscratch2, s, 2 * wordSize - 1);
      } else {
	__ neg(rscratch2, s);
	__ andr(rscratch2, rscratch2, 2 * wordSize - 1);
      }
      // rscratch2 is the byte adjustment needed to align s.
      __ cbz(rscratch2, aligned);
      __ lsr(rscratch2, rscratch2, exact_log2(granularity));
      __ sub(count, count, rscratch2);

#if 0
      // ?? This code is only correct for a disjoint copy.  It may or
      // may not make sense to use it in that case.

      // Copy the first pair; s and d may not be aligned.
      __ ldp(t0, t1, Address(s, is_backwards ? -2 * wordSize : 0));
      __ stp(t0, t1, Address(d, is_backwards ? -2 * wordSize : 0));

      // Align s and d, adjust count
      if (is_backwards) {
	__ sub(s, s, rscratch2);
	__ sub(d, d, rscratch2);
      } else {
	__ add(s, s, rscratch2);
	__ add(d, d, rscratch2);
      }
#else
      copy_memory_small(s, d, rscratch2, rscratch1, step);
#endif
    }

    __ cmp(count, 16/granularity);
    __ br(Assembler::LT, tail);
    __ bind(aligned);

    // s is now 2-word-aligned.

    // We have a count of units and some trailing bytes.  Adjust the
    // count and do a bulk copy of words.
    __ lsr(rscratch2, count, exact_log2(wordSize/granularity));
    if (direction == copy_forwards)
      __ bl(copy_f);
    else
      __ bl(copy_b);

    // And the tail.

    __ bind(tail);
    copy_memory_small(s, d, count, tmp, step);
  }


  void clobber_registers() {
#ifdef ASSERT
    __ mov(rscratch1, (uint64_t)0xdeadbeef);
    __ orr(rscratch1, rscratch1, rscratch1, Assembler::LSL, 32);
    for (Register r = r3; r <= r18; r++)
      if (r != rscratch1) __ mov(r, rscratch1);
#endif
  }

  // Scan over array at a for count oops, verifying each one.
  // Preserves a and count, clobbers rscratch1 and rscratch2.
  void verify_oop_array (size_t size, Register a, Register count, Register temp) {
    Label loop, end;
    __ mov(rscratch1, a);
    __ mov(rscratch2, zr);
    __ bind(loop);
    __ cmp(rscratch2, count);
    __ br(Assembler::HS, end);
    if (size == (size_t)wordSize) {
      __ ldr(temp, Address(a, rscratch2, Address::uxtw(exact_log2(size))));
      __ verify_oop(temp);
    } else {
      __ ldrw(r16, Address(a, rscratch2, Address::uxtw(exact_log2(size))));
      __ decode_heap_oop(temp); // calls verify_oop
    }
    __ add(rscratch2, rscratch2, size);
    __ b(loop);
    __ bind(end);
  }

  // Arguments:
  //   aligned - true => Input and output aligned on a HeapWord == 8-byte boundary
  //             ignored
  //   is_oop  - true => oop array, so generate store check code
  //   name    - stub name string
  //
  // Inputs:
  //   c_rarg0   - source array address
  //   c_rarg1   - destination array address
  //   c_rarg2   - element count, treated as ssize_t, can be zero
  //
  // If 'from' and/or 'to' are aligned on 4-byte boundaries, we let
  // the hardware handle it.  The two dwords within qwords that span
  // cache line boundaries will still be loaded and stored atomicly.
  //
  // Side Effects:
  //   disjoint_int_copy_entry is set to the no-overlap entry point
  //   used by generate_conjoint_int_oop_copy().
  //
  address generate_disjoint_copy(size_t size, bool aligned, bool is_oop, address *entry,
				  const char *name, bool dest_uninitialized = false) {
    Register s = c_rarg0, d = c_rarg1, count = c_rarg2;
    __ align(CodeEntryAlignment);
    StubCodeMark mark(this, "StubRoutines", name);
    address start = __ pc();
    if (entry != NULL) {
      *entry = __ pc();
      // caller can pass a 64-bit byte count here (from Unsafe.copyMemory)
      BLOCK_COMMENT("Entry:");
    }
    __ enter();
    if (is_oop) {
      __ push(RegSet::of(d, count), sp);
      // no registers are destroyed by this call
      gen_write_ref_array_pre_barrier(d, count, dest_uninitialized);
    }
    copy_memory(aligned, s, d, count, rscratch1, size);
    if (is_oop) {
      __ pop(RegSet::of(d, count), sp);
      if (VerifyOops)
	verify_oop_array(size, d, count, r16);
      __ sub(count, count, 1); // make an inclusive end pointer
      __ lea(count, Address(d, count, Address::uxtw(exact_log2(size))));
      gen_write_ref_array_post_barrier(d, count, rscratch1);
    }
    __ leave();
    __ ret(lr);
#ifdef BUILTIN_SIM
    {
      AArch64Simulator *sim = AArch64Simulator::get_current(UseSimulatorCache, DisableBCCheck);
      sim->notifyCompile(const_cast<char*>(name), start);
    }
#endif
    return start;
  }

  // Arguments:
  //   aligned - true => Input and output aligned on a HeapWord == 8-byte boundary
  //             ignored
  //   is_oop  - true => oop array, so generate store check code
  //   name    - stub name string
  //
  // Inputs:
  //   c_rarg0   - source array address
  //   c_rarg1   - destination array address
  //   c_rarg2   - element count, treated as ssize_t, can be zero
  //
  // If 'from' and/or 'to' are aligned on 4-byte boundaries, we let
  // the hardware handle it.  The two dwords within qwords that span
  // cache line boundaries will still be loaded and stored atomicly.
  //
  address generate_conjoint_copy(size_t size, bool aligned, bool is_oop, address nooverlap_target,
				 address *entry, const char *name,
				 bool dest_uninitialized = false) {
    Register s = c_rarg0, d = c_rarg1, count = c_rarg2;

    StubCodeMark mark(this, "StubRoutines", name);
    address start = __ pc();

    __ cmp(d, s);
    __ br(Assembler::LS, nooverlap_target);

    __ enter();
    if (is_oop) {
      __ push(RegSet::of(d, count), sp);
      // no registers are destroyed by this call
      gen_write_ref_array_pre_barrier(d, count, dest_uninitialized);
    }
    copy_memory(aligned, s, d, count, rscratch1, -size);
    if (is_oop) {
      __ pop(RegSet::of(d, count), sp);
      if (VerifyOops)
	verify_oop_array(size, d, count, r16);
      __ sub(count, count, 1); // make an inclusive end pointer
      __ lea(count, Address(d, count, Address::uxtw(exact_log2(size))));
      gen_write_ref_array_post_barrier(d, count, rscratch1);
    }
    __ leave();
    __ ret(lr);
#ifdef BUILTIN_SIM
    {
      AArch64Simulator *sim = AArch64Simulator::get_current(UseSimulatorCache, DisableBCCheck);
      sim->notifyCompile(const_cast<char*>(name), start);
    }
#endif
    return start;
}

  // Arguments:
  //   aligned - true => Input and output aligned on a HeapWord == 8-byte boundary
  //             ignored
  //   name    - stub name string
  //
  // Inputs:
  //   c_rarg0   - source array address
  //   c_rarg1   - destination array address
  //   c_rarg2   - element count, treated as ssize_t, can be zero
  //
  // If 'from' and/or 'to' are aligned on 4-, 2-, or 1-byte boundaries,
  // we let the hardware handle it.  The one to eight bytes within words,
  // dwords or qwords that span cache line boundaries will still be loaded
  // and stored atomically.
  //
  // Side Effects:
  //   disjoint_byte_copy_entry is set to the no-overlap entry point  //
  // If 'from' and/or 'to' are aligned on 4-, 2-, or 1-byte boundaries,
  // we let the hardware handle it.  The one to eight bytes within words,
  // dwords or qwords that span cache line boundaries will still be loaded
  // and stored atomically.
  //
  // Side Effects:
  //   disjoint_byte_copy_entry is set to the no-overlap entry point
  //   used by generate_conjoint_byte_copy().
  //
  address generate_disjoint_byte_copy(bool aligned, address* entry, const char *name) {
    const bool not_oop = false;
    return generate_disjoint_copy(sizeof (jbyte), aligned, not_oop, entry, name);
  }

  // Arguments:
  //   aligned - true => Input and output aligned on a HeapWord == 8-byte boundary
  //             ignored
  //   name    - stub name string
  //
  // Inputs:
  //   c_rarg0   - source array address
  //   c_rarg1   - destination array address
  //   c_rarg2   - element count, treated as ssize_t, can be zero
  //
  // If 'from' and/or 'to' are aligned on 4-, 2-, or 1-byte boundaries,
  // we let the hardware handle it.  The one to eight bytes within words,
  // dwords or qwords that span cache line boundaries will still be loaded
  // and stored atomically.
  //
  address generate_conjoint_byte_copy(bool aligned, address nooverlap_target,
                                      address* entry, const char *name) {
    const bool not_oop = false;
    return generate_conjoint_copy(sizeof (jbyte), aligned, not_oop, nooverlap_target, entry, name);
  }

  // Arguments:
  //   aligned - true => Input and output aligned on a HeapWord == 8-byte boundary
  //             ignored
  //   name    - stub name string
  //
  // Inputs:
  //   c_rarg0   - source array address
  //   c_rarg1   - destination array address
  //   c_rarg2   - element count, treated as ssize_t, can be zero
  //
  // If 'from' and/or 'to' are aligned on 4- or 2-byte boundaries, we
  // let the hardware handle it.  The two or four words within dwords
  // or qwords that span cache line boundaries will still be loaded
  // and stored atomically.
  //
  // Side Effects:
  //   disjoint_short_copy_entry is set to the no-overlap entry point
  //   used by generate_conjoint_short_copy().
  //
  address generate_disjoint_short_copy(bool aligned,
				       address* entry, const char *name) {
    const bool not_oop = false;
    return generate_disjoint_copy(sizeof (jshort), aligned, not_oop, entry, name);
  }

  // Arguments:
  //   aligned - true => Input and output aligned on a HeapWord == 8-byte boundary
  //             ignored
  //   name    - stub name string
  //
  // Inputs:
  //   c_rarg0   - source array address
  //   c_rarg1   - destination array address
  //   c_rarg2   - element count, treated as ssize_t, can be zero
  //
  // If 'from' and/or 'to' are aligned on 4- or 2-byte boundaries, we
  // let the hardware handle it.  The two or four words within dwords
  // or qwords that span cache line boundaries will still be loaded
  // and stored atomically.
  //
  address generate_conjoint_short_copy(bool aligned, address nooverlap_target,
                                       address *entry, const char *name) {
    const bool not_oop = false;
    return generate_conjoint_copy(sizeof (jshort), aligned, not_oop, nooverlap_target, entry, name);

  }
  // Arguments:
  //   aligned - true => Input and output aligned on a HeapWord == 8-byte boundary
  //             ignored
  //   name    - stub name string
  //
  // Inputs:
  //   c_rarg0   - source array address
  //   c_rarg1   - destination array address
  //   c_rarg2   - element count, treated as ssize_t, can be zero
  //
  // If 'from' and/or 'to' are aligned on 4-byte boundaries, we let
  // the hardware handle it.  The two dwords within qwords that span
  // cache line boundaries will still be loaded and stored atomicly.
  //
  // Side Effects:
  //   disjoint_int_copy_entry is set to the no-overlap entry point
  //   used by generate_conjoint_int_oop_copy().
  //
  address generate_disjoint_int_copy(bool aligned, address *entry,
					 const char *name, bool dest_uninitialized = false) {
    const bool not_oop = false;
    return generate_disjoint_copy(sizeof (jint), aligned, not_oop, entry, name);
  }

  // Arguments:
  //   aligned - true => Input and output aligned on a HeapWord == 8-byte boundary
  //             ignored
  //   name    - stub name string
  //
  // Inputs:
  //   c_rarg0   - source array address
  //   c_rarg1   - destination array address
  //   c_rarg2   - element count, treated as ssize_t, can be zero
  //
  // If 'from' and/or 'to' are aligned on 4-byte boundaries, we let
  // the hardware handle it.  The two dwords within qwords that span
  // cache line boundaries will still be loaded and stored atomicly.
  //
  address generate_conjoint_int_copy(bool aligned, address nooverlap_target,
				     address *entry, const char *name,
				     bool dest_uninitialized = false) {
    const bool not_oop = false;
    return generate_conjoint_copy(sizeof (jint), aligned, not_oop, nooverlap_target, entry, name);
  }


  // Arguments:
  //   aligned - true => Input and output aligned on a HeapWord boundary == 8 bytes
  //             ignored
  //   name    - stub name string
  //
  // Inputs:
  //   c_rarg0   - source array address
  //   c_rarg1   - destination array address
  //   c_rarg2   - element count, treated as size_t, can be zero
  //
  // Side Effects:
  //   disjoint_oop_copy_entry or disjoint_long_copy_entry is set to the
  //   no-overlap entry point used by generate_conjoint_long_oop_copy().
  //
  address generate_disjoint_long_copy(bool aligned, address *entry,
                                          const char *name, bool dest_uninitialized = false) {
    const bool not_oop = false;
    return generate_disjoint_copy(sizeof (jlong), aligned, not_oop, entry, name);
  }

  // Arguments:
  //   aligned - true => Input and output aligned on a HeapWord boundary == 8 bytes
  //             ignored
  //   name    - stub name string
  //
  // Inputs:
  //   c_rarg0   - source array address
  //   c_rarg1   - destination array address
  //   c_rarg2   - element count, treated as size_t, can be zero
  //
  address generate_conjoint_long_copy(bool aligned,
				      address nooverlap_target, address *entry,
				      const char *name, bool dest_uninitialized = false) {
    const bool not_oop = false;
    return generate_conjoint_copy(sizeof (jlong), aligned, not_oop, nooverlap_target, entry, name);
  }

  // Arguments:
  //   aligned - true => Input and output aligned on a HeapWord boundary == 8 bytes
  //             ignored
  //   name    - stub name string
  //
  // Inputs:
  //   c_rarg0   - source array address
  //   c_rarg1   - destination array address
  //   c_rarg2   - element count, treated as size_t, can be zero
  //
  // Side Effects:
  //   disjoint_oop_copy_entry or disjoint_long_copy_entry is set to the
  //   no-overlap entry point used by generate_conjoint_long_oop_copy().
  //
  address generate_disjoint_oop_copy(bool aligned, address *entry,
				     const char *name, bool dest_uninitialized = false) {
    const bool is_oop = true;
    const size_t size = UseCompressedOops ? sizeof (jint) : sizeof (jlong);
    return generate_disjoint_copy(size, aligned, is_oop, entry, name);
  }

  // Arguments:
  //   aligned - true => Input and output aligned on a HeapWord boundary == 8 bytes
  //             ignored
  //   name    - stub name string
  //
  // Inputs:
  //   c_rarg0   - source array address
  //   c_rarg1   - destination array address
  //   c_rarg2   - element count, treated as size_t, can be zero
  //
  address generate_conjoint_oop_copy(bool aligned,
				     address nooverlap_target, address *entry,
				     const char *name, bool dest_uninitialized = false) {
    const bool is_oop = true;
    const size_t size = UseCompressedOops ? sizeof (jint) : sizeof (jlong);
    return generate_conjoint_copy(size, aligned, is_oop, nooverlap_target, entry, name);
  }


  // Helper for generating a dynamic type check.
  // Smashes rscratch1.
  void generate_type_check(Register sub_klass,
                           Register super_check_offset,
                           Register super_klass,
                           Label& L_success) {
    assert_different_registers(sub_klass, super_check_offset, super_klass);

    BLOCK_COMMENT("type_check:");

    Label L_miss;

    __ check_klass_subtype_fast_path(sub_klass, super_klass, noreg,        &L_success, &L_miss, NULL,
                                     super_check_offset);
    __ check_klass_subtype_slow_path(sub_klass, super_klass, noreg, noreg, &L_success, NULL);

    // Fall through on failure!
    __ BIND(L_miss);
  }

  //
  //  Generate checkcasting array copy stub
  //
  //  Input:
  //    c_rarg0   - source array address
  //    c_rarg1   - destination array address
  //    c_rarg2   - element count, treated as ssize_t, can be zero
  //    c_rarg3   - size_t ckoff (super_check_offset)
  //    c_rarg4   - oop ckval (super_klass)
  //
  //  Output:
  //    r0 ==  0  -  success
  //    r0 == -1^K - failure, where K is partial transfer count
  //
  address generate_checkcast_copy(const char *name, address *entry,
                                  bool dest_uninitialized = false) {

    Label L_load_element, L_store_element, L_do_card_marks, L_done, L_done_pop;

    // Input registers (after setup_arg_regs)
    const Register from        = c_rarg0;   // source array address
    const Register to          = c_rarg1;   // destination array address
    const Register count       = c_rarg2;   // elementscount
    const Register ckoff       = c_rarg3;   // super_check_offset
    const Register ckval       = c_rarg4;   // super_klass

    // Registers used as temps (r18, r19, r20 are save-on-entry)
    const Register count_save  = r21;       // orig elementscount
    const Register start_to    = r20;       // destination array start address
    const Register copied_oop  = r18;       // actual oop copied
    const Register r19_klass   = r19;       // oop._klass

    //---------------------------------------------------------------
    // Assembler stub will be used for this call to arraycopy
    // if the two arrays are subtypes of Object[] but the
    // destination array type is not equal to or a supertype
    // of the source type.  Each element must be separately
    // checked.

    assert_different_registers(from, to, count, ckoff, ckval, start_to,
			       copied_oop, r19_klass, count_save);

    __ align(CodeEntryAlignment);
    StubCodeMark mark(this, "StubRoutines", name);
    address start = __ pc();

    __ enter(); // required for proper stackwalking of RuntimeStub frame

#ifdef ASSERT
    // caller guarantees that the arrays really are different
    // otherwise, we would have to make conjoint checks
    { Label L;
      array_overlap_test(L, TIMES_OOP);
      __ stop("checkcast_copy within a single array");
      __ bind(L);
    }
#endif //ASSERT

    // Caller of this entry point must set up the argument registers.
    if (entry != NULL) {
      *entry = __ pc();
      BLOCK_COMMENT("Entry:");
    }

     // Empty array:  Nothing to do.
    __ cbz(count, L_done);

    __ push(RegSet::of(r18, r19, r20, r21), sp);

#ifdef ASSERT
    BLOCK_COMMENT("assert consistent ckoff/ckval");
    // The ckoff and ckval must be mutually consistent,
    // even though caller generates both.
    { Label L;
      int sco_offset = in_bytes(Klass::super_check_offset_offset());
      __ ldrw(start_to, Address(ckval, sco_offset));
      __ cmpw(ckoff, start_to);
      __ br(Assembler::EQ, L);
      __ stop("super_check_offset inconsistent");
      __ bind(L);
    }
#endif //ASSERT

    // save the original count
    __ mov(count_save, count);

    // Copy from low to high addresses
    __ mov(start_to, to);              // Save destination array start address
    __ b(L_load_element);

    // ======== begin loop ========
    // (Loop is rotated; its entry is L_load_element.)
    // Loop control:
    //   for (; count != 0; count--) {
    //     copied_oop = load_heap_oop(from++);
    //     ... generate_type_check ...;
    //     store_heap_oop(to++, copied_oop);
    //   }
    __ align(OptoLoopAlignment);

    __ BIND(L_store_element);
    __ store_heap_oop(__ post(to, UseCompressedOops ? 4 : 8), copied_oop);  // store the oop
    __ sub(count, count, 1);
    __ cbz(count, L_do_card_marks);

    // ======== loop entry is here ========
    __ BIND(L_load_element);
    __ load_heap_oop(copied_oop, __ post(from, UseCompressedOops ? 4 : 8)); // load the oop
    __ cbz(copied_oop, L_store_element);

    __ load_klass(r19_klass, copied_oop);// query the object klass
    generate_type_check(r19_klass, ckoff, ckval, L_store_element);
    // ======== end loop ========

    // It was a real error; we must depend on the caller to finish the job.
    // Register count = remaining oops, count_orig = total oops.
    // Emit GC store barriers for the oops we have copied and report
    // their number to the caller.

    __ subs(count, count_save, count);     // K = partially copied oop count
    __ eon(count, count, zr);                   // report (-1^K) to caller
    __ br(Assembler::EQ, L_done_pop);

    __ BIND(L_do_card_marks);
    __ add(to, to, -heapOopSize);         // make an inclusive end pointer
    gen_write_ref_array_post_barrier(start_to, to, rscratch1);

    __ bind(L_done_pop);
    __ pop(RegSet::of(r18, r19, r20, r21), sp);
    inc_counter_np(SharedRuntime::_checkcast_array_copy_ctr);

    __ bind(L_done);
    __ mov(r0, count);
    __ leave();
    __ ret(lr);

    return start;
  }

  // Perform range checks on the proposed arraycopy.
  // Kills temp, but nothing else.
  // Also, clean the sign bits of src_pos and dst_pos.
  void arraycopy_range_checks(Register src,     // source array oop (c_rarg0)
                              Register src_pos, // source position (c_rarg1)
                              Register dst,     // destination array oo (c_rarg2)
                              Register dst_pos, // destination position (c_rarg3)
                              Register length,
                              Register temp,
                              Label& L_failed) { Unimplemented(); }

  // These stubs get called from some dumb test routine.
  // I'll write them properly when they're called from
  // something that's actually doing something.
  static void fake_arraycopy_stub(address src, address dst, int count) {
    assert(count == 0, "huh?");
  }


  void generate_arraycopy_stubs() {
    address entry;
    address entry_jbyte_arraycopy;
    address entry_jshort_arraycopy;
    address entry_jint_arraycopy;
    address entry_oop_arraycopy;
    address entry_jlong_arraycopy;
    address entry_checkcast_arraycopy;

    generate_copy_longs(copy_f, r0, r1, rscratch2, copy_forwards);
    generate_copy_longs(copy_b, r0, r1, rscratch2, copy_backwards);

    //*** jbyte
    // Always need aligned and unaligned versions
    StubRoutines::_jbyte_disjoint_arraycopy         = generate_disjoint_byte_copy(false, &entry,
                                                                                  "jbyte_disjoint_arraycopy");
    StubRoutines::_jbyte_arraycopy                  = generate_conjoint_byte_copy(false, entry,
                                                                                  &entry_jbyte_arraycopy,
                                                                                  "jbyte_arraycopy");
    StubRoutines::_arrayof_jbyte_disjoint_arraycopy = generate_disjoint_byte_copy(true, &entry,
                                                                                  "arrayof_jbyte_disjoint_arraycopy");
    StubRoutines::_arrayof_jbyte_arraycopy          = generate_conjoint_byte_copy(true, entry, NULL,
                                                                                  "arrayof_jbyte_arraycopy");

    //*** jshort
    // Always need aligned and unaligned versions
    StubRoutines::_jshort_disjoint_arraycopy         = generate_disjoint_short_copy(false, &entry,
                                                                                    "jshort_disjoint_arraycopy");
    StubRoutines::_jshort_arraycopy                  = generate_conjoint_short_copy(false, entry,
                                                                                    &entry_jshort_arraycopy,
                                                                                    "jshort_arraycopy");
    StubRoutines::_arrayof_jshort_disjoint_arraycopy = generate_disjoint_short_copy(true, &entry,
                                                                                    "arrayof_jshort_disjoint_arraycopy");
    StubRoutines::_arrayof_jshort_arraycopy          = generate_conjoint_short_copy(true, entry, NULL,
                                                                                    "arrayof_jshort_arraycopy");

    //*** jint
    // Aligned versions
    StubRoutines::_arrayof_jint_disjoint_arraycopy = generate_disjoint_int_copy(true, &entry,
										"arrayof_jint_disjoint_arraycopy");
    StubRoutines::_arrayof_jint_arraycopy          = generate_conjoint_int_copy(true, entry, &entry_jint_arraycopy,
										"arrayof_jint_arraycopy");
    // In 64 bit we need both aligned and unaligned versions of jint arraycopy.
    // entry_jint_arraycopy always points to the unaligned version
    StubRoutines::_jint_disjoint_arraycopy         = generate_disjoint_int_copy(false, &entry,
										"jint_disjoint_arraycopy");
    StubRoutines::_jint_arraycopy                  = generate_conjoint_int_copy(false, entry,
										&entry_jint_arraycopy,
										"jint_arraycopy");

    //*** jlong
    // It is always aligned
    StubRoutines::_arrayof_jlong_disjoint_arraycopy = generate_disjoint_long_copy(true, &entry,
										  "arrayof_jlong_disjoint_arraycopy");
    StubRoutines::_arrayof_jlong_arraycopy          = generate_conjoint_long_copy(true, entry, &entry_jlong_arraycopy,
										  "arrayof_jlong_arraycopy");
    StubRoutines::_jlong_disjoint_arraycopy         = StubRoutines::_arrayof_jlong_disjoint_arraycopy;
    StubRoutines::_jlong_arraycopy                  = StubRoutines::_arrayof_jlong_arraycopy;

    //*** oops
    {
      // With compressed oops we need unaligned versions; notice that
      // we overwrite entry_oop_arraycopy.
      bool aligned = !UseCompressedOops;

      StubRoutines::_arrayof_oop_disjoint_arraycopy
	= generate_disjoint_oop_copy(aligned, &entry, "arrayof_oop_disjoint_arraycopy");
      StubRoutines::_arrayof_oop_arraycopy
	= generate_conjoint_oop_copy(aligned, entry, &entry_oop_arraycopy, "arrayof_oop_arraycopy");
      // Aligned versions without pre-barriers
      StubRoutines::_arrayof_oop_disjoint_arraycopy_uninit
	= generate_disjoint_oop_copy(aligned, &entry, "arrayof_oop_disjoint_arraycopy_uninit",
				     /*dest_uninitialized*/true);
      StubRoutines::_arrayof_oop_arraycopy_uninit
	= generate_conjoint_oop_copy(aligned, entry, NULL, "arrayof_oop_arraycopy_uninit",
				     /*dest_uninitialized*/true);
    }

    StubRoutines::_oop_disjoint_arraycopy            = StubRoutines::_arrayof_oop_disjoint_arraycopy;
    StubRoutines::_oop_arraycopy                     = StubRoutines::_arrayof_oop_arraycopy;
    StubRoutines::_oop_disjoint_arraycopy_uninit     = StubRoutines::_arrayof_oop_disjoint_arraycopy_uninit;
    StubRoutines::_oop_arraycopy_uninit              = StubRoutines::_arrayof_oop_arraycopy_uninit;

    StubRoutines::_checkcast_arraycopy        = generate_checkcast_copy("checkcast_arraycopy", &entry_checkcast_arraycopy);
    StubRoutines::_checkcast_arraycopy_uninit = generate_checkcast_copy("checkcast_arraycopy_uninit", NULL,
                                                                        /*dest_uninitialized*/true);
  }

  // Arguments:
  //
  // Inputs:
  //   c_rarg0   - source byte array address
  //   c_rarg1   - destination byte array address
  //   c_rarg2   - K (key) in little endian int array
  //
  address generate_aescrypt_encryptBlock() {
    __ align(CodeEntryAlignment);
    StubCodeMark mark(this, "StubRoutines", "aescrypt_encryptBlock");

    Label L_doLast;

    const Register from        = c_rarg0;  // source array address
    const Register to          = c_rarg1;  // destination array address
    const Register key         = c_rarg2;  // key array address
    const Register keylen      = rscratch1;

    address start = __ pc();
    __ enter();

    __ ldrw(keylen, Address(key, arrayOopDesc::length_offset_in_bytes() - arrayOopDesc::base_offset_in_bytes(T_INT)));

    __ ld1(v0, __ T16B, from); // get 16 bytes of input

    __ ld1(v1, v2, v3, v4, __ T16B, __ post(key, 64));
    __ rev32(v1, __ T16B, v1);
    __ rev32(v2, __ T16B, v2);
    __ rev32(v3, __ T16B, v3);
    __ rev32(v4, __ T16B, v4);
    __ aese(v0, v1);
    __ aesmc(v0, v0);
    __ aese(v0, v2);
    __ aesmc(v0, v0);
    __ aese(v0, v3);
    __ aesmc(v0, v0);
    __ aese(v0, v4);
    __ aesmc(v0, v0);

    __ ld1(v1, v2, v3, v4, __ T16B, __ post(key, 64));
    __ rev32(v1, __ T16B, v1);
    __ rev32(v2, __ T16B, v2);
    __ rev32(v3, __ T16B, v3);
    __ rev32(v4, __ T16B, v4);
    __ aese(v0, v1);
    __ aesmc(v0, v0);
    __ aese(v0, v2);
    __ aesmc(v0, v0);
    __ aese(v0, v3);
    __ aesmc(v0, v0);
    __ aese(v0, v4);
    __ aesmc(v0, v0);

    __ ld1(v1, v2, __ T16B, __ post(key, 32));
    __ rev32(v1, __ T16B, v1);
    __ rev32(v2, __ T16B, v2);

    __ cmpw(keylen, 44);
    __ br(Assembler::EQ, L_doLast);

    __ aese(v0, v1);
    __ aesmc(v0, v0);
    __ aese(v0, v2);
    __ aesmc(v0, v0);

    __ ld1(v1, v2, __ T16B, __ post(key, 32));
    __ rev32(v1, __ T16B, v1);
    __ rev32(v2, __ T16B, v2);

    __ cmpw(keylen, 52);
    __ br(Assembler::EQ, L_doLast);

    __ aese(v0, v1);
    __ aesmc(v0, v0);
    __ aese(v0, v2);
    __ aesmc(v0, v0);

    __ ld1(v1, v2, __ T16B, __ post(key, 32));
    __ rev32(v1, __ T16B, v1);
    __ rev32(v2, __ T16B, v2);

    __ BIND(L_doLast);

    __ aese(v0, v1);
    __ aesmc(v0, v0);
    __ aese(v0, v2);

    __ ld1(v1, __ T16B, key);
    __ rev32(v1, __ T16B, v1);
    __ eor(v0, __ T16B, v0, v1);

    __ st1(v0, __ T16B, to);

    __ mov(r0, 0);

    __ leave();
    __ ret(lr);

    return start;
  }

  // Arguments:
  //
  // Inputs:
  //   c_rarg0   - source byte array address
  //   c_rarg1   - destination byte array address
  //   c_rarg2   - K (key) in little endian int array
  //
  address generate_aescrypt_decryptBlock() {
    assert(UseAES, "need AES instructions and misaligned SSE support");
    __ align(CodeEntryAlignment);
    StubCodeMark mark(this, "StubRoutines", "aescrypt_decryptBlock");
    Label L_doLast;

    const Register from        = c_rarg0;  // source array address
    const Register to          = c_rarg1;  // destination array address
    const Register key         = c_rarg2;  // key array address
    const Register keylen      = rscratch1;

    address start = __ pc();
    __ enter(); // required for proper stackwalking of RuntimeStub frame

    __ ldrw(keylen, Address(key, arrayOopDesc::length_offset_in_bytes() - arrayOopDesc::base_offset_in_bytes(T_INT)));

    __ ld1(v0, __ T16B, from); // get 16 bytes of input

    __ ld1(v5, __ T16B, __ post(key, 16));
    __ rev32(v5, __ T16B, v5);

    __ ld1(v1, v2, v3, v4, __ T16B, __ post(key, 64));
    __ rev32(v1, __ T16B, v1);
    __ rev32(v2, __ T16B, v2);
    __ rev32(v3, __ T16B, v3);
    __ rev32(v4, __ T16B, v4);
    __ aesd(v0, v1);
    __ aesimc(v0, v0);
    __ aesd(v0, v2);
    __ aesimc(v0, v0);
    __ aesd(v0, v3);
    __ aesimc(v0, v0);
    __ aesd(v0, v4);
    __ aesimc(v0, v0);

    __ ld1(v1, v2, v3, v4, __ T16B, __ post(key, 64));
    __ rev32(v1, __ T16B, v1);
    __ rev32(v2, __ T16B, v2);
    __ rev32(v3, __ T16B, v3);
    __ rev32(v4, __ T16B, v4);
    __ aesd(v0, v1);
    __ aesimc(v0, v0);
    __ aesd(v0, v2);
    __ aesimc(v0, v0);
    __ aesd(v0, v3);
    __ aesimc(v0, v0);
    __ aesd(v0, v4);
    __ aesimc(v0, v0);

    __ ld1(v1, v2, __ T16B, __ post(key, 32));
    __ rev32(v1, __ T16B, v1);
    __ rev32(v2, __ T16B, v2);

    __ cmpw(keylen, 44);
    __ br(Assembler::EQ, L_doLast);

    __ aesd(v0, v1);
    __ aesimc(v0, v0);
    __ aesd(v0, v2);
    __ aesimc(v0, v0);

    __ ld1(v1, v2, __ T16B, __ post(key, 32));
    __ rev32(v1, __ T16B, v1);
    __ rev32(v2, __ T16B, v2);

    __ cmpw(keylen, 52);
    __ br(Assembler::EQ, L_doLast);

    __ aesd(v0, v1);
    __ aesimc(v0, v0);
    __ aesd(v0, v2);
    __ aesimc(v0, v0);

    __ ld1(v1, v2, __ T16B, __ post(key, 32));
    __ rev32(v1, __ T16B, v1);
    __ rev32(v2, __ T16B, v2);

    __ BIND(L_doLast);

    __ aesd(v0, v1);
    __ aesimc(v0, v0);
    __ aesd(v0, v2);

    __ eor(v0, __ T16B, v0, v5);

    __ st1(v0, __ T16B, to);

    __ mov(r0, 0);

    __ leave();
    __ ret(lr);

    return start;
  }

  // Arguments:
  //
  // Inputs:
  //   c_rarg0   - source byte array address
  //   c_rarg1   - destination byte array address
  //   c_rarg2   - K (key) in little endian int array
  //   c_rarg3   - r vector byte array address
  //   c_rarg4   - input length
  //
  // Output:
  //   x0        - input length
  //
  address generate_cipherBlockChaining_encryptAESCrypt() {
    assert(UseAES, "need AES instructions and misaligned SSE support");
    __ align(CodeEntryAlignment);
    StubCodeMark mark(this, "StubRoutines", "cipherBlockChaining_encryptAESCrypt");

    Label L_loadkeys_44, L_loadkeys_52, L_aes_loop, L_rounds_44, L_rounds_52;

    const Register from        = c_rarg0;  // source array address
    const Register to          = c_rarg1;  // destination array address
    const Register key         = c_rarg2;  // key array address
    const Register rvec        = c_rarg3;  // r byte array initialized from initvector array address
                                           // and left with the results of the last encryption block
    const Register len_reg     = c_rarg4;  // src len (must be multiple of blocksize 16)
    const Register keylen      = rscratch1;

    address start = __ pc();
      __ enter();

      __ mov(rscratch2, len_reg);
      __ ldrw(keylen, Address(key, arrayOopDesc::length_offset_in_bytes() - arrayOopDesc::base_offset_in_bytes(T_INT)));

      __ ld1(v0, __ T16B, rvec);

      __ cmpw(keylen, 52);
      __ br(Assembler::CC, L_loadkeys_44);
      __ br(Assembler::EQ, L_loadkeys_52);

      __ ld1(v17, v18, __ T16B, __ post(key, 32));
      __ rev32(v17, __ T16B, v17);
      __ rev32(v18, __ T16B, v18);
    __ BIND(L_loadkeys_52);
      __ ld1(v19, v20, __ T16B, __ post(key, 32));
      __ rev32(v19, __ T16B, v19);
      __ rev32(v20, __ T16B, v20);
    __ BIND(L_loadkeys_44);
      __ ld1(v21, v22, v23, v24, __ T16B, __ post(key, 64));
      __ rev32(v21, __ T16B, v21);
      __ rev32(v22, __ T16B, v22);
      __ rev32(v23, __ T16B, v23);
      __ rev32(v24, __ T16B, v24);
      __ ld1(v25, v26, v27, v28, __ T16B, __ post(key, 64));
      __ rev32(v25, __ T16B, v25);
      __ rev32(v26, __ T16B, v26);
      __ rev32(v27, __ T16B, v27);
      __ rev32(v28, __ T16B, v28);
      __ ld1(v29, v30, v31, __ T16B, key);
      __ rev32(v29, __ T16B, v29);
      __ rev32(v30, __ T16B, v30);
      __ rev32(v31, __ T16B, v31);

    __ BIND(L_aes_loop);
      __ ld1(v1, __ T16B, __ post(from, 16));
      __ eor(v0, __ T16B, v0, v1);

      __ br(Assembler::CC, L_rounds_44);
      __ br(Assembler::EQ, L_rounds_52);

      __ aese(v0, v17); __ aesmc(v0, v0);
      __ aese(v0, v18); __ aesmc(v0, v0);
    __ BIND(L_rounds_52);
      __ aese(v0, v19); __ aesmc(v0, v0);
      __ aese(v0, v20); __ aesmc(v0, v0);
    __ BIND(L_rounds_44);
      __ aese(v0, v21); __ aesmc(v0, v0);
      __ aese(v0, v22); __ aesmc(v0, v0);
      __ aese(v0, v23); __ aesmc(v0, v0);
      __ aese(v0, v24); __ aesmc(v0, v0);
      __ aese(v0, v25); __ aesmc(v0, v0);
      __ aese(v0, v26); __ aesmc(v0, v0);
      __ aese(v0, v27); __ aesmc(v0, v0);
      __ aese(v0, v28); __ aesmc(v0, v0);
      __ aese(v0, v29); __ aesmc(v0, v0);
      __ aese(v0, v30);
      __ eor(v0, __ T16B, v0, v31);

      __ st1(v0, __ T16B, __ post(to, 16));
      __ sub(len_reg, len_reg, 16);
      __ cbnz(len_reg, L_aes_loop);

      __ st1(v0, __ T16B, rvec);

      __ mov(r0, rscratch2);

      __ leave();
      __ ret(lr);

      return start;
  }

  // Arguments:
  //
  // Inputs:
  //   c_rarg0   - source byte array address
  //   c_rarg1   - destination byte array address
  //   c_rarg2   - K (key) in little endian int array
  //   c_rarg3   - r vector byte array address
  //   c_rarg4   - input length
  //
  // Output:
  //   rax       - input length
  //
  address generate_cipherBlockChaining_decryptAESCrypt() {
    assert(UseAES, "need AES instructions and misaligned SSE support");
    __ align(CodeEntryAlignment);
    StubCodeMark mark(this, "StubRoutines", "cipherBlockChaining_decryptAESCrypt");

    Label L_loadkeys_44, L_loadkeys_52, L_aes_loop, L_rounds_44, L_rounds_52;

    const Register from        = c_rarg0;  // source array address
    const Register to          = c_rarg1;  // destination array address
    const Register key         = c_rarg2;  // key array address
    const Register rvec        = c_rarg3;  // r byte array initialized from initvector array address
                                           // and left with the results of the last encryption block
    const Register len_reg     = c_rarg4;  // src len (must be multiple of blocksize 16)
    const Register keylen      = rscratch1;

    address start = __ pc();
      __ enter();

      __ mov(rscratch2, len_reg);
      __ ldrw(keylen, Address(key, arrayOopDesc::length_offset_in_bytes() - arrayOopDesc::base_offset_in_bytes(T_INT)));

      __ ld1(v2, __ T16B, rvec);

      __ ld1(v31, __ T16B, __ post(key, 16));
      __ rev32(v31, __ T16B, v31);

      __ cmpw(keylen, 52);
      __ br(Assembler::CC, L_loadkeys_44);
      __ br(Assembler::EQ, L_loadkeys_52);

      __ ld1(v17, v18, __ T16B, __ post(key, 32));
      __ rev32(v17, __ T16B, v17);
      __ rev32(v18, __ T16B, v18);
    __ BIND(L_loadkeys_52);
      __ ld1(v19, v20, __ T16B, __ post(key, 32));
      __ rev32(v19, __ T16B, v19);
      __ rev32(v20, __ T16B, v20);
    __ BIND(L_loadkeys_44);
      __ ld1(v21, v22, v23, v24, __ T16B, __ post(key, 64));
      __ rev32(v21, __ T16B, v21);
      __ rev32(v22, __ T16B, v22);
      __ rev32(v23, __ T16B, v23);
      __ rev32(v24, __ T16B, v24);
      __ ld1(v25, v26, v27, v28, __ T16B, __ post(key, 64));
      __ rev32(v25, __ T16B, v25);
      __ rev32(v26, __ T16B, v26);
      __ rev32(v27, __ T16B, v27);
      __ rev32(v28, __ T16B, v28);
      __ ld1(v29, v30, __ T16B, key);
      __ rev32(v29, __ T16B, v29);
      __ rev32(v30, __ T16B, v30);

    __ BIND(L_aes_loop);
      __ ld1(v0, __ T16B, __ post(from, 16));
      __ orr(v1, __ T16B, v0, v0);

      __ br(Assembler::CC, L_rounds_44);
      __ br(Assembler::EQ, L_rounds_52);

      __ aesd(v0, v17); __ aesimc(v0, v0);
      __ aesd(v0, v17); __ aesimc(v0, v0);
    __ BIND(L_rounds_52);
      __ aesd(v0, v19); __ aesimc(v0, v0);
      __ aesd(v0, v20); __ aesimc(v0, v0);
    __ BIND(L_rounds_44);
      __ aesd(v0, v21); __ aesimc(v0, v0);
      __ aesd(v0, v22); __ aesimc(v0, v0);
      __ aesd(v0, v23); __ aesimc(v0, v0);
      __ aesd(v0, v24); __ aesimc(v0, v0);
      __ aesd(v0, v25); __ aesimc(v0, v0);
      __ aesd(v0, v26); __ aesimc(v0, v0);
      __ aesd(v0, v27); __ aesimc(v0, v0);
      __ aesd(v0, v28); __ aesimc(v0, v0);
      __ aesd(v0, v29); __ aesimc(v0, v0);
      __ aesd(v0, v30);
      __ eor(v0, __ T16B, v0, v31);
      __ eor(v0, __ T16B, v0, v2);

      __ st1(v0, __ T16B, __ post(to, 16));
      __ orr(v2, __ T16B, v1, v1);

      __ sub(len_reg, len_reg, 16);
      __ cbnz(len_reg, L_aes_loop);

      __ st1(v2, __ T16B, rvec);

      __ mov(r0, rscratch2);

      __ leave();
      __ ret(lr);

    return start;
  }

  // Arguments:
  //
  // Inputs:
  //   c_rarg0   - byte[]  source+offset
  //   c_rarg1   - int[]   SHA.state
  //   c_rarg2   - int     offset
  //   c_rarg3   - int     limit
  //
  address generate_sha1_implCompress(bool multi_block, const char *name) {
    __ align(CodeEntryAlignment);
    StubCodeMark mark(this, "StubRoutines", name);
    address start = __ pc();

    Register buf   = c_rarg0;
    Register state = c_rarg1;
    Register ofs   = c_rarg2;
    Register limit = c_rarg3;

    Label keys;
    Label sha1_loop;

    // load the keys into v0..v3
    __ adr(rscratch1, keys);
    __ ld4r(v0, v1, v2, v3, __ T4S, Address(rscratch1));
    // load 5 words state into v6, v7
    __ ldrq(v6, Address(state, 0));
    __ ldrs(v7, Address(state, 16));


    __ BIND(sha1_loop);
    // load 64 bytes of data into v16..v19
    __ ld1(v16, v17, v18, v19, __ T4S, multi_block ? __ post(buf, 64) : buf);
    __ rev32(v16, __ T16B, v16);
    __ rev32(v17, __ T16B, v17);
    __ rev32(v18, __ T16B, v18);
    __ rev32(v19, __ T16B, v19);

    // do the sha1
    __ addv(v4, __ T4S, v16, v0);
    __ orr(v20, __ T16B, v6, v6);

    FloatRegister d0 = v16;
    FloatRegister d1 = v17;
    FloatRegister d2 = v18;
    FloatRegister d3 = v19;

    for (int round = 0; round < 20; round++) {
      FloatRegister tmp1 = (round & 1) ? v4 : v5;
      FloatRegister tmp2 = (round & 1) ? v21 : v22;
      FloatRegister tmp3 = round ? ((round & 1) ? v22 : v21) : v7;
      FloatRegister tmp4 = (round & 1) ? v5 : v4;
      FloatRegister key = (round < 4) ? v0 : ((round < 9) ? v1 : ((round < 14) ? v2 : v3));

      if (round < 16) __ sha1su0(d0, __ T4S, d1, d2);
      if (round < 19) __ addv(tmp1, __ T4S, d1, key);
      __ sha1h(tmp2, __ T4S, v20);
      if (round < 5)
        __ sha1c(v20, __ T4S, tmp3, tmp4);
      else if (round < 10 || round >= 15)
        __ sha1p(v20, __ T4S, tmp3, tmp4);
      else
        __ sha1m(v20, __ T4S, tmp3, tmp4);
      if (round < 16) __ sha1su1(d0, __ T4S, d3);

      tmp1 = d0; d0 = d1; d1 = d2; d2 = d3; d3 = tmp1;
    }

    __ addv(v7, __ T2S, v7, v21);
    __ addv(v6, __ T4S, v6, v20);

    if (multi_block) {
      __ add(ofs, ofs, 64);
      __ cmp(ofs, limit);
      __ br(Assembler::LE, sha1_loop);
      __ mov(c_rarg0, ofs); // return ofs
    }

    __ strq(v6, Address(state, 0));
    __ strs(v7, Address(state, 16));

    __ ret(lr);

    __ bind(keys);
    __ emit_int32(0x5a827999);
    __ emit_int32(0x6ed9eba1);
    __ emit_int32(0x8f1bbcdc);
    __ emit_int32(0xca62c1d6);

    return start;
  }


  // Arguments:
  //
  // Inputs:
  //   c_rarg0   - byte[]  source+offset
  //   c_rarg1   - int[]   SHA.state
  //   c_rarg2   - int     offset
  //   c_rarg3   - int     limit
  //
  address generate_sha256_implCompress(bool multi_block, const char *name) {
    static const uint32_t round_consts[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
      0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
      0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
      0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
      0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
      0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };
    __ align(CodeEntryAlignment);
    StubCodeMark mark(this, "StubRoutines", name);
    address start = __ pc();

    Register buf   = c_rarg0;
    Register state = c_rarg1;
    Register ofs   = c_rarg2;
    Register limit = c_rarg3;

    Label sha1_loop;

    __ stpd(v8, v9, __ pre(sp, -32));
    __ stpd(v10, v11, Address(sp, 16));

// dga == v0
// dgb == v1
// dg0 == v2
// dg1 == v3
// dg2 == v4
// t0 == v6
// t1 == v7

    // load 16 keys to v16..v31
    __ lea(rscratch1, ExternalAddress((address)round_consts));
    __ ld1(v16, v17, v18, v19, __ T4S, __ post(rscratch1, 64));
    __ ld1(v20, v21, v22, v23, __ T4S, __ post(rscratch1, 64));
    __ ld1(v24, v25, v26, v27, __ T4S, __ post(rscratch1, 64));
    __ ld1(v28, v29, v30, v31, __ T4S, rscratch1);

    // load 8 words (256 bits) state
    __ ldpq(v0, v1, state);

    __ BIND(sha1_loop);
    // load 64 bytes of data into v8..v11
    __ ld1(v8, v9, v10, v11, __ T4S, multi_block ? __ post(buf, 64) : buf);
    __ rev32(v8, __ T16B, v8);
    __ rev32(v9, __ T16B, v9);
    __ rev32(v10, __ T16B, v10);
    __ rev32(v11, __ T16B, v11);

    __ addv(v6, __ T4S, v8, v16);
    __ orr(v2, __ T16B, v0, v0);
    __ orr(v3, __ T16B, v1, v1);

    FloatRegister d0 = v8;
    FloatRegister d1 = v9;
    FloatRegister d2 = v10;
    FloatRegister d3 = v11;


    for (int round = 0; round < 16; round++) {
      FloatRegister tmp1 = (round & 1) ? v6 : v7;
      FloatRegister tmp2 = (round & 1) ? v7 : v6;
      FloatRegister tmp3 = (round & 1) ? v2 : v4;
      FloatRegister tmp4 = (round & 1) ? v4 : v2;

      if (round < 12) __ sha256su0(d0, __ T4S, d1);
       __ orr(v4, __ T16B, v2, v2);
      if (round < 15)
        __ addv(tmp1, __ T4S, d1, as_FloatRegister(round + 17));
      __ sha256h(v2, __ T4S, v3, tmp2);
      __ sha256h2(v3, __ T4S, v4, tmp2);
      if (round < 12) __ sha256su1(d0, __ T4S, d2, d3);

      tmp1 = d0; d0 = d1; d1 = d2; d2 = d3; d3 = tmp1;
    }

    __ addv(v0, __ T4S, v0, v2);
    __ addv(v1, __ T4S, v1, v3);

    if (multi_block) {
      __ add(ofs, ofs, 64);
      __ cmp(ofs, limit);
      __ br(Assembler::LE, sha1_loop);
      __ mov(c_rarg0, ofs); // return ofs
    }

    __ ldpd(v10, v11, Address(sp, 16));
    __ ldpd(v8, v9, __ post(sp, 32));

    __ stpq(v0, v1, state);

    __ ret(lr);

    return start;
  }

#ifndef BUILTIN_SIM
  // Safefetch stubs.
  void generate_safefetch(const char* name, int size, address* entry,
                          address* fault_pc, address* continuation_pc) {
    // safefetch signatures:
    //   int      SafeFetch32(int*      adr, int      errValue);
    //   intptr_t SafeFetchN (intptr_t* adr, intptr_t errValue);
    //
    // arguments:
    //   c_rarg0 = adr
    //   c_rarg1 = errValue
    //
    // result:
    //   PPC_RET  = *adr or errValue

    StubCodeMark mark(this, "StubRoutines", name);

    // Entry point, pc or function descriptor.
    *entry = __ pc();

    // Load *adr into c_rarg1, may fault.
    *fault_pc = __ pc();
    switch (size) {
      case 4:
        // int32_t
	__ ldrw(c_rarg1, Address(c_rarg0, 0));
        break;
      case 8:
        // int64_t
	__ ldr(c_rarg1, Address(c_rarg0, 0));
        break;
      default:
        ShouldNotReachHere();
    }

    // return errValue or *adr
    *continuation_pc = __ pc();
    __ mov(r0, c_rarg1);
    __ ret(lr);
  }
#endif

  /**
   *  Arguments:
   *
   * Inputs:
   *   c_rarg0   - int crc
   *   c_rarg1   - byte* buf
   *   c_rarg2   - int length
   *
   * Output:
   *       r0   - int crc result
   *
   * Preserves:
   *       r13
   *
   */
  address generate_updateBytesCRC32() {
    assert(UseCRC32Intrinsics, "what are we doing here?");

    __ align(CodeEntryAlignment);
    StubCodeMark mark(this, "StubRoutines", "updateBytesCRC32");

    address start = __ pc();

    const Register crc   = c_rarg0;  // crc
    const Register buf   = c_rarg1;  // source java byte array address
    const Register len   = c_rarg2;  // length
    const Register table0 = c_rarg3; // crc_table address
    const Register table1 = c_rarg4;
    const Register table2 = c_rarg5;
    const Register table3 = c_rarg6;
    const Register tmp3 = c_rarg7;

    BLOCK_COMMENT("Entry:");
    __ enter(); // required for proper stackwalking of RuntimeStub frame

    __ kernel_crc32(crc, buf, len,
              table0, table1, table2, table3, rscratch1, rscratch2, tmp3);

    __ leave(); // required for proper stackwalking of RuntimeStub frame
    __ ret(lr);

    return start;
  }

  /**
   *  Arguments:
   *
   *  Input:
   *    c_rarg0   - x address
   *    c_rarg1   - x length
   *    c_rarg2   - y address
   *    c_rarg3   - y lenth
   *    c_rarg4   - z address
   *    c_rarg5   - z length
   */
  address generate_multiplyToLen() {
    __ align(CodeEntryAlignment);
    StubCodeMark mark(this, "StubRoutines", "multiplyToLen");

    address start = __ pc();
    const Register x     = r0;
    const Register xlen  = r1;
    const Register y     = r2;
    const Register ylen  = r3;
    const Register z     = r4;
    const Register zlen  = r5;

    const Register tmp1  = r10;
    const Register tmp2  = r11;
    const Register tmp3  = r12;
    const Register tmp4  = r13;
    const Register tmp5  = r14;
    const Register tmp6  = r15;
    const Register tmp7  = r16;

    BLOCK_COMMENT("Entry:");
    __ enter(); // required for proper stackwalking of RuntimeStub frame
    __ multiply_to_len(x, xlen, y, ylen, z, zlen, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7);
    __ leave(); // required for proper stackwalking of RuntimeStub frame
    __ ret(lr);

    return start;
  }

  // Continuation point for throwing of implicit exceptions that are
  // not handled in the current activation. Fabricates an exception
  // oop and initiates normal exception dispatching in this
  // frame. Since we need to preserve callee-saved values (currently
  // only for C2, but done for C1 as well) we need a callee-saved oop
  // map and therefore have to make these stubs into RuntimeStubs
  // rather than BufferBlobs.  If the compiler needs all registers to
  // be preserved between the fault point and the exception handler
  // then it must assume responsibility for that in
  // AbstractCompiler::continuation_for_implicit_null_exception or
  // continuation_for_implicit_division_by_zero_exception. All other
  // implicit exceptions (e.g., NullPointerException or
  // AbstractMethodError on entry) are either at call sites or
  // otherwise assume that stack unwinding will be initiated, so
  // caller saved registers were assumed volatile in the compiler.

#undef __
#define __ masm->

  address generate_throw_exception(const char* name,
                                   address runtime_entry,
                                   Register arg1 = noreg,
                                   Register arg2 = noreg) {
    // Information about frame layout at time of blocking runtime call.
    // Note that we only have to preserve callee-saved registers since
    // the compilers are responsible for supplying a continuation point
    // if they expect all registers to be preserved.
    // n.b. aarch64 asserts that frame::arg_reg_save_area_bytes == 0
    enum layout {
      rfp_off = 0,
      rfp_off2,
      return_off,
      return_off2,
      framesize // inclusive of return address
    };

    int insts_size = 512;
    int locs_size  = 64;

    CodeBuffer code(name, insts_size, locs_size);
    OopMapSet* oop_maps  = new OopMapSet();
    MacroAssembler* masm = new MacroAssembler(&code);

    address start = __ pc();

    // This is an inlined and slightly modified version of call_VM
    // which has the ability to fetch the return PC out of
    // thread-local storage and also sets up last_Java_sp slightly
    // differently than the real call_VM

    __ enter(); // Save FP and LR before call

    assert(is_even(framesize/2), "sp not 16-byte aligned");

    // lr and fp are already in place
    __ sub(sp, rfp, ((unsigned)framesize-4) << LogBytesPerInt); // prolog

    int frame_complete = __ pc() - start;

    // Set up last_Java_sp and last_Java_fp
    address the_pc = __ pc();
    __ set_last_Java_frame(sp, rfp, (address)NULL, rscratch1);

    // Call runtime
    if (arg1 != noreg) {
      assert(arg2 != c_rarg1, "clobbered");
      __ mov(c_rarg1, arg1);
    }
    if (arg2 != noreg) {
      __ mov(c_rarg2, arg2);
    }
    __ mov(c_rarg0, rthread);
    BLOCK_COMMENT("call runtime_entry");
    __ mov(rscratch1, runtime_entry);
    __ blrt(rscratch1, 3 /* number_of_arguments */, 0, 1);

    // Generate oop map
    OopMap* map = new OopMap(framesize, 0);

    oop_maps->add_gc_map(the_pc - start, map);

    __ reset_last_Java_frame(true, true);
    __ maybe_isb();

    __ leave();

    // check for pending exceptions
#ifdef ASSERT
    Label L;
    __ ldr(rscratch1, Address(rthread, Thread::pending_exception_offset()));
    __ cbnz(rscratch1, L);
    __ should_not_reach_here();
    __ bind(L);
#endif // ASSERT
    __ b(RuntimeAddress(StubRoutines::forward_exception_entry()));


    // codeBlob framesize is in words (not VMRegImpl::slot_size)
    RuntimeStub* stub =
      RuntimeStub::new_runtime_stub(name,
                                    &code,
                                    frame_complete,
                                    (framesize >> (LogBytesPerWord - LogBytesPerInt)),
                                    oop_maps, false);
    return stub->entry_point();
  }

  // Initialization
  void generate_initial() {
    // Generate initial stubs and initializes the entry points

    // entry points that exist in all platforms Note: This is code
    // that could be shared among different platforms - however the
    // benefit seems to be smaller than the disadvantage of having a
    // much more complicated generator structure. See also comment in
    // stubRoutines.hpp.

    StubRoutines::_forward_exception_entry = generate_forward_exception();

    StubRoutines::_call_stub_entry =
      generate_call_stub(StubRoutines::_call_stub_return_address);

    // is referenced by megamorphic call
    StubRoutines::_catch_exception_entry = generate_catch_exception();

    // Build this early so it's available for the interpreter.
    StubRoutines::_throw_StackOverflowError_entry =
      generate_throw_exception("StackOverflowError throw_exception",
                               CAST_FROM_FN_PTR(address,
                                                SharedRuntime::
                                                throw_StackOverflowError));
    if (UseCRC32Intrinsics) {
      // set table address before stub generation which use it
      StubRoutines::_crc_table_adr = (address)StubRoutines::aarch64::_crc_table;
      StubRoutines::_updateBytesCRC32 = generate_updateBytesCRC32();
    }
  }

  void generate_all() {
    // support for verify_oop (must happen after universe_init)
    StubRoutines::_verify_oop_subroutine_entry     = generate_verify_oop();
    StubRoutines::_throw_AbstractMethodError_entry =
      generate_throw_exception("AbstractMethodError throw_exception",
                               CAST_FROM_FN_PTR(address,
                                                SharedRuntime::
                                                throw_AbstractMethodError));

    StubRoutines::_throw_IncompatibleClassChangeError_entry =
      generate_throw_exception("IncompatibleClassChangeError throw_exception",
                               CAST_FROM_FN_PTR(address,
                                                SharedRuntime::
                                                throw_IncompatibleClassChangeError));

    StubRoutines::_throw_NullPointerException_at_call_entry =
      generate_throw_exception("NullPointerException at call throw_exception",
                               CAST_FROM_FN_PTR(address,
                                                SharedRuntime::
                                                throw_NullPointerException_at_call));

    // arraycopy stubs used by compilers
    generate_arraycopy_stubs();

    if (UseMultiplyToLenIntrinsic) {
      StubRoutines::_multiplyToLen = generate_multiplyToLen();
    }

#ifndef BUILTIN_SIM
    if (UseAESIntrinsics) {
      StubRoutines::_aescrypt_encryptBlock = generate_aescrypt_encryptBlock();
      StubRoutines::_aescrypt_decryptBlock = generate_aescrypt_decryptBlock();
      StubRoutines::_cipherBlockChaining_encryptAESCrypt = generate_cipherBlockChaining_encryptAESCrypt();
      StubRoutines::_cipherBlockChaining_decryptAESCrypt = generate_cipherBlockChaining_decryptAESCrypt();
    }

    if (UseSHA1Intrinsics) {
      StubRoutines::_sha1_implCompress     = generate_sha1_implCompress(false,   "sha1_implCompress");
      StubRoutines::_sha1_implCompressMB   = generate_sha1_implCompress(true,    "sha1_implCompressMB");
    }
    if (UseSHA256Intrinsics) {
      StubRoutines::_sha256_implCompress   = generate_sha256_implCompress(false, "sha256_implCompress");
      StubRoutines::_sha256_implCompressMB = generate_sha256_implCompress(true,  "sha256_implCompressMB");
    }

    // Safefetch stubs.
    generate_safefetch("SafeFetch32", sizeof(int),     &StubRoutines::_safefetch32_entry,
                                                       &StubRoutines::_safefetch32_fault_pc,
                                                       &StubRoutines::_safefetch32_continuation_pc);
    generate_safefetch("SafeFetchN", sizeof(intptr_t), &StubRoutines::_safefetchN_entry,
                                                       &StubRoutines::_safefetchN_fault_pc,
                                                       &StubRoutines::_safefetchN_continuation_pc);
#endif
  }

 public:
  StubGenerator(CodeBuffer* code, bool all) : StubCodeGenerator(code) {
    if (all) {
      generate_all();
    } else {
      generate_initial();
    }
  }
}; // end class declaration

void StubGenerator_generate(CodeBuffer* code, bool all) {
  StubGenerator g(code, all);
}
