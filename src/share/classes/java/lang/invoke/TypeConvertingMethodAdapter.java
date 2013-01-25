/*
 * Copyright (c) 2012, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
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
 */

package java.lang.invoke;

import jdk.internal.org.objectweb.asm.MethodVisitor;
import jdk.internal.org.objectweb.asm.Opcodes;
import sun.invoke.util.Wrapper;
import static sun.invoke.util.Wrapper.*;

class TypeConvertingMethodAdapter extends MethodVisitor {

    TypeConvertingMethodAdapter(MethodVisitor mv) {
        super(Opcodes.ASM4, mv);
    }

    private static final int NUM_WRAPPERS = Wrapper.values().length;

    private static final String NAME_OBJECT = "java/lang/Object";
    private static final String WRAPPER_PREFIX = "Ljava/lang/";

    // Same for all primitives; name of the boxing method
    private static final String NAME_BOX_METHOD = "valueOf";

    // Table of opcodes for widening primitive conversions; NOP = no conversion
    private static final int[][] wideningOpcodes = new int[NUM_WRAPPERS][NUM_WRAPPERS];

    private static final Wrapper[] FROM_WRAPPER_NAME = new Wrapper[16];

    static {
        for (Wrapper w : Wrapper.values()) {
            if (w.basicTypeChar() != 'L') {
                int wi = hashWrapperName(w.wrapperSimpleName());
                assert (FROM_WRAPPER_NAME[wi] == null);
                FROM_WRAPPER_NAME[wi] = w;
            }
        }

        for (int i = 0; i < NUM_WRAPPERS; i++) {
            for (int j = 0; j < NUM_WRAPPERS; j++) {
                wideningOpcodes[i][j] = Opcodes.NOP;
            }
        }

        initWidening(LONG,   Opcodes.I2L, BYTE, SHORT, INT, CHAR);
        initWidening(LONG,   Opcodes.F2L, FLOAT);
        initWidening(FLOAT,  Opcodes.I2F, BYTE, SHORT, INT, CHAR);
        initWidening(FLOAT,  Opcodes.L2F, LONG);
        initWidening(DOUBLE, Opcodes.I2D, BYTE, SHORT, INT, CHAR);
        initWidening(DOUBLE, Opcodes.F2D, FLOAT);
        initWidening(DOUBLE, Opcodes.L2D, LONG);
    }

    private static void initWidening(Wrapper to, int opcode, Wrapper... from) {
        for (Wrapper f : from) {
            wideningOpcodes[f.ordinal()][to.ordinal()] = opcode;
        }
    }

    /**
     * Class name to Wrapper hash, derived from Wrapper.hashWrap()
     * @param xn
     * @return The hash code 0-15
     */
    private static int hashWrapperName(String xn) {
        if (xn.length() < 3) {
            return 0;
        }
        return (3 * xn.charAt(1) + xn.charAt(2)) % 16;
    }

    private Wrapper wrapperOrNullFromDescriptor(String desc) {
        if (!desc.startsWith(WRAPPER_PREFIX)) {
            // Not a class type (array or method), so not a boxed type
            // or not in the right package
            return null;
        }
        // Pare it down to the simple class name
        String cname = desc.substring(WRAPPER_PREFIX.length(), desc.length() - 1);
        // Hash to a Wrapper
        Wrapper w = FROM_WRAPPER_NAME[hashWrapperName(cname)];
        if (w == null || w.wrapperSimpleName().equals(cname)) {
            return w;
        } else {
            return null;
        }
    }

    private static String wrapperName(Wrapper w) {
        return "java/lang/" + w.wrapperSimpleName();
    }

    private static String unboxMethod(Wrapper w) {
        return w.primitiveSimpleName() + "Value";
    }

    private static String boxingDescriptor(Wrapper w) {
        return String.format("(%s)L%s;", w.basicTypeChar(), wrapperName(w));
    }

    private static String unboxingDescriptor(Wrapper w) {
        return "()" + w.basicTypeChar();
    }

    void boxIfPrimitive(Wrapper w) {
        if (w.zero() != null) {
            box(w);
        }
    }

