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
 * Copyright (c) 2008-2012, Stephen Colebourne & Michael Nascimento Santos
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
package java.time;

import static java.time.temporal.ChronoField.MONTH_OF_YEAR;
import static java.time.temporal.ChronoUnit.DAYS;
import static java.time.temporal.ChronoUnit.MONTHS;
import static java.time.temporal.ChronoUnit.YEARS;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import java.io.InvalidObjectException;
import java.io.ObjectStreamException;
import java.io.Serializable;
import java.time.chrono.ChronoLocalDate;
import java.time.chrono.Chronology;
import java.time.format.DateTimeParseException;
import java.time.temporal.ChronoUnit;
import java.time.temporal.Temporal;
import java.time.temporal.TemporalAmount;
import java.time.temporal.TemporalUnit;
import java.time.temporal.ValueRange;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Objects;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * A date-based amount of time, such as '2 years, 3 months and 4 days'.
 * <p>
 * This class models a quantity or amount of time in terms of years, months and days.
 * See {@link Duration} for the time-based equivalent to this class.
 * <p>
 * Durations and period differ in their treatment of daylight savings time
 * when added to {@link ZonedDateTime}. A {@code Duration} will add an exact
 * number of seconds, thus a duration of one day is always exactly 24 hours.
 * By contrast, a {@code Period} will add a conceptual day, trying to maintain
 * the local time.
 * <p>
 * For example, consider adding a period of one day and a duration of one day to
 * 18:00 on the evening before a daylight savings gap. The {@code Period} will add
 * the conceptual day and result in a {@code ZonedDateTime} at 18:00 the following day.
 * By contrast, the {@code Duration} will add exactly 24 hours, resulting in a
 * {@code ZonedDateTime} at 19:00 the following day (assuming a one hour DST gap).
 * <p>
 * The supported units of a period are {@link ChronoUnit#YEARS YEARS},
 * {@link ChronoUnit#MONTHS MONTHS} and {@link ChronoUnit#DAYS DAYS}.
 * All three fields are always present, but may be set to zero.
 * <p>
 * The period may be used with any calendar system.
 * The meaning of a "year" or "month" is only applied when the object is added to a date.
 * <p>
 * The period is modeled as a directed amount of time, meaning that individual parts of the
 * period may be negative.
 * <p>
 * The months and years fields may be {@linkplain #normalized() normalized}.
 * The normalization assumes a 12 month year, so is not appropriate for all calendar systems.
 *
 * <h3>Specification for implementors</h3>
 * This class is immutable and thread-safe.
 *
 * @since 1.8
 */
public final class Period
        implements TemporalAmount, Serializable {

    /**
     * A constant for a period of zero.
     */
    public static final Period ZERO = new Period(0, 0, 0);
    /**
     * Serialization version.
     */
    private static final long serialVersionUID = -3587258372562876L;
    /**
     * The pattern for parsing.
     */
    private final static Pattern PATTERN =
            Pattern.compile("([-+]?)P(?:([-+]?[0-9]+)Y)?(?:([-+]?[0-9]+)M)?(?:([-+]?[0-9]+)D)?", Pattern.CASE_INSENSITIVE);
    /**
     * The set of supported units.
     */
    private final static List<TemporalUnit> SUPPORTED_UNITS =
            Collections.unmodifiableList(Arrays.<TemporalUnit>asList(YEARS, MONTHS, DAYS));

    /**
     * The number of years.
     */
    private final int years;
    /**
     * The number of months.
     */
    private final int months;
    /**
     * The number of days.
     */
    private final int days;

    //-----------------------------------------------------------------------
    /**
     * Obtains a {@code Period} representing a number of years.
     * <p>
     * The resulting period will have the specified years.
     * The months and days units will be zero.
     *
     * @param years  the number of years, positive or negative
     * @return the period of years, not null
     */
    public static Period ofYears(int years) {
        return create(years, 0, 0);
    }

    /**
     * Obtains a {@code Period} representing a number of months.
     * <p>
     * The resulting period will have the specified months.
     * The years and days units will be zero.
     *
     * @param months  the number of months, positive or negative
     * @return the period of months, not null
     */
    public static Period ofMonths(int months) {
        return create(0, months, 0);
    }

    /**
     * Obtains a {@code Period} representing a number of days.
     * <p>
     * The resulting period will have the specified days.
     * The years and months units will be zero.
     *
     * @param days  the number of days, positive or negative
     * @return the period of days, not null
     */
    public static Period ofDays(int days) {
        return create(0, 0, days);
    }

    //-----------------------------------------------------------------------
    /**
     * Obtains a {@code Period} representing a number of years, months and days.
     * <p>
     * This creates an instance based on years, months and days.
     *
     * @param years  the amount of years, may be negative
     * @param months  the amount of months, may be negative
     * @param days  the amount of days, may be negative
     * @return the period of years, months and days, not null
     */
    public static Period of(int years, int months, int days) {
        return create(years, months, days);
    }

    //-----------------------------------------------------------------------
    /**
     * Obtains a {@code Period} consisting of the number of years, months,
     * and days between two dates.
     * <p>
     * The start date is included, but the end date is not.
     * The period is calculated by removing complete months, then calculating
     * the remaining number of days, adjusting to ensure that both have the same sign.
     * The number of months is then split into years and months based on a 12 month year.
     * A month is considered if the end day-of-month is greater than or equal to the start day-of-month.
     * For example, from {@code 2010-01-15} to {@code 2011-03-18} is one year, two months and three days.
     * <p>
     * The result of this method can be a negative period if the end is before the start.
     * The negative sign will be the same in each of year, month and day.
     *
     * @param startDate  the start date, inclusive, not null
     * @param endDate  the end date, exclusive, not null
     * @return the period between this date and the end date, not null
     * @see ChronoLocalDate#periodUntil(ChronoLocalDate)
     */
    public static Period between(LocalDate startDate, LocalDate endDate) {
        return startDate.periodUntil(endDate);
    }

    //-----------------------------------------------------------------------
    /**
     * Obtains a {@code Period} from a text string such as {@code PnYnMnD}.
     * <p>
     * This will parse the string produced by {@code toString()} which is
     * based on the ISO-8601 period format {@code PnYnMnD}.
     * <p>
     * The string starts with an optional sign, denoted by the ASCII negative
     * or positive symbol. If negative, the whole period is negated.
     * The ASCII letter "P" is next in upper or lower case.
     * There are then three sections, each consisting of a number and a suffix.
     * At least one of the three sections must be present.
     * The sections have suffixes in ASCII of "Y", "M" and "D" for
     * years, months and days, accepted in upper or lower case.
     * The suffixes must occur in order.
     * The number part of each section must consist of ASCII digits.
     * The number may be prefixed by the ASCII negative or positive symbol.
     * The number must parse to an {@code int}.
     * <p>
     * The leading plus/minus sign, and negative values for other units are
     * not part of the ISO-8601 standard.
     *
     * @param text  the text to parse, not null
     * @return the parsed period, not null
     * @throws DateTimeParseException if the text cannot be parsed to a period
     */
    public static Period parse(CharSequence text) {
        Objects.requireNonNull(text, "text");
        Matcher matcher = PATTERN.matcher(text);
        if (matcher.matches()) {
            int negate = ("-".equals(matcher.group(1)) ? -1 : 1);
            String yearMatch = matcher.group(2);
            String monthMatch = matcher.group(3);
            String dayMatch = matcher.group(4);
            if (yearMatch != null || monthMatch != null || dayMatch != null) {
                try {
                    return create(parseNumber(text, yearMatch, negate),
                            parseNumber(text, monthMatch, negate),
                            parseNumber(text, dayMatch, negate));
                } catch (NumberFormatException ex) {
                    throw (DateTimeParseException) new DateTimeParseException("Text cannot be parsed to a Period", text, 0).initCause(ex);
                }
            }
        }
        throw new DateTimeParseException("Text cannot be parsed to a Period", text, 0);
    }

    private static int parseNumber(CharSequence text, String str, int negate) {
        if (str == null) {
            return 0;
        }
        int val = Integer.parseInt(str);
        try {
            return Math.multiplyExact(val, negate);
        } catch (ArithmeticException ex) {
            throw (DateTimeParseException) new DateTimeParseException("Text cannot be parsed to a Period", text, 0).initCause(ex);
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Creates an instance.
     *
     * @param years  the amount
     * @param months  the amount
     * @param days  the amount
     */
    private static Period create(int years, int months, int days) {
        if ((years | months | days) == 0) {
            return ZERO;
        }
        return new Period(years, months, days);
    }

    /**
     * Constructor.
     *
     * @param years  the amount
     * @param months  the amount
     * @param days  the amount
     */
    private Period(int years, int months, int days) {
        this.years = years;
        this.months = months;
        this.days = days;
    }

    //-----------------------------------------------------------------------
    /**
     * Gets the value of the requested unit.
     * <p>
     * This returns a value for each of the three supported units,
     * {@link ChronoUnit#YEARS YEARS}, {@link ChronoUnit#MONTHS MONTHS} and
     * {@link ChronoUnit#DAYS DAYS}.
     * All other units throw an exception.
     *
     * @param unit the {@code TemporalUnit} for which to return the value
     * @return the long value of the unit
     * @throws DateTimeException if the unit is not supported
     */
    @Override
    public long get(TemporalUnit unit) {
        if (unit == ChronoUnit.YEARS) {
            return getYears();
        } else if (unit == ChronoUnit.MONTHS) {
            return getMonths();
        } else if (unit == ChronoUnit.DAYS) {
            return getDays();
        } else {
            throw new DateTimeException("Unsupported unit: " + unit.getName());
        }
    }

    /**
     * Gets the set of units supported by this period.
     * <p>
     * The supported units are {@link ChronoUnit#YEARS YEARS},
     * {@link ChronoUnit#MONTHS MONTHS} and {@link ChronoUnit#DAYS DAYS}.
     * They are returned in the order years, months, days.
     * <p>
     * This set can be used in conjunction with {@link #get(TemporalUnit)}
     * to access the entire state of the period.
     *
     * @return a list containing the years, months and days units, not null
     */
    @Override
    public List<TemporalUnit> getUnits() {
        return SUPPORTED_UNITS;
    }

    //-----------------------------------------------------------------------
    /**
     * Checks if all three units of this period are zero.
     * <p>
     * A zero period has the value zero for the years, months and days units.
     *
     * @return true if this period is zero-length
     */
    public boolean isZero() {
        return (this == ZERO);
    }

    /**
     * Checks if any of the three units of this period are negative.
     * <p>
     * This checks whether the years, months or days units are less than zero.
     *
     * @return true if any unit of this period is negative
     */
    public boolean isNegative() {
        return years < 0 || months < 0 || days < 0;
    }

    //-----------------------------------------------------------------------
    /**
     * Gets the amount of years of this period.
     * <p>
     * This returns the years unit.
     * <p>
     * The months unit is not normalized with the years unit.
     * This means that a period of "15 months" is different to a period
     * of "1 year and 3 months".
     *
     * @return the amount of years of this period, may be negative
     */
    public int getYears() {
        return years;
    }

    /**
     * Gets the amount of months of this period.
     * <p>
     * This returns the months unit.
     * <p>
     * The months unit is not normalized with the years unit.
     * This means that a period of "15 months" is different to a period
     * of "1 year and 3 months".
     *
     * @return the amount of months of this period, may be negative
     */
    public int getMonths() {
        return months;
    }

    /**
     * Gets the amount of days of this period.
     * <p>
     * This returns the days unit.
     *
     * @return the amount of days of this period, may be negative
     */
    public int getDays() {
        return days;
    }

    //-----------------------------------------------------------------------
    /**
     * Returns a copy of this period with the specified amount of years.
     * <p>
     * This sets the amount of the years unit in a copy of this period.
     * The months and days units are unaffected.
     * <p>
     * The months unit is not normalized with the years unit.
     * This means that a period of "15 months" is different to a period
     * of "1 year and 3 months".
     * <p>
     * This instance is immutable and unaffected by this method call.
     *
     * @param years  the years to represent, may be negative
     * @return a {@code Period} based on this period with the requested years, not null
     */
    public Period withYears(int years) {
        if (years == this.years) {
            return this;
        }
        return create(years, months, days);
    }

    /**
     * Returns a copy of this period with the specified amount of months.
     * <p>
     * This sets the amount of the months unit in a copy of this period.
     * The years and days units are unaffected.
     * <p>
     * The months unit is not normalized with the years unit.
     * This means that a period of "15 months" is different to a period
     * of "1 year and 3 months".
     * <p>
     * This instance is immutable and unaffected by this method call.
     *
     * @param months  the months to represent, may be negative
     * @return a {@code Period} based on this period with the requested months, not null
     */
    public Period withMonths(int months) {
        if (months == this.months) {
            return this;
        }
        return create(years, months, days);
    }

    /**
     * Returns a copy of this period with the specified amount of days.
     * <p>
     * This sets the amount of the days unit in a copy of this period.
     * The years and months units are unaffected.
     * <p>
     * This instance is immutable and unaffected by this method call.
     *
     * @param days  the days to represent, may be negative
     * @return a {@code Period} based on this period with the requested days, not null
     */
    public Period withDays(int days) {
        if (days == this.days) {
            return this;
        }
        return create(years, months, days);
    }

    //-----------------------------------------------------------------------
    /**
     * Returns a copy of this period with the specified period added.
     * <p>
     * This operates separately on the years, months, days and the normalized time.
     * There is no further normalization beyond the normalized time.
     * <p>
     * For example, "1 year, 6 months and 3 days" plus "2 years, 2 months and 2 days"
     * returns "3 years, 8 months and 5 days".
     * <p>
     * This instance is immutable and unaffected by this method call.
     *
     * @param amountToAdd  the period to add, not null
     * @return a {@code Period} based on this period with the requested period added, not null
     * @throws ArithmeticException if numeric overflow occurs
     */
    public Period plus(Period amountToAdd) {
        return create(
                Math.addExact(years, amountToAdd.years),
                Math.addExact(months, amountToAdd.months),
                Math.addExact(days, amountToAdd.days));
    }

    /**
     * Returns a copy of this period with the specified years added.
     * <p>
     * This adds the amount to the years unit in a copy of this period.
     * The months and days units are unaffected.
     * For example, "1 year, 6 months and 3 days" plus 2 years returns "3 years, 6 months and 3 days".
     * <p>
     * This instance is immutable and unaffected by this method call.
     *
     * @param yearsToAdd  the years to add, positive or negative
     * @return a {@code Period} based on this period with the specified years added, not null
     * @throws ArithmeticException if numeric overflow occurs
     */
    public Period plusYears(long yearsToAdd) {
        if (yearsToAdd == 0) {
            return this;
        }
        return create(Math.toIntExact(Math.addExact(years, yearsToAdd)), months, days);
    }

    /**
     * Returns a copy of this period with the specified months added.
     * <p>
     * This adds the amount to the months unit in a copy of this period.
     * The years and days units are unaffected.
     * For example, "1 year, 6 months and 3 days" plus 2 months returns "1 year, 8 months and 3 days".
     * <p>
     * This instance is immutable and unaffected by this method call.
     *
     * @param monthsToAdd  the months to add, positive or negative
     * @return a {@code Period} based on this period with the specified months added, not null
     * @throws ArithmeticException if numeric overflow occurs
     */
    public Period plusMonths(long monthsToAdd) {
        if (monthsToAdd == 0) {
            return this;
        }
        return create(years, Math.toIntExact(Math.addExact(months, monthsToAdd)), days);
    }

    /**
     * Returns a copy of this period with the specified days added.
     * <p>
     * This adds the amount to the days unit in a copy of this period.
     * The years and months units are unaffected.
     * For example, "1 year, 6 months and 3 days" plus 2 days returns "1 year, 6 months and 5 days".
     * <p>
     * This instance is immutable and unaffected by this method call.
     *
     * @param daysToAdd  the days to add, positive or negative
     * @return a {@code Period} based on this period with the specified days added, not null
     * @throws ArithmeticException if numeric overflow occurs
     */
    public Period plusDays(long daysToAdd) {
        if (daysToAdd == 0) {
            return this;
        }
        return create(years, months, Math.toIntExact(Math.addExact(days, daysToAdd)));
    }

    //-----------------------------------------------------------------------
    /**
     * Returns a copy of this period with the specified period subtracted.
     * <p>
     * This operates separately on the years, months, days and the normalized time.
     * There is no further normalization beyond the normalized time.
     * <p>
     * For example, "1 year, 6 months and 3 days" minus "2 years, 2 months and 2 days"
     * returns "-1 years, 4 months and 1 day".
     * <p>
     * This instance is immutable and unaffected by this method call.
     *
     * @param amountToSubtract  the period to subtract, not null
     * @return a {@code Period} based on this period with the requested period subtracted, not null
     * @throws ArithmeticException if numeric overflow occurs
     */
    public Period minus(Period amountToSubtract) {
        return create(
                Math.subtractExact(years, amountToSubtract.years),
                Math.subtractExact(months, amountToSubtract.months),
                Math.subtractExact(days, amountToSubtract.days));
    }

    /**
     * Returns a copy of this period with the specified years subtracted.
     * <p>
     * This subtracts the amount from the years unit in a copy of this period.
     * The months and days units are unaffected.
     * For example, "1 year, 6 months and 3 days" minus 2 years returns "-1 years, 6 months and 3 days".
     * <p>
     * This instance is immutable and unaffected by this method call.
     *
     * @param yearsToSubtract  the years to subtract, positive or negative
     * @return a {@code Period} based on this period with the specified years subtracted, not null
     * @throws ArithmeticException if numeric overflow occurs
     */
    public Period minusYears(long yearsToSubtract) {
        return (yearsToSubtract == Long.MIN_VALUE ? plusYears(Long.MAX_VALUE).plusYears(1) : plusYears(-yearsToSubtract));
    }

    /**
     * Returns a copy of this period with the specified months subtracted.
     * <p>
     * This subtracts the amount from the months unit in a copy of this period.
     * The years and days units are unaffected.
     * For example, "1 year, 6 months and 3 days" minus 2 months returns "1 year, 4 months and 3 days".
     * <p>
     * This instance is immutable and unaffected by this method call.
     *
     * @param monthsToSubtract  the years to subtract, positive or negative
     * @return a {@code Period} based on this period with the specified months subtracted, not null
     * @throws ArithmeticException if numeric overflow occurs
     */
    public Period minusMonths(long monthsToSubtract) {
        return (monthsToSubtract == Long.MIN_VALUE ? plusMonths(Long.MAX_VALUE).plusMonths(1) : plusMonths(-monthsToSubtract));
    }

    /**
     * Returns a copy of this period with the specified days subtracted.
     * <p>
     * This subtracts the amount from the days unit in a copy of this period.
     * The years and months units are unaffected.
     * For example, "1 year, 6 months and 3 days" minus 2 days returns "1 year, 6 months and 1 day".
     * <p>
     * This instance is immutable and unaffected by this method call.
     *
     * @param daysToSubtract  the months to subtract, positive or negative
     * @return a {@code Period} based on this period with the specified days subtracted, not null
     * @throws ArithmeticException if numeric overflow occurs
     */
    public Period minusDays(long daysToSubtract) {
        return (daysToSubtract == Long.MIN_VALUE ? plusDays(Long.MAX_VALUE).plusDays(1) : plusDays(-daysToSubtract));
    }

    //-----------------------------------------------------------------------
    /**
     * Returns a new instance with each element in this period multiplied
     * by the specified scalar.
     * <p>
     * This returns a period with each of the years, months and days units
     * individually multiplied.
     * For example, a period of "2 years, -3 months and 4 days" multiplied by
     * 3 will return "6 years, -9 months and 12 days".
     * No normalization is performed.
     *
     * @param scalar  the scalar to multiply by, not null
     * @return a {@code Period} based on this period with the amounts multiplied by the scalar, not null
     * @throws ArithmeticException if numeric overflow occurs
     */
    public Period multipliedBy(int scalar) {
        if (this == ZERO || scalar == 1) {
            return this;
        }
        return create(
                Math.multiplyExact(years, scalar),
                Math.multiplyExact(months, scalar),
                Math.multiplyExact(days, scalar));
    }

    /**
     * Returns a new instance with each amount in this period negated.
     * <p>
     * This returns a period with each of the years, months and days units
     * individually negated.
     * For example, a period of "2 years, -3 months and 4 days" will be
     * negated to "-2 years, 3 months and -4 days".
     * No normalization is performed.
     *
     * @return a {@code Period} based on this period with the amounts negated, not null
     * @throws ArithmeticException if numeric overflow occurs, which only happens if
     *  one of the units has the value {@code Long.MIN_VALUE}
     */
    public Period negated() {
        return multipliedBy(-1);
    }

    //-----------------------------------------------------------------------
    /**
     * Returns a copy of this period with the years and months normalized
     * using a 12 month year.
     * <p>
     * This normalizes the years and months units, leaving the days unit unchanged.
     * The months unit is adjusted to have an absolute value less than 11,
     * with the years unit being adjusted to compensate. For example, a period of
     * "1 Year and 15 months" will be normalized to "2 years and 3 months".
     * <p>
     * The sign of the years and months units will be the same after normalization.
     * For example, a period of "1 year and -25 months" will be normalized to
     * "-1 year and -1 month".
     * <p>
     * This normalization uses a 12 month year which is not valid for all calendar systems.
     * <p>
     * This instance is immutable and unaffected by this method call.
     *
     * @return a {@code Period} based on this period with excess months normalized to years, not null
     * @throws ArithmeticException if numeric overflow occurs
     */
    public Period normalized() {
        long totalMonths = toTotalMonths();
        long splitYears = totalMonths / 12;
        int splitMonths = (int) (totalMonths % 12);  // no overflow
        if (splitYears == years && splitMonths == months) {
            return this;
        }
        return create(Math.toIntExact(splitYears), splitMonths, days);
    }

    /**
     * Gets the total number of months in this period using a 12 month year.
     * <p>
     * This returns the total number of months in the period by multiplying the
     * number of years by 12 and adding the number of months.
     * <p>
     * This uses a 12 month year which is not valid for all calendar systems.
     * <p>
     * This instance is immutable and unaffected by this method call.
     *
     * @return the total number of months in the period, may be negative
     */
    public long toTotalMonths() {
        return years * 12L + months;  // no overflow
    }

    //-------------------------------------------------------------------------
    /**
     * Adds this period to the specified temporal object.
     * <p>
     * This returns a temporal object of the same observable type as the input
     * with this period added.
     * <p>
     * In most cases, it is clearer to reverse the calling pattern by using
     * {@link Temporal#plus(TemporalAmount)}.
     * <pre>
     *   // these two lines are equivalent, but the second approach is recommended
     *   dateTime = thisPeriod.addTo(dateTime);
     *   dateTime = dateTime.plus(thisPeriod);
     * </pre>
     * <p>
     * The calculation will add the years, then months, then days.
     * Only non-zero amounts will be added.
     * If the date-time has a calendar system with a fixed number of months in a
     * year, then the years and months will be combined before being added.
     * <p>
     * This instance is immutable and unaffected by this method call.
     *
     * @param temporal  the temporal object to adjust, not null
     * @return an object of the same type with the adjustment made, not null
     * @throws DateTimeException if unable to add
     * @throws ArithmeticException if numeric overflow occurs
     */
    @Override
    public Temporal addTo(Temporal temporal) {
        Objects.requireNonNull(temporal, "temporal");
        if ((years | months) != 0) {
            long monthRange = monthRange(temporal);
            if (monthRange >= 0) {
                temporal = temporal.plus(years * monthRange + months, MONTHS);
            } else {
                if (years != 0) {
                    temporal = temporal.plus(years, YEARS);
                }
                if (months != 0) {
                    temporal = temporal.plus(months, MONTHS);
                }
            }
        }
        if (days != 0) {
            temporal = temporal.plus(days, DAYS);
        }
        return temporal;
    }

    /**
     * Subtracts this period from the specified temporal object.
     * <p>
     * This returns a temporal object of the same observable type as the input
     * with this period subtracted.
     * <p>
     * In most cases, it is clearer to reverse the calling pattern by using
     * {@link Temporal#minus(TemporalAmount)}.
     * <pre>
     *   // these two lines are equivalent, but the second approach is recommended
     *   dateTime = thisPeriod.subtractFrom(dateTime);
     *   dateTime = dateTime.minus(thisPeriod);
     * </pre>
     * <p>
     * The calculation will subtract the years, then months, then days.
     * Only non-zero amounts will be subtracted.
     * If the date-time has a calendar system with a fixed number of months in a
     * year, then the years and months will be combined before being subtracted.
     * <p>
     * This instance is immutable and unaffected by this method call.
     *
     * @param temporal  the temporal object to adjust, not null
     * @return an object of the same type with the adjustment made, not null
     * @throws DateTimeException if unable to subtract
     * @throws ArithmeticException if numeric overflow occurs
     */
    @Override
    public Temporal subtractFrom(Temporal temporal) {
        Objects.requireNonNull(temporal, "temporal");
        if ((years | months) != 0) {
            long monthRange = monthRange(temporal);
            if (monthRange >= 0) {
                temporal = temporal.minus(years * monthRange + months, MONTHS);
            } else {
                if (years != 0) {
                    temporal = temporal.minus(years, YEARS);
                }
                if (months != 0) {
                    temporal = temporal.minus(months, MONTHS);
                }
            }
        }
        if (days != 0) {
            temporal = temporal.minus(days, DAYS);
        }
        return temporal;
    }

    /**
     * Calculates the range of months based on the temporal.
     *
     * @param temporal  the temporal, not null
     * @return the month range, negative if not fixed range
     */
    private long monthRange(Temporal temporal) {
        ValueRange startRange = Chronology.from(temporal).range(MONTH_OF_YEAR);
        if (startRange.isFixed() && startRange.isIntValue()) {
            return startRange.getMaximum() - startRange.getMinimum() + 1;
        }
        return -1;
    }

    //-----------------------------------------------------------------------
    /**
     * Checks if this period is equal to another period.
     * <p>
     * The comparison is based on the amounts held in the period.
     * To be equal, the years, months and days units must be individually equal.
     * Note that this means that a period of "15 Months" is not equal to a period
     * of "1 Year and 3 Months".
     *
     * @param obj  the object to check, null returns false
     * @return true if this is equal to the other period
     */
    @Override
    public boolean equals(Object obj) {
        if (this == obj) {
            return true;
        }
        if (obj instanceof Period) {
            Period other = (Period) obj;
            return years == other.years &&
                    months == other.months &&
                    days == other.days;
        }
        return false;
    }

    /**
     * A hash code for this period.
     *
     * @return a suitable hash code
     */
    @Override
    public int hashCode() {
        return years + Integer.rotateLeft(months, 8) + Integer.rotateLeft(days, 16);
    }

    //-----------------------------------------------------------------------
    /**
     * Outputs this period as a {@code String}, such as {@code P6Y3M1D}.
     * <p>
     * The output will be in the ISO-8601 period format.
     * A zero period will be represented as zero days, 'P0D'.
     *
     * @return a string representation of this period, not null
     */
    @Override
    public String toString() {
        if (this == ZERO) {
            return "P0D";
        } else {
            StringBuilder buf = new StringBuilder();
            buf.append('P');
            if (years != 0) {
                buf.append(years).append('Y');
            }
            if (months != 0) {
                buf.append(months).append('M');
            }
            if (days != 0) {
                buf.append(days).append('D');
            }
            return buf.toString();
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Writes the object using a
     * <a href="../../serialized-form.html#java.time.Ser">dedicated serialized form</a>.
     * <pre>
     *  out.writeByte(14);  // identifies this as a Period
     *  out.writeInt(years);
     *  out.writeInt(months);
     *  out.writeInt(seconds);
     * </pre>
     *
     * @return the instance of {@code Ser}, not null
     */
    private Object writeReplace() {
        return new Ser(Ser.PERIOD_TYPE, this);
    }

    /**
     * Defend against malicious streams.
     * @return never
     * @throws java.io.InvalidObjectException always
     */
    private Object readResolve() throws ObjectStreamException {
        throw new InvalidObjectException("Deserialization via serialization delegate");
    }

    void writeExternal(DataOutput out) throws IOException {
        out.writeInt(years);
        out.writeInt(months);
        out.writeInt(days);
    }

    static Period readExternal(DataInput in) throws IOException {
        int years = in.readInt();
        int months = in.readInt();
        int days = in.readInt();
        return Period.of(years, months, days);
    }

}
