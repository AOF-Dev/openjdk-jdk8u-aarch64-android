/*
 * Copyright (c) 2012, 2013, Oracle and/or its affiliates. All rights reserved.
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

/*
 * This file is available under and governed by the GNU General Public
 * License version 2 only, as published by the Free Software Foundation.
 * However, the following notice accompanied the original version of this
 * file:
 *
 * Copyright (c) 2012, Stephen Colebourne & Michael Nascimento Santos
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither the name of JSR-310 nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
package java.time.temporal;

import java.time.DateTimeException;
import java.time.ZoneId;

/**
 * Framework-level interface defining read-only access to a temporal object,
 * such as a date, time, offset or some combination of these.
 * <p>
 * This is the base interface type for date, time and offset objects.
 * It is implemented by those classes that can provide information
 * as {@linkplain TemporalField fields} or {@linkplain TemporalQuery queries}.
 * <p>
 * Most date and time information can be represented as a number.
 * These are modeled using {@code TemporalField} with the number held using
 * a {@code long} to handle large values. Year, month and day-of-month are
 * simple examples of fields, but they also include instant and offsets.
 * See {@link ChronoField} for the standard set of fields.
 * <p>
 * Two pieces of date/time information cannot be represented by numbers,
 * the {@linkplain java.time.chrono.Chronology chronology} and the {@linkplain ZoneId time-zone}.
 * These can be accessed via {@link #query(TemporalQuery) queries} using
 * the static methods defined on {@link Queries}.
 * <p>
 * A sub-interface, {@link Temporal}, extends this definition to one that also
 * supports adjustment and manipulation on more complete temporal objects.
 * <p>
 * This interface is a framework-level interface that should not be widely
 * used in application code. Instead, applications should create and pass
 * around instances of concrete types, such as {@code LocalDate}.
 * There are many reasons for this, part of which is that implementations
 * of this interface may be in calendar systems other than ISO.
 * See {@link java.time.chrono.ChronoLocalDate} for a fuller discussion of the issues.
 *
 * <h3>Specification for implementors</h3>
 * This interface places no restrictions on the mutability of implementations,
 * however immutability is strongly recommended.
 *
 * @since 1.8
 */
public interface TemporalAccessor {

    /**
     * Checks if the specified field is supported.
     * <p>
     * This checks if the date-time can be queried for the specified field.
     * If false, then calling the {@link #range(TemporalField) range} and {@link #get(TemporalField) get}
     * methods will throw an exception.
     *
     * <h3>Specification for implementors</h3>
     * Implementations must check and handle all fields defined in {@link ChronoField}.
     * If the field is supported, then true is returned, otherwise false
     * <p>
     * If the field is not a {@code ChronoField}, then the result of this method
     * is obtained by invoking {@code TemporalField.isSupportedBy(TemporalAccessor)}
     * passing {@code this} as the argument.
     * <p>
     * Implementations must not alter either this object.
     *
     * @param field  the field to check, null returns false
     * @return true if this date-time can be queried for the field, false if not
     */
    boolean isSupported(TemporalField field);

    /**
     * Gets the range of valid values for the specified field.
     * <p>
     * All fields can be expressed as a {@code long} integer.
     * This method returns an object that describes the valid range for that value.
     * The value of this temporal object is used to enhance the accuracy of the returned range.
     * If the date-time cannot return the range, because the field is unsupported or for
     * some other reason, an exception will be thrown.
     * <p>
     * Note that the result only describes the minimum and maximum valid values
     * and it is important not to read too much into them. For example, there
     * could be values within the range that are invalid for the field.
     *
     * <h3>Specification for implementors</h3>
     * Implementations must check and handle all fields defined in {@link ChronoField}.
     * If the field is supported, then the range of the field must be returned.
     * If unsupported, then a {@code DateTimeException} must be thrown.
     * <p>
     * If the field is not a {@code ChronoField}, then the result of this method
     * is obtained by invoking {@code TemporalField.rangeRefinedBy(TemporalAccessorl)}
     * passing {@code this} as the argument.
     * <p>
     * Implementations must not alter either this object.
     * <p>
     * The default implementation must behave equivalent to this code:
     * <pre>
     *  if (field instanceof ChronoField) {
     *    if (isSupported(field)) {
     *      return field.range();
     *    }
     *    throw new DateTimeException("Unsupported field: " + field.getName());
     *  }
     *  return field.rangeRefinedBy(this);
     * </pre>
     *
     * @param field  the field to query the range for, not null
     * @return the range of valid values for the field, not null
     * @throws DateTimeException if the range for the field cannot be obtained
     */
    public default ValueRange range(TemporalField field) {
        if (field instanceof ChronoField) {
            if (isSupported(field)) {
                return field.range();
            }
            throw new DateTimeException("Unsupported field: " + field.getName());
        }
        return field.rangeRefinedBy(this);
    }

