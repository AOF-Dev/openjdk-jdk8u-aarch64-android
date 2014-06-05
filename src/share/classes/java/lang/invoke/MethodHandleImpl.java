/*
 * Copyright (c) 2008, 2013, Oracle and/or its affiliates. All rights reserved.
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

import java.security.AccessController;
import java.security.PrivilegedAction;
import java.util.Arrays;
import java.util.HashMap;
import sun.invoke.empty.Empty;
import sun.invoke.util.ValueConversions;
import sun.invoke.util.VerifyType;
import sun.invoke.util.Wrapper;
import sun.reflect.CallerSensitive;
import sun.reflect.Reflection;
import static java.lang.invoke.LambdaForm.*;
import static java.lang.invoke.MethodHandleStatics.*;
import static java.lang.invoke.MethodHandles.Lookup.IMPL_LOOKUP;

/**
 * Trusted implementation code for MethodHandle.
 * @author jrose
 */
/*non-public*/ abstract class MethodHandleImpl {
    /// Factory methods to create method handles:

    static void initStatics() {
        // Trigger selected static initializations.
        MemberName.Factory.INSTANCE.getClass();
    }

    static MethodHandle makeArrayElementAccessor(Class<?> arrayClass, boolean isSetter) {
        if (!arrayClass.isArray())
            throw newIllegalArgumentException("not an array: "+arrayClass);
        MethodHandle accessor = ArrayAccessor.getAccessor(arrayClass, isSetter);
        MethodType srcType = accessor.type().erase();
        MethodType lambdaType = srcType.invokerType();
        Name[] names = arguments(1, lambdaType);
        Name[] args  = Arrays.copyOfRange(names, 1, 1 + srcType.parameterCount());
        names[names.length - 1] = new Name(accessor.asType(srcType), (Object[]) args);
        LambdaForm form = new LambdaForm("getElement", lambdaType.parameterCount(), names);
        MethodHandle mh = SimpleMethodHandle.make(srcType, form);
        if (ArrayAccessor.needCast(arrayClass)) {
            mh = mh.bindTo(arrayClass);
        }
        mh = mh.asType(ArrayAccessor.correctType(arrayClass, isSetter));
        return mh;
    }

    static final class ArrayAccessor {
        /// Support for array element access
        static final HashMap<Class<?>, MethodHandle> GETTER_CACHE = new HashMap<>();  // TODO use it
        static final HashMap<Class<?>, MethodHandle> SETTER_CACHE = new HashMap<>();  // TODO use it

        static int     getElementI(int[]     a, int i)            { return              a[i]; }
        static long    getElementJ(long[]    a, int i)            { return              a[i]; }
        static float   getElementF(float[]   a, int i)            { return              a[i]; }
        static double  getElementD(double[]  a, int i)            { return              a[i]; }
        static boolean getElementZ(boolean[] a, int i)            { return              a[i]; }
        static byte    getElementB(byte[]    a, int i)            { return              a[i]; }
        static short   getElementS(short[]   a, int i)            { return              a[i]; }
        static char    getElementC(char[]    a, int i)            { return              a[i]; }
        static Object  getElementL(Object[]  a, int i)            { return              a[i]; }

        static void    setElementI(int[]     a, int i, int     x) {              a[i] = x; }
        static void    setElementJ(long[]    a, int i, long    x) {              a[i] = x; }
        static void    setElementF(float[]   a, int i, float   x) {              a[i] = x; }
        static void    setElementD(double[]  a, int i, double  x) {              a[i] = x; }
        static void    setElementZ(boolean[] a, int i, boolean x) {              a[i] = x; }
        static void    setElementB(byte[]    a, int i, byte    x) {              a[i] = x; }
        static void    setElementS(short[]   a, int i, short   x) {              a[i] = x; }
        static void    setElementC(char[]    a, int i, char    x) {              a[i] = x; }
        static void    setElementL(Object[]  a, int i, Object  x) {              a[i] = x; }

        static Object  getElementL(Class<?> arrayClass, Object[] a, int i)           { arrayClass.cast(a); return a[i]; }
        static void    setElementL(Class<?> arrayClass, Object[] a, int i, Object x) { arrayClass.cast(a); a[i] = x; }

        // Weakly typed wrappers of Object[] accessors:
        static Object  getElementL(Object    a, int i)            { return getElementL((Object[])a, i); }
        static void    setElementL(Object    a, int i, Object  x) {        setElementL((Object[]) a, i, x); }
        static Object  getElementL(Object   arrayClass, Object a, int i)             { return getElementL((Class<?>) arrayClass, (Object[])a, i); }
        static void    setElementL(Object   arrayClass, Object a, int i, Object x)   {        setElementL((Class<?>) arrayClass, (Object[])a, i, x); }

        static boolean needCast(Class<?> arrayClass) {
            Class<?> elemClass = arrayClass.getComponentType();
            return !elemClass.isPrimitive() && elemClass != Object.class;
        }
        static String name(Class<?> arrayClass, boolean isSetter) {
            Class<?> elemClass = arrayClass.getComponentType();
            if (elemClass == null)  throw new IllegalArgumentException();
            return (!isSetter ? "getElement" : "setElement") + Wrapper.basicTypeChar(elemClass);
        }
        static final boolean USE_WEAKLY_TYPED_ARRAY_ACCESSORS = false;  // FIXME: decide
        static MethodType type(Class<?> arrayClass, boolean isSetter) {
            Class<?> elemClass = arrayClass.getComponentType();
            Class<?> arrayArgClass = arrayClass;
            if (!elemClass.isPrimitive()) {
                arrayArgClass = Object[].class;
                if (USE_WEAKLY_TYPED_ARRAY_ACCESSORS)
                    arrayArgClass = Object.class;
            }
            if (!needCast(arrayClass)) {
                return !isSetter ?
                    MethodType.methodType(elemClass,  arrayArgClass, int.class) :
                    MethodType.methodType(void.class, arrayArgClass, int.class, elemClass);
            } else {
                Class<?> classArgClass = Class.class;
                if (USE_WEAKLY_TYPED_ARRAY_ACCESSORS)
                    classArgClass = Object.class;
                return !isSetter ?
                    MethodType.methodType(Object.class, classArgClass, arrayArgClass, int.class) :
                    MethodType.methodType(void.class,   classArgClass, arrayArgClass, int.class, Object.class);
            }
        }
        static MethodType correctType(Class<?> arrayClass, boolean isSetter) {
            Class<?> elemClass = arrayClass.getComponentType();
            return !isSetter ?
                    MethodType.methodType(elemClass,  arrayClass, int.class) :
                    MethodType.methodType(void.class, arrayClass, int.class, elemClass);
        }
        static MethodHandle getAccessor(Class<?> arrayClass, boolean isSetter) {
            String     name = name(arrayClass, isSetter);
            MethodType type = type(arrayClass, isSetter);
            try {
                return IMPL_LOOKUP.findStatic(ArrayAccessor.class, name, type);
            } catch (ReflectiveOperationException ex) {
                throw uncaughtException(ex);
            }
        }
    }

    /**
     * Create a JVM-level adapter method handle to conform the given method
     * handle to the similar newType, using only pairwise argument conversions.
     * For each argument, convert incoming argument to the exact type needed.
     * The argument conversions allowed are casting, boxing and unboxing,
     * integral widening or narrowing, and floating point widening or narrowing.
     * @param srcType required call type
     * @param target original method handle
     * @param level which strength of conversion is allowed
     * @return an adapter to the original handle with the desired new type,
     *          or the original target if the types are already identical
     *          or null if the adaptation cannot be made
     */
    static MethodHandle makePairwiseConvert(MethodHandle target, MethodType srcType, int level) {
        assert(level >= 0 && level <= 2);
        MethodType dstType = target.type();
        assert(dstType.parameterCount() == target.type().parameterCount());
        if (srcType == dstType)
            return target;

        // Calculate extra arguments (temporaries) required in the names array.
        // FIXME: Use an ArrayList<Name>.  Some arguments require more than one conversion step.
        final int INARG_COUNT = srcType.parameterCount();
        int conversions = 0;
        boolean[] needConv = new boolean[1+INARG_COUNT];
        for (int i = 0; i <= INARG_COUNT; i++) {
            Class<?> src = (i == INARG_COUNT) ? dstType.returnType() : srcType.parameterType(i);
            Class<?> dst = (i == INARG_COUNT) ? srcType.returnType() : dstType.parameterType(i);
            if (!VerifyType.isNullConversion(src, dst) ||
                level <= 1 && dst.isInterface() && !dst.isAssignableFrom(src)) {
                needConv[i] = true;
                conversions++;
            }
        }
        boolean retConv = needConv[INARG_COUNT];

        final int IN_MH         = 0;
        final int INARG_BASE    = 1;
        final int INARG_LIMIT   = INARG_BASE + INARG_COUNT;
        final int NAME_LIMIT    = INARG_LIMIT + conversions + 1;
        final int RETURN_CONV   = (!retConv ? -1         : NAME_LIMIT - 1);
        final int OUT_CALL      = (!retConv ? NAME_LIMIT : RETURN_CONV) - 1;

        // Now build a LambdaForm.
        MethodType lambdaType = srcType.basicType().invokerType();
        Name[] names = arguments(NAME_LIMIT - INARG_LIMIT, lambdaType);

        // Collect the arguments to the outgoing call, maybe with conversions:
        final int OUTARG_BASE = 0;  // target MH is Name.function, name Name.arguments[0]
        Object[] outArgs = new Object[OUTARG_BASE + INARG_COUNT];

        int nameCursor = INARG_LIMIT;
        for (int i = 0; i < INARG_COUNT; i++) {
            Class<?> src = srcType.parameterType(i);
            Class<?> dst = dstType.parameterType(i);

            if (!needConv[i]) {
                // do nothing: difference is trivial
                outArgs[OUTARG_BASE + i] = names[INARG_BASE + i];
                continue;
            }

            // Tricky case analysis follows.
            MethodHandle fn = null;
            if (src.isPrimitive()) {
                if (dst.isPrimitive()) {
                    fn = ValueConversions.convertPrimitive(src, dst);
                } else {
                    Wrapper w = Wrapper.forPrimitiveType(src);
                    MethodHandle boxMethod = ValueConversions.box(w);
                    if (dst == w.wrapperType())
                        fn = boxMethod;
                    else
                        fn = boxMethod.asType(MethodType.methodType(dst, src));
                }
            } else {
                if (dst.isPrimitive()) {
                    // Caller has boxed a primitive.  Unbox it for the target.
                    Wrapper w = Wrapper.forPrimitiveType(dst);
                    if (level == 0 || VerifyType.isNullConversion(src, w.wrapperType())) {
                        fn = ValueConversions.unbox(dst);
                    } else if (src == Object.class || !Wrapper.isWrapperType(src)) {
                        // Examples:  Object->int, Number->int, Comparable->int; Byte->int, Character->int
                        // must include additional conversions
                        // src must be examined at runtime, to detect Byte, Character, etc.
                        MethodHandle unboxMethod = (level == 1
                                                    ? ValueConversions.unbox(dst)
                                                    : ValueConversions.unboxCast(dst));
                        fn = unboxMethod;
                    } else {
                        // Example: Byte->int
                        // Do this by reformulating the problem to Byte->byte.
                        Class<?> srcPrim = Wrapper.forWrapperType(src).primitiveType();
                        MethodHandle unbox = ValueConversions.unbox(srcPrim);
                        // Compose the two conversions.  FIXME:  should make two Names for this job
                        fn = unbox.asType(MethodType.methodType(dst, src));
                    }
                } else {
                    // Simple reference conversion.
                    // Note:  Do not check for a class hierarchy relation
                    // between src and dst.  In all cases a 'null' argument
                    // will pass the cast conversion.
                    fn = ValueConversions.cast(dst, Lazy.MH_castReference);
                }
            }
            Name conv = new Name(fn, names[INARG_BASE + i]);
            assert(names[nameCursor] == null);
            names[nameCursor++] = conv;
            assert(outArgs[OUTARG_BASE + i] == null);
            outArgs[OUTARG_BASE + i] = conv;
        }

        // Build argument array for the call.
        assert(nameCursor == OUT_CALL);
        names[OUT_CALL] = new Name(target, outArgs);

        if (RETURN_CONV < 0) {
            assert(OUT_CALL == names.length-1);
        } else {
            Class<?> needReturn = srcType.returnType();
            Class<?> haveReturn = dstType.returnType();
            MethodHandle fn;
            Object[] arg = { names[OUT_CALL] };
            if (haveReturn == void.class) {
                // synthesize a zero value for the given void
                Object zero = Wrapper.forBasicType(needReturn).zero();
                fn = MethodHandles.constant(needReturn, zero);
                arg = new Object[0];  // don't pass names[OUT_CALL] to conversion
            } else {
                MethodHandle identity = MethodHandles.identity(needReturn);
                MethodType needConversion = identity.type().changeParameterType(0, haveReturn);
                fn = makePairwiseConvert(identity, needConversion, level);
            }
            assert(names[RETURN_CONV] == null);
            names[RETURN_CONV] = new Name(fn, arg);
            assert(RETURN_CONV == names.length-1);
        }

        LambdaForm form = new LambdaForm("convert", lambdaType.parameterCount(), names);
        return SimpleMethodHandle.make(srcType, form);
    }

    /**
     * Identity function, with reference cast.
     * @param t an arbitrary reference type
     * @param x an arbitrary reference value
     * @return the same value x
     */
    @ForceInline
    @SuppressWarnings("unchecked")
    static <T,U> T castReference(Class<? extends T> t, U x) {
        // inlined Class.cast because we can't ForceInline it
        if (x != null && !t.isInstance(x))
            throw newClassCastException(t, x);
        return (T) x;
    }

    private static ClassCastException newClassCastException(Class<?> t, Object obj) {
        return new ClassCastException("Cannot cast " + obj.getClass().getName() + " to " + t.getName());
    }

    static MethodHandle makeReferenceIdentity(Class<?> refType) {
        MethodType lambdaType = MethodType.genericMethodType(1).invokerType();
        Name[] names = arguments(1, lambdaType);
        names[names.length - 1] = new Name(ValueConversions.identity(), names[1]);
        LambdaForm form = new LambdaForm("identity", lambdaType.parameterCount(), names);
        return SimpleMethodHandle.make(MethodType.methodType(refType, refType), form);
    }

    static MethodHandle makeVarargsCollector(MethodHandle target, Class<?> arrayType) {
        MethodType type = target.type();
        int last = type.parameterCount() - 1;
        if (type.parameterType(last) != arrayType)
            target = target.asType(type.changeParameterType(last, arrayType));
        target = target.asFixedArity();  // make sure this attribute is turned off
        return new AsVarargsCollector(target, target.type(), arrayType);
    }

    static class AsVarargsCollector extends MethodHandle {
        private final MethodHandle target;
        private final Class<?> arrayType;
        private /*@Stable*/ MethodHandle asCollectorCache;

        AsVarargsCollector(MethodHandle target, MethodType type, Class<?> arrayType) {
            super(type, reinvokerForm(target));
            this.target = target;
            this.arrayType = arrayType;
            this.asCollectorCache = target.asCollector(arrayType, 0);
        }

        @Override MethodHandle reinvokerTarget() { return target; }

        @Override
        public boolean isVarargsCollector() {
            return true;
        }

        @Override
        public MethodHandle asFixedArity() {
            return target;
        }

        @Override
        public MethodHandle asTypeUncached(MethodType newType) {
            MethodType type = this.type();
            int collectArg = type.parameterCount() - 1;
            int newArity = newType.parameterCount();
            if (newArity == collectArg+1 &&
                type.parameterType(collectArg).isAssignableFrom(newType.parameterType(collectArg))) {
                // if arity and trailing parameter are compatible, do normal thing
                return asTypeCache = asFixedArity().asType(newType);
            }
            // check cache
            MethodHandle acc = asCollectorCache;
            if (acc != null && acc.type().parameterCount() == newArity)
                return asTypeCache = acc.asType(newType);
            // build and cache a collector
            int arrayLength = newArity - collectArg;
            MethodHandle collector;
            try {
                collector = asFixedArity().asCollector(arrayType, arrayLength);
                assert(collector.type().parameterCount() == newArity) : "newArity="+newArity+" but collector="+collector;
            } catch (IllegalArgumentException ex) {
                throw new WrongMethodTypeException("cannot build collector", ex);
            }
            asCollectorCache = collector;
            return asTypeCache = collector.asType(newType);
        }

        @Override
        MethodHandle setVarargs(MemberName member) {
            if (member.isVarargs())  return this;
            return asFixedArity();
        }

        @Override
        MethodHandle viewAsType(MethodType newType) {
            if (newType.lastParameterType() != type().lastParameterType())
                throw new InternalError();
            MethodHandle newTarget = asFixedArity().viewAsType(newType);
            // put back the varargs bit:
            return new AsVarargsCollector(newTarget, newType, arrayType);
        }

        @Override
        MemberName internalMemberName() {
            return asFixedArity().internalMemberName();
        }
        @Override
        Class<?> internalCallerClass() {
            return asFixedArity().internalCallerClass();
        }

        /*non-public*/
        @Override
        boolean isInvokeSpecial() {
            return asFixedArity().isInvokeSpecial();
        }


        @Override
        MethodHandle bindArgument(int pos, char basicType, Object value) {
            return asFixedArity().bindArgument(pos, basicType, value);
        }

        @Override
        MethodHandle bindReceiver(Object receiver) {
            return asFixedArity().bindReceiver(receiver);
        }

        @Override
        MethodHandle dropArguments(MethodType srcType, int pos, int drops) {
            return asFixedArity().dropArguments(srcType, pos, drops);
        }

        @Override
        MethodHandle permuteArguments(MethodType newType, int[] reorder) {
            return asFixedArity().permuteArguments(newType, reorder);
        }
    }

    /** Factory method:  Spread selected argument. */
    static MethodHandle makeSpreadArguments(MethodHandle target,
                                            Class<?> spreadArgType, int spreadArgPos, int spreadArgCount) {
        MethodType targetType = target.type();

        for (int i = 0; i < spreadArgCount; i++) {
            Class<?> arg = VerifyType.spreadArgElementType(spreadArgType, i);
            if (arg == null)  arg = Object.class;
            targetType = targetType.changeParameterType(spreadArgPos + i, arg);
        }
        target = target.asType(targetType);

        MethodType srcType = targetType
                .replaceParameterTypes(spreadArgPos, spreadArgPos + spreadArgCount, spreadArgType);
        // Now build a LambdaForm.
        MethodType lambdaType = srcType.invokerType();
        Name[] names = arguments(spreadArgCount + 2, lambdaType);
        int nameCursor = lambdaType.parameterCount();
        int[] indexes = new int[targetType.parameterCount()];

        for (int i = 0, argIndex = 1; i < targetType.parameterCount() + 1; i++, argIndex++) {
            Class<?> src = lambdaType.parameterType(i);
            if (i == spreadArgPos) {
                // Spread the array.
                MethodHandle aload = MethodHandles.arrayElementGetter(spreadArgType);
                Name array = names[argIndex];
                names[nameCursor++] = new Name(Lazy.NF_checkSpreadArgument, array, spreadArgCount);
                for (int j = 0; j < spreadArgCount; i++, j++) {
                    indexes[i] = nameCursor;
                    names[nameCursor++] = new Name(aload, array, j);
                }
            } else if (i < indexes.length) {
                indexes[i] = argIndex;
            }
        }
        assert(nameCursor == names.length-1);  // leave room for the final call

        // Build argument array for the call.
        Name[] targetArgs = new Name[targetType.parameterCount()];
        for (int i = 0; i < targetType.parameterCount(); i++) {
            int idx = indexes[i];
            targetArgs[i] = names[idx];
        }
        names[names.length - 1] = new Name(target, (Object[]) targetArgs);

        LambdaForm form = new LambdaForm("spread", lambdaType.parameterCount(), names);
        return SimpleMethodHandle.make(srcType, form);
    }

    static void checkSpreadArgument(Object av, int n) {
        if (av == null) {
            if (n == 0)  return;
        } else if (av instanceof Object[]) {
            int len = ((Object[])av).length;
            if (len == n)  return;
        } else {
            int len = java.lang.reflect.Array.getLength(av);
            if (len == n)  return;
        }
        // fall through to error:
        throw newIllegalArgumentException("array is not of length "+n);
    }

    /**
     * Pre-initialized NamedFunctions for bootstrapping purposes.
     * Factored in an inner class to delay initialization until first usage.
     */
    private static class Lazy {
        private static final Class<?> MHI = MethodHandleImpl.class;

        static final NamedFunction NF_checkSpreadArgument;
        static final NamedFunction NF_guardWithCatch;
        static final NamedFunction NF_selectAlternative;
        static final NamedFunction NF_throwException;

        static final MethodHandle MH_castReference;

        static {
            try {
                NF_checkSpreadArgument = new NamedFunction(MHI.getDeclaredMethod("checkSpreadArgument", Object.class, int.class));
                NF_guardWithCatch      = new NamedFunction(MHI.getDeclaredMethod("guardWithCatch", MethodHandle.class, Class.class,
                                                                                 MethodHandle.class, Object[].class));
                NF_selectAlternative   = new NamedFunction(MHI.getDeclaredMethod("selectAlternative", boolean.class, MethodHandle.class,
                                                                                 MethodHandle.class));
                NF_throwException      = new NamedFunction(MHI.getDeclaredMethod("throwException", Throwable.class));

                NF_checkSpreadArgument.resolve();
                NF_guardWithCatch.resolve();
                NF_selectAlternative.resolve();
                NF_throwException.resolve();

                MethodType mt = MethodType.methodType(Object.class, Class.class, Object.class);
                MH_castReference = IMPL_LOOKUP.findStatic(MHI, "castReference", mt);
            } catch (ReflectiveOperationException ex) {
                throw newInternalError(ex);
            }
        }
    }

    /** Factory method:  Collect or filter selected argument(s). */
    static MethodHandle makeCollectArguments(MethodHandle target,
                MethodHandle collector, int collectArgPos, boolean retainOriginalArgs) {
        MethodType targetType = target.type();          // (a..., c, [b...])=>r
        MethodType collectorType = collector.type();    // (b...)=>c
        int collectArgCount = collectorType.parameterCount();
        Class<?> collectValType = collectorType.returnType();
        int collectValCount = (collectValType == void.class ? 0 : 1);
        MethodType srcType = targetType                 // (a..., [b...])=>r
                .dropParameterTypes(collectArgPos, collectArgPos+collectValCount);
        if (!retainOriginalArgs) {                      // (a..., b...)=>r
            srcType = srcType.insertParameterTypes(collectArgPos, collectorType.parameterList());
        }
        // in  arglist: [0: ...keep1 | cpos: collect...  | cpos+cacount: keep2... ]
        // out arglist: [0: ...keep1 | cpos: collectVal? | cpos+cvcount: keep2... ]
        // out(retain): [0: ...keep1 | cpos: cV? coll... | cpos+cvc+cac: keep2... ]

        // Now build a LambdaForm.
        MethodType lambdaType = srcType.invokerType();
        Name[] names = arguments(2, lambdaType);
        final int collectNamePos = names.length - 2;
        final int targetNamePos  = names.length - 1;

        Name[] collectorArgs = Arrays.copyOfRange(names, 1 + collectArgPos, 1 + collectArgPos + collectArgCount);
        names[collectNamePos] = new Name(collector, (Object[]) collectorArgs);

        // Build argument array for the target.
        // Incoming LF args to copy are: [ (mh) headArgs collectArgs tailArgs ].
        // Output argument array is [ headArgs (collectVal)? (collectArgs)? tailArgs ].
        Name[] targetArgs = new Name[targetType.parameterCount()];
        int inputArgPos  = 1;  // incoming LF args to copy to target
        int targetArgPos = 0;  // fill pointer for targetArgs
        int chunk = collectArgPos;  // |headArgs|
        System.arraycopy(names, inputArgPos, targetArgs, targetArgPos, chunk);
        inputArgPos  += chunk;
        targetArgPos += chunk;
        if (collectValType != void.class) {
            targetArgs[targetArgPos++] = names[collectNamePos];
        }
        chunk = collectArgCount;
        if (retainOriginalArgs) {
            System.arraycopy(names, inputArgPos, targetArgs, targetArgPos, chunk);
            targetArgPos += chunk;   // optionally pass on the collected chunk
        }
        inputArgPos += chunk;
        chunk = targetArgs.length - targetArgPos;  // all the rest
        System.arraycopy(names, inputArgPos, targetArgs, targetArgPos, chunk);
        assert(inputArgPos + chunk == collectNamePos);  // use of rest of input args also
        names[targetNamePos] = new Name(target, (Object[]) targetArgs);

        LambdaForm form = new LambdaForm("collect", lambdaType.parameterCount(), names);
        return SimpleMethodHandle.make(srcType, form);
    }

    @LambdaForm.Hidden
    static
    MethodHandle selectAlternative(boolean testResult, MethodHandle target, MethodHandle fallback) {
        return testResult ? target : fallback;
    }

    static
    MethodHandle makeGuardWithTest(MethodHandle test,
                                   MethodHandle target,
                                   MethodHandle fallback) {
        MethodType basicType = target.type().basicType();
        MethodHandle invokeBasic = MethodHandles.basicInvoker(basicType);
        int arity = basicType.parameterCount();
        int extraNames = 3;
        MethodType lambdaType = basicType.invokerType();
        Name[] names = arguments(extraNames, lambdaType);

        Object[] testArgs   = Arrays.copyOfRange(names, 1, 1 + arity, Object[].class);
        Object[] targetArgs = Arrays.copyOfRange(names, 0, 1 + arity, Object[].class);

        // call test
        names[arity + 1] = new Name(test, testArgs);

        // call selectAlternative
        Object[] selectArgs = { names[arity + 1], target, fallback };
        names[arity + 2] = new Name(Lazy.NF_selectAlternative, selectArgs);
        targetArgs[0] = names[arity + 2];

        // call target or fallback
        names[arity + 3] = new Name(new NamedFunction(invokeBasic), targetArgs);

        LambdaForm form = new LambdaForm("guard", lambdaType.parameterCount(), names);
        return SimpleMethodHandle.make(target.type(), form);
    }

    /**
     * The LambaForm shape for catchException combinator is the following:
     * <blockquote><pre>{@code
     *  guardWithCatch=Lambda(a0:L,a1:L,a2:L)=>{
     *    t3:L=BoundMethodHandle$Species_LLLLL.argL0(a0:L);
     *    t4:L=BoundMethodHandle$Species_LLLLL.argL1(a0:L);
     *    t5:L=BoundMethodHandle$Species_LLLLL.argL2(a0:L);
     *    t6:L=BoundMethodHandle$Species_LLLLL.argL3(a0:L);
     *    t7:L=BoundMethodHandle$Species_LLLLL.argL4(a0:L);
     *    t8:L=MethodHandle.invokeBasic(t6:L,a1:L,a2:L);
     *    t9:L=MethodHandleImpl.guardWithCatch(t3:L,t4:L,t5:L,t8:L);
     *   t10:I=MethodHandle.invokeBasic(t7:L,t9:L);t10:I}
     * }</pre></blockquote>
     *
     * argL0 and argL2 are target and catcher method handles. argL1 is exception class.
     * argL3 and argL4 are auxiliary method handles: argL3 boxes arguments and wraps them into Object[]
     * (ValueConversions.array()) and argL4 unboxes result if necessary (ValueConversions.unbox()).
     *
     * Having t8 and t10 passed outside and not hardcoded into a lambda form allows to share lambda forms
     * among catchException combinators with the same basic type.
     */
    private static LambdaForm makeGuardWithCatchForm(MethodType basicType) {
        MethodType lambdaType = basicType.invokerType();

        LambdaForm lform = basicType.form().cachedLambdaForm(MethodTypeForm.LF_GWC);
        if (lform != null) {
            return lform;
        }
        final int THIS_MH      = 0;  // the BMH_LLLLL
        final int ARG_BASE     = 1;  // start of incoming arguments
        final int ARG_LIMIT    = ARG_BASE + basicType.parameterCount();

        int nameCursor = ARG_LIMIT;
        final int GET_TARGET       = nameCursor++;
        final int GET_CLASS        = nameCursor++;
        final int GET_CATCHER      = nameCursor++;
        final int GET_COLLECT_ARGS = nameCursor++;
        final int GET_UNBOX_RESULT = nameCursor++;
        final int BOXED_ARGS       = nameCursor++;
        final int TRY_CATCH        = nameCursor++;
        final int UNBOX_RESULT     = nameCursor++;

        Name[] names = arguments(nameCursor - ARG_LIMIT, lambdaType);

        BoundMethodHandle.SpeciesData data = BoundMethodHandle.speciesData_LLLLL();
        names[GET_TARGET]       = new Name(data.getterFunction(0), names[THIS_MH]);
        names[GET_CLASS]        = new Name(data.getterFunction(1), names[THIS_MH]);
        names[GET_CATCHER]      = new Name(data.getterFunction(2), names[THIS_MH]);
        names[GET_COLLECT_ARGS] = new Name(data.getterFunction(3), names[THIS_MH]);
        names[GET_UNBOX_RESULT] = new Name(data.getterFunction(4), names[THIS_MH]);

        // FIXME: rework argument boxing/result unboxing logic for LF interpretation

        // t_{i}:L=MethodHandle.invokeBasic(collectArgs:L,a1:L,...);
        MethodType collectArgsType = basicType.changeReturnType(Object.class);
        MethodHandle invokeBasic = MethodHandles.basicInvoker(collectArgsType);
        Object[] args = new Object[invokeBasic.type().parameterCount()];
        args[0] = names[GET_COLLECT_ARGS];
        System.arraycopy(names, ARG_BASE, args, 1, ARG_LIMIT-ARG_BASE);
        names[BOXED_ARGS] = new Name(new NamedFunction(invokeBasic), args);

        // t_{i+1}:L=MethodHandleImpl.guardWithCatch(target:L,exType:L,catcher:L,t_{i}:L);
        Object[] gwcArgs = new Object[] {names[GET_TARGET], names[GET_CLASS], names[GET_CATCHER], names[BOXED_ARGS]};
        names[TRY_CATCH] = new Name(Lazy.NF_guardWithCatch, gwcArgs);

        // t_{i+2}:I=MethodHandle.invokeBasic(unbox:L,t_{i+1}:L);
        MethodHandle invokeBasicUnbox = MethodHandles.basicInvoker(MethodType.methodType(basicType.rtype(), Object.class));
        Object[] unboxArgs  = new Object[] {names[GET_UNBOX_RESULT], names[TRY_CATCH]};
        names[UNBOX_RESULT] = new Name(new NamedFunction(invokeBasicUnbox), unboxArgs);

        lform = new LambdaForm("guardWithCatch", lambdaType.parameterCount(), names);

        basicType.form().setCachedLambdaForm(MethodTypeForm.LF_GWC, lform);
        return lform;
    }

    static
    MethodHandle makeGuardWithCatch(MethodHandle target,
                                    Class<? extends Throwable> exType,
                                    MethodHandle catcher) {
        MethodType type = target.type();
        LambdaForm form = makeGuardWithCatchForm(type.basicType());

        // Prepare auxiliary method handles used during LambdaForm interpreation.
        // Box arguments and wrap them into Object[]: ValueConversions.array().
        MethodType varargsType = type.changeReturnType(Object[].class);
        MethodHandle collectArgs = ValueConversions.varargsArray(type.parameterCount())
                                                   .asType(varargsType);
        // Result unboxing: ValueConversions.unbox() OR ValueConversions.identity() OR ValueConversions.ignore().
        MethodHandle unboxResult;
        if (type.returnType().isPrimitive()) {
            unboxResult = ValueConversions.unbox(type.returnType());
        } else {
            unboxResult = ValueConversions.identity();
        }

        BoundMethodHandle.SpeciesData data = BoundMethodHandle.speciesData_LLLLL();
        BoundMethodHandle mh;
        try {
            mh = (BoundMethodHandle)
                    data.constructor[0].invokeBasic(type, form, (Object) target, (Object) exType, (Object) catcher,
                                                    (Object) collectArgs, (Object) unboxResult);
        } catch (Throwable ex) {
            throw uncaughtException(ex);
        }
        assert(mh.type() == type);
        return mh;
    }

    /**
     * Intrinsified during LambdaForm compilation
     * (see {@link InvokerBytecodeGenerator#emitGuardWithCatch emitGuardWithCatch}).
     */
    @LambdaForm.Hidden
    static Object guardWithCatch(MethodHandle target, Class<? extends Throwable> exType, MethodHandle catcher,
                                 Object... av) throws Throwable {
        // Use asFixedArity() to avoid unnecessary boxing of last argument for VarargsCollector case.
        try {
            return target.asFixedArity().invokeWithArguments(av);
        } catch (Throwable t) {
            if (!exType.isInstance(t)) throw t;
            return catcher.asFixedArity().invokeWithArguments(prepend(t, av));
        }
    }

    /** Prepend an element {@code elem} to an {@code array}. */
    @LambdaForm.Hidden
    private static Object[] prepend(Object elem, Object[] array) {
        Object[] newArray = new Object[array.length+1];
        newArray[0] = elem;
        System.arraycopy(array, 0, newArray, 1, array.length);
        return newArray;
    }

    static
    MethodHandle throwException(MethodType type) {
        assert(Throwable.class.isAssignableFrom(type.parameterType(0)));
        int arity = type.parameterCount();
        if (arity > 1) {
            return throwException(type.dropParameterTypes(1, arity)).dropArguments(type, 1, arity-1);
        }
        return makePairwiseConvert(Lazy.NF_throwException.resolvedHandle(), type, 2);
    }

    static <T extends Throwable> Empty throwException(T t) throws T { throw t; }

    static MethodHandle[] FAKE_METHOD_HANDLE_INVOKE = new MethodHandle[2];
    static MethodHandle fakeMethodHandleInvoke(MemberName method) {
        int idx;
        assert(method.isMethodHandleInvoke());
        switch (method.getName()) {
        case "invoke":       idx = 0; break;
        case "invokeExact":  idx = 1; break;
        default:             throw new InternalError(method.getName());
        }
        MethodHandle mh = FAKE_METHOD_HANDLE_INVOKE[idx];
        if (mh != null)  return mh;
        MethodType type = MethodType.methodType(Object.class, UnsupportedOperationException.class,
                                                MethodHandle.class, Object[].class);
        mh = throwException(type);
        mh = mh.bindTo(new UnsupportedOperationException("cannot reflectively invoke MethodHandle"));
        if (!method.getInvocationType().equals(mh.type()))
            throw new InternalError(method.toString());
        mh = mh.withInternalMemberName(method);
        mh = mh.asVarargsCollector(Object[].class);
        assert(method.isVarargs());
        FAKE_METHOD_HANDLE_INVOKE[idx] = mh;
        return mh;
    }

    /**
     * Create an alias for the method handle which, when called,
     * appears to be called from the same class loader and protection domain
     * as hostClass.
     * This is an expensive no-op unless the method which is called
     * is sensitive to its caller.  A small number of system methods
     * are in this category, including Class.forName and Method.invoke.
     */
    static
    MethodHandle bindCaller(MethodHandle mh, Class<?> hostClass) {
        return BindCaller.bindCaller(mh, hostClass);
    }

    // Put the whole mess into its own nested class.
    // That way we can lazily load the code and set up the constants.
    private static class BindCaller {
        static
        MethodHandle bindCaller(MethodHandle mh, Class<?> hostClass) {
            // Do not use this function to inject calls into system classes.
            if (hostClass == null
                ||    (hostClass.isArray() ||
                       hostClass.isPrimitive() ||
                       hostClass.getName().startsWith("java.") ||
                       hostClass.getName().startsWith("sun."))) {
                throw new InternalError();  // does not happen, and should not anyway
            }
            // For simplicity, convert mh to a varargs-like method.
            MethodHandle vamh = prepareForInvoker(mh);
            // Cache the result of makeInjectedInvoker once per argument class.
            MethodHandle bccInvoker = CV_makeInjectedInvoker.get(hostClass);
            return restoreToType(bccInvoker.bindTo(vamh), mh.type(), mh.internalMemberName(), hostClass);
        }

        private static MethodHandle makeInjectedInvoker(Class<?> hostClass) {
            Class<?> bcc = UNSAFE.defineAnonymousClass(hostClass, T_BYTES, null);
            if (hostClass.getClassLoader() != bcc.getClassLoader())
                throw new InternalError(hostClass.getName()+" (CL)");
            try {
                if (hostClass.getProtectionDomain() != bcc.getProtectionDomain())
                    throw new InternalError(hostClass.getName()+" (PD)");
            } catch (SecurityException ex) {
                // Self-check was blocked by security manager.  This is OK.
                // In fact the whole try body could be turned into an assertion.
            }
            try {
                MethodHandle init = IMPL_LOOKUP.findStatic(bcc, "init", MethodType.methodType(void.class));
                init.invokeExact();  // force initialization of the class
            } catch (Throwable ex) {
                throw uncaughtException(ex);
            }
            MethodHandle bccInvoker;
            try {
                MethodType invokerMT = MethodType.methodType(Object.class, MethodHandle.class, Object[].class);
                bccInvoker = IMPL_LOOKUP.findStatic(bcc, "invoke_V", invokerMT);
            } catch (ReflectiveOperationException ex) {
                throw uncaughtException(ex);
            }
            // Test the invoker, to ensure that it really injects into the right place.
            try {
                MethodHandle vamh = prepareForInvoker(MH_checkCallerClass);
                Object ok = bccInvoker.invokeExact(vamh, new Object[]{hostClass, bcc});
            } catch (Throwable ex) {
                throw new InternalError(ex);
            }
            return bccInvoker;
        }
        private static ClassValue<MethodHandle> CV_makeInjectedInvoker = new ClassValue<MethodHandle>() {
            @Override protected MethodHandle computeValue(Class<?> hostClass) {
                return makeInjectedInvoker(hostClass);
            }
        };

        // Adapt mh so that it can be called directly from an injected invoker:
        private static MethodHandle prepareForInvoker(MethodHandle mh) {
            mh = mh.asFixedArity();
            MethodType mt = mh.type();
            int arity = mt.parameterCount();
            MethodHandle vamh = mh.asType(mt.generic());
            vamh.internalForm().compileToBytecode();  // eliminate LFI stack frames
            vamh = vamh.asSpreader(Object[].class, arity);
            vamh.internalForm().compileToBytecode();  // eliminate LFI stack frames
            return vamh;
        }

        // Undo the adapter effect of prepareForInvoker:
        private static MethodHandle restoreToType(MethodHandle vamh, MethodType type,
                                                  MemberName member,
                                                  Class<?> hostClass) {
            MethodHandle mh = vamh.asCollector(Object[].class, type.parameterCount());
            mh = mh.asType(type);
            mh = new WrappedMember(mh, type, member, hostClass);
            return mh;
        }

        private static final MethodHandle MH_checkCallerClass;
        static {
            final Class<?> THIS_CLASS = BindCaller.class;
            assert(checkCallerClass(THIS_CLASS, THIS_CLASS));
            try {
                MH_checkCallerClass = IMPL_LOOKUP
                    .findStatic(THIS_CLASS, "checkCallerClass",
                                MethodType.methodType(boolean.class, Class.class, Class.class));
                assert((boolean) MH_checkCallerClass.invokeExact(THIS_CLASS, THIS_CLASS));
            } catch (Throwable ex) {
                throw new InternalError(ex);
            }
        }

        @CallerSensitive
        private static boolean checkCallerClass(Class<?> expected, Class<?> expected2) {
            // This method is called via MH_checkCallerClass and so it's
            // correct to ask for the immediate caller here.
            Class<?> actual = Reflection.getCallerClass();
            if (actual != expected && actual != expected2)
                throw new InternalError("found "+actual.getName()+", expected "+expected.getName()
                                        +(expected == expected2 ? "" : ", or else "+expected2.getName()));
            return true;
        }

        private static final byte[] T_BYTES;
        static {
            final Object[] values = {null};
            AccessController.doPrivileged(new PrivilegedAction<Void>() {
                    public Void run() {
                        try {
                            Class<T> tClass = T.class;
                            String tName = tClass.getName();
                            String tResource = tName.substring(tName.lastIndexOf('.')+1)+".class";
                            java.net.URLConnection uconn = tClass.getResource(tResource).openConnection();
                            int len = uconn.getContentLength();
                            byte[] bytes = new byte[len];
                            try (java.io.InputStream str = uconn.getInputStream()) {
                                int nr = str.read(bytes);
                                if (nr != len)  throw new java.io.IOException(tResource);
                            }
                            values[0] = bytes;
                        } catch (java.io.IOException ex) {
                            throw new InternalError(ex);
                        }
                        return null;
                    }
                });
            T_BYTES = (byte[]) values[0];
        }

        // The following class is used as a template for Unsafe.defineAnonymousClass:
        private static class T {
            static void init() { }  // side effect: initializes this class
            static Object invoke_V(MethodHandle vamh, Object[] args) throws Throwable {
                return vamh.invokeExact(args);
            }
        }
    }


    /** This subclass allows a wrapped method handle to be re-associated with an arbitrary member name. */
    static class WrappedMember extends MethodHandle {
        private final MethodHandle target;
        private final MemberName member;
        private final Class<?> callerClass;

        private WrappedMember(MethodHandle target, MethodType type, MemberName member, Class<?> callerClass) {
            super(type, reinvokerForm(target));
            this.target = target;
            this.member = member;
            this.callerClass = callerClass;
        }

        @Override
        MethodHandle reinvokerTarget() {
            return target;
        }
        @Override
        public MethodHandle asTypeUncached(MethodType newType) {
            // This MH is an alias for target, except for the MemberName
            // Drop the MemberName if there is any conversion.
            return asTypeCache = target.asType(newType);
        }
        @Override
        MemberName internalMemberName() {
            return member;
        }
        @Override
        Class<?> internalCallerClass() {
            return callerClass;
        }
        @Override
        boolean isInvokeSpecial() {
            return target.isInvokeSpecial();
        }
        @Override
        MethodHandle viewAsType(MethodType newType) {
            return new WrappedMember(target, newType, member, callerClass);
        }
    }

    static MethodHandle makeWrappedMember(MethodHandle target, MemberName member) {
        if (member.equals(target.internalMemberName()))
            return target;
        return new WrappedMember(target, target.type(), member, null);
    }

}
