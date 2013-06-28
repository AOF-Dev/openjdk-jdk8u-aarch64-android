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

import java.io.Serializable;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import sun.invoke.util.Wrapper;
import static sun.invoke.util.Wrapper.*;

/**
 * Abstract implementation of a lambda metafactory which provides parameter unrolling and input validation.
 *
 * @see LambdaMetafactory
 */
/* package */ abstract class AbstractValidatingLambdaMetafactory {

    /*
     * For context, the comments for the following fields are marked in quotes with their values, given this program:
     * interface II<T> {  Object foo(T x); }
     * interface JJ<R extends Number> extends II<R> { }
     * class CC {  String impl(int i) { return "impl:"+i; }}
     * class X {
     *     public static void main(String[] args) {
     *         JJ<Integer> iii = (new CC())::impl;
     *         System.out.printf(">>> %s\n", iii.foo(44));
     * }}
     */
    final Class<?> targetClass;               // The class calling the meta-factory via invokedynamic "class X"
    final MethodType invokedType;             // The type of the invoked method "(CC)II"
    final Class<?> samBase;                   // The type of the returned instance "interface JJ"
    final MethodHandle samMethod;             // Raw method handle for the functional interface method
    final MethodHandleInfo samInfo;           // Info about the SAM method handle "MethodHandleInfo[9 II.foo(Object)Object]"
    final Class<?> samClass;                  // Interface containing the SAM method "interface II"
    final MethodType samMethodType;           // Type of the SAM method "(Object)Object"
    final MethodHandle implMethod;            // Raw method handle for the implementation method
    final MethodHandleInfo implInfo;          // Info about the implementation method handle "MethodHandleInfo[5 CC.impl(int)String]"
    final int implKind;                       // Invocation kind for implementation "5"=invokevirtual
    final boolean implIsInstanceMethod;       // Is the implementation an instance method "true"
    final Class<?> implDefiningClass;         // Type defining the implementation "class CC"
    final MethodType implMethodType;          // Type of the implementation method "(int)String"
    final MethodType instantiatedMethodType;  // Instantiated erased functional interface method type "(Integer)Object"
    final boolean isSerializable;             // Should the returned instance be serializable
    final Class<?>[] markerInterfaces;        // Additional marker interfaces to be implemented


    /**
     * Meta-factory constructor.
     *
     * @param caller Stacked automatically by VM; represents a lookup context with the accessibility privileges
     *               of the caller.
     * @param invokedType Stacked automatically by VM; the signature of the invoked method, which includes the
     *                    expected static type of the returned lambda object, and the static types of the captured
     *                    arguments for the lambda.  In the event that the implementation method is an instance method,
     *                    the first argument in the invocation signature will correspond to the receiver.
     * @param samMethod The primary method in the functional interface to which the lambda or method reference is
     *                  being converted, represented as a method handle.
     * @param implMethod The implementation method which should be called (with suitable adaptation of argument
     *                   types, return types, and adjustment for captured arguments) when methods of the resulting
     *                   functional interface instance are invoked.
     * @param instantiatedMethodType The signature of the primary functional interface method after type variables
     *                               are substituted with their instantiation from the capture site
     * @throws ReflectiveOperationException
     * @throws LambdaConversionException If any of the meta-factory protocol invariants are violated
     */
    AbstractValidatingLambdaMetafactory(MethodHandles.Lookup caller,
                                       MethodType invokedType,
                                       MethodHandle samMethod,
                                       MethodHandle implMethod,
                                       MethodType instantiatedMethodType,
                                       int flags,
                                       Class<?>[] markerInterfaces)
            throws ReflectiveOperationException, LambdaConversionException {
        this.targetClass = caller.lookupClass();
        this.invokedType = invokedType;

        this.samBase = invokedType.returnType();

        this.samMethod = samMethod;
        this.samInfo = new MethodHandleInfo(samMethod);
        this.samClass = samInfo.getDeclaringClass();
        this.samMethodType  = samInfo.getMethodType();

        this.implMethod = implMethod;
        this.implInfo = new MethodHandleInfo(implMethod);
        // @@@ Temporary work-around pending resolution of 8005119
        this.implKind = (implInfo.getReferenceKind() == MethodHandleInfo.REF_invokeSpecial)
                        ? MethodHandleInfo.REF_invokeVirtual
                        : implInfo.getReferenceKind();
        this.implIsInstanceMethod =
                implKind == MethodHandleInfo.REF_invokeVirtual ||
                implKind == MethodHandleInfo.REF_invokeSpecial ||
                implKind == MethodHandleInfo.REF_invokeInterface;
        this.implDefiningClass = implInfo.getDeclaringClass();
        this.implMethodType = implInfo.getMethodType();

        this.instantiatedMethodType = instantiatedMethodType;

        if (!samClass.isInterface()) {
            throw new LambdaConversionException(String.format(
                    "Functional interface %s is not an interface",
                    samClass.getName()));
        }

        boolean foundSerializableSupertype = Serializable.class.isAssignableFrom(samBase);
        for (Class<?> c : markerInterfaces) {
            if (!c.isInterface()) {
                throw new LambdaConversionException(String.format(
                        "Marker interface %s is not an interface",
                        c.getName()));
            }
            foundSerializableSupertype |= Serializable.class.isAssignableFrom(c);
        }
        this.isSerializable = ((flags & LambdaMetafactory.FLAG_SERIALIZABLE) != 0)
                              || foundSerializableSupertype;

        if (isSerializable && !foundSerializableSupertype) {
            markerInterfaces = Arrays.copyOf(markerInterfaces, markerInterfaces.length + 1);
            markerInterfaces[markerInterfaces.length-1] = Serializable.class;
        }
        this.markerInterfaces = markerInterfaces;
    }

    /**
     * Build the CallSite.
     *
     * @return a CallSite, which, when invoked, will return an instance of the
     * functional interface
     * @throws ReflectiveOperationException
     */
    abstract CallSite buildCallSite() throws ReflectiveOperationException, LambdaConversionException;

    /**
     * Check the meta-factory arguments for errors
     * @throws LambdaConversionException if there are improper conversions
     */
    void validateMetafactoryArgs() throws LambdaConversionException {
        // Check target type is a subtype of class where SAM method is defined
        if (!samClass.isAssignableFrom(samBase)) {
            throw new LambdaConversionException(
                    String.format("Invalid target type %s for lambda conversion; not a subtype of functional interface %s",
                                  samBase.getName(), samClass.getName()));
        }

        switch (implKind) {
            case MethodHandleInfo.REF_invokeInterface:
            case MethodHandleInfo.REF_invokeVirtual:
            case MethodHandleInfo.REF_invokeStatic:
            case MethodHandleInfo.REF_newInvokeSpecial:
            case MethodHandleInfo.REF_invokeSpecial:
                break;
            default:
                throw new LambdaConversionException(String.format("Unsupported MethodHandle kind: %s", implInfo));
        }

        // Check arity: optional-receiver + captured + SAM == impl
        final int implArity = implMethodType.parameterCount();
        final int receiverArity = implIsInstanceMethod ? 1 : 0;
        final int capturedArity = invokedType.parameterCount();
        final int samArity = samMethodType.parameterCount();
        final int instantiatedArity = instantiatedMethodType.parameterCount();
        if (implArity + receiverArity != capturedArity + samArity) {
            throw new LambdaConversionException(
                    String.format("Incorrect number of parameters for %s method %s; %d captured parameters, %d functional interface method parameters, %d implementation parameters",
                                  implIsInstanceMethod ? "instance" : "static", implInfo,
                                  capturedArity, samArity, implArity));
        }
        if (instantiatedArity != samArity) {
            throw new LambdaConversionException(
                    String.format("Incorrect number of parameters for %s method %s; %d instantiated parameters, %d functional interface method parameters",
                                  implIsInstanceMethod ? "instance" : "static", implInfo,
                                  instantiatedArity, samArity));
        }

        // If instance: first captured arg (receiver) must be subtype of class where impl method is defined
        final int capturedStart;
        final int samStart;
        if (implIsInstanceMethod) {
            final Class<?> receiverClass;

            // implementation is an instance method, adjust for receiver in captured variables / SAM arguments
            if (capturedArity == 0) {
                // receiver is function parameter
                capturedStart = 0;
                samStart = 1;
                receiverClass = instantiatedMethodType.parameterType(0);
            } else {
                // receiver is a captured variable
                capturedStart = 1;
                samStart = 0;
                receiverClass = invokedType.parameterType(0);
            }

            // check receiver type
            if (!implDefiningClass.isAssignableFrom(receiverClass)) {
                throw new LambdaConversionException(
                        String.format("Invalid receiver type %s; not a subtype of implementation type %s",
                                      receiverClass, implDefiningClass));
            }
        } else {
            // no receiver
            capturedStart = 0;
            samStart = 0;
        }

        // Check for exact match on non-receiver captured arguments
        final int implFromCaptured = capturedArity - capturedStart;
        for (int i=0; i<implFromCaptured; i++) {
            Class<?> implParamType = implMethodType.parameterType(i);
            Class<?> capturedParamType = invokedType.parameterType(i + capturedStart);
            if (!capturedParamType.equals(implParamType)) {
                throw new LambdaConversionException(
                        String.format("Type mismatch in captured lambda parameter %d: expecting %s, found %s",
                                      i, capturedParamType, implParamType));
            }
        }
        // Check for adaptation match on SAM arguments
        final int samOffset = samStart - implFromCaptured;
        for (int i=implFromCaptured; i<implArity; i++) {
            Class<?> implParamType = implMethodType.parameterType(i);
            Class<?> instantiatedParamType = instantiatedMethodType.parameterType(i + samOffset);
            if (!isAdaptableTo(instantiatedParamType, implParamType, true)) {
                throw new LambdaConversionException(
                        String.format("Type mismatch for lambda argument %d: %s is not convertible to %s",
                                      i, instantiatedParamType, implParamType));
            }
        }

        // Adaptation match: return type
        Class<?> expectedType = instantiatedMethodType.returnType();
        Class<?> actualReturnType =
                (implKind == MethodHandleInfo.REF_newInvokeSpecial)
                  ? implDefiningClass
                  : implMethodType.returnType();
        if (!isAdaptableToAsReturn(actualReturnType, expectedType)) {
            throw new LambdaConversionException(
                    String.format("Type mismatch for lambda return: %s is not convertible to %s",
                                  actualReturnType, expectedType));
        }
     }

    /**
     * Check type adaptability
     * @param fromType
     * @param toType
     * @param strict If true, do strict checks, else allow that fromType may be parameterized
     * @return True if 'fromType' can be passed to an argument of 'toType'
     */
    private boolean isAdaptableTo(Class<?> fromType, Class<?> toType, boolean strict) {
        if (fromType.equals(toType)) {
            return true;
        }
        if (fromType.isPrimitive()) {
            Wrapper wfrom = forPrimitiveType(fromType);
            if (toType.isPrimitive()) {
                // both are primitive: widening
                Wrapper wto = forPrimitiveType(toType);
                return wto.isConvertibleFrom(wfrom);
            } else {
                // from primitive to reference: boxing
                return toType.isAssignableFrom(wfrom.wrapperType());
            }
        } else {
            if (toType.isPrimitive()) {
                // from reference to primitive: unboxing
                Wrapper wfrom;
                if (isWrapperType(fromType) && (wfrom = forWrapperType(fromType)).primitiveType().isPrimitive()) {
                    // fromType is a primitive wrapper; unbox+widen
                    Wrapper wto = forPrimitiveType(toType);
                    return wto.isConvertibleFrom(wfrom);
                } else {
                    // must be convertible to primitive
                    return !strict;
                }
            } else {
                // both are reference types: fromType should be a superclass of toType.
                return strict? toType.isAssignableFrom(fromType) : true;
            }
        }
    }

    /**
     * Check type adaptability for return types -- special handling of void type) and parameterized fromType
     * @param fromType
     * @param toType
     * @return True if 'fromType' can be converted to 'toType'
     */
    private boolean isAdaptableToAsReturn(Class<?> fromType, Class<?> toType) {
        return toType.equals(void.class)
               || !fromType.equals(void.class) && isAdaptableTo(fromType, toType, false);
    }


    /*********** Logging support -- for debugging only, uncomment as needed
    static final Executor logPool = Executors.newSingleThreadExecutor();
    protected static void log(final String s) {
        MethodHandleProxyLambdaMetafactory.logPool.execute(new Runnable() {
            @Override
            public void run() {
                System.out.println(s);
            }
        });
    }

    protected static void log(final String s, final Throwable e) {
        MethodHandleProxyLambdaMetafactory.logPool.execute(new Runnable() {
            @Override
            public void run() {
                System.out.println(s);
                e.printStackTrace(System.out);
            }
        });
    }
    ***********************/

    /**
     * Find the functional interface method and corresponding abstract methods
     * which should be bridged. The functional interface method and those to be
     * bridged will have the same name and number of parameters. Check for
     * matching default methods (non-abstract), the VM will create bridges for
     * default methods; We don't have enough readily available type information
     * to distinguish between where the functional interface method should be
     * bridged and where the default method should be bridged; This situation is
     * flagged.
     */
    class MethodAnalyzer {
        private final Method[] methods = samBase.getMethods();

        private Method samMethod = null;
        private final List<Method> methodsToBridge = new ArrayList<>(methods.length);
        private boolean conflictFoundBetweenDefaultAndBridge = false;

        MethodAnalyzer() {
            String samMethodName = samInfo.getName();
            Class<?>[] samParamTypes = samMethodType.parameterArray();
            int samParamLength = samParamTypes.length;
            Class<?> samReturnType = samMethodType.returnType();
            Class<?> objectClass = Object.class;
            List<Method> defaultMethods = new ArrayList<>(methods.length);

            for (Method m : methods) {
                if (m.getName().equals(samMethodName) && m.getDeclaringClass() != objectClass) {
                    Class<?>[] mParamTypes = m.getParameterTypes();
                    if (mParamTypes.length == samParamLength) {
                        // Method matches name and parameter length -- and is not Object
                        if (Modifier.isAbstract(m.getModifiers())) {
                            // Method is abstract
                            if (m.getReturnType().equals(samReturnType)
                                    && Arrays.equals(mParamTypes, samParamTypes)) {
                                // Exact match, this is the SAM method signature
                                samMethod = m;
                            } else if (!hasMatchingBridgeSignature(m)) {
                                // Record bridges, exclude methods with duplicate signatures
                                methodsToBridge.add(m);
                            }
                        } else {
                            // Record default methods for conflict testing
                            defaultMethods.add(m);
                        }
                    }
                }
            }
            for (Method dm : defaultMethods) {
                if (hasMatchingBridgeSignature(dm)) {
                    conflictFoundBetweenDefaultAndBridge = true;
                    break;
                }
            }
        }

        Method getSamMethod() {
            return samMethod;
        }

        List<Method> getMethodsToBridge() {
            return methodsToBridge;
        }

        boolean conflictFoundBetweenDefaultAndBridge() {
            return conflictFoundBetweenDefaultAndBridge;
        }

        /**
         * Search the list of previously found bridge methods to determine if there is a method with the same signature
         * (return and parameter types) as the specified method.
         *
         * @param m The method to match
         * @return True if the method was found, False otherwise
         */
        private boolean hasMatchingBridgeSignature(Method m) {
            Class<?>[] ptypes = m.getParameterTypes();
            Class<?> rtype = m.getReturnType();
            for (Method md : methodsToBridge) {
                if (md.getReturnType().equals(rtype) && Arrays.equals(ptypes, md.getParameterTypes())) {
                    return true;
                }
            }
                    return false;
                }
            }
}
