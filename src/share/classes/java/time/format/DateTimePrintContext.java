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
 * Copyright (c) 2011-2012, Stephen Colebourne & Michael Nascimento Santos
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
package java.time.format;

import static java.time.temporal.ChronoField.EPOCH_DAY;
import static java.time.temporal.ChronoField.INSTANT_SECONDS;

import java.time.DateTimeException;
import java.time.Instant;
import java.time.ZoneId;
import java.time.chrono.Chronology;
import java.time.temporal.ChronoField;
import java.time.chrono.ChronoLocalDate;
import java.time.temporal.Queries;
import java.time.temporal.TemporalAccessor;
import java.time.temporal.TemporalField;
import java.time.temporal.TemporalQuery;
import java.time.temporal.ValueRange;
import java.util.Locale;
import java.util.Objects;

/**
 * Context object used during date and time printing.
 * <p>
 * This class provides a single wrapper to items used in the format.
 *
 * <h3>Specification for implementors</h3>
 * This class is a mutable context intended for use from a single thread.
 * Usage of the class is thread-safe within standard printing as the framework creates
 * a new instance of the class for each format and printing is single-threaded.
 *
 * @since 1.8
 */
final class DateTimePrintContext {

    /**
     * The temporal being output.
     */
    private TemporalAccessor temporal;
    /**
     * The formatter, not null.
     */
    private DateTimeFormatter formatter;
    /**
     * Whether the current formatter is optional.
     */
    private int optional;

    /**
     * Creates a new instance of the context.
     *
     * @param temporal  the temporal object being output, not null
     * @param formatter  the formatter controlling the format, not null
     */
    DateTimePrintContext(TemporalAccessor temporal, DateTimeFormatter formatter) {
        super();
        this.temporal = adjust(temporal, formatter);
        this.formatter = formatter;
    }

    private static TemporalAccessor adjust(final TemporalAccessor temporal, DateTimeFormatter formatter) {
        // normal case first
        Chronology overrideChrono = formatter.getChronology();
        ZoneId overrideZone = formatter.getZone();
        if (overrideChrono == null && overrideZone == null) {
            return temporal;
        }

        // ensure minimal change
        Chronology temporalChrono = Chronology.from(temporal);  // default to ISO, handles Instant
        ZoneId temporalZone = temporal.query(Queries.zone());  // zone then offset, handles OffsetDateTime
        if (temporal.isSupported(EPOCH_DAY) == false || Objects.equals(overrideChrono, temporalChrono)) {
            overrideChrono = null;
        }
        if (temporal.isSupported(INSTANT_SECONDS) == false || Objects.equals(overrideZone, temporalZone)) {
            overrideZone = null;
        }
        if (overrideChrono == null && overrideZone == null) {
            return temporal;
        }

        // make adjustment
        if (overrideChrono != null && overrideZone != null) {
            return overrideChrono.zonedDateTime(Instant.from(temporal), overrideZone);
        } else if (overrideZone != null) {
            return temporalChrono.zonedDateTime(Instant.from(temporal), overrideZone);
        } else {  // overrideChrono != null
            // need class here to handle non-standard cases
            final ChronoLocalDate date = overrideChrono.date(temporal);
            return new TemporalAccessor() {
                @Override
                public boolean isSupported(TemporalField field) {
                    return temporal.isSupported(field);
                }
                @Override
                public ValueRange range(TemporalField field) {
                    if (field instanceof ChronoField) {
                        if (((ChronoField) field).isDateField()) {
                            return date.range(field);
                        } else {
                            return temporal.range(field);
                        }
                    }
                    return field.rangeRefinedBy(this);
                }
                @Override
                public long getLong(TemporalField field) {
                    if (field instanceof ChronoField) {
                        if (((ChronoField) field).isDateField()) {
                            return date.getLong(field);
                        } else {
                            return temporal.getLong(field);
                        }
                    }
                    return field.getFrom(this);
                }
                @SuppressWarnings("unchecked")
                @Override
                public <R> R query(TemporalQuery<R> query) {
                    if (query == Queries.chronology()) {
                        return (R) date.getChronology();
                    }
                    if (query == Queries.zoneId() || query == Queries.precision()) {
                        return temporal.query(query);
                    }
                    return query.queryFrom(this);
                }
            };
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Gets the temporal object being output.
     *
     * @return the temporal object, not null
     */
    TemporalAccessor getTemporal() {
        return temporal;
    }

    /**
     * Gets the locale.
     * <p>
     * This locale is used to control localization in the format output except
     * where localization is controlled by the symbols.
     *
     * @return the locale, not null
     */
    Locale getLocale() {
        return formatter.getLocale();
    }

    /**
     * Gets the formatting symbols.
     * <p>
     * The symbols control the localization of numeric output.
     *
     * @return the formatting symbols, not null
     */
    DateTimeFormatSymbols getSymbols() {
        return formatter.getSymbols();
    }

    //-----------------------------------------------------------------------
    /**
     * Starts the printing of an optional segment of the input.
     */
    void startOptional() {
        this.optional++;
    }

    /**
     * Ends the printing of an optional segment of the input.
     */
    void endOptional() {
        this.optional--;
    }

    /**
     * Gets a value using a query.
     *
     * @param query  the query to use, not null
     * @return the result, null if not found and optional is true
     * @throws DateTimeException if the type is not available and the section is not optional
     */
    <R> R getValue(TemporalQuery<R> query) {
        R result = temporal.query(query);
        if (result == null && optional == 0) {
            throw new DateTimeException("Unable to extract value: " + temporal.getClass());
        }
        return result;
    }

    /**
     * Gets the value of the specified field.
     * <p>
     * This will return the value for the specified field.
     *
     * @param field  the field to find, not null
     * @return the value, null if not found and optional is true
     * @throws DateTimeException if the field is not available and the section is not optional
     */
    Long getValue(TemporalField field) {
        try {
            return temporal.getLong(field);
        } catch (DateTimeException ex) {
            if (optional > 0) {
                return null;
            }
            throw ex;
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Returns a string version of the context for debugging.
     *
     * @return a string representation of the context, not null
     */
    @Override
    public String toString() {
        return temporal.toString();
    }

}
