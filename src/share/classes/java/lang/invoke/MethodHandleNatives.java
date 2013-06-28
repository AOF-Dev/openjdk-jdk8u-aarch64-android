/*
 * Copyright (c) 2008, 2012, Oracle and/or its affiliates. All rights reserved.
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

import java.lang.invoke.MethodHandles.Lookup;
import java.lang.reflect.AccessibleObject;
import java.lang.reflect.Field;
import static java.lang.invoke.MethodHandleNatives.Constants.*;
import static java.lang.invoke.MethodHandleStatics.*;
import static java.lang.invoke.MethodHandles.Lookup.IMPL_LOOKUP;

/**
 * The JVM interface for the method handles package is all here.
 * This is an interface internal and private to an implemetantion of JSR 292.
 * <em>This class is not part of the JSR 292 standard.</em>
 * @author jrose
 */
class MethodHandleNatives {

    private MethodHandleNatives() { } // static only

    /// MemberName support

    static native void init(MemberName self, Object ref);
    static native void expand(MemberName self);
    static native MemberName resolve(MemberName self, Class<?> caller) throws LinkageError;
    static native int getMembers(Class<?> defc, String matchName, String matchSig,
            int matchFlags, Class<?> caller, int skip, MemberName[] results);

    /// Field layout queries parallel to sun.misc.Unsafe:
    static native long objectFieldOffset(MemberName self);  // e.g., returns vmindex
    static native long staticFieldOffset(MemberName self);  // e.g., returns vmindex
    static native Object staticFieldBase(MemberName self);  // e.g., returns clazz
    static native Object getMemberVMInfo(MemberName self);  // returns {vmindex,vmtarget}

    /// MethodHandle support

    /** Fetch MH-related JVM parameter.
     *  which=0 retrieves MethodHandlePushLimit
     *  which=1 retrieves stack slot push size (in address units)
     */
    static native int getConstant(int which);

    static final boolean COUNT_GWT;

    /// CallSite support

    /** Tell the JVM that we need to change the target of a CallSite. */
    static native void setCallSiteTargetNormal(CallSite site, MethodHandle target);
    static native void setCallSiteTargetVolatile(CallSite site, MethodHandle target);

    private static native void registerNatives();
    static {
        registerNatives();
        COUNT_GWT                   = getConstant(Constants.GC_COUNT_GWT) != 0;

        // The JVM calls MethodHandleNatives.<clinit>.  Cascade the <clinit> calls as needed:
        MethodHandleImpl.initStatics();
}

    // All compile-time constants go here.
    // There is an opportunity to check them against the JVM's idea of them.
    static class Constants {
        Constants() { } // static only
        // MethodHandleImpl
        static final int // for getConstant
                GC_COUNT_GWT = 4,
                GC_LAMBDA_SUPPORT = 5;

        // MemberName
        // The JVM uses values of -2 and above for vtable indexes.
        // Field values are simple positive offsets.
        // Ref: src/share/vm/oops/methodOop.hpp
        // This value is negative enough to avoid such numbers,
        // but not too negative.
        static final int
                MN_IS_METHOD           = 0x00010000, // method (not constructor)
                MN_IS_CONSTRUCTOR      = 0x00020000, // constructor
                MN_IS_FIELD            = 0x00040000, // field
                MN_IS_TYPE             = 0x00080000, // nested type
                MN_REFERENCE_KIND_SHIFT = 24, // refKind
                MN_REFERENCE_KIND_MASK = 0x0F000000 >> MN_REFERENCE_KIND_SHIFT,
                // The SEARCH_* bits are not for MN.flags but for the matchFlags argument of MHN.getMembers:
                MN_SEARCH_SUPERCLASSES = 0x00100000,
                MN_SEARCH_INTERFACES   = 0x00200000;

        /**
         * Basic types as encoded in the JVM.  These code values are not
         * intended for use outside this class.  They are used as part of
         * a private interface between the JVM and this class.
         */
        static final int
            T_BOOLEAN  =  4,
            T_CHAR     =  5,
            T_FLOAT    =  6,
            T_DOUBLE   =  7,
            T_BYTE     =  8,
            T_SHORT    =  9,
            T_INT      = 10,
            T_LONG     = 11,
            T_OBJECT   = 12,
            //T_ARRAY    = 13
            T_VOID     = 14,
            //T_ADDRESS  = 15
            T_ILLEGAL  = 99;

        /**
         * Constant pool entry types.
         */
        static final byte
            CONSTANT_Utf8                = 1,
            CONSTANT_Integer             = 3,
            CONSTANT_Float               = 4,
            CONSTANT_Long                = 5,
            CONSTANT_Double              = 6,
            CONSTANT_Class               = 7,
            CONSTANT_String              = 8,
            CONSTANT_Fieldref            = 9,
            CONSTANT_Methodref           = 10,
            CONSTANT_InterfaceMethodref  = 11,
            CONSTANT_NameAndType         = 12,
            CONSTANT_MethodHandle        = 15,  // JSR 292
            CONSTANT_MethodType          = 16,  // JSR 292
            CONSTANT_InvokeDynamic       = 18,
            CONSTANT_LIMIT               = 19;   // Limit to tags found in classfiles

