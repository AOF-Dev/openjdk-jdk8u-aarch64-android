/*
 * Copyright (c) 2010, 2013, Oracle and/or its affiliates. All rights reserved.
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
package java.util.function;

import java.util.Objects;

/**
 * Determines if the {@code double} input value matches some criteria. This is
 * the {@code double}-consuming primitive type specialization of
 * {@link Predicate}.
 *
 * @see Predicate
 * @since 1.8
 */
@FunctionalInterface
public interface DoublePredicate {

    /**
     * Returns {@code true} if the input value matches some criteria.
     *
     * @param value the value to be tested.
     * @return {@code true} if the input value matches some criteria, otherwise
     * {@code false}.
     */
    public boolean test(double value);

    /**
     * Returns a predicate which evaluates to {@code true} only if this
     * predicate and the provided predicate both evaluate to {@code true}. If
     * this predicate returns {@code false} then the remaining predicate is not
     * evaluated.
     *
     * @param p a predicate which will be logically-ANDed with this predicate.
     * @return a new predicate which returns {@code true} only if both
     * predicates return {@code true}.
     */
    public default DoublePredicate and(DoublePredicate p) {
        Objects.requireNonNull(p);
        return (value) -> test(value) && p.test(value);
    }

    /**
     * Returns a predicate which negates the result of this predicate.
     *
     * @return a new predicate who's result is always the opposite of this
     * predicate.
     */
    public default DoublePredicate negate() {
        return (value) -> !test(value);
    }

    /**
     * Returns a predicate which evaluates to {@code true} if either this
     * predicate or the provided predicate evaluates to {@code true}. If this
     * predicate returns {@code true} then the remaining predicate is not
     * evaluated.
     *
     * @param p a predicate which will be logically-ANDed with this predicate.
     * @return a new predicate which returns {@code true} if either predicate
     * returns {@code true}.
     */
    public default DoublePredicate or(DoublePredicate p) {
        Objects.requireNonNull(p);
        return (value) -> test(value) || p.test(value);
    }

    /**
     * Returns a predicate that evaluates to {@code true} if both or neither of
     * the component predicates evaluate to {@code true}.
     *
     * @param p a predicate which will be logically-XORed with this predicate.
     * @return a predicate that evaluates to {@code true} if all or none of the
     * component predicates evaluate to {@code true}.
     */
    public default DoublePredicate xor(DoublePredicate p) {
        Objects.requireNonNull(p);
        return (value) -> test(value) ^ p.test(value);
    }
}