    void widen(Wrapper ws, Wrapper wt) {
        if (ws != wt) {
            int opcode = wideningOpcodes[ws.ordinal()][wt.ordinal()];
            if (opcode != Opcodes.NOP) {
                visitInsn(opcode);
            }
        }
    }

    void box(Wrapper w) {
        visitMethodInsn(Opcodes.INVOKESTATIC,
                wrapperName(w),
                NAME_BOX_METHOD,
                boxingDescriptor(w));
    }

    /**
     * Convert types by unboxing. The source type is known to be a primitive wrapper.
     * @param ws A primitive wrapper corresponding to wrapped reference source type
     * @param wt A primitive wrapper being converted to
     */
    void unbox(String sname, Wrapper wt) {
        visitMethodInsn(Opcodes.INVOKEVIRTUAL,
                sname,
                unboxMethod(wt),
                unboxingDescriptor(wt));
    }

    private String descriptorToName(String desc) {
        int last = desc.length() - 1;
        if (desc.charAt(0) == 'L' && desc.charAt(last) == ';') {
            // In descriptor form
            return desc.substring(1, last);
        } else {
            // Already in internal name form
            return desc;
        }
    }

    void cast(String ds, String dt) {
        String ns = descriptorToName(ds);
        String nt = descriptorToName(dt);
        if (!nt.equals(ns) && !nt.equals(NAME_OBJECT)) {
            visitTypeInsn(Opcodes.CHECKCAST, nt);
        }
    }

    private boolean isPrimitive(Wrapper w) {
        return w != OBJECT;
    }

    private Wrapper toWrapper(String desc) {
        char first = desc.charAt(0);
        if (first == '[' || first == '(') {
            first = 'L';
        }
        return Wrapper.forBasicType(first);
    }

    /**
     * Convert an argument of type 'argType' to be passed to 'targetType' assuring that it is 'functionalType'.
     * Insert the needed conversion instructions in the method code.
     * @param argType
     * @param targetType
     * @param functionalType
     */
    void convertType(String dArg, String dTarget, String dFunctional) {
        if (dArg.equals(dTarget)) {
            return;
        }
        Wrapper wArg = toWrapper(dArg);
        Wrapper wTarget = toWrapper(dTarget);
        if (wArg == VOID || wTarget == VOID) {
            return;
        }
        if (isPrimitive(wArg)) {
            if (isPrimitive(wTarget)) {
                // Both primitives: widening
                widen(wArg, wTarget);
            } else {
                // Primitive argument to reference target
                Wrapper wPrimTarget = wrapperOrNullFromDescriptor(dTarget);
                if (wPrimTarget != null) {
                    // The target is a boxed primitive type, widen to get there before boxing
                    widen(wArg, wPrimTarget);
                    box(wPrimTarget);
                } else {
                    // Otherwise, box and cast
                    box(wArg);
                    cast(wrapperName(wArg), dTarget);
                }
            }
        } else {
            String dSrc;
            Wrapper wFunctional = toWrapper(dFunctional);
            if (isPrimitive(wFunctional)) {
                dSrc = dArg;
            } else {
                // Cast to convert to possibly more specific type, and generate CCE for invalid arg
                dSrc = dFunctional;
                cast(dArg, dFunctional);
            }
            if (isPrimitive(wTarget)) {
                // Reference argument to primitive target
                Wrapper wps = wrapperOrNullFromDescriptor(dSrc);
                if (wps != null) {
                    if (wps.isSigned() || wps.isFloating()) {
                        // Boxed number to primitive
                        unbox(wrapperName(wps), wTarget);
                    } else {
                        // Character or Boolean
                        unbox(wrapperName(wps), wps);
                        widen(wps, wTarget);
                    }
                } else {
                    // Source type is reference type, but not boxed type,
                    // assume it is super type of target type
                    String intermediate;
                    if (wTarget.isSigned() || wTarget.isFloating()) {
                        // Boxed number to primitive
                        intermediate = "java/lang/Number";
                    } else {
                        // Character or Boolean
                        intermediate = wrapperName(wTarget);
                    }
                    cast(dSrc, intermediate);
                    unbox(intermediate, wTarget);
                }
            } else {
                // Both reference types: just case to target type
                cast(dSrc, dTarget);
            }
        }
    }
}
