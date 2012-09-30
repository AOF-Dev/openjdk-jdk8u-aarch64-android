/*
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
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
#include "asm/assembler.hpp"
#include "assembler_aarch64.inline.hpp"
#include "interpreter/interpreter.hpp"
#include "nativeInst_aarch64.hpp"
#include "oops/instanceOop.hpp"
#include "oops/methodOop.hpp"
#include "oops/objArrayKlass.hpp"
#include "oops/oop.inline.hpp"
#include "prims/methodHandles.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubCodeGenerator.hpp"
#include "runtime/stubRoutines.hpp"
#include "utilities/top.hpp"
#ifdef TARGET_OS_FAMILY_linux
# include "thread_linux.inline.hpp"
#endif
#ifdef TARGET_OS_FAMILY_solaris
# include "thread_solaris.inline.hpp"
#endif
#ifdef TARGET_OS_FAMILY_windows
# include "thread_windows.inline.hpp"
#endif
#ifdef TARGET_OS_FAMILY_bsd
# include "thread_bsd.inline.hpp"
#endif
#ifdef COMPILER2
#include "opto/runtime.hpp"
#endif

// Declaration and definition of StubGenerator (no .hpp file).
// For a more detailed description of the stub routine structure
// see the comment in stubRoutines.hpp

#define __ _masm->
#define TIMES_OOP (UseCompressedOops ? Address::times_4 : Address::times_8)
#define a__ ((Assembler*)_masm)->

#ifdef PRODUCT
#define BLOCK_COMMENT(str) /* nothing */
#else
#define BLOCK_COMMENT(str) __ block_comment(str)
#endif

#define BIND(label) bind(label); BLOCK_COMMENT(#label ":")

// Stub Code definitions

static address handle_unsafe_access() { Unimplemented(); return 0; }

class StubGenerator: public StubCodeGenerator {
 private:

#ifdef PRODUCT
#define inc_counter_np(counter) (0)
#else
  void inc_counter_np_(int& counter) { Unimplemented(); }
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
  //    c_rarg3:   method                                 methodOop
  //    c_rarg4:   (interpreter) entry point              address
  //    c_rarg5:   parameters                             intptr_t*
  //    c_rarg6:   parameter size (in words)              int
  //    c_rarg7:   thread                                 Thread*
  //
  // There is no return form the stub itself as any Java result
  // is written to result
  //
  // we save r30 (lr)a as the return PC at the base of the frame and
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
  // -19 [ argument word 1      ]
  // -18 [ saved r28            ] <--- sp_after_call
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
    sp_after_call_off = -18,
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

    // we need a C prolog to bootstrap teh x86 caller into the sim

    __ c_stub_prolog(8, 0, MacroAssembler::ret_type_void);

    address aarch64_entry = __ pc();

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
    BLOCK_COMMENT("pass parameters if any");
    Label parameters_done;
    // parameter count is still in c_rarg6
    // and parameter pointer identifying param 1 is in c_rarg5
    __ cmpw(c_rarg6, 0U);
    __ br(Assembler::EQ, parameters_done);

    address loop = __ pc();
    __ ldr(rscratch1, Address(__ post(c_rarg5, wordSize)));
    __ subsw(c_rarg6, c_rarg6, 1);
    __ push(rscratch1);
    __ br(Assembler::GT, loop);

    __ BIND(parameters_done);

    // call Java entry -- passing methdoOop, and current sp
    //      rmethod: methodOop
    //      r10: sender sp
    __ mov(r10, sp);
    BLOCK_COMMENT("call Java function");
    __ blr(c_rarg4);

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
    __ sub(sp, rfp, -sp_after_call_off * wordSize);

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
  // rax: exception oop

  // NOTE: this is used as a target from the signal handler so it
  // needs an x86 prolog which returns into the current simulator
  // executing the generated catch_exception code. so the prolog
  // needs to install rax in a sim register and adjust the sim's
  // restart pc to enter the generated code at the start position
  // then return from native to simulated execution.

  address generate_catch_exception() { return 0; }

  // Continuation point for runtime calls returning with a pending
  // exception.  The pending exception check happened in the runtime
  // or native call stub.  The pending exception in Thread is
  // converted into a Java-level exception.
  //
  // Contract with Java-level exception handlers:
  // rax: exception
  // rdx: throwing pc
  //
  // NOTE: At entry of this stub, exception-pc must be on stack !!

  // NOTE: this is always used as a jump target within generated code
  // so it just needs to be generated code wiht no x86 prolog

