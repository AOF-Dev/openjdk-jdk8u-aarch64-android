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
import java.security.AccessController;
import java.security.PrivilegedActionException;
import java.security.PrivilegedExceptionAction;
import java.util.Objects;

/**
 * Serialized form of a lambda expression.  The properties of this class represent the information that is present
 * at the lambda factory site, including the identity of the primary functional interface method, the identity of the
 * implementation method, and any variables captured from the local environment at the time of lambda capture.
 *
 * @see LambdaMetafactory
 */
public final class SerializedLambda implements Serializable {
    private static final long serialVersionUID = 8025925345765570181L;
    private final Class<?> capturingClass;
    private final String functionalInterfaceClass;
    private final String functionalInterfaceMethodName;
    private final String functionalInterfaceMethodSignature;
    private final int functionalInterfaceMethodKind;
    private final String implClass;
    private final String implMethodName;
    private final String implMethodSignature;
    private final int implMethodKind;
    private final String instantiatedMethodType;
    private final Object[] capturedArgs;

    /**
     * Create a {@code SerializedLambda} from the low-level information present at the lambda factory site.
     *
     * @param capturingClass The class in which the lambda expression appears
     * @param functionalInterfaceMethodKind Method handle kind (see {@link MethodHandleInfo}) for the
     *                                      functional interface method handle present at the lambda factory site
     * @param functionalInterfaceClass Name, in slash-delimited form, for the functional interface class present at the
     *                                 lambda factory site
     * @param functionalInterfaceMethodName Name of the primary method for the functional interface present at the
     *                                      lambda factory site
     * @param functionalInterfaceMethodSignature Signature of the primary method for the functional interface present
     *                                           at the lambda factory site
     * @param implMethodKind Method handle kind for the implementation method
     * @param implClass Name, in slash-delimited form, for the class holding the implementation method
     * @param implMethodName Name of the implementation method
     * @param implMethodSignature Signature of the implementation method
     * @param instantiatedMethodType The signature of the primary functional interface method after type variables
     *                               are substituted with their instantiation from the capture site
     * @param capturedArgs The dynamic arguments to the lambda factory site, which represent variables captured by
     *                     the lambda
     */
    public SerializedLambda(Class<?> capturingClass,
                            int functionalInterfaceMethodKind,
                            String functionalInterfaceClass,
                            String functionalInterfaceMethodName,
                            String functionalInterfaceMethodSignature,
                            int implMethodKind,
                            String implClass,
                            String implMethodName,
                            String implMethodSignature,
                            String instantiatedMethodType,
                            Object[] capturedArgs) {
        this.capturingClass = capturingClass;
        this.functionalInterfaceMethodKind = functionalInterfaceMethodKind;
        this.functionalInterfaceClass = functionalInterfaceClass;
        this.functionalInterfaceMethodName = functionalInterfaceMethodName;
        this.functionalInterfaceMethodSignature = functionalInterfaceMethodSignature;
        this.implMethodKind = implMethodKind;
        this.implClass = implClass;
        this.implMethodName = implMethodName;
        this.implMethodSignature = implMethodSignature;
        this.instantiatedMethodType = instantiatedMethodType;
        this.capturedArgs = Objects.requireNonNull(capturedArgs).clone();
    }

    /** Get the name of the class that captured this lambda */
    public String getCapturingClass() {
        return capturingClass.getName().replace('.', '/');
    }

    /** Get the name of the functional interface class to which this lambda has been converted */
    public String getFunctionalInterfaceClass() {
        return functionalInterfaceClass;
    }

    /** Get the name of the primary method for the functional interface to which this lambda has been converted */
    public String getFunctionalInterfaceMethodName() {
        return functionalInterfaceMethodName;
    }

    /** Get the signature of the primary method for the functional interface to which this lambda has been converted */
    public String getFunctionalInterfaceMethodSignature() {
        return functionalInterfaceMethodSignature;
    }

    /** Get the method handle kind (see {@link MethodHandleInfo}) of the primary method for the functional interface
     * to which this lambda has been converted */
    public int getFunctionalInterfaceMethodKind() {
        return functionalInterfaceMethodKind;
    }

    /** Get the name of the class containing the implementation method */
    public String getImplClass() {
        return implClass;
    }

    /** Get the name of the implementation method */
    public String getImplMethodName() {
        return implMethodName;
    }

    /** Get the signature of the implementation method */
    public String getImplMethodSignature() {
        return implMethodSignature;
    }

    /** Get the method handle kind (see {@link MethodHandleInfo}) of the implementation method */
    public int getImplMethodKind() {
        return implMethodKind;
    }

    /**
     * Get the signature of the primary functional interface method after type variables are substituted with
     * their instantiation from the capture site
     */
    public final String getInstantiatedMethodType() {
        return instantiatedMethodType;
    }

    /** Get the count of dynamic arguments to the lambda capture site */
    public int getCapturedArgCount() {
        return capturedArgs.length;
    }

    /** Get a dynamic argument to the lambda capture site */
    public Object getCapturedArg(int i) {
        return capturedArgs[i];
    }

    private Object readResolve() throws ReflectiveOperationException {
        try {
            Method deserialize = AccessController.doPrivileged(new PrivilegedExceptionAction<Method>() {
                @Override
                public Method run() throws Exception {
                    Method m = capturingClass.getDeclaredMethod("$deserializeLambda$", SerializedLambda.class);
                    m.setAccessible(true);
                    return m;
                }
            });

            return deserialize.invoke(null, this);
        }
        catch (PrivilegedActionException e) {
            Exception cause = e.getException();
            if (cause instanceof ReflectiveOperationException)
                throw (ReflectiveOperationException) cause;
            else if (cause instanceof RuntimeException)
                throw (RuntimeException) cause;
            else
                throw new RuntimeException("Exception in SerializedLambda.readResolve", e);
        }
    }

    @Override
    public String toString() {
        return String.format("SerializedLambda[capturingClass=%s, functionalInterfaceMethod=%s %s.%s:%s, " +
                             "implementation=%s %s.%s:%s, instantiatedMethodType=%s, numCaptured=%d]",
                             capturingClass, MethodHandleInfo.getReferenceKindString(functionalInterfaceMethodKind),
                             functionalInterfaceClass, functionalInterfaceMethodName, functionalInterfaceMethodSignature,
                             MethodHandleInfo.getReferenceKindString(implMethodKind), implClass, implMethodName,
                             implMethodSignature, instantiatedMethodType, capturedArgs.length);
    }
}