        /**
         * Access modifier flags.
         */
        static final char
            ACC_PUBLIC                 = 0x0001,
            ACC_PRIVATE                = 0x0002,
            ACC_PROTECTED              = 0x0004,
            ACC_STATIC                 = 0x0008,
            ACC_FINAL                  = 0x0010,
            ACC_SYNCHRONIZED           = 0x0020,
            ACC_VOLATILE               = 0x0040,
            ACC_TRANSIENT              = 0x0080,
            ACC_NATIVE                 = 0x0100,
            ACC_INTERFACE              = 0x0200,
            ACC_ABSTRACT               = 0x0400,
            ACC_STRICT                 = 0x0800,
            ACC_SYNTHETIC              = 0x1000,
            ACC_ANNOTATION             = 0x2000,
            ACC_ENUM                   = 0x4000,
            // aliases:
            ACC_SUPER                  = ACC_SYNCHRONIZED,
            ACC_BRIDGE                 = ACC_VOLATILE,
            ACC_VARARGS                = ACC_TRANSIENT;

        /**
         * Constant pool reference-kind codes, as used by CONSTANT_MethodHandle CP entries.
         */
        static final byte
            REF_NONE                    = 0,  // null value
            REF_getField                = 1,
            REF_getStatic               = 2,
            REF_putField                = 3,
            REF_putStatic               = 4,
            REF_invokeVirtual           = 5,
            REF_invokeStatic            = 6,
            REF_invokeSpecial           = 7,
            REF_newInvokeSpecial        = 8,
            REF_invokeInterface         = 9,
            REF_LIMIT                  = 10;
    }

    static boolean refKindIsValid(int refKind) {
        return (refKind > REF_NONE && refKind < REF_LIMIT);
    }
    static boolean refKindIsField(byte refKind) {
        assert(refKindIsValid(refKind));
        return (refKind <= REF_putStatic);
    }
    static boolean refKindIsGetter(byte refKind) {
        assert(refKindIsValid(refKind));
        return (refKind <= REF_getStatic);
    }
    static boolean refKindIsSetter(byte refKind) {
        return refKindIsField(refKind) && !refKindIsGetter(refKind);
    }
    static boolean refKindIsMethod(byte refKind) {
        return !refKindIsField(refKind) && (refKind != REF_newInvokeSpecial);
    }
    static boolean refKindHasReceiver(byte refKind) {
        assert(refKindIsValid(refKind));
        return (refKind & 1) != 0;
    }
    static boolean refKindIsStatic(byte refKind) {
        return !refKindHasReceiver(refKind) && (refKind != REF_newInvokeSpecial);
    }
    static boolean refKindDoesDispatch(byte refKind) {
        assert(refKindIsValid(refKind));
        return (refKind == REF_invokeVirtual ||
                refKind == REF_invokeInterface);
    }
    static {
        final int HR_MASK = ((1 << REF_getField) |
                             (1 << REF_putField) |
                             (1 << REF_invokeVirtual) |
                             (1 << REF_invokeSpecial) |
                             (1 << REF_invokeInterface)
                            );
        for (byte refKind = REF_NONE+1; refKind < REF_LIMIT; refKind++) {
            assert(refKindHasReceiver(refKind) == (((1<<refKind) & HR_MASK) != 0)) : refKind;
        }
    }
    static String refKindName(byte refKind) {
        assert(refKindIsValid(refKind));
        return REFERENCE_KIND_NAME[refKind];
    }
    private static String[] REFERENCE_KIND_NAME = {
            null,
            "getField",
            "getStatic",
            "putField",
            "putStatic",
            "invokeVirtual",
            "invokeStatic",
            "invokeSpecial",
            "newInvokeSpecial",
            "invokeInterface"
    };

    private static native int getNamedCon(int which, Object[] name);
    static boolean verifyConstants() {
        Object[] box = { null };
        for (int i = 0; ; i++) {
            box[0] = null;
            int vmval = getNamedCon(i, box);
            if (box[0] == null)  break;
            String name = (String) box[0];
            try {
                Field con = Constants.class.getDeclaredField(name);
                int jval = con.getInt(null);
                if (jval == vmval)  continue;
                String err = (name+": JVM has "+vmval+" while Java has "+jval);
                if (name.equals("CONV_OP_LIMIT")) {
                    System.err.println("warning: "+err);
                    continue;
                }
                throw new InternalError(err);
            } catch (NoSuchFieldException | IllegalAccessException ex) {
                String err = (name+": JVM has "+vmval+" which Java does not define");
                // ignore exotic ops the JVM cares about; we just wont issue them
                //System.err.println("warning: "+err);
                continue;
            }
        }
        return true;
    }
    static {
        assert(verifyConstants());
    }