  address generate_forward_exception() {
    StubCodeMark mark(this, "StubRoutines", "forward exception");
    address start = __ pc();

    // Upon entry, the sp points to the return address returning into
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
    __ mov(c_rarg1, lr);
    BLOCK_COMMENT("call exception_handler_for_return_address");
    __ call_VM_leaf(CAST_FROM_FN_PTR(address,
                         SharedRuntime::exception_handler_for_return_address),
                    rthread, c_rarg1);
    __ mov(r19, r0);

    // setup r0 & r3, remove return address & clear pending exception
    __ mov(r3, lr);
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

    // continue at exception handler (return address removed)
    // r0: exception
    // r19: exception handler
    // r3: throwing pc
    __ verify_oop(r0);
    __ br(r19);

    return start;
  }

  // Support for jint atomic::xchg(jint exchange_value, volatile jint* dest)
  //
  // Arguments :
  //    c_rarg0: exchange_value
  //    c_rarg0: dest
  //
  // Result:
  //    *dest <- ex, return (orig *dest)

  // NOTE: not sure this is actually needed but if so it looks like it
  // is called from os-specific code i.e. it needs an x86 prolog

  address generate_atomic_xchg() { return 0; }

  // Support for intptr_t atomic::xchg_ptr(intptr_t exchange_value, volatile intptr_t* dest)
  //
  // Arguments :
  //    c_rarg0: exchange_value
  //    c_rarg1: dest
  //
  // Result:
  //    *dest <- ex, return (orig *dest)

  // NOTE: not sure this is actually needed but if so it looks like it
  // is called from os-specific code i.e. it needs an x86 prolog

  address generate_atomic_xchg_ptr() { return 0; }

  // Support for jint atomic::atomic_cmpxchg(jint exchange_value, volatile jint* dest,
  //                                         jint compare_value)
  //
  // Arguments :
  //    c_rarg0: exchange_value
  //    c_rarg1: dest
  //    c_rarg2: compare_value
  //
  // Result:
  //    if ( compare_value == *dest ) {
  //       *dest = exchange_value
  //       return compare_value;
  //    else
  //       return *dest;
  address generate_atomic_cmpxchg() { return 0; }

  // Support for jint atomic::atomic_cmpxchg_long(jlong exchange_value,
  //                                             volatile jlong* dest,
  //                                             jlong compare_value)
  // Arguments :
  //    c_rarg0: exchange_value
  //    c_rarg1: dest
  //    c_rarg2: compare_value
  //
  // Result:
  //    if ( compare_value == *dest ) {
  //       *dest = exchange_value
  //       return compare_value;
  //    else
  //       return *dest;

  // NOTE: not sure this is actually needed but if so it looks like it
  // is called from os-specific code i.e. it needs an x86 prolog

  address generate_atomic_cmpxchg_long() { return 0; }

  // Support for jint atomic::add(jint add_value, volatile jint* dest)
  //
  // Arguments :
  //    c_rarg0: add_value
  //    c_rarg1: dest
  //
  // Result:
  //    *dest += add_value
  //    return *dest;

  // NOTE: not sure this is actually needed but if so it looks like it
  // is called from os-specific code i.e. it needs an x86 prolog

  address generate_atomic_add() { return 0; }

  // Support for intptr_t atomic::add_ptr(intptr_t add_value, volatile intptr_t* dest)
  //
  // Arguments :
  //    c_rarg0: add_value
  //    c_rarg1: dest
  //
  // Result:
  //    *dest += add_value
  //    return *dest;

  // NOTE: not sure this is actually needed but if so it looks like it
  // is called from os-specific code i.e. it needs an x86 prolog

  address generate_atomic_add_ptr() { return 0; }

  // Support for intptr_t OrderAccess::fence()
  //
  // Arguments :
  //
  // Result:

  // NOTE: this is called from C code so it needs an x86 prolog
  // or else we need to fiddle it with inline asm for now

  address generate_orderaccess_fence() { return 0; }

  // Support for intptr_t get_previous_fp()
  //
  // This routine is used to find the previous frame pointer for the
  // caller (current_frame_guess). This is used as part of debugging
  // ps() is seemingly lost trying to find frames.
  // This code assumes that caller current_frame_guess) has a frame.

  // NOTE: this is called from C code in os_windows.cpp with AMD64. other
  // builds use inline asm -- so we should be ok for aarch64

  address generate_get_previous_fp() { return 0; }

  // Support for intptr_t get_previous_sp()
  //
  // This routine is used to find the previous stack pointer for the
  // caller.

  // NOTE: this is called from C code in os_windows.cpp with AMD64. other
  // builds use inline asm -- so we should be ok for aarch64