    /**
     * Gets the value of the specified field as an {@code int}.
     * <p>
     * This queries the date-time for the value for the specified field.
     * The returned value will always be within the valid range of values for the field.
     * If the date-time cannot return the value, because the field is unsupported or for
     * some other reason, an exception will be thrown.
     *
     * <h3>Specification for implementors</h3>
     * Implementations must check and handle all fields defined in {@link ChronoField}.
     * If the field is supported and has an {@code int} range, then the value of
     * the field must be returned.
     * If unsupported, then a {@code DateTimeException} must be thrown.
     * <p>
     * If the field is not a {@code ChronoField}, then the result of this method
     * is obtained by invoking {@code TemporalField.getFrom(TemporalAccessor)}
     * passing {@code this} as the argument.
     * <p>
     * Implementations must not alter either this object.
     * <p>
     * The default implementation must behave equivalent to this code:
     * <pre>
     *  return range(field).checkValidIntValue(getLong(field), field);
     * </pre>
     *
     * @param field  the field to get, not null
     * @return the value for the field, within the valid range of values
     * @throws DateTimeException if a value for the field cannot be obtained
     * @throws DateTimeException if the range of valid values for the field exceeds an {@code int}
     * @throws DateTimeException if the value is outside the range of valid values for the field
     * @throws ArithmeticException if numeric overflow occurs
     */
    public default int get(TemporalField field) {
        return range(field).checkValidIntValue(getLong(field), field);
    }

    /**
     * Gets the value of the specified field as a {@code long}.
     * <p>
     * This queries the date-time for the value for the specified field.
     * The returned value may be outside the valid range of values for the field.
     * If the date-time cannot return the value, because the field is unsupported or for
     * some other reason, an exception will be thrown.
     *
     * <h3>Specification for implementors</h3>
     * Implementations must check and handle all fields defined in {@link ChronoField}.
     * If the field is supported, then the value of the field must be returned.
     * If unsupported, then a {@code DateTimeException} must be thrown.
     * <p>
     * If the field is not a {@code ChronoField}, then the result of this method
     * is obtained by invoking {@code TemporalField.getFrom(TemporalAccessor)}
     * passing {@code this} as the argument.
     * <p>
     * Implementations must not alter either this object.
     *
     * @param field  the field to get, not null
     * @return the value for the field
     * @throws DateTimeException if a value for the field cannot be obtained
     * @throws ArithmeticException if numeric overflow occurs
     */
    long getLong(TemporalField field);

    /**
     * Queries this date-time.
     * <p>
     * This queries this date-time using the specified query strategy object.
     * <p>
     * Queries are a key tool for extracting information from date-times.
     * They exists to externalize the process of querying, permitting different
     * approaches, as per the strategy design pattern.
     * Examples might be a query that checks if the date is the day before February 29th
     * in a leap year, or calculates the number of days to your next birthday.
     * <p>
     * The most common query implementations are method references, such as
     * {@code LocalDate::from} and {@code ZoneId::from}.
     * Further implementations are on {@link Queries}.
     * Queries may also be defined by applications.
     *
     * <h3>Specification for implementors</h3>
     * The default implementation must behave equivalent to this code:
     * <pre>
     *  if (query == Queries.zoneId() || query == Queries.chronology() || query == Queries.precision()) {
     *    return null;
     *  }
     *  return query.queryFrom(this);
     * </pre>
     * Future versions are permitted to add further queries to the if statement.
     * <p>
     * All classes implementing this interface and overriding this method must call
     * {@code TemporalAccessor.super.query(query)}. JDK classes may avoid calling
     * super if they provide behavior equivalent to the default behaviour, however
     * non-JDK classes may not utilize this optimization and must call {@code super}.
     * <p>
     * If the implementation can supply a value for one of the queries listed in the
     * if statement of the default implementation, then it must do so.
     * For example, an application-defined {@code HourMin} class storing the hour
     * and minute must override this method as follows:
     * <pre>
     *  if (query == Queries.precision()) {
     *    return MINUTES;
     *  }
     *  return TemporalAccessor.super.query(query);
     * </pre>
     *
     * @param <R> the type of the result
     * @param query  the query to invoke, not null
     * @return the query result, null may be returned (defined by the query)
     * @throws DateTimeException if unable to query
     * @throws ArithmeticException if numeric overflow occurs
     */
    public default <R> R query(TemporalQuery<R> query) {
        if (query == Queries.zoneId() || query == Queries.chronology() || query == Queries.precision()) {
            return null;
        }
        return query.queryFrom(this);
    }

}