    // Up-calls from the JVM.
    // These must NOT be public.

    /**
     * The JVM is linking an invokedynamic instruction.  Create a reified call site for it.
     */
    static MemberName linkCallSite(Object callerObj,
                                   Object bootstrapMethodObj,
                                   Object nameObj, Object typeObj,
                                   Object staticArguments,
                                   Object[] appendixResult) {
        MethodHandle bootstrapMethod = (MethodHandle)bootstrapMethodObj;
        Class<?> caller = (Class<?>)callerObj;
        String name = nameObj.toString().intern();
        MethodType type = (MethodType)typeObj;
        appendixResult[0] = CallSite.makeSite(bootstrapMethod,
                                              name,
                                              type,
                                              staticArguments,
                                              caller);
        return Invokers.linkToCallSiteMethod(type);
    }

    /**
     * The JVM wants a pointer to a MethodType.  Oblige it by finding or creating one.
     */
    static MethodType findMethodHandleType(Class<?> rtype, Class<?>[] ptypes) {
        return MethodType.makeImpl(rtype, ptypes, true);
    }

    /**
     * The JVM wants to link a call site that requires a dynamic type check.
     * Name is a type-checking invoker, invokeExact or invoke.
     * Return a JVM method (MemberName) to handle the invoking.
     * The method assumes the following arguments on the stack:
     * 0: the method handle being invoked
     * 1-N: the arguments to the method handle invocation
     * N+1: an implicitly added type argument (the given MethodType)
     */
    static MemberName linkMethod(Class<?> callerClass, int refKind,
                                 Class<?> defc, String name, Object type,
                                 Object[] appendixResult) {
        if (!TRACE_METHOD_LINKAGE)
            return linkMethodImpl(callerClass, refKind, defc, name, type, appendixResult);
        return linkMethodTracing(callerClass, refKind, defc, name, type, appendixResult);
    }
    static MemberName linkMethodImpl(Class<?> callerClass, int refKind,
                                     Class<?> defc, String name, Object type,
                                     Object[] appendixResult) {
        try {
            if (defc == MethodHandle.class && refKind == REF_invokeVirtual) {
                switch (name) {
                case "invoke":
                    return Invokers.genericInvokerMethod(fixMethodType(callerClass, type), appendixResult);
                case "invokeExact":
                    return Invokers.exactInvokerMethod(fixMethodType(callerClass, type), appendixResult);
                }
            }
        } catch (Throwable ex) {
            if (ex instanceof LinkageError)
                throw (LinkageError) ex;
            else
                throw new LinkageError(ex.getMessage(), ex);
        }
        throw new LinkageError("no such method "+defc.getName()+"."+name+type);
    }
    private static MethodType fixMethodType(Class<?> callerClass, Object type) {
        if (type instanceof MethodType)
            return (MethodType) type;
        else
            return MethodType.fromMethodDescriptorString((String)type, callerClass.getClassLoader());
    }
    // Tracing logic:
    static MemberName linkMethodTracing(Class<?> callerClass, int refKind,
                                        Class<?> defc, String name, Object type,
                                        Object[] appendixResult) {
        System.out.println("linkMethod "+defc.getName()+"."+
                           name+type+"/"+Integer.toHexString(refKind));
        try {
            MemberName res = linkMethodImpl(callerClass, refKind, defc, name, type, appendixResult);
            System.out.println("linkMethod => "+res+" + "+appendixResult[0]);
            return res;
        } catch (Throwable ex) {
            System.out.println("linkMethod => throw "+ex);
            throw ex;
        }
    }


    /**
     * The JVM is resolving a CONSTANT_MethodHandle CP entry.  And it wants our help.
     * It will make an up-call to this method.  (Do not change the name or signature.)
     * The type argument is a Class for field requests and a MethodType for non-fields.
     * <p>
     * Recent versions of the JVM may also pass a resolved MemberName for the type.
     * In that case, the name is ignored and may be null.
     */
    static MethodHandle linkMethodHandleConstant(Class<?> callerClass, int refKind,
                                                 Class<?> defc, String name, Object type) {
        try {
            Lookup lookup = IMPL_LOOKUP.in(callerClass);
            assert(refKindIsValid(refKind));
            return lookup.linkMethodHandleConstant((byte) refKind, defc, name, type);
        } catch (ReflectiveOperationException ex) {
            Error err = new IncompatibleClassChangeError();
            err.initCause(ex);
            throw err;
        }
    }