  address generate_get_previous_sp() { return 0; }

  //----------------------------------------------------------------------------------------------------
  // Support for void verify_mxcsr()
  //
  // This routine is used with -Xcheck:jni to verify that native
  // JNI code does not return to Java code without restoring the
  // MXCSR register to our expected state.

  // NOTE: on x86 this is called from the cpp and template
  // interpreters and internally from the call stub -- we can probbaly
  // do without any equivalent for aarch64 for now at least

  address generate_verify_mxcsr() { Unimplemented(); return 0; }

  // NOTE: these fixup routines appear only to be called from the
  // opto code (they are mentioned in x86_64.ad) so we can do
  // without them for now on aarch64

  address generate_f2i_fixup() { Unimplemented(); return 0; }

  address generate_f2l_fixup() { Unimplemented(); return 0; }

  address generate_d2i_fixup() { Unimplemented(); return 0; }

  address generate_d2l_fixup() { Unimplemented(); return 0; }

  // NOTE: this appears only to be used internal to the x86 call stub
  // to support the mxcsr code so we can do without it for now on aarch64

  address generate_fp_mask(const char *stub_name, int64_t mask) { Unimplemented(); return 0; }

  // The following routine generates a subroutine to throw an
  // asynchronous UnknownError when an unsafe access gets a fault that
  // could not be reasonably prevented by the programmer.  (Example:
  // SIGBUS/OBJERR.)

  // NOTE: this is used by the signal handler code as a return address
  // to re-enter Java execution so it needs an x86 prolog which will
  // reenter the simulator executing the generated handler code. so
  // the prolog needs to adjust the sim's restart pc to enter the
  // generated code at the start position then return from native to
  // simulated execution.

  address generate_handler_for_unsafe_access() { return 0; }

  // Non-destructive plausibility checks for oops
  //
  // Arguments:
  //    all args on stack!
  //
  // Stack after saving c_rarg3:
  //    [tos + 0]: saved c_rarg3
  //    [tos + 1]: saved c_rarg2
  //    [tos + 2]: saved r12 (several TemplateTable methods use it)
  //    [tos + 3]: saved flags
  //    [tos + 4]: return address
  //  * [tos + 5]: error message (char*)
  //  * [tos + 6]: object to verify (oop)
  //  * [tos + 7]: saved rax - saved by caller and bashed
  //  * [tos + 8]: saved r10 (rscratch1) - saved by caller
  //  * = popped on exit

  // NOTE: this is called from within Java code so it needs no prolog

  address generate_verify_oop() { Unimplemented(); return 0; }

  //
  // Verify that a register contains clean 32-bits positive value
  // (high 32-bits are 0) so it could be used in 64-bits shifts.
  //
  //  Input:
  //    Rint  -  32-bits value
  //    Rtmp  -  scratch
  //
  void assert_clean_int(Register Rint, Register Rtmp) { Unimplemented(); }

  //  Generate overlap test for array copy stubs
  //
  //  Input:
  //     c_rarg0 - from
  //     c_rarg1 - to
  //     c_rarg2 - element count
  //
  //  Output:
  //     rax   - &from[element count - 1]
  //
  void array_overlap_test(address no_overlap_target, Address::ScaleFactor sf) { Unimplemented(); }
  void array_overlap_test(Label& L_no_overlap, Address::ScaleFactor sf) { Unimplemented(); }
  void array_overlap_test(address no_overlap_target, Label* NOLp, Address::ScaleFactor sf) { Unimplemented(); }

  // Shuffle first three arg regs on Windows into Linux/Solaris locations.
  //
  // Outputs:
  //    rdi - rcx
  //    rsi - rdx
  //    rdx - r8
  //    rcx - r9
  //
  // Registers r9 and r10 are used to save rdi and rsi on Windows, which latter
  // are non-volatile.  r9 and r10 should not be used by the caller.
  //
  void setup_arg_regs(int nargs = 3) { Unimplemented(); }

  void restore_arg_regs() { Unimplemented(); }

  // Generate code for an array write pre barrier
  //
  //     addr    -  starting address
  //     count   -  element count
  //     tmp     - scratch register
  //
  //     Destroy no registers!
  //
  void  gen_write_ref_array_pre_barrier(Register addr, Register count, bool dest_uninitialized) { Unimplemented(); }

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
  void  gen_write_ref_array_post_barrier(Register start, Register end, Register scratch) { Unimplemented(); }


  // Copy big chunks forward
  //
  // Inputs:
  //   end_from     - source arrays end address
  //   end_to       - destination array end address
  //   qword_count  - 64-bits element count, negative
  //   to           - scratch
  //   L_copy_32_bytes - entry label
  //   L_copy_8_bytes  - exit  label
  //
  void copy_32_bytes_forward(Register end_from, Register end_to,
                             Register qword_count, Register to,
                             Label& L_copy_32_bytes, Label& L_copy_8_bytes) { Unimplemented(); }


  // Copy big chunks backward
  //
  // Inputs:
  //   from         - source arrays address
  //   dest         - destination array address
  //   qword_count  - 64-bits element count
  //   to           - scratch
  //   L_copy_32_bytes - entry label
  //   L_copy_8_bytes  - exit  label
  //
  void copy_32_bytes_backward(Register from, Register dest,
                              Register qword_count, Register to,
                              Label& L_copy_32_bytes, Label& L_copy_8_bytes) { Unimplemented(); }


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
  //   disjoint_byte_copy_entry is set to the no-overlap entry point
  //   used by generate_conjoint_byte_copy().
  //
  address generate_disjoint_byte_copy(bool aligned, address* entry, const char *name) { Unimplemented(); return 0; }

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
                                      address* entry, const char *name) { Unimplemented(); return 0; }

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
  address generate_disjoint_short_copy(bool aligned, address *entry, const char *name) { Unimplemented(); return 0; }

  address generate_fill(BasicType t, bool aligned, const char *name) { Unimplemented(); return 0; }

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
                                       address *entry, const char *name) { Unimplemented(); return 0; }

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
  address generate_disjoint_int_oop_copy(bool aligned, bool is_oop, address* entry,
                                         const char *name, bool dest_uninitialized = false) { Unimplemented(); return 0; }

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
  address generate_conjoint_int_oop_copy(bool aligned, bool is_oop, address nooverlap_target,
                                         address *entry, const char *name,
                                         bool dest_uninitialized = false) { Unimplemented(); return 0; }

  // Arguments:
  //   aligned - true => Input and output aligned on a HeapWord boundary == 8 bytes
  //             ignored
  //   is_oop  - true => oop array, so generate store check code
  //   name    - stub name string
  //
  // Inputs:
  //   c_rarg0   - source array address
  //   c_rarg1   - destination array address
  //   c_rarg2   - element count, treated as ssize_t, can be zero
  //
 // Side Effects:
  //   disjoint_oop_copy_entry or disjoint_long_copy_entry is set to the
  //   no-overlap entry point used by generate_conjoint_long_oop_copy().
  //
  address generate_disjoint_long_oop_copy(bool aligned, bool is_oop, address *entry,
                                          const char *name, bool dest_uninitialized = false) { Unimplemented(); return 0; }

  // Arguments:
  //   aligned - true => Input and output aligned on a HeapWord boundary == 8 bytes
  //             ignored
  //   is_oop  - true => oop array, so generate store check code
  //   name    - stub name string
  //
  // Inputs:
  //   c_rarg0   - source array address
  //   c_rarg1   - destination array address
  //   c_rarg2   - element count, treated as ssize_t, can be zero
  //
  address generate_conjoint_long_oop_copy(bool aligned, bool is_oop,
                                          address nooverlap_target, address *entry,
                                          const char *name, bool dest_uninitialized = false) { Unimplemented(); return 0; }


  // Helper for generating a dynamic type check.
  // Smashes no registers.
  void generate_type_check(Register sub_klass,
                           Register super_check_offset,
                           Register super_klass,
                           Label& L_success) { Unimplemented(); }

  //
  //  Generate checkcasting array copy stub
  //
  //  Input:
  //    c_rarg0   - source array address
  //    c_rarg1   - destination array address
  //    c_rarg2   - element count, treated as ssize_t, can be zero
  //    c_rarg3   - size_t ckoff (super_check_offset)
  // not Win64
  //    c_rarg4   - oop ckval (super_klass)
  // Win64
  //    rsp+40    - oop ckval (super_klass)
  //
  //  Output:
  //    rax ==  0  -  success
  //    rax == -1^K - failure, where K is partial transfer count
  //
  address generate_checkcast_copy(const char *name, address *entry,
                                  bool dest_uninitialized = false) { Unimplemented(); return 0; }

  //
  //  Generate 'unsafe' array copy stub
  //  Though just as safe as the other stubs, it takes an unscaled
  //  size_t argument instead of an element count.
  //
  //  Input:
  //    c_rarg0   - source array address
  //    c_rarg1   - destination array address
  //    c_rarg2   - byte count, treated as ssize_t, can be zero
  //
  // Examines the alignment of the operands and dispatches
  // to a long, int, short, or byte copy loop.
  //
  address generate_unsafe_copy(const char *name,
                               address byte_copy_entry, address short_copy_entry,
                               address int_copy_entry, address long_copy_entry) { Unimplemented(); return 0; }

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

  //
  //  Generate generic array copy stubs
  //
  //  Input:
  //    c_rarg0    -  src oop
  //    c_rarg1    -  src_pos (32-bits)
  //    c_rarg2    -  dst oop
  //    c_rarg3    -  dst_pos (32-bits)
  // not Win64
  //    c_rarg4    -  element count (32-bits)
  // Win64
  //    rsp+40     -  element count (32-bits)
  //
  //  Output:
  //    rax ==  0  -  success
  //    rax == -1^K - failure, where K is partial transfer count
  //
  address generate_generic_copy(const char *name,
                                address byte_copy_entry, address short_copy_entry,
                                address int_copy_entry, address oop_copy_entry,
                                address long_copy_entry, address checkcast_copy_entry) { Unimplemented(); return 0; }

  void generate_arraycopy_stubs() { Unimplemented(); }

  void generate_math_stubs() { Unimplemented(); }