    /**
     * Is this method a caller-sensitive method?
     * I.e., does it call Reflection.getCallerClass or a similer method
     * to ask about the identity of its caller?
     */
    // FIXME: Replace this pattern match by an annotation @sun.reflect.CallerSensitive.
    static boolean isCallerSensitive(MemberName mem) {
        if (!mem.isInvocable())  return false;  // fields are not caller sensitive
        Class<?> defc = mem.getDeclaringClass();
        switch (mem.getName()) {
        case "doPrivileged":
        case "doPrivilegedWithCombiner":
            return defc == java.security.AccessController.class;
        case "checkMemberAccess":
            return canBeCalledVirtual(mem, java.lang.SecurityManager.class);
        case "getUnsafe":
            return defc == sun.misc.Unsafe.class;
        case "lookup":
            return defc == java.lang.invoke.MethodHandles.class;
        case "findStatic":
        case "findVirtual":
        case "findConstructor":
        case "findSpecial":
        case "findGetter":
        case "findSetter":
        case "findStaticGetter":
        case "findStaticSetter":
        case "bind":
        case "unreflect":
        case "unreflectSpecial":
        case "unreflectConstructor":
        case "unreflectGetter":
        case "unreflectSetter":
            return defc == java.lang.invoke.MethodHandles.Lookup.class;
        case "invoke":
            return defc == java.lang.reflect.Method.class;
        case "get":
        case "getBoolean":
        case "getByte":
        case "getChar":
        case "getShort":
        case "getInt":
        case "getLong":
        case "getFloat":
        case "getDouble":
        case "set":
        case "setBoolean":
        case "setByte":
        case "setChar":
        case "setShort":
        case "setInt":
        case "setLong":
        case "setFloat":
        case "setDouble":
            return defc == java.lang.reflect.Field.class;
        case "newInstance":
            if (defc == java.lang.reflect.Constructor.class)  return true;
            if (defc == java.lang.Class.class)  return true;
            break;
        case "forName":
        case "getClassLoader":
        case "getClasses":
        case "getFields":
        case "getMethods":
        case "getConstructors":
        case "getDeclaredClasses":
        case "getDeclaredFields":
        case "getDeclaredMethods":
        case "getDeclaredConstructors":
        case "getField":
        case "getMethod":
        case "getConstructor":
        case "getDeclaredField":
        case "getDeclaredMethod":
        case "getDeclaredConstructor":
            return defc == java.lang.Class.class;
        case "getConnection":
        case "getDriver":
        case "getDrivers":
        case "deregisterDriver":
            return defc == getClass("java.sql.DriverManager");
        case "newUpdater":
            if (defc == java.util.concurrent.atomic.AtomicIntegerFieldUpdater.class)  return true;
            if (defc == java.util.concurrent.atomic.AtomicLongFieldUpdater.class)  return true;
            if (defc == java.util.concurrent.atomic.AtomicReferenceFieldUpdater.class)  return true;
            break;
        case "getContextClassLoader":
            return canBeCalledVirtual(mem, java.lang.Thread.class);
        case "getPackage":
        case "getPackages":
            return defc == java.lang.Package.class;
        case "getParent":
        case "getSystemClassLoader":
            return defc == java.lang.ClassLoader.class;
        case "load":
        case "loadLibrary":
            if (defc == java.lang.Runtime.class)  return true;
            if (defc == java.lang.System.class)  return true;
            break;
        case "getCallerClass":
            if (defc == sun.reflect.Reflection.class)  return true;
            if (defc == java.lang.System.class)  return true;
            break;
        case "getCallerClassLoader":
            return defc == java.lang.ClassLoader.class;
        case "registerAsParallelCapable":
            return canBeCalledVirtual(mem, java.lang.ClassLoader.class);
        case "getProxyClass":
        case "newProxyInstance":
            return defc == java.lang.reflect.Proxy.class;
        case "asInterfaceInstance":
            return defc == java.lang.invoke.MethodHandleProxies.class;
        case "getBundle":
        case "clearCache":
            return defc == java.util.ResourceBundle.class;
        }
        return false;
    }

    // avoid static dependency to a class in other modules
    private static Class<?> getClass(String cn) {
        try {
            return Class.forName(cn, false,
                                 MethodHandleNatives.class.getClassLoader());
        } catch (ClassNotFoundException e) {
            throw new InternalError(e);
        }
    }
    static boolean canBeCalledVirtual(MemberName symbolicRef, Class<?> definingClass) {
        Class<?> symbolicRefClass = symbolicRef.getDeclaringClass();
        if (symbolicRefClass == definingClass)  return true;
        if (symbolicRef.isStatic() || symbolicRef.isPrivate())  return false;
        return (definingClass.isAssignableFrom(symbolicRefClass) ||  // Msym overrides Mdef
                symbolicRefClass.isInterface());                     // Mdef implements Msym
    }
}