#undef __
#define __ masm->

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

  // NOTE: this needs carefully checking to see where the generated
  // code gets called from for each generated error
  //
  // WrongMethodTypeException : jumped to directly from generated method
  // handle code.
  //
  // StackOverflowError : jumped to directly from generated code in
  // cpp and template interpreter. the generated code address also
  // appears to be returned from the signal handler as the re-entry
  // address forJava execution to continue from. This means it needs
  // to be enterable from x86 code. Hmm, we may need to expose both an
  // x86 prolog and the address of the generated ARM code and clients
  // will have to be mdoified to pick the correct one.
  //
  // AbstractMethodError : never jumped to from generated code but the
  // generated code address appears to be returned from the signal
  // handler as the re-entry address for Java execution to continue
  // from. This means it needs to be enterable from x86 code. So, we
  // will need to provide this one with an x86 prolog as per
  // StackOverflowError
  //
  // IncompatibleClassChangeError : only appears to be jumped to
  // directly from vtableStubs code
  //
  // NullPointerException : never jumped to from generated code but
  // the generated code address appears to be returned from the signal
  // handler as the re-entry address for Java execution to continue
  // from. This means it needs to be enterable from x86 code. So, we
  // will need to provide this one with an x86 prolog as per
  // StackOverflowError


  address generate_throw_exception(const char* name,
                                   address runtime_entry,
                                   Register arg1 = noreg,
                                   Register arg2 = noreg) { return 0; }

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

    // atomic calls
    StubRoutines::_atomic_xchg_entry         = generate_atomic_xchg();
    StubRoutines::_atomic_xchg_ptr_entry     = generate_atomic_xchg_ptr();
    StubRoutines::_atomic_cmpxchg_entry      = generate_atomic_cmpxchg();
    StubRoutines::_atomic_cmpxchg_long_entry = generate_atomic_cmpxchg_long();
    StubRoutines::_atomic_add_entry          = generate_atomic_add();
    StubRoutines::_atomic_add_ptr_entry      = generate_atomic_add_ptr();
    StubRoutines::_fence_entry               = generate_orderaccess_fence();

    StubRoutines::_handler_for_unsafe_access_entry =
      generate_handler_for_unsafe_access();

    // platform dependent
    StubRoutines::x86::_get_previous_fp_entry = generate_get_previous_fp();
    StubRoutines::x86::_get_previous_sp_entry = generate_get_previous_sp();

    // we don't need this or aarch64

    // StubRoutines::x86::_verify_mxcsr_entry    = generate_verify_mxcsr();

    // Build this early so it's available for the interpreter.  Stub
    // expects the required and actual types as register arguments in
    // j_rarg0 and j_rarg1 respectively.
    StubRoutines::_throw_WrongMethodTypeException_entry =
      generate_throw_exception("WrongMethodTypeException throw_exception",
                               CAST_FROM_FN_PTR(address, SharedRuntime::throw_WrongMethodTypeException),
                               j_rarg0, j_rarg1);

    // Build this early so it's available for the interpreter.
    StubRoutines::_throw_StackOverflowError_entry =
      generate_throw_exception("StackOverflowError throw_exception",
                               CAST_FROM_FN_PTR(address,
                                                SharedRuntime::
                                                throw_StackOverflowError));
  }
    
  void generate_all() {  }

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
