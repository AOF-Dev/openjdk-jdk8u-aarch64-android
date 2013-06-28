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
package java.time.format;

import static java.time.temporal.ChronoField.DAY_OF_MONTH;
import static java.time.temporal.ChronoField.HOUR_OF_DAY;
import static java.time.temporal.ChronoField.INSTANT_SECONDS;
import static java.time.temporal.ChronoField.MINUTE_OF_HOUR;
import static java.time.temporal.ChronoField.MONTH_OF_YEAR;
import static java.time.temporal.ChronoField.NANO_OF_SECOND;
import static java.time.temporal.ChronoField.OFFSET_SECONDS;
import static java.time.temporal.ChronoField.SECOND_OF_MINUTE;
import static java.time.temporal.ChronoField.YEAR;

import java.lang.ref.SoftReference;
import java.math.BigDecimal;
import java.math.BigInteger;
import java.math.RoundingMode;
import java.text.ParsePosition;
import java.time.DateTimeException;
import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneId;
import java.time.ZoneOffset;
import java.time.chrono.Chronology;
import java.time.chrono.IsoChronology;
import java.time.chrono.JapaneseChronology;
import java.time.format.DateTimeTextProvider.LocaleStore;
import java.time.temporal.ChronoField;
import java.time.temporal.IsoFields;
import java.time.temporal.Queries;
import java.time.temporal.TemporalAccessor;
import java.time.temporal.TemporalField;
import java.time.temporal.TemporalQuery;
import java.time.temporal.ValueRange;
import java.time.temporal.WeekFields;
import java.time.zone.ZoneRulesProvider;
import java.util.AbstractMap.SimpleImmutableEntry;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Set;
import java.util.TimeZone;
import java.util.concurrent.ConcurrentHashMap;

import sun.util.locale.provider.TimeZoneNameUtility;

/**
 * Builder to create date-time formatters.
 * <p>
 * This allows a {@code DateTimeFormatter} to be created.
 * All date-time formatters are created ultimately using this builder.
 * <p>
 * The basic elements of date-time can all be added:
 * <p><ul>
 * <li>Value - a numeric value</li>
 * <li>Fraction - a fractional value including the decimal place. Always use this when
 * outputting fractions to ensure that the fraction is parsed correctly</li>
 * <li>Text - the textual equivalent for the value</li>
 * <li>OffsetId/Offset - the {@linkplain ZoneOffset zone offset}</li>
 * <li>ZoneId - the {@linkplain ZoneId time-zone} id</li>
 * <li>ZoneText - the name of the time-zone</li>
 * <li>Literal - a text literal</li>
 * <li>Nested and Optional - formats can be nested or made optional</li>
 * <li>Other - the printer and parser interfaces can be used to add user supplied formatting</li>
 * </ul><p>
 * In addition, any of the elements may be decorated by padding, either with spaces or any other character.
 * <p>
 * Finally, a shorthand pattern, mostly compatible with {@code java.text.SimpleDateFormat SimpleDateFormat}
 * can be used, see {@link #appendPattern(String)}.
 * In practice, this simply parses the pattern and calls other methods on the builder.
 *
 * <h3>Specification for implementors</h3>
 * This class is a mutable builder intended for use from a single thread.
 *
 * @since 1.8
 */
public final class DateTimeFormatterBuilder {

    /**
     * Query for a time-zone that is region-only.
     */
    private static final TemporalQuery<ZoneId> QUERY_REGION_ONLY = (temporal) -> {
        ZoneId zone = temporal.query(Queries.zoneId());
        return (zone != null && zone instanceof ZoneOffset == false ? zone : null);
    };

    /**
     * The currently active builder, used by the outermost builder.
     */
    private DateTimeFormatterBuilder active = this;
    /**
     * The parent builder, null for the outermost builder.
     */
    private final DateTimeFormatterBuilder parent;
    /**
     * The list of printers that will be used.
     */
    private final List<DateTimePrinterParser> printerParsers = new ArrayList<>();
    /**
     * Whether this builder produces an optional formatter.
     */
    private final boolean optional;
    /**
     * The width to pad the next field to.
     */
    private int padNextWidth;
    /**
     * The character to pad the next field with.
     */
    private char padNextChar;
    /**
     * The index of the last variable width value parser.
     */
    private int valueParserIndex = -1;

    /**
     * Constructs a new instance of the builder.
     */
    public DateTimeFormatterBuilder() {
        super();
        parent = null;
        optional = false;
    }

    /**
     * Constructs a new instance of the builder.
     *
     * @param parent  the parent builder, not null
     * @param optional  whether the formatter is optional, not null
     */
    private DateTimeFormatterBuilder(DateTimeFormatterBuilder parent, boolean optional) {
        super();
        this.parent = parent;
        this.optional = optional;
    }

    //-----------------------------------------------------------------------
    /**
     * Changes the parse style to be case sensitive for the remainder of the formatter.
     * <p>
     * Parsing can be case sensitive or insensitive - by default it is case sensitive.
     * This method allows the case sensitivity setting of parsing to be changed.
     * <p>
     * Calling this method changes the state of the builder such that all
     * subsequent builder method calls will parse text in case sensitive mode.
     * See {@link #parseCaseInsensitive} for the opposite setting.
     * The parse case sensitive/insensitive methods may be called at any point
     * in the builder, thus the parser can swap between case parsing modes
     * multiple times during the parse.
     * <p>
     * Since the default is case sensitive, this method should only be used after
     * a previous call to {@code #parseCaseInsensitive}.
     *
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder parseCaseSensitive() {
        appendInternal(SettingsParser.SENSITIVE);
        return this;
    }

    /**
     * Changes the parse style to be case insensitive for the remainder of the formatter.
     * <p>
     * Parsing can be case sensitive or insensitive - by default it is case sensitive.
     * This method allows the case sensitivity setting of parsing to be changed.
     * <p>
     * Calling this method changes the state of the builder such that all
     * subsequent builder method calls will parse text in case insensitive mode.
     * See {@link #parseCaseSensitive()} for the opposite setting.
     * The parse case sensitive/insensitive methods may be called at any point
     * in the builder, thus the parser can swap between case parsing modes
     * multiple times during the parse.
     *
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder parseCaseInsensitive() {
        appendInternal(SettingsParser.INSENSITIVE);
        return this;
    }

    //-----------------------------------------------------------------------
    /**
     * Changes the parse style to be strict for the remainder of the formatter.
     * <p>
     * Parsing can be strict or lenient - by default its strict.
     * This controls the degree of flexibility in matching the text and sign styles.
     * <p>
     * When used, this method changes the parsing to be strict from this point onwards.
     * As strict is the default, this is normally only needed after calling {@link #parseLenient()}.
     * The change will remain in force until the end of the formatter that is eventually
     * constructed or until {@code parseLenient} is called.
     *
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder parseStrict() {
        appendInternal(SettingsParser.STRICT);
        return this;
    }

    /**
     * Changes the parse style to be lenient for the remainder of the formatter.
     * Note that case sensitivity is set separately to this method.
     * <p>
     * Parsing can be strict or lenient - by default its strict.
     * This controls the degree of flexibility in matching the text and sign styles.
     * Applications calling this method should typically also call {@link #parseCaseInsensitive()}.
     * <p>
     * When used, this method changes the parsing to be lenient from this point onwards.
     * The change will remain in force until the end of the formatter that is eventually
     * constructed or until {@code parseStrict} is called.
     *
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder parseLenient() {
        appendInternal(SettingsParser.LENIENT);
        return this;
    }

    //-----------------------------------------------------------------------
    /**
     * Appends the value of a date-time field to the formatter using a normal
     * output style.
     * <p>
     * The value of the field will be output during a format.
     * If the value cannot be obtained then an exception will be thrown.
     * <p>
     * The value will be printed as per the normal format of an integer value.
     * Only negative numbers will be signed. No padding will be added.
     * <p>
     * The parser for a variable width value such as this normally behaves greedily,
     * requiring one digit, but accepting as many digits as possible.
     * This behavior can be affected by 'adjacent value parsing'.
     * See {@link #appendValue(java.time.temporal.TemporalField, int)} for full details.
     *
     * @param field  the field to append, not null
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder appendValue(TemporalField field) {
        Objects.requireNonNull(field, "field");
        active.valueParserIndex = appendInternal(new NumberPrinterParser(field, 1, 19, SignStyle.NORMAL));
        return this;
    }

    /**
     * Appends the value of a date-time field to the formatter using a fixed
     * width, zero-padded approach.
     * <p>
     * The value of the field will be output during a format.
     * If the value cannot be obtained then an exception will be thrown.
     * <p>
     * The value will be zero-padded on the left. If the size of the value
     * means that it cannot be printed within the width then an exception is thrown.
     * If the value of the field is negative then an exception is thrown during formatting.
     * <p>
     * This method supports a special technique of parsing known as 'adjacent value parsing'.
     * This technique solves the problem where a variable length value is followed by one or more
     * fixed length values. The standard parser is greedy, and thus it would normally
     * steal the digits that are needed by the fixed width value parsers that follow the
     * variable width one.
     * <p>
     * No action is required to initiate 'adjacent value parsing'.
     * When a call to {@code appendValue} with a variable width is made, the builder
     * enters adjacent value parsing setup mode. If the immediately subsequent method
     * call or calls on the same builder are to this method, then the parser will reserve
     * space so that the fixed width values can be parsed.
     * <p>
     * For example, consider {@code builder.appendValue(YEAR).appendValue(MONTH_OF_YEAR, 2);}
     * The year is a variable width parse of between 1 and 19 digits.
     * The month is a fixed width parse of 2 digits.
     * Because these were appended to the same builder immediately after one another,
     * the year parser will reserve two digits for the month to parse.
     * Thus, the text '201106' will correctly parse to a year of 2011 and a month of 6.
     * Without adjacent value parsing, the year would greedily parse all six digits and leave
     * nothing for the month.
     * <p>
     * Adjacent value parsing applies to each set of fixed width not-negative values in the parser
     * that immediately follow any kind of variable width value.
     * Calling any other append method will end the setup of adjacent value parsing.
     * Thus, in the unlikely event that you need to avoid adjacent value parsing behavior,
     * simply add the {@code appendValue} to another {@code DateTimeFormatterBuilder}
     * and add that to this builder.
     * <p>
     * If adjacent parsing is active, then parsing must match exactly the specified
     * number of digits in both strict and lenient modes.
     * In addition, no positive or negative sign is permitted.
     *
     * @param field  the field to append, not null
     * @param width  the width of the printed field, from 1 to 19
     * @return this, for chaining, not null
     * @throws IllegalArgumentException if the width is invalid
     */
    public DateTimeFormatterBuilder appendValue(TemporalField field, int width) {
        Objects.requireNonNull(field, "field");
        if (width < 1 || width > 19) {
            throw new IllegalArgumentException("The width must be from 1 to 19 inclusive but was " + width);
        }
        NumberPrinterParser pp = new NumberPrinterParser(field, width, width, SignStyle.NOT_NEGATIVE);
        return appendFixedWidth(width, pp);
    }

    /**
     * Appends the value of a date-time field to the formatter providing full
     * control over formatting.
     * <p>
     * The value of the field will be output during a format.
     * If the value cannot be obtained then an exception will be thrown.
     * <p>
     * This method provides full control of the numeric formatting, including
     * zero-padding and the positive/negative sign.
     * <p>
     * The parser for a variable width value such as this normally behaves greedily,
     * accepting as many digits as possible.
     * This behavior can be affected by 'adjacent value parsing'.
     * See {@link #appendValue(java.time.temporal.TemporalField, int)} for full details.
     * <p>
     * In strict parsing mode, the minimum number of parsed digits is {@code minWidth}.
     * In lenient parsing mode, the minimum number of parsed digits is one.
     * <p>
     * If this method is invoked with equal minimum and maximum widths and a sign style of
     * {@code NOT_NEGATIVE} then it delegates to {@code appendValue(TemporalField,int)}.
     * In this scenario, the formatting and parsing behavior described there occur.
     *
     * @param field  the field to append, not null
     * @param minWidth  the minimum field width of the printed field, from 1 to 19
     * @param maxWidth  the maximum field width of the printed field, from 1 to 19
     * @param signStyle  the positive/negative output style, not null
     * @return this, for chaining, not null
     * @throws IllegalArgumentException if the widths are invalid
     */
    public DateTimeFormatterBuilder appendValue(
            TemporalField field, int minWidth, int maxWidth, SignStyle signStyle) {
        if (minWidth == maxWidth && signStyle == SignStyle.NOT_NEGATIVE) {
            return appendValue(field, maxWidth);
        }
        Objects.requireNonNull(field, "field");
        Objects.requireNonNull(signStyle, "signStyle");
        if (minWidth < 1 || minWidth > 19) {
            throw new IllegalArgumentException("The minimum width must be from 1 to 19 inclusive but was " + minWidth);
        }
        if (maxWidth < 1 || maxWidth > 19) {
            throw new IllegalArgumentException("The maximum width must be from 1 to 19 inclusive but was " + maxWidth);
        }
        if (maxWidth < minWidth) {
            throw new IllegalArgumentException("The maximum width must exceed or equal the minimum width but " +
                    maxWidth + " < " + minWidth);
        }
        NumberPrinterParser pp = new NumberPrinterParser(field, minWidth, maxWidth, signStyle);
        if (minWidth == maxWidth) {
            appendInternal(pp);
        } else {
            active.valueParserIndex = appendInternal(pp);
        }
        return this;
    }

    //-----------------------------------------------------------------------
    /**
     * Appends the reduced value of a date-time field to the formatter.
     * <p>
     * This is typically used for formatting and parsing a two digit year.
     * The {@code width} is the printed and parsed width.
     * The {@code baseValue} is used during parsing to determine the valid range.
     * <p>
     * For formatting, the width is used to determine the number of characters to format.
     * The rightmost characters are output to match the width, left padding with zero.
     * <p>
     * For parsing, exactly the number of characters specified by the width are parsed.
     * This is incomplete information however, so the base value is used to complete the parse.
     * The base value is the first valid value in a range of ten to the power of width.
     * <p>
     * For example, a base value of {@code 1980} and a width of {@code 2} will have
     * valid values from {@code 1980} to {@code 2079}.
     * During parsing, the text {@code "12"} will result in the value {@code 2012} as that
     * is the value within the range where the last two digits are "12".
     * <p>
     * This is a fixed width parser operating using 'adjacent value parsing'.
     * See {@link #appendValue(java.time.temporal.TemporalField, int)} for full details.
     *
     * @param field  the field to append, not null
     * @param width  the width of the printed and parsed field, from 1 to 18
     * @param baseValue  the base value of the range of valid values
     * @return this, for chaining, not null
     * @throws IllegalArgumentException if the width or base value is invalid
     */
    public DateTimeFormatterBuilder appendValueReduced(
            TemporalField field, int width, int baseValue) {
        Objects.requireNonNull(field, "field");
        ReducedPrinterParser pp = new ReducedPrinterParser(field, width, baseValue);
        appendFixedWidth(width, pp);
        return this;
    }

    /**
     * Appends a fixed width printer-parser.
     *
     * @param width  the width
     * @param pp  the printer-parser, not null
     * @return this, for chaining, not null
     */
    private DateTimeFormatterBuilder appendFixedWidth(int width, NumberPrinterParser pp) {
        if (active.valueParserIndex >= 0) {
            // adjacent parsing mode, update setting in previous parsers
            NumberPrinterParser basePP = (NumberPrinterParser) active.printerParsers.get(active.valueParserIndex);
            basePP = basePP.withSubsequentWidth(width);
            int activeValueParser = active.valueParserIndex;
            active.printerParsers.set(active.valueParserIndex, basePP);
            appendInternal(pp.withFixedWidth());
            active.valueParserIndex = activeValueParser;
        } else {
            // not adjacent parsing
            appendInternal(pp);
        }
        return this;
    }

    //-----------------------------------------------------------------------
    /**
     * Appends the fractional value of a date-time field to the formatter.
     * <p>
     * The fractional value of the field will be output including the
     * preceding decimal point. The preceding value is not output.
     * For example, the second-of-minute value of 15 would be output as {@code .25}.
     * <p>
     * The width of the printed fraction can be controlled. Setting the
     * minimum width to zero will cause no output to be generated.
     * The printed fraction will have the minimum width necessary between
     * the minimum and maximum widths - trailing zeroes are omitted.
     * No rounding occurs due to the maximum width - digits are simply dropped.
     * <p>
     * When parsing in strict mode, the number of parsed digits must be between
     * the minimum and maximum width. When parsing in lenient mode, the minimum
     * width is considered to be zero and the maximum is nine.
     * <p>
     * If the value cannot be obtained then an exception will be thrown.
     * If the value is negative an exception will be thrown.
     * If the field does not have a fixed set of valid values then an
     * exception will be thrown.
     * If the field value in the date-time to be printed is invalid it
     * cannot be printed and an exception will be thrown.
     *
     * @param field  the field to append, not null
     * @param minWidth  the minimum width of the field excluding the decimal point, from 0 to 9
     * @param maxWidth  the maximum width of the field excluding the decimal point, from 1 to 9
     * @param decimalPoint  whether to output the localized decimal point symbol
     * @return this, for chaining, not null
     * @throws IllegalArgumentException if the field has a variable set of valid values or
     *  either width is invalid
     */
    public DateTimeFormatterBuilder appendFraction(
            TemporalField field, int minWidth, int maxWidth, boolean decimalPoint) {
        appendInternal(new FractionPrinterParser(field, minWidth, maxWidth, decimalPoint));
        return this;
    }

    //-----------------------------------------------------------------------
    /**
     * Appends the text of a date-time field to the formatter using the full
     * text style.
     * <p>
     * The text of the field will be output during a format.
     * The value must be within the valid range of the field.
     * If the value cannot be obtained then an exception will be thrown.
     * If the field has no textual representation, then the numeric value will be used.
     * <p>
     * The value will be printed as per the normal format of an integer value.
     * Only negative numbers will be signed. No padding will be added.
     *
     * @param field  the field to append, not null
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder appendText(TemporalField field) {
        return appendText(field, TextStyle.FULL);
    }

    /**
     * Appends the text of a date-time field to the formatter.
     * <p>
     * The text of the field will be output during a format.
     * The value must be within the valid range of the field.
     * If the value cannot be obtained then an exception will be thrown.
     * If the field has no textual representation, then the numeric value will be used.
     * <p>
     * The value will be printed as per the normal format of an integer value.
     * Only negative numbers will be signed. No padding will be added.
     *
     * @param field  the field to append, not null
     * @param textStyle  the text style to use, not null
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder appendText(TemporalField field, TextStyle textStyle) {
        Objects.requireNonNull(field, "field");
        Objects.requireNonNull(textStyle, "textStyle");
        appendInternal(new TextPrinterParser(field, textStyle, DateTimeTextProvider.getInstance()));
        return this;
    }

    /**
     * Appends the text of a date-time field to the formatter using the specified
     * map to supply the text.
     * <p>
     * The standard text outputting methods use the localized text in the JDK.
     * This method allows that text to be specified directly.
     * The supplied map is not validated by the builder to ensure that formatting or
     * parsing is possible, thus an invalid map may throw an error during later use.
     * <p>
     * Supplying the map of text provides considerable flexibility in formatting and parsing.
     * For example, a legacy application might require or supply the months of the
     * year as "JNY", "FBY", "MCH" etc. These do not match the standard set of text
     * for localized month names. Using this method, a map can be created which
     * defines the connection between each value and the text:
     * <pre>
     * Map&lt;Long, String&gt; map = new HashMap&lt;&gt;();
     * map.put(1, "JNY");
     * map.put(2, "FBY");
     * map.put(3, "MCH");
     * ...
     * builder.appendText(MONTH_OF_YEAR, map);
     * </pre>
     * <p>
     * Other uses might be to output the value with a suffix, such as "1st", "2nd", "3rd",
     * or as Roman numerals "I", "II", "III", "IV".
     * <p>
     * During formatting, the value is obtained and checked that it is in the valid range.
     * If text is not available for the value then it is output as a number.
     * During parsing, the parser will match against the map of text and numeric values.
     *
     * @param field  the field to append, not null
     * @param textLookup  the map from the value to the text
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder appendText(TemporalField field, Map<Long, String> textLookup) {
        Objects.requireNonNull(field, "field");
        Objects.requireNonNull(textLookup, "textLookup");
        Map<Long, String> copy = new LinkedHashMap<>(textLookup);
        Map<TextStyle, Map<Long, String>> map = Collections.singletonMap(TextStyle.FULL, copy);
        final LocaleStore store = new LocaleStore(map);
        DateTimeTextProvider provider = new DateTimeTextProvider() {
            @Override
            public String getText(TemporalField field, long value, TextStyle style, Locale locale) {
                return store.getText(value, style);
            }
            @Override
            public Iterator<Entry<String, Long>> getTextIterator(TemporalField field, TextStyle style, Locale locale) {
                return store.getTextIterator(style);
            }
        };
        appendInternal(new TextPrinterParser(field, TextStyle.FULL, provider));
        return this;
    }

    //-----------------------------------------------------------------------
    /**
     * Appends an instant using ISO-8601 to the formatter.
     * <p>
     * Instants have a fixed output format.
     * They are converted to a date-time with a zone-offset of UTC and printed
     * using the standard ISO-8601 format.
     * <p>
     * An alternative to this method is to format/parse the instant as a single
     * epoch-seconds value. That is achieved using {@code appendValue(INSTANT_SECONDS)}.
     *
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder appendInstant() {
        appendInternal(new InstantPrinterParser());
        return this;
    }

    /**
     * Appends the zone offset, such as '+01:00', to the formatter.
     * <p>
     * This appends an instruction to format/parse the offset ID to the builder.
     * This is equivalent to calling {@code appendOffset("HH:MM:ss", "Z")}.
     *
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder appendOffsetId() {
        appendInternal(OffsetIdPrinterParser.INSTANCE_ID_Z);
        return this;
    }

    /**
     * Appends the zone offset, such as '+01:00', to the formatter.
     * <p>
     * This appends an instruction to format/parse the offset ID to the builder.
     * <p>
     * During formatting, the offset is obtained using a mechanism equivalent
     * to querying the temporal with {@link Queries#offset()}.
     * It will be printed using the format defined below.
     * If the offset cannot be obtained then an exception is thrown unless the
     * section of the formatter is optional.
     * <p>
     * During parsing, the offset is parsed using the format defined below.
     * If the offset cannot be parsed then an exception is thrown unless the
     * section of the formatter is optional.
     * <p>
     * The format of the offset is controlled by a pattern which must be one
     * of the following:
     * <p><ul>
     * <li>{@code +HH} - hour only, ignoring minute and second
     * <li>{@code +HHmm} - hour, with minute if non-zero, ignoring second, no colon
     * <li>{@code +HH:mm} - hour, with minute if non-zero, ignoring second, with colon
     * <li>{@code +HHMM} - hour and minute, ignoring second, no colon
     * <li>{@code +HH:MM} - hour and minute, ignoring second, with colon
     * <li>{@code +HHMMss} - hour and minute, with second if non-zero, no colon
     * <li>{@code +HH:MM:ss} - hour and minute, with second if non-zero, with colon
     * <li>{@code +HHMMSS} - hour, minute and second, no colon
     * <li>{@code +HH:MM:SS} - hour, minute and second, with colon
     * </ul><p>
     * The "no offset" text controls what text is printed when the total amount of
     * the offset fields to be output is zero.
     * Example values would be 'Z', '+00:00', 'UTC' or 'GMT'.
     * Three formats are accepted for parsing UTC - the "no offset" text, and the
     * plus and minus versions of zero defined by the pattern.
     *
     * @param pattern  the pattern to use, not null
     * @param noOffsetText  the text to use when the offset is zero, not null
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder appendOffset(String pattern, String noOffsetText) {
        appendInternal(new OffsetIdPrinterParser(pattern, noOffsetText));
        return this;
    }

    //-----------------------------------------------------------------------
    /**
     * Appends the time-zone ID, such as 'Europe/Paris' or '+02:00', to the formatter.
     * <p>
     * This appends an instruction to format/parse the zone ID to the builder.
     * The zone ID is obtained in a strict manner suitable for {@code ZonedDateTime}.
     * By contrast, {@code OffsetDateTime} does not have a zone ID suitable
     * for use with this method, see {@link #appendZoneOrOffsetId()}.
     * <p>
     * During formatting, the zone is obtained using a mechanism equivalent
     * to querying the temporal with {@link Queries#zoneId()}.
     * It will be printed using the result of {@link ZoneId#getId()}.
     * If the zone cannot be obtained then an exception is thrown unless the
     * section of the formatter is optional.
     * <p>
     * During parsing, the text must match a known zone or offset.
     * There are two types of zone ID, offset-based, such as '+01:30' and
     * region-based, such as 'Europe/London'. These are parsed differently.
     * If the parse starts with '+', '-', 'UT', 'UTC' or 'GMT', then the parser
     * expects an offset-based zone and will not match region-based zones.
     * The offset ID, such as '+02:30', may be at the start of the parse,
     * or prefixed by  'UT', 'UTC' or 'GMT'. The offset ID parsing is
     * equivalent to using {@link #appendOffset(String, String)} using the
     * arguments 'HH:MM:ss' and the no offset string '0'.
     * If the parse starts with 'UT', 'UTC' or 'GMT', and the parser cannot
     * match a following offset ID, then {@link ZoneOffset#UTC} is selected.
     * In all other cases, the list of known region-based zones is used to
     * find the longest available match. If no match is found, and the parse
     * starts with 'Z', then {@code ZoneOffset.UTC} is selected.
     * The parser uses the {@linkplain #parseCaseInsensitive() case sensitive} setting.
     * <p>
     * For example, the following will parse:
     * <pre>
     *   "Europe/London"           -> ZoneId.of("Europe/London")
     *   "Z"                       -> ZoneOffset.UTC
     *   "UT"                      -> ZoneOffset.UTC
     *   "UTC"                     -> ZoneOffset.UTC
     *   "GMT"                     -> ZoneOffset.UTC
     *   "UT0"                     -> ZoneOffset.UTC
     *   "UTC0"                    -> ZoneOffset.UTC
     *   "GMT0"                    -> ZoneOffset.UTC
     *   "+01:30"                  -> ZoneOffset.of("+01:30")
     *   "UT+01:30"                -> ZoneOffset.of("+01:30")
     *   "UTC+01:30"               -> ZoneOffset.of("+01:30")
     *   "GMT+01:30"               -> ZoneOffset.of("+01:30")
     * </pre>
     *
     * @return this, for chaining, not null
     * @see #appendZoneRegionId()
     */
    public DateTimeFormatterBuilder appendZoneId() {
        appendInternal(new ZoneIdPrinterParser(Queries.zoneId(), "ZoneId()"));
        return this;
    }

    /**
     * Appends the time-zone region ID, such as 'Europe/Paris', to the formatter,
     * rejecting the zone ID if it is a {@code ZoneOffset}.
     * <p>
     * This appends an instruction to format/parse the zone ID to the builder
     * only if it is a region-based ID.
     * <p>
     * During formatting, the zone is obtained using a mechanism equivalent
     * to querying the temporal with {@link Queries#zoneId()}.
     * If the zone is a {@code ZoneOffset} or it cannot be obtained then
     * an exception is thrown unless the section of the formatter is optional.
     * If the zone is not an offset, then the zone will be printed using
     * the zone ID from {@link ZoneId#getId()}.
     * <p>
     * During parsing, the text must match a known zone or offset.
     * There are two types of zone ID, offset-based, such as '+01:30' and
     * region-based, such as 'Europe/London'. These are parsed differently.
     * If the parse starts with '+', '-', 'UT', 'UTC' or 'GMT', then the parser
     * expects an offset-based zone and will not match region-based zones.
     * The offset ID, such as '+02:30', may be at the start of the parse,
     * or prefixed by  'UT', 'UTC' or 'GMT'. The offset ID parsing is
     * equivalent to using {@link #appendOffset(String, String)} using the
     * arguments 'HH:MM:ss' and the no offset string '0'.
     * If the parse starts with 'UT', 'UTC' or 'GMT', and the parser cannot
     * match a following offset ID, then {@link ZoneOffset#UTC} is selected.
     * In all other cases, the list of known region-based zones is used to
     * find the longest available match. If no match is found, and the parse
     * starts with 'Z', then {@code ZoneOffset.UTC} is selected.
     * The parser uses the {@linkplain #parseCaseInsensitive() case sensitive} setting.
     * <p>
     * For example, the following will parse:
     * <pre>
     *   "Europe/London"           -> ZoneId.of("Europe/London")
     *   "Z"                       -> ZoneOffset.UTC
     *   "UT"                      -> ZoneOffset.UTC
     *   "UTC"                     -> ZoneOffset.UTC
     *   "GMT"                     -> ZoneOffset.UTC
     *   "UT0"                     -> ZoneOffset.UTC
     *   "UTC0"                    -> ZoneOffset.UTC
     *   "GMT0"                    -> ZoneOffset.UTC
     *   "+01:30"                  -> ZoneOffset.of("+01:30")
     *   "UT+01:30"                -> ZoneOffset.of("+01:30")
     *   "UTC+01:30"               -> ZoneOffset.of("+01:30")
     *   "GMT+01:30"               -> ZoneOffset.of("+01:30")
     * </pre>
     * <p>
     * Note that this method is is identical to {@code appendZoneId()} except
     * in the mechanism used to obtain the zone.
     * Note also that parsing accepts offsets, whereas formatting will never
     * produce one.
     *
     * @return this, for chaining, not null
     * @see #appendZoneId()
     */
    public DateTimeFormatterBuilder appendZoneRegionId() {
        appendInternal(new ZoneIdPrinterParser(QUERY_REGION_ONLY, "ZoneRegionId()"));
        return this;
    }

    /**
     * Appends the time-zone ID, such as 'Europe/Paris' or '+02:00', to
     * the formatter, using the best available zone ID.
     * <p>
     * This appends an instruction to format/parse the best available
     * zone or offset ID to the builder.
     * The zone ID is obtained in a lenient manner that first attempts to
     * find a true zone ID, such as that on {@code ZonedDateTime}, and
     * then attempts to find an offset, such as that on {@code OffsetDateTime}.
     * <p>
     * During formatting, the zone is obtained using a mechanism equivalent
     * to querying the temporal with {@link Queries#zone()}.
     * It will be printed using the result of {@link ZoneId#getId()}.
     * If the zone cannot be obtained then an exception is thrown unless the
     * section of the formatter is optional.
     * <p>
     * During parsing, the text must match a known zone or offset.
     * There are two types of zone ID, offset-based, such as '+01:30' and
     * region-based, such as 'Europe/London'. These are parsed differently.
     * If the parse starts with '+', '-', 'UT', 'UTC' or 'GMT', then the parser
     * expects an offset-based zone and will not match region-based zones.
     * The offset ID, such as '+02:30', may be at the start of the parse,
     * or prefixed by  'UT', 'UTC' or 'GMT'. The offset ID parsing is
     * equivalent to using {@link #appendOffset(String, String)} using the
     * arguments 'HH:MM:ss' and the no offset string '0'.
     * If the parse starts with 'UT', 'UTC' or 'GMT', and the parser cannot
     * match a following offset ID, then {@link ZoneOffset#UTC} is selected.
     * In all other cases, the list of known region-based zones is used to
     * find the longest available match. If no match is found, and the parse
     * starts with 'Z', then {@code ZoneOffset.UTC} is selected.
     * The parser uses the {@linkplain #parseCaseInsensitive() case sensitive} setting.
     * <p>
     * For example, the following will parse:
     * <pre>
     *   "Europe/London"           -> ZoneId.of("Europe/London")
     *   "Z"                       -> ZoneOffset.UTC
     *   "UT"                      -> ZoneOffset.UTC
     *   "UTC"                     -> ZoneOffset.UTC
     *   "GMT"                     -> ZoneOffset.UTC
     *   "UT0"                     -> ZoneOffset.UTC
     *   "UTC0"                    -> ZoneOffset.UTC
     *   "GMT0"                    -> ZoneOffset.UTC
     *   "+01:30"                  -> ZoneOffset.of("+01:30")
     *   "UT+01:30"                -> ZoneOffset.of("+01:30")
     *   "UTC+01:30"               -> ZoneOffset.of("+01:30")
     *   "GMT+01:30"               -> ZoneOffset.of("+01:30")
     * </pre>
     * <p>
     * Note that this method is is identical to {@code appendZoneId()} except
     * in the mechanism used to obtain the zone.
     *
     * @return this, for chaining, not null
     * @see #appendZoneId()
     */
    public DateTimeFormatterBuilder appendZoneOrOffsetId() {
        appendInternal(new ZoneIdPrinterParser(Queries.zone(), "ZoneOrOffsetId()"));
        return this;
    }

    /**
     * Appends the time-zone name, such as 'British Summer Time', to the formatter.
     * <p>
     * This appends an instruction to format/parse the textual name of the zone to
     * the builder.
     * <p>
     * During formatting, the zone is obtained using a mechanism equivalent
     * to querying the temporal with {@link Queries#zoneId()}.
     * If the zone is a {@code ZoneOffset} it will be printed using the
     * result of {@link ZoneOffset#getId()}.
     * If the zone is not an offset, the textual name will be looked up
     * for the locale set in the {@link DateTimeFormatter}.
     * If the temporal object being printed represents an instant, then the text
     * will be the summer or winter time text as appropriate.
     * If the lookup for text does not find any suitable reuslt, then the
     * {@link ZoneId#getId() ID} will be printed instead.
     * If the zone cannot be obtained then an exception is thrown unless the
     * section of the formatter is optional.
     * <p>
     * During parsing, either the textual zone name, the zone ID or the offset
     * is accepted. Many textual zone names are not unique, such as CST can be
     * for both "Central Standard Time" and "China Standard Time". In this
     * situation, the zone id will be determined by the region information from
     * formatter's  {@link DateTimeFormatter#getLocale() locale} and the standard
     * zone id for that area, for example, America/New_York for the America Eastern
     * zone. The {@link #appendZoneText(TextStyle, Set)} may be used
     * to specify a set of preferred {@link ZoneId} in this situation.
     *
     * @param textStyle  the text style to use, not null
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder appendZoneText(TextStyle textStyle) {
        appendInternal(new ZoneTextPrinterParser(textStyle, null));
        return this;
    }

    /**
     * Appends the time-zone name, such as 'British Summer Time', to the formatter.
     * <p>
     * This appends an instruction to format/parse the textual name of the zone to
     * the builder.
     * <p>
     * During formatting, the zone is obtained using a mechanism equivalent
     * to querying the temporal with {@link Queries#zoneId()}.
     * If the zone is a {@code ZoneOffset} it will be printed using the
     * result of {@link ZoneOffset#getId()}.
     * If the zone is not an offset, the textual name will be looked up
     * for the locale set in the {@link DateTimeFormatter}.
     * If the temporal object being printed represents an instant, then the text
     * will be the summer or winter time text as appropriate.
     * If the lookup for text does not find any suitable reuslt, then the
     * {@link ZoneId#getId() ID} will be printed instead.
     * If the zone cannot be obtained then an exception is thrown unless the
     * section of the formatter is optional.
     * <p>
     * During parsing, either the textual zone name, the zone ID or the offset
     * is accepted. Many textual zone names are not unique, such as CST can be
     * for both "Central Standard Time" and "China Standard Time". In this
     * situation, the zone id will be determined by the region information from
     * formatter's  {@link DateTimeFormatter#getLocale() locale} and the standard
     * zone id for that area, for example, America/New_York for the America Eastern
     * zone. This method also allows a set of preferred {@link ZoneId} to be
     * specified for parsing. The matched preferred zone id will be used if the
     * textural zone name being parsed is not unique.
     *
     * If the zone cannot be parsed then an exception is thrown unless the
     * section of the formatter is optional.
     *
     * @param textStyle  the text style to use, not null
     * @param preferredZones  the set of preferred zone ids, not null
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder appendZoneText(TextStyle textStyle,
                                                   Set<ZoneId> preferredZones) {
        Objects.requireNonNull(preferredZones, "preferredZones");
        appendInternal(new ZoneTextPrinterParser(textStyle, preferredZones));
        return this;
    }

    //-----------------------------------------------------------------------
    /**
     * Appends the chronology ID, such as 'ISO' or 'ThaiBuddhist', to the formatter.
     * <p>
     * This appends an instruction to format/parse the chronology ID to the builder.
     * <p>
     * During formatting, the chronology is obtained using a mechanism equivalent
     * to querying the temporal with {@link Queries#chronology()}.
     * It will be printed using the result of {@link Chronology#getId()}.
     * If the chronology cannot be obtained then an exception is thrown unless the
     * section of the formatter is optional.
     * <p>
     * During parsing, the chronology is parsed and must match one of the chronologies
     * in {@link Chronology#getAvailableChronologies()}.
     * If the chronology cannot be parsed then an exception is thrown unless the
     * section of the formatter is optional.
     * The parser uses the {@linkplain #parseCaseInsensitive() case sensitive} setting.
     *
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder appendChronologyId() {
        appendInternal(new ChronoPrinterParser(null));
        return this;
    }

    /**
     * Appends the chronology name to the formatter.
     * <p>
     * The calendar system name will be output during a format.
     * If the chronology cannot be obtained then an exception will be thrown.
     * The calendar system name is obtained from the formatting symbols.
     *
     * @param textStyle  the text style to use, not null
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder appendChronologyText(TextStyle textStyle) {
        Objects.requireNonNull(textStyle, "textStyle");
        appendInternal(new ChronoPrinterParser(textStyle));
        return this;
    }

    //-----------------------------------------------------------------------
    /**
     * Appends a localized date-time pattern to the formatter.
     * <p>
     * This appends a localized section to the builder, suitable for outputting
     * a date, time or date-time combination. The format of the localized
     * section is lazily looked up based on four items:
     * <p><ul>
     * <li>the {@code dateStyle} specified to this method
     * <li>the {@code timeStyle} specified to this method
     * <li>the {@code Locale} of the {@code DateTimeFormatter}
     * <li>the {@code Chronology}, selecting the best available
     * </ul><p>
     * During formatting, the chronology is obtained from the temporal object
     * being formatted, which may have been overridden by
     * {@link DateTimeFormatter#withChronology(Chronology)}.
     * <p>
     * During parsing, if a chronology has already been parsed, then it is used.
     * Otherwise the default from {@code DateTimeFormatter.withChronology(Chronology)}
     * is used, with {@code IsoChronology} as the fallback.
     * <p>
     * Note that this method provides similar functionality to methods on
     * {@code DateFormat} such as {@link java.text.DateFormat#getDateTimeInstance(int, int)}.
     *
     * @param dateStyle  the date style to use, null means no date required
     * @param timeStyle  the time style to use, null means no time required
     * @return this, for chaining, not null
     * @throws IllegalArgumentException if both the date and time styles are null
     */
    public DateTimeFormatterBuilder appendLocalized(FormatStyle dateStyle, FormatStyle timeStyle) {
        if (dateStyle == null && timeStyle == null) {
            throw new IllegalArgumentException("Either the date or time style must be non-null");
        }
        appendInternal(new LocalizedPrinterParser(dateStyle, timeStyle));
        return this;
    }

    //-----------------------------------------------------------------------
    /**
     * Appends a character literal to the formatter.
     * <p>
     * This character will be output during a format.
     *
     * @param literal  the literal to append, not null
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder appendLiteral(char literal) {
        appendInternal(new CharLiteralPrinterParser(literal));
        return this;
    }

    /**
     * Appends a string literal to the formatter.
     * <p>
     * This string will be output during a format.
     * <p>
     * If the literal is empty, nothing is added to the formatter.
     *
     * @param literal  the literal to append, not null
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder appendLiteral(String literal) {
        Objects.requireNonNull(literal, "literal");
        if (literal.length() > 0) {
            if (literal.length() == 1) {
                appendInternal(new CharLiteralPrinterParser(literal.charAt(0)));
            } else {
                appendInternal(new StringLiteralPrinterParser(literal));
            }
        }
        return this;
    }

    //-----------------------------------------------------------------------
    /**
     * Appends all the elements of a formatter to the builder.
     * <p>
     * This method has the same effect as appending each of the constituent
     * parts of the formatter directly to this builder.
     *
     * @param formatter  the formatter to add, not null
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder append(DateTimeFormatter formatter) {
        Objects.requireNonNull(formatter, "formatter");
        appendInternal(formatter.toPrinterParser(false));
        return this;
    }

    /**
     * Appends a formatter to the builder which will optionally format/parse.
     * <p>
     * This method has the same effect as appending each of the constituent
     * parts directly to this builder surrounded by an {@link #optionalStart()} and
     * {@link #optionalEnd()}.
     * <p>
     * The formatter will format if data is available for all the fields contained within it.
     * The formatter will parse if the string matches, otherwise no error is returned.
     *
     * @param formatter  the formatter to add, not null
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder appendOptional(DateTimeFormatter formatter) {
        Objects.requireNonNull(formatter, "formatter");
        appendInternal(formatter.toPrinterParser(true));
        return this;
    }

    //-----------------------------------------------------------------------
    /**
     * Appends the elements defined by the specified pattern to the builder.
     * <p>
     * All letters 'A' to 'Z' and 'a' to 'z' are reserved as pattern letters.
     * The characters '{' and '}' are reserved for future use.
     * The characters '[' and ']' indicate optional patterns.
     * The following pattern letters are defined:
     * <pre>
     *  Symbol  Meaning                     Presentation      Examples
     *  ------  -------                     ------------      -------
     *   G       era                         text              A; AD; Anno Domini
     *   y       year                        year              2004; 04
     *   D       day-of-year                 number            189
     *   M       month-of-year               number/text       7; 07; Jul; July; J
     *   d       day-of-month                number            10
     *
     *   Q       quarter-of-year             number/text       3; 03; Q3
     *   Y       week-based-year             year              1996; 96
     *   w       week-of-year                number            27
     *   W       week-of-month               number            27
     *   e       localized day-of-week       number            2; Tue; Tuesday; T
     *   E       day-of-week                 number/text       2; Tue; Tuesday; T
     *   F       week-of-month               number            3
     *
     *   a       am-pm-of-day                text              PM
     *   h       clock-hour-of-am-pm (1-12)  number            12
     *   K       hour-of-am-pm (0-11)        number            0
     *   k       clock-hour-of-am-pm (1-24)  number            0
     *
     *   H       hour-of-day (0-23)          number            0
     *   m       minute-of-hour              number            30
     *   s       second-of-minute            number            55
     *   S       fraction-of-second          fraction          978
     *   A       milli-of-day                number            1234
     *   n       nano-of-second              number            987654321
     *   N       nano-of-day                 number            1234000000
     *
     *   V       time-zone ID                zone-id           America/Los_Angeles; Z; -08:30
     *   z       time-zone name              zone-name         Pacific Standard Time; PST
     *   X       zone-offset 'Z' for zero    offset-X          Z; -08; -0830; -08:30; -083015; -08:30:15;
     *   x       zone-offset                 offset-x          +0000; -08; -0830; -08:30; -083015; -08:30:15;
     *   Z       zone-offset                 offset-Z          +0000; -0800; -08:00;
     *
     *   p       pad next                    pad modifier      1
     *
     *   '       escape for text             delimiter
     *   ''      single quote                literal           '
     *   [       optional section start
     *   ]       optional section end
     *   {}      reserved for future use
     * </pre>
     * <p>
     * The count of pattern letters determine the format.
     * <p>
     * <b>Text</b>: The text style is determined based on the number of pattern letters used.
     * Less than 4 pattern letters will use the {@link TextStyle#SHORT short form}.
     * Exactly 4 pattern letters will use the {@link TextStyle#FULL full form}.
     * Exactly 5 pattern letters will use the {@link TextStyle#NARROW narrow form}.
     * <p>
     * <b>Number</b>: If the count of letters is one, then the value is printed using the minimum number
     * of digits and without padding as per {@link #appendValue(java.time.temporal.TemporalField)}. Otherwise, the
     * count of digits is used as the width of the output field as per {@link #appendValue(java.time.temporal.TemporalField, int)}.
     * <p>
     * <b>Number/Text</b>: If the count of pattern letters is 3 or greater, use the Text rules above.
     * Otherwise use the Number rules above.
     * <p>
     * <b>Fraction</b>: Outputs the nano-of-second field as a fraction-of-second.
     * The nano-of-second value has nine digits, thus the count of pattern letters is from 1 to 9.
     * If it is less than 9, then the nano-of-second value is truncated, with only the most
     * significant digits being output.
     * When parsing in strict mode, the number of parsed digits must match the count of pattern letters.
     * When parsing in lenient mode, the number of parsed digits must be at least the count of pattern
     * letters, up to 9 digits.
     * <p>
     * <b>Year</b>: The count of letters determines the minimum field width below which padding is used.
     * If the count of letters is two, then a {@link #appendValueReduced reduced} two digit form is used.
     * For formatting, this outputs the rightmost two digits. For parsing, this will parse using the
     * base value of 2000, resulting in a year within the range 2000 to 2099 inclusive.
     * If the count of letters is less than four (but not two), then the sign is only output for negative
     * years as per {@link SignStyle#NORMAL}.
     * Otherwise, the sign is output if the pad width is exceeded, as per {@link SignStyle#EXCEEDS_PAD}
     * <p>
     * <b>ZoneId</b>: This outputs the time-zone ID, such as 'Europe/Paris'.
     * If the count of letters is two, then the time-zone ID is output.
     * Any other count of letters throws {@code IllegalArgumentException}.
     * <pre>
     *  Pattern     Equivalent builder methods
     *   VV          appendZoneId()
     * </pre>
     * <p>
     * <b>Zone names</b>: This outputs the display name of the time-zone ID.
     * If the count of letters is one, two or three, then the short name is output.
     * If the count of letters is four, then the full name is output.
     * Five or more letters throws {@code IllegalArgumentException}.
     * <pre>
     *  Pattern     Equivalent builder methods
     *   z           appendZoneText(TextStyle.SHORT)
     *   zz          appendZoneText(TextStyle.SHORT)
     *   zzz         appendZoneText(TextStyle.SHORT)
     *   zzzz        appendZoneText(TextStyle.FULL)
     * </pre>
     * <p>
     * <b>Offset X and x</b>: This formats the offset based on the number of pattern letters.
     * One letter outputs just the hour', such as '+01', unless the minute is non-zero
     * in which case the minute is also output, such as '+0130'.
     * Two letters outputs the hour and minute, without a colon, such as '+0130'.
     * Three letters outputs the hour and minute, with a colon, such as '+01:30'.
     * Four letters outputs the hour and minute and optional second, without a colon, such as '+013015'.
     * Five letters outputs the hour and minute and optional second, with a colon, such as '+01:30:15'.
     * Six or more letters throws {@code IllegalArgumentException}.
     * Pattern letter 'X' (upper case) will output 'Z' when the offset to be output would be zero,
     * whereas pattern letter 'x' (lower case) will output '+00', '+0000', or '+00:00'.
     * <pre>
     *  Pattern     Equivalent builder methods
     *   X           appendOffset("+HHmm","Z")
     *   XX          appendOffset("+HHMM","Z")
     *   XXX         appendOffset("+HH:MM","Z")
     *   XXXX        appendOffset("+HHMMss","Z")
     *   XXXXX       appendOffset("+HH:MM:ss","Z")
     *   x           appendOffset("+HHmm","+00")
     *   xx          appendOffset("+HHMM","+0000")
     *   xxx         appendOffset("+HH:MM","+00:00")
     *   xxxx        appendOffset("+HHMMss","+0000")
     *   xxxxx       appendOffset("+HH:MM:ss","+00:00")
     * </pre>
     * <p>
     * <b>Offset Z</b>: This formats the offset based on the number of pattern letters.
     * One, two or three letters outputs the hour and minute, without a colon, such as '+0130'.
     * Four or more letters throws {@code IllegalArgumentException}.
     * The output will be '+0000' when the offset is zero.
     * <pre>
     *  Pattern     Equivalent builder methods
     *   Z           appendOffset("+HHMM","+0000")
     *   ZZ          appendOffset("+HHMM","+0000")
     *   ZZZ         appendOffset("+HHMM","+0000")
     * </pre>
     * <p>
     * <b>Optional section</b>: The optional section markers work exactly like calling {@link #optionalStart()}
     * and {@link #optionalEnd()}.
     * <p>
     * <b>Pad modifier</b>: Modifies the pattern that immediately follows to be padded with spaces.
     * The pad width is determined by the number of pattern letters.
     * This is the same as calling {@link #padNext(int)}.
     * <p>
     * For example, 'ppH' outputs the hour-of-day padded on the left with spaces to a width of 2.
     * <p>
     * Any unrecognized letter is an error.
     * Any non-letter character, other than '[', ']', '{', '}' and the single quote will be output directly.
     * Despite this, it is recommended to use single quotes around all characters that you want to
     * output directly to ensure that future changes do not break your application.
     * <p>
     * Note that the pattern string is similar, but not identical, to
     * {@link java.text.SimpleDateFormat SimpleDateFormat}.
     * The pattern string is also similar, but not identical, to that defined by the
     * Unicode Common Locale Data Repository (CLDR/LDML).
     * Pattern letters 'E' and 'u' are merged, which changes the meaning of "E" and "EE" to be numeric.
     * Pattern letters 'X' is aligned with Unicode CLDR/LDML, which affects pattern 'X'.
     * Pattern letter 'y' and 'Y' parse years of two digits and more than 4 digits differently.
     * Pattern letters 'n', 'A', 'N', 'I' and 'p' are added.
     * Number types will reject large numbers.
     *
     * @param pattern  the pattern to add, not null
     * @return this, for chaining, not null
     * @throws IllegalArgumentException if the pattern is invalid
     */
    public DateTimeFormatterBuilder appendPattern(String pattern) {
        Objects.requireNonNull(pattern, "pattern");
        parsePattern(pattern);
        return this;
    }

    private void parsePattern(String pattern) {
        for (int pos = 0; pos < pattern.length(); pos++) {
            char cur = pattern.charAt(pos);
            if ((cur >= 'A' && cur <= 'Z') || (cur >= 'a' && cur <= 'z')) {
                int start = pos++;
                for ( ; pos < pattern.length() && pattern.charAt(pos) == cur; pos++);  // short loop
                int count = pos - start;
                // padding
                if (cur == 'p') {
                    int pad = 0;
                    if (pos < pattern.length()) {
                        cur = pattern.charAt(pos);
                        if ((cur >= 'A' && cur <= 'Z') || (cur >= 'a' && cur <= 'z')) {
                            pad = count;
                            start = pos++;
                            for ( ; pos < pattern.length() && pattern.charAt(pos) == cur; pos++);  // short loop
                            count = pos - start;
                        }
                    }
                    if (pad == 0) {
                        throw new IllegalArgumentException(
                                "Pad letter 'p' must be followed by valid pad pattern: " + pattern);
                    }
                    padNext(pad); // pad and continue parsing
                }
                // main rules
                TemporalField field = FIELD_MAP.get(cur);
                if (field != null) {
                    parseField(cur, count, field);
                } else if (cur == 'z') {
                    if (count > 4) {
                        throw new IllegalArgumentException("Too many pattern letters: " + cur);
                    } else if (count == 4) {
                        appendZoneText(TextStyle.FULL);
                    } else {
                        appendZoneText(TextStyle.SHORT);
                    }
                } else if (cur == 'V') {
                    if (count != 2) {
                        throw new IllegalArgumentException("Pattern letter count must be 2: " + cur);
                    }
                    appendZoneId();
                } else if (cur == 'Z') {
                    if (count > 3) {
                        throw new IllegalArgumentException("Too many pattern letters: " + cur);
                    }
                    appendOffset("+HHMM", "+0000");
                } else if (cur == 'X') {
                    if (count > 5) {
                        throw new IllegalArgumentException("Too many pattern letters: " + cur);
                    }
                    appendOffset(OffsetIdPrinterParser.PATTERNS[count + (count == 1 ? 0 : 1)], "Z");
                } else if (cur == 'x') {
                    if (count > 5) {
                        throw new IllegalArgumentException("Too many pattern letters: " + cur);
                    }
                    String zero = (count == 1 ? "+00" : (count % 2 == 0 ? "+0000" : "+00:00"));
                    appendOffset(OffsetIdPrinterParser.PATTERNS[count + (count == 1 ? 0 : 1)], zero);
                } else if (cur == 'w' || cur == 'e') {
                    // Fields defined by Locale
                    if (count > 1) {
                        throw new IllegalArgumentException("Too many pattern letters: " + cur);
                    }
                    appendInternal(new WeekBasedFieldPrinterParser(cur, count));
                } else if (cur == 'W') {
                    // Fields defined by Locale
                    if (count > 2) {
                        throw new IllegalArgumentException("Too many pattern letters: " + cur);
                    }
                    appendInternal(new WeekBasedFieldPrinterParser(cur, count));
                } else {
                    throw new IllegalArgumentException("Unknown pattern letter: " + cur);
                }
                pos--;

            } else if (cur == '\'') {
                // parse literals
                int start = pos++;
                for ( ; pos < pattern.length(); pos++) {
                    if (pattern.charAt(pos) == '\'') {
                        if (pos + 1 < pattern.length() && pattern.charAt(pos + 1) == '\'') {
                            pos++;
                        } else {
                            break;  // end of literal
                        }
                    }
                }
                if (pos >= pattern.length()) {
                    throw new IllegalArgumentException("Pattern ends with an incomplete string literal: " + pattern);
                }
                String str = pattern.substring(start + 1, pos);
                if (str.length() == 0) {
                    appendLiteral('\'');
                } else {
                    appendLiteral(str.replace("''", "'"));
                }

            } else if (cur == '[') {
                optionalStart();

            } else if (cur == ']') {
                if (active.parent == null) {
                    throw new IllegalArgumentException("Pattern invalid as it contains ] without previous [");
                }
                optionalEnd();

            } else if (cur == '{' || cur == '}') {
                throw new IllegalArgumentException("Pattern includes reserved character: '" + cur + "'");
            } else {
                appendLiteral(cur);
            }
        }
    }

    private void parseField(char cur, int count, TemporalField field) {
        switch (cur) {
            case 'y':
            case 'Y':
                if (count == 2) {
                    appendValueReduced(field, 2, 2000);
                } else if (count < 4) {
                    appendValue(field, count, 19, SignStyle.NORMAL);
                } else {
                    appendValue(field, count, 19, SignStyle.EXCEEDS_PAD);
                }
                break;
            case 'M':
            case 'Q':
            case 'E':
                switch (count) {
                    case 1:
                        appendValue(field);
                        break;
                    case 2:
                        appendValue(field, 2);
                        break;
                    case 3:
                        appendText(field, TextStyle.SHORT);
                        break;
                    case 4:
                        appendText(field, TextStyle.FULL);
                        break;
                    case 5:
                        appendText(field, TextStyle.NARROW);
                        break;
                    default:
                        throw new IllegalArgumentException("Too many pattern letters: " + cur);
                }
                break;
            case 'G':
            case 'a':
                switch (count) {
                    case 1:
                    case 2:
                    case 3:
                        appendText(field, TextStyle.SHORT);
                        break;
                    case 4:
                        appendText(field, TextStyle.FULL);
                        break;
                    case 5:
                        appendText(field, TextStyle.NARROW);
                        break;
                    default:
                        throw new IllegalArgumentException("Too many pattern letters: " + cur);
                }
                break;
            case 'S':
                appendFraction(NANO_OF_SECOND, count, count, false);
                break;
            default:
                if (count == 1) {
                    appendValue(field);
                } else {
                    appendValue(field, count);
                }
                break;
        }
    }

    /** Map of letters to fields. */
    private static final Map<Character, TemporalField> FIELD_MAP = new HashMap<>();
    static {
        FIELD_MAP.put('G', ChronoField.ERA);                       // Java, LDML (different to both for 1/2 chars)
        FIELD_MAP.put('y', ChronoField.YEAR);                      // LDML
        // FIELD_MAP.put('y', ChronoField.YEAR_OF_ERA);            // Java, LDML  // TODO redefine from above
        // FIELD_MAP.put('u', ChronoField.YEAR);                   // LDML  // TODO
        // FIELD_MAP.put('Y', IsoFields.WEEK_BASED_YEAR);          // Java7, LDML (needs localized week number)  // TODO
        FIELD_MAP.put('Q', IsoFields.QUARTER_OF_YEAR);             // LDML (removed quarter from 310)
        FIELD_MAP.put('M', ChronoField.MONTH_OF_YEAR);             // Java, LDML
        // FIELD_MAP.put('w', WeekFields.weekOfYear());            // Java, LDML (needs localized week number)
        // FIELD_MAP.put('W', WeekFields.weekOfMonth());           // Java, LDML (needs localized week number)
        FIELD_MAP.put('D', ChronoField.DAY_OF_YEAR);               // Java, LDML
        FIELD_MAP.put('d', ChronoField.DAY_OF_MONTH);              // Java, LDML
        FIELD_MAP.put('F', ChronoField.ALIGNED_WEEK_OF_MONTH);     // Java, LDML
        FIELD_MAP.put('E', ChronoField.DAY_OF_WEEK);               // Java, LDML (different to both for 1/2 chars)
        // FIELD_MAP.put('e', WeekFields.dayOfWeek());             // LDML (needs localized week number)
        FIELD_MAP.put('a', ChronoField.AMPM_OF_DAY);               // Java, LDML
        FIELD_MAP.put('H', ChronoField.HOUR_OF_DAY);               // Java, LDML
        FIELD_MAP.put('k', ChronoField.CLOCK_HOUR_OF_DAY);         // Java, LDML
        FIELD_MAP.put('K', ChronoField.HOUR_OF_AMPM);              // Java, LDML
        FIELD_MAP.put('h', ChronoField.CLOCK_HOUR_OF_AMPM);        // Java, LDML
        FIELD_MAP.put('m', ChronoField.MINUTE_OF_HOUR);            // Java, LDML
        FIELD_MAP.put('s', ChronoField.SECOND_OF_MINUTE);          // Java, LDML
        FIELD_MAP.put('S', ChronoField.NANO_OF_SECOND);            // LDML (Java uses milli-of-second number)
        FIELD_MAP.put('A', ChronoField.MILLI_OF_DAY);              // LDML
        FIELD_MAP.put('n', ChronoField.NANO_OF_SECOND);            // 310 (proposed for LDML)
        FIELD_MAP.put('N', ChronoField.NANO_OF_DAY);               // 310 (proposed for LDML)
        // 310 - z - time-zone names, matches LDML and SimpleDateFormat 1 to 4
        // 310 - Z - matches SimpleDateFormat and LDML
        // 310 - V - time-zone id, matches proposed LDML
        // 310 - p - prefix for padding
        // 310 - X - matches proposed LDML, almost matches JavaSDF for 1, exact match 2&3, extended 4&5
        // 310 - x - matches proposed LDML
        // Java - u - clashes with LDML, go with LDML (year-proleptic) here
        // LDML - U - cycle year name, not supported by 310 yet
        // LDML - l - deprecated
        // LDML - j - not relevant
        // LDML - g - modified-julian-day
        // LDML - v,V - extended time-zone names
        // LDML - q/c/L - standalone quarter/day-of-week/month
    }

    //-----------------------------------------------------------------------
    /**
     * Causes the next added printer/parser to pad to a fixed width using a space.
     * <p>
     * This padding will pad to a fixed width using spaces.
     * <p>
     * During formatting, the decorated element will be output and then padded
     * to the specified width. An exception will be thrown during formatting if
     * the pad width is exceeded.
     * <p>
     * During parsing, the padding and decorated element are parsed.
     * If parsing is lenient, then the pad width is treated as a maximum.
     * If parsing is case insensitive, then the pad character is matched ignoring case.
     * The padding is parsed greedily. Thus, if the decorated element starts with
     * the pad character, it will not be parsed.
     *
     * @param padWidth  the pad width, 1 or greater
     * @return this, for chaining, not null
     * @throws IllegalArgumentException if pad width is too small
     */
    public DateTimeFormatterBuilder padNext(int padWidth) {
        return padNext(padWidth, ' ');
    }

    /**
     * Causes the next added printer/parser to pad to a fixed width.
     * <p>
     * This padding is intended for padding other than zero-padding.
     * Zero-padding should be achieved using the appendValue methods.
     * <p>
     * During formatting, the decorated element will be output and then padded
     * to the specified width. An exception will be thrown during formatting if
     * the pad width is exceeded.
     * <p>
     * During parsing, the padding and decorated element are parsed.
     * If parsing is lenient, then the pad width is treated as a maximum.
     * If parsing is case insensitive, then the pad character is matched ignoring case.
     * The padding is parsed greedily. Thus, if the decorated element starts with
     * the pad character, it will not be parsed.
     *
     * @param padWidth  the pad width, 1 or greater
     * @param padChar  the pad character
     * @return this, for chaining, not null
     * @throws IllegalArgumentException if pad width is too small
     */
    public DateTimeFormatterBuilder padNext(int padWidth, char padChar) {
        if (padWidth < 1) {
            throw new IllegalArgumentException("The pad width must be at least one but was " + padWidth);
        }
        active.padNextWidth = padWidth;
        active.padNextChar = padChar;
        active.valueParserIndex = -1;
        return this;
    }

    //-----------------------------------------------------------------------
    /**
     * Mark the start of an optional section.
     * <p>
     * The output of formatting can include optional sections, which may be nested.
     * An optional section is started by calling this method and ended by calling
     * {@link #optionalEnd()} or by ending the build process.
     * <p>
     * All elements in the optional section are treated as optional.
     * During formatting, the section is only output if data is available in the
     * {@code TemporalAccessor} for all the elements in the section.
     * During parsing, the whole section may be missing from the parsed string.
     * <p>
     * For example, consider a builder setup as
     * {@code builder.appendValue(HOUR_OF_DAY,2).optionalStart().appendValue(MINUTE_OF_HOUR,2)}.
     * The optional section ends automatically at the end of the builder.
     * During formatting, the minute will only be output if its value can be obtained from the date-time.
     * During parsing, the input will be successfully parsed whether the minute is present or not.
     *
     * @return this, for chaining, not null
     */
    public DateTimeFormatterBuilder optionalStart() {
        active.valueParserIndex = -1;
        active = new DateTimeFormatterBuilder(active, true);
        return this;
    }

    /**
     * Ends an optional section.
     * <p>
     * The output of formatting can include optional sections, which may be nested.
     * An optional section is started by calling {@link #optionalStart()} and ended
     * using this method (or at the end of the builder).
     * <p>
     * Calling this method without having previously called {@code optionalStart}
     * will throw an exception.
     * Calling this method immediately after calling {@code optionalStart} has no effect
     * on the formatter other than ending the (empty) optional section.
     * <p>
     * All elements in the optional section are treated as optional.
     * During formatting, the section is only output if data is available in the
     * {@code TemporalAccessor} for all the elements in the section.
     * During parsing, the whole section may be missing from the parsed string.
     * <p>
     * For example, consider a builder setup as
     * {@code builder.appendValue(HOUR_OF_DAY,2).optionalStart().appendValue(MINUTE_OF_HOUR,2).optionalEnd()}.
     * During formatting, the minute will only be output if its value can be obtained from the date-time.
     * During parsing, the input will be successfully parsed whether the minute is present or not.
     *
     * @return this, for chaining, not null
     * @throws IllegalStateException if there was no previous call to {@code optionalStart}
     */
    public DateTimeFormatterBuilder optionalEnd() {
        if (active.parent == null) {
            throw new IllegalStateException("Cannot call optionalEnd() as there was no previous call to optionalStart()");
        }
        if (active.printerParsers.size() > 0) {
            CompositePrinterParser cpp = new CompositePrinterParser(active.printerParsers, active.optional);
            active = active.parent;
            appendInternal(cpp);
        } else {
            active = active.parent;
        }
        return this;
    }

    //-----------------------------------------------------------------------
    /**
     * Appends a printer and/or parser to the internal list handling padding.
     *
     * @param pp  the printer-parser to add, not null
     * @return the index into the active parsers list
     */
    private int appendInternal(DateTimePrinterParser pp) {
        Objects.requireNonNull(pp, "pp");
        if (active.padNextWidth > 0) {
            if (pp != null) {
                pp = new PadPrinterParserDecorator(pp, active.padNextWidth, active.padNextChar);
            }
            active.padNextWidth = 0;
            active.padNextChar = 0;
        }
        active.printerParsers.add(pp);
        active.valueParserIndex = -1;
        return active.printerParsers.size() - 1;
    }

    //-----------------------------------------------------------------------
    /**
     * Completes this builder by creating the DateTimeFormatter using the default locale.
     * <p>
     * This will create a formatter with the {@link Locale#getDefault(Locale.Category) default FORMAT locale}.
     * Numbers will be printed and parsed using the standard non-localized set of symbols.
     * <p>
     * Calling this method will end any open optional sections by repeatedly
     * calling {@link #optionalEnd()} before creating the formatter.
     * <p>
     * This builder can still be used after creating the formatter if desired,
     * although the state may have been changed by calls to {@code optionalEnd}.
     *
     * @return the created formatter, not null
     */
    public DateTimeFormatter toFormatter() {
        return toFormatter(Locale.getDefault(Locale.Category.FORMAT));
    }

    /**
     * Completes this builder by creating the DateTimeFormatter using the specified locale.
     * <p>
     * This will create a formatter with the specified locale.
     * Numbers will be printed and parsed using the standard non-localized set of symbols.
     * <p>
     * Calling this method will end any open optional sections by repeatedly
     * calling {@link #optionalEnd()} before creating the formatter.
     * <p>
     * This builder can still be used after creating the formatter if desired,
     * although the state may have been changed by calls to {@code optionalEnd}.
     *
     * @param locale  the locale to use for formatting, not null
     * @return the created formatter, not null
     */
    public DateTimeFormatter toFormatter(Locale locale) {
        Objects.requireNonNull(locale, "locale");
        while (active.parent != null) {
            optionalEnd();
        }
        CompositePrinterParser pp = new CompositePrinterParser(printerParsers, false);
        return new DateTimeFormatter(pp, locale, DateTimeFormatSymbols.STANDARD, null, null);
    }

    //-----------------------------------------------------------------------
    /**
     * Strategy for formatting/parsing date-time information.
     * <p>
     * The printer may format any part, or the whole, of the input date-time object.
     * Typically, a complete format is constructed from a number of smaller
     * units, each outputting a single field.
     * <p>
     * The parser may parse any piece of text from the input, storing the result
     * in the context. Typically, each individual parser will just parse one
     * field, such as the day-of-month, storing the value in the context.
     * Once the parse is complete, the caller will then resolve the parsed values
     * to create the desired object, such as a {@code LocalDate}.
     * <p>
     * The parse position will be updated during the parse. Parsing will start at
     * the specified index and the return value specifies the new parse position
     * for the next parser. If an error occurs, the returned index will be negative
     * and will have the error position encoded using the complement operator.
     *
     * <h3>Specification for implementors</h3>
     * This interface must be implemented with care to ensure other classes operate correctly.
     * All implementations that can be instantiated must be final, immutable and thread-safe.
     * <p>
     * The context is not a thread-safe object and a new instance will be created
     * for each format that occurs. The context must not be stored in an instance
     * variable or shared with any other threads.
     */
    interface DateTimePrinterParser {

        /**
         * Prints the date-time object to the buffer.
         * <p>
         * The context holds information to use during the format.
         * It also contains the date-time information to be printed.
         * <p>
         * The buffer must not be mutated beyond the content controlled by the implementation.
         *
         * @param context  the context to format using, not null
         * @param buf  the buffer to append to, not null
         * @return false if unable to query the value from the date-time, true otherwise
         * @throws DateTimeException if the date-time cannot be printed successfully
         */
        boolean format(DateTimePrintContext context, StringBuilder buf);

        /**
         * Parses text into date-time information.
         * <p>
         * The context holds information to use during the parse.
         * It is also used to store the parsed date-time information.
         *
         * @param context  the context to use and parse into, not null
         * @param text  the input text to parse, not null
         * @param position  the position to start parsing at, from 0 to the text length
         * @return the new parse position, where negative means an error with the
         *  error position encoded using the complement ~ operator
         * @throws NullPointerException if the context or text is null
         * @throws IndexOutOfBoundsException if the position is invalid
         */
        int parse(DateTimeParseContext context, CharSequence text, int position);
    }

    //-----------------------------------------------------------------------
    /**
     * Composite printer and parser.
     */
    static final class CompositePrinterParser implements DateTimePrinterParser {
        private final DateTimePrinterParser[] printerParsers;
        private final boolean optional;

        CompositePrinterParser(List<DateTimePrinterParser> printerParsers, boolean optional) {
            this(printerParsers.toArray(new DateTimePrinterParser[printerParsers.size()]), optional);
        }

        CompositePrinterParser(DateTimePrinterParser[] printerParsers, boolean optional) {
            this.printerParsers = printerParsers;
            this.optional = optional;
        }

        /**
         * Returns a copy of this printer-parser with the optional flag changed.
         *
         * @param optional  the optional flag to set in the copy
         * @return the new printer-parser, not null
         */
        public CompositePrinterParser withOptional(boolean optional) {
            if (optional == this.optional) {
                return this;
            }
            return new CompositePrinterParser(printerParsers, optional);
        }

        @Override
        public boolean format(DateTimePrintContext context, StringBuilder buf) {
            int length = buf.length();
            if (optional) {
                context.startOptional();
            }
            try {
                for (DateTimePrinterParser pp : printerParsers) {
                    if (pp.format(context, buf) == false) {
                        buf.setLength(length);  // reset buffer
                        return true;
                    }
                }
            } finally {
                if (optional) {
                    context.endOptional();
                }
            }
            return true;
        }

        @Override
        public int parse(DateTimeParseContext context, CharSequence text, int position) {
            if (optional) {
                context.startOptional();
                int pos = position;
                for (DateTimePrinterParser pp : printerParsers) {
                    pos = pp.parse(context, text, pos);
                    if (pos < 0) {
                        context.endOptional(false);
                        return position;  // return original position
                    }
                }
                context.endOptional(true);
                return pos;
            } else {
                for (DateTimePrinterParser pp : printerParsers) {
                    position = pp.parse(context, text, position);
                    if (position < 0) {
                        break;
                    }
                }
                return position;
            }
        }

        @Override
        public String toString() {
            StringBuilder buf = new StringBuilder();
            if (printerParsers != null) {
                buf.append(optional ? "[" : "(");
                for (DateTimePrinterParser pp : printerParsers) {
                    buf.append(pp);
                }
                buf.append(optional ? "]" : ")");
            }
            return buf.toString();
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Pads the output to a fixed width.
     */
    static final class PadPrinterParserDecorator implements DateTimePrinterParser {
        private final DateTimePrinterParser printerParser;
        private final int padWidth;
        private final char padChar;

        /**
         * Constructor.
         *
         * @param printerParser  the printer, not null
         * @param padWidth  the width to pad to, 1 or greater
         * @param padChar  the pad character
         */
        PadPrinterParserDecorator(DateTimePrinterParser printerParser, int padWidth, char padChar) {
            // input checked by DateTimeFormatterBuilder
            this.printerParser = printerParser;
            this.padWidth = padWidth;
            this.padChar = padChar;
        }

        @Override
        public boolean format(DateTimePrintContext context, StringBuilder buf) {
            int preLen = buf.length();
            if (printerParser.format(context, buf) == false) {
                return false;
            }
            int len = buf.length() - preLen;
            if (len > padWidth) {
                throw new DateTimeException(
                    "Cannot print as output of " + len + " characters exceeds pad width of " + padWidth);
            }
            for (int i = 0; i < padWidth - len; i++) {
                buf.insert(preLen, padChar);
            }
            return true;
        }

        @Override
        public int parse(DateTimeParseContext context, CharSequence text, int position) {
            // cache context before changed by decorated parser
            final boolean strict = context.isStrict();
            // parse
            if (position > text.length()) {
                throw new IndexOutOfBoundsException();
            }
            if (position == text.length()) {
                return ~position;  // no more characters in the string
            }
            int endPos = position + padWidth;
            if (endPos > text.length()) {
                if (strict) {
                    return ~position;  // not enough characters in the string to meet the parse width
                }
                endPos = text.length();
            }
            int pos = position;
            while (pos < endPos && context.charEquals(text.charAt(pos), padChar)) {
                pos++;
            }
            text = text.subSequence(0, endPos);
            int resultPos = printerParser.parse(context, text, pos);
            if (resultPos != endPos && strict) {
                return ~(position + pos);  // parse of decorated field didn't parse to the end
            }
            return resultPos;
        }

        @Override
        public String toString() {
            return "Pad(" + printerParser + "," + padWidth + (padChar == ' ' ? ")" : ",'" + padChar + "')");
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Enumeration to apply simple parse settings.
     */
    static enum SettingsParser implements DateTimePrinterParser {
        SENSITIVE,
        INSENSITIVE,
        STRICT,
        LENIENT;

        @Override
        public boolean format(DateTimePrintContext context, StringBuilder buf) {
            return true;  // nothing to do here
        }

        @Override
        public int parse(DateTimeParseContext context, CharSequence text, int position) {
            // using ordinals to avoid javac synthetic inner class
            switch (ordinal()) {
                case 0: context.setCaseSensitive(true); break;
                case 1: context.setCaseSensitive(false); break;
                case 2: context.setStrict(true); break;
                case 3: context.setStrict(false); break;
            }
            return position;
        }

        @Override
        public String toString() {
            // using ordinals to avoid javac synthetic inner class
            switch (ordinal()) {
                case 0: return "ParseCaseSensitive(true)";
                case 1: return "ParseCaseSensitive(false)";
                case 2: return "ParseStrict(true)";
                case 3: return "ParseStrict(false)";
            }
            throw new IllegalStateException("Unreachable");
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Prints or parses a character literal.
     */
    static final class CharLiteralPrinterParser implements DateTimePrinterParser {
        private final char literal;

        CharLiteralPrinterParser(char literal) {
            this.literal = literal;
        }

        @Override
        public boolean format(DateTimePrintContext context, StringBuilder buf) {
            buf.append(literal);
            return true;
        }

        @Override
        public int parse(DateTimeParseContext context, CharSequence text, int position) {
            int length = text.length();
            if (position == length) {
                return ~position;
            }
            char ch = text.charAt(position);
            if (ch != literal) {
                if (context.isCaseSensitive() ||
                        (Character.toUpperCase(ch) != Character.toUpperCase(literal) &&
                         Character.toLowerCase(ch) != Character.toLowerCase(literal))) {
                    return ~position;
                }
            }
            return position + 1;
        }

        @Override
        public String toString() {
            if (literal == '\'') {
                return "''";
            }
            return "'" + literal + "'";
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Prints or parses a string literal.
     */
    static final class StringLiteralPrinterParser implements DateTimePrinterParser {
        private final String literal;

        StringLiteralPrinterParser(String literal) {
            this.literal = literal;  // validated by caller
        }

        @Override
        public boolean format(DateTimePrintContext context, StringBuilder buf) {
            buf.append(literal);
            return true;
        }

        @Override
        public int parse(DateTimeParseContext context, CharSequence text, int position) {
            int length = text.length();
            if (position > length || position < 0) {
                throw new IndexOutOfBoundsException();
            }
            if (context.subSequenceEquals(text, position, literal, 0, literal.length()) == false) {
                return ~position;
            }
            return position + literal.length();
        }

        @Override
        public String toString() {
            String converted = literal.replace("'", "''");
            return "'" + converted + "'";
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Prints and parses a numeric date-time field with optional padding.
     */
    static class NumberPrinterParser implements DateTimePrinterParser {

        /**
         * Array of 10 to the power of n.
         */
        static final int[] EXCEED_POINTS = new int[] {
            0,
            10,
            100,
            1000,
            10000,
            100000,
            1000000,
            10000000,
            100000000,
            1000000000,
        };

        final TemporalField field;
        final int minWidth;
        private final int maxWidth;
        private final SignStyle signStyle;
        private final int subsequentWidth;

        /**
         * Constructor.
         *
         * @param field  the field to format, not null
         * @param minWidth  the minimum field width, from 1 to 19
         * @param maxWidth  the maximum field width, from minWidth to 19
         * @param signStyle  the positive/negative sign style, not null
         */
        NumberPrinterParser(TemporalField field, int minWidth, int maxWidth, SignStyle signStyle) {
            // validated by caller
            this.field = field;
            this.minWidth = minWidth;
            this.maxWidth = maxWidth;
            this.signStyle = signStyle;
            this.subsequentWidth = 0;
        }

        /**
         * Constructor.
         *
         * @param field  the field to format, not null
         * @param minWidth  the minimum field width, from 1 to 19
         * @param maxWidth  the maximum field width, from minWidth to 19
         * @param signStyle  the positive/negative sign style, not null
         * @param subsequentWidth  the width of subsequent non-negative numbers, 0 or greater,
         *  -1 if fixed width due to active adjacent parsing
         */
        private NumberPrinterParser(TemporalField field, int minWidth, int maxWidth, SignStyle signStyle, int subsequentWidth) {
            // validated by caller
            this.field = field;
            this.minWidth = minWidth;
            this.maxWidth = maxWidth;
            this.signStyle = signStyle;
            this.subsequentWidth = subsequentWidth;
        }

        /**
         * Returns a new instance with fixed width flag set.
         *
         * @return a new updated printer-parser, not null
         */
        NumberPrinterParser withFixedWidth() {
            return new NumberPrinterParser(field, minWidth, maxWidth, signStyle, -1);
        }

        /**
         * Returns a new instance with an updated subsequent width.
         *
         * @param subsequentWidth  the width of subsequent non-negative numbers, 0 or greater
         * @return a new updated printer-parser, not null
         */
        NumberPrinterParser withSubsequentWidth(int subsequentWidth) {
            return new NumberPrinterParser(field, minWidth, maxWidth, signStyle, this.subsequentWidth + subsequentWidth);
        }

        @Override
        public boolean format(DateTimePrintContext context, StringBuilder buf) {
            Chronology chrono = context.getTemporal().query(Queries.chronology());
            Long valueLong;
            if (chrono == JapaneseChronology.INSTANCE && field == ChronoField.YEAR) {
                valueLong = context.getValue(ChronoField.YEAR_OF_ERA);
            } else {
                valueLong = context.getValue(field);
            }
            if (valueLong == null) {
                return false;
            }
            long value = getValue(valueLong);
            DateTimeFormatSymbols symbols = context.getSymbols();
            String str = (value == Long.MIN_VALUE ? "9223372036854775808" : Long.toString(Math.abs(value)));
            if (str.length() > maxWidth) {
                throw new DateTimeException("Field " + field.getName() +
                    " cannot be printed as the value " + value +
                    " exceeds the maximum print width of " + maxWidth);
            }
            str = symbols.convertNumberToI18N(str);

            if (value >= 0) {
                switch (signStyle) {
                    case EXCEEDS_PAD:
                        if (minWidth < 19 && value >= EXCEED_POINTS[minWidth]) {
                            buf.append(symbols.getPositiveSign());
                        }
                        break;
                    case ALWAYS:
                        buf.append(symbols.getPositiveSign());
                        break;
                }
            } else {
                switch (signStyle) {
                    case NORMAL:
                    case EXCEEDS_PAD:
                    case ALWAYS:
                        buf.append(symbols.getNegativeSign());
                        break;
                    case NOT_NEGATIVE:
                        throw new DateTimeException("Field " + field.getName() +
                            " cannot be printed as the value " + value +
                            " cannot be negative according to the SignStyle");
                }
            }
            for (int i = 0; i < minWidth - str.length(); i++) {
                buf.append(symbols.getZeroDigit());
            }
            buf.append(str);
            return true;
        }

        /**
         * Gets the value to output.
         *
         * @param value  the base value of the field, not null
         * @return the value
         */
        long getValue(long value) {
            return value;
        }

        boolean isFixedWidth() {
            return subsequentWidth == -1;
        }

        @Override
        public int parse(DateTimeParseContext context, CharSequence text, int position) {
            int length = text.length();
            if (position == length) {
                return ~position;
            }
            char sign = text.charAt(position);  // IOOBE if invalid position
            boolean negative = false;
            boolean positive = false;
            if (sign == context.getSymbols().getPositiveSign()) {
                if (signStyle.parse(true, context.isStrict(), minWidth == maxWidth) == false) {
                    return ~position;
                }
                positive = true;
                position++;
            } else if (sign == context.getSymbols().getNegativeSign()) {
                if (signStyle.parse(false, context.isStrict(), minWidth == maxWidth) == false) {
                    return ~position;
                }
                negative = true;
                position++;
            } else {
                if (signStyle == SignStyle.ALWAYS && context.isStrict()) {
                    return ~position;
                }
            }
            int effMinWidth = (context.isStrict() || isFixedWidth() ? minWidth : 1);
            int minEndPos = position + effMinWidth;
            if (minEndPos > length) {
                return ~position;
            }
            int effMaxWidth = maxWidth + Math.max(subsequentWidth, 0);
            long total = 0;
            BigInteger totalBig = null;
            int pos = position;
            for (int pass = 0; pass < 2; pass++) {
                int maxEndPos = Math.min(pos + effMaxWidth, length);
                while (pos < maxEndPos) {
                    char ch = text.charAt(pos++);
                    int digit = context.getSymbols().convertToDigit(ch);
                    if (digit < 0) {
                        pos--;
                        if (pos < minEndPos) {
                            return ~position;  // need at least min width digits
                        }
                        break;
                    }
                    if ((pos - position) > 18) {
                        if (totalBig == null) {
                            totalBig = BigInteger.valueOf(total);
                        }
                        totalBig = totalBig.multiply(BigInteger.TEN).add(BigInteger.valueOf(digit));
                    } else {
                        total = total * 10 + digit;
                    }
                }
                if (subsequentWidth > 0 && pass == 0) {
                    // re-parse now we know the correct width
                    int parseLen = pos - position;
                    effMaxWidth = Math.max(effMinWidth, parseLen - subsequentWidth);
                    pos = position;
                    total = 0;
                    totalBig = null;
                } else {
                    break;
                }
            }
            if (negative) {
                if (totalBig != null) {
                    if (totalBig.equals(BigInteger.ZERO) && context.isStrict()) {
                        return ~(position - 1);  // minus zero not allowed
                    }
                    totalBig = totalBig.negate();
                } else {
                    if (total == 0 && context.isStrict()) {
                        return ~(position - 1);  // minus zero not allowed
                    }
                    total = -total;
                }
            } else if (signStyle == SignStyle.EXCEEDS_PAD && context.isStrict()) {
                int parseLen = pos - position;
                if (positive) {
                    if (parseLen <= minWidth) {
                        return ~(position - 1);  // '+' only parsed if minWidth exceeded
                    }
                } else {
                    if (parseLen > minWidth) {
                        return ~position;  // '+' must be parsed if minWidth exceeded
                    }
                }
            }
            if (totalBig != null) {
                if (totalBig.bitLength() > 63) {
                    // overflow, parse 1 less digit
                    totalBig = totalBig.divide(BigInteger.TEN);
                    pos--;
                }
                return setValue(context, totalBig.longValue(), position, pos);
            }
            return setValue(context, total, position, pos);
        }

        /**
         * Stores the value.
         *
         * @param context  the context to store into, not null
         * @param value  the value
         * @param errorPos  the position of the field being parsed
         * @param successPos  the position after the field being parsed
         * @return the new position
         */
        int setValue(DateTimeParseContext context, long value, int errorPos, int successPos) {
            TemporalField f = field;
            if (field == ChronoField.YEAR) {
                Chronology chrono = context.getEffectiveChronology();
                if (chrono == JapaneseChronology.INSTANCE) {
                    f = ChronoField.YEAR_OF_ERA;
                }
            }
            return context.setParsedField(f, value, errorPos, successPos);
        }

        @Override
        public String toString() {
            if (minWidth == 1 && maxWidth == 19 && signStyle == SignStyle.NORMAL) {
                return "Value(" + field.getName() + ")";
            }
            if (minWidth == maxWidth && signStyle == SignStyle.NOT_NEGATIVE) {
                return "Value(" + field.getName() + "," + minWidth + ")";
            }
            return "Value(" + field.getName() + "," + minWidth + "," + maxWidth + "," + signStyle + ")";
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Prints and parses a reduced numeric date-time field.
     */
    static final class ReducedPrinterParser extends NumberPrinterParser {
        private final int baseValue;
        private final int range;

        /**
         * Constructor.
         *
         * @param field  the field to format, validated not null
         * @param width  the field width, from 1 to 18
         * @param baseValue  the base value
         */
        ReducedPrinterParser(TemporalField field, int width, int baseValue) {
            super(field, width, width, SignStyle.NOT_NEGATIVE);
            if (width < 1 || width > 18) {
                throw new IllegalArgumentException("The width must be from 1 to 18 inclusive but was " + width);
            }
            if (field.range().isValidValue(baseValue) == false) {
                throw new IllegalArgumentException("The base value must be within the range of the field");
            }
            this.baseValue = baseValue;
            this.range = EXCEED_POINTS[width];
            if ((((long) baseValue) + range) > Integer.MAX_VALUE) {
                throw new DateTimeException("Unable to add printer-parser as the range exceeds the capacity of an int");
            }
        }

        @Override
        long getValue(long value) {
            return Math.abs(value % range);
        }

        @Override
        int setValue(DateTimeParseContext context, long value, int errorPos, int successPos) {
            int lastPart = baseValue % range;
            if (baseValue > 0) {
                value = baseValue - lastPart + value;
            } else {
                value = baseValue - lastPart - value;
            }
            if (value < baseValue) {
                value += range;
            }
            return context.setParsedField(field, value, errorPos, successPos);
        }

        @Override
        NumberPrinterParser withFixedWidth() {
            return this;
        }

        @Override
        boolean isFixedWidth() {
            return true;
        }

        @Override
        public String toString() {
            return "ReducedValue(" + field.getName() + "," + minWidth + "," + baseValue + ")";
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Prints and parses a numeric date-time field with optional padding.
     */
    static final class FractionPrinterParser implements DateTimePrinterParser {
        private final TemporalField field;
        private final int minWidth;
        private final int maxWidth;
        private final boolean decimalPoint;

        /**
         * Constructor.
         *
         * @param field  the field to output, not null
         * @param minWidth  the minimum width to output, from 0 to 9
         * @param maxWidth  the maximum width to output, from 0 to 9
         * @param decimalPoint  whether to output the localized decimal point symbol
         */
        FractionPrinterParser(TemporalField field, int minWidth, int maxWidth, boolean decimalPoint) {
            Objects.requireNonNull(field, "field");
            if (field.range().isFixed() == false) {
                throw new IllegalArgumentException("Field must have a fixed set of values: " + field.getName());
            }
            if (minWidth < 0 || minWidth > 9) {
                throw new IllegalArgumentException("Minimum width must be from 0 to 9 inclusive but was " + minWidth);
            }
            if (maxWidth < 1 || maxWidth > 9) {
                throw new IllegalArgumentException("Maximum width must be from 1 to 9 inclusive but was " + maxWidth);
            }
            if (maxWidth < minWidth) {
                throw new IllegalArgumentException("Maximum width must exceed or equal the minimum width but " +
                        maxWidth + " < " + minWidth);
            }
            this.field = field;
            this.minWidth = minWidth;
            this.maxWidth = maxWidth;
            this.decimalPoint = decimalPoint;
        }

        @Override
        public boolean format(DateTimePrintContext context, StringBuilder buf) {
            Long value = context.getValue(field);
            if (value == null) {
                return false;
            }
            DateTimeFormatSymbols symbols = context.getSymbols();
            BigDecimal fraction = convertToFraction(value);
            if (fraction.scale() == 0) {  // scale is zero if value is zero
                if (minWidth > 0) {
                    if (decimalPoint) {
                        buf.append(symbols.getDecimalSeparator());
                    }
                    for (int i = 0; i < minWidth; i++) {
                        buf.append(symbols.getZeroDigit());
                    }
                }
            } else {
                int outputScale = Math.min(Math.max(fraction.scale(), minWidth), maxWidth);
                fraction = fraction.setScale(outputScale, RoundingMode.FLOOR);
                String str = fraction.toPlainString().substring(2);
                str = symbols.convertNumberToI18N(str);
                if (decimalPoint) {
                    buf.append(symbols.getDecimalSeparator());
                }
                buf.append(str);
            }
            return true;
        }

        @Override
        public int parse(DateTimeParseContext context, CharSequence text, int position) {
            int effectiveMin = (context.isStrict() ? minWidth : 0);
            int effectiveMax = (context.isStrict() ? maxWidth : 9);
            int length = text.length();
            if (position == length) {
                // valid if whole field is optional, invalid if minimum width
                return (effectiveMin > 0 ? ~position : position);
            }
            if (decimalPoint) {
                if (text.charAt(position) != context.getSymbols().getDecimalSeparator()) {
                    // valid if whole field is optional, invalid if minimum width
                    return (effectiveMin > 0 ? ~position : position);
                }
                position++;
            }
            int minEndPos = position + effectiveMin;
            if (minEndPos > length) {
                return ~position;  // need at least min width digits
            }
            int maxEndPos = Math.min(position + effectiveMax, length);
            int total = 0;  // can use int because we are only parsing up to 9 digits
            int pos = position;
            while (pos < maxEndPos) {
                char ch = text.charAt(pos++);
                int digit = context.getSymbols().convertToDigit(ch);
                if (digit < 0) {
                    if (pos < minEndPos) {
                        return ~position;  // need at least min width digits
                    }
                    pos--;
                    break;
                }
                total = total * 10 + digit;
            }
            BigDecimal fraction = new BigDecimal(total).movePointLeft(pos - position);
            long value = convertFromFraction(fraction);
            return context.setParsedField(field, value, position, pos);
        }

        /**
         * Converts a value for this field to a fraction between 0 and 1.
         * <p>
         * The fractional value is between 0 (inclusive) and 1 (exclusive).
         * It can only be returned if the {@link java.time.temporal.TemporalField#range() value range} is fixed.
         * The fraction is obtained by calculation from the field range using 9 decimal
         * places and a rounding mode of {@link RoundingMode#FLOOR FLOOR}.
         * The calculation is inaccurate if the values do not run continuously from smallest to largest.
         * <p>
         * For example, the second-of-minute value of 15 would be returned as 0.25,
         * assuming the standard definition of 60 seconds in a minute.
         *
         * @param value  the value to convert, must be valid for this rule
         * @return the value as a fraction within the range, from 0 to 1, not null
         * @throws DateTimeException if the value cannot be converted to a fraction
         */
        private BigDecimal convertToFraction(long value) {
            ValueRange range = field.range();
            range.checkValidValue(value, field);
            BigDecimal minBD = BigDecimal.valueOf(range.getMinimum());
            BigDecimal rangeBD = BigDecimal.valueOf(range.getMaximum()).subtract(minBD).add(BigDecimal.ONE);
            BigDecimal valueBD = BigDecimal.valueOf(value).subtract(minBD);
            BigDecimal fraction = valueBD.divide(rangeBD, 9, RoundingMode.FLOOR);
            // stripTrailingZeros bug
            return fraction.compareTo(BigDecimal.ZERO) == 0 ? BigDecimal.ZERO : fraction.stripTrailingZeros();
        }

        /**
         * Converts a fraction from 0 to 1 for this field to a value.
         * <p>
         * The fractional value must be between 0 (inclusive) and 1 (exclusive).
         * It can only be returned if the {@link java.time.temporal.TemporalField#range() value range} is fixed.
         * The value is obtained by calculation from the field range and a rounding
         * mode of {@link RoundingMode#FLOOR FLOOR}.
         * The calculation is inaccurate if the values do not run continuously from smallest to largest.
         * <p>
         * For example, the fractional second-of-minute of 0.25 would be converted to 15,
         * assuming the standard definition of 60 seconds in a minute.
         *
         * @param fraction  the fraction to convert, not null
         * @return the value of the field, valid for this rule
         * @throws DateTimeException if the value cannot be converted
         */
        private long convertFromFraction(BigDecimal fraction) {
            ValueRange range = field.range();
            BigDecimal minBD = BigDecimal.valueOf(range.getMinimum());
            BigDecimal rangeBD = BigDecimal.valueOf(range.getMaximum()).subtract(minBD).add(BigDecimal.ONE);
            BigDecimal valueBD = fraction.multiply(rangeBD).setScale(0, RoundingMode.FLOOR).add(minBD);
            return valueBD.longValueExact();
        }

        @Override
        public String toString() {
            String decimal = (decimalPoint ? ",DecimalPoint" : "");
            return "Fraction(" + field.getName() + "," + minWidth + "," + maxWidth + decimal + ")";
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Prints or parses field text.
     */
    static final class TextPrinterParser implements DateTimePrinterParser {
        private final TemporalField field;
        private final TextStyle textStyle;
        private final DateTimeTextProvider provider;
        /**
         * The cached number printer parser.
         * Immutable and volatile, so no synchronization needed.
         */
        private volatile NumberPrinterParser numberPrinterParser;

        /**
         * Constructor.
         *
         * @param field  the field to output, not null
         * @param textStyle  the text style, not null
         * @param provider  the text provider, not null
         */
        TextPrinterParser(TemporalField field, TextStyle textStyle, DateTimeTextProvider provider) {
            // validated by caller
            this.field = field;
            this.textStyle = textStyle;
            this.provider = provider;
        }

        @Override
        public boolean format(DateTimePrintContext context, StringBuilder buf) {
            Long value = context.getValue(field);
            if (value == null) {
                return false;
            }
            String text;
            Chronology chrono = context.getTemporal().query(Queries.chronology());
            if (chrono == null || chrono == IsoChronology.INSTANCE) {
                text = provider.getText(field, value, textStyle, context.getLocale());
            } else {
                text = provider.getText(chrono, field, value, textStyle, context.getLocale());
            }
            if (text == null) {
                return numberPrinterParser().format(context, buf);
            }
            buf.append(text);
            return true;
        }

        @Override
        public int parse(DateTimeParseContext context, CharSequence parseText, int position) {
            int length = parseText.length();
            if (position < 0 || position > length) {
                throw new IndexOutOfBoundsException();
            }
            TextStyle style = (context.isStrict() ? textStyle : null);
            Chronology chrono = context.getEffectiveChronology();
            Iterator<Entry<String, Long>> it;
            if (chrono == null || chrono == IsoChronology.INSTANCE) {
                it = provider.getTextIterator(field, style, context.getLocale());
            } else {
                it = provider.getTextIterator(chrono, field, style, context.getLocale());
            }
            if (it != null) {
                while (it.hasNext()) {
                    Entry<String, Long> entry = it.next();
                    String itText = entry.getKey();
                    if (context.subSequenceEquals(itText, 0, parseText, position, itText.length())) {
                        return context.setParsedField(field, entry.getValue(), position, position + itText.length());
                    }
                }
                if (context.isStrict()) {
                    return ~position;
                }
            }
            return numberPrinterParser().parse(context, parseText, position);
        }

        /**
         * Create and cache a number printer parser.
         * @return the number printer parser for this field, not null
         */
        private NumberPrinterParser numberPrinterParser() {
            if (numberPrinterParser == null) {
                numberPrinterParser = new NumberPrinterParser(field, 1, 19, SignStyle.NORMAL);
            }
            return numberPrinterParser;
        }

        @Override
        public String toString() {
            if (textStyle == TextStyle.FULL) {
                return "Text(" + field.getName() + ")";
            }
            return "Text(" + field.getName() + "," + textStyle + ")";
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Prints or parses an ISO-8601 instant.
     */
    static final class InstantPrinterParser implements DateTimePrinterParser {
        // days in a 400 year cycle = 146097
        // days in a 10,000 year cycle = 146097 * 25
        // seconds per day = 86400
        private static final long SECONDS_PER_10000_YEARS = 146097L * 25L * 86400L;
        private static final long SECONDS_0000_TO_1970 = ((146097L * 5L) - (30L * 365L + 7L)) * 86400L;
        private static final CompositePrinterParser PARSER = new DateTimeFormatterBuilder()
                    .parseCaseInsensitive()
                    .append(DateTimeFormatter.ISO_LOCAL_DATE).appendLiteral('T')
                    .append(DateTimeFormatter.ISO_LOCAL_TIME).appendLiteral('Z')
                    .toFormatter().toPrinterParser(false);

        InstantPrinterParser() {
        }

        @Override
        public boolean format(DateTimePrintContext context, StringBuilder buf) {
            // use INSTANT_SECONDS, thus this code is not bound by Instant.MAX
            Long inSecs = context.getValue(INSTANT_SECONDS);
            Long inNanos = context.getValue(NANO_OF_SECOND);
            if (inSecs == null || inNanos == null) {
                return false;
            }
            long inSec = inSecs;
            int inNano = NANO_OF_SECOND.checkValidIntValue(inNanos);
            if (inSec >= -SECONDS_0000_TO_1970) {
                // current era
                long zeroSecs = inSec - SECONDS_PER_10000_YEARS + SECONDS_0000_TO_1970;
                long hi = Math.floorDiv(zeroSecs, SECONDS_PER_10000_YEARS) + 1;
                long lo = Math.floorMod(zeroSecs, SECONDS_PER_10000_YEARS);
                LocalDateTime ldt = LocalDateTime.ofEpochSecond(lo - SECONDS_0000_TO_1970, inNano, ZoneOffset.UTC);
                if (hi > 0) {
                    buf.append('+').append(hi);
                }
                buf.append(ldt).append('Z');
            } else {
                // before current era
                long zeroSecs = inSec + SECONDS_0000_TO_1970;
                long hi = zeroSecs / SECONDS_PER_10000_YEARS;
                long lo = zeroSecs % SECONDS_PER_10000_YEARS;
                LocalDateTime ldt = LocalDateTime.ofEpochSecond(lo - SECONDS_0000_TO_1970, inNano, ZoneOffset.UTC);
                int pos = buf.length();
                buf.append(ldt).append('Z');
                if (hi < 0) {
                    if (ldt.getYear() == -10_000) {
                        buf.replace(pos, pos + 2, Long.toString(hi - 1));
                    } else if (lo == 0) {
                        buf.insert(pos, hi);
                    } else {
                        buf.insert(pos + 1, Math.abs(hi));
                    }
                }
            }
            return true;
        }

        @Override
        public int parse(DateTimeParseContext context, CharSequence text, int position) {
            // new context to avoid overwriting fields like year/month/day
            DateTimeParseContext newContext = context.copy();
            int pos = PARSER.parse(newContext, text, position);
            if (pos < 0) {
                return pos;
            }
            // parser restricts most fields to 2 digits, so definitely int
            // correctly parsed nano is also guaranteed to be valid
            long yearParsed = newContext.getParsed(YEAR);
            int month = newContext.getParsed(MONTH_OF_YEAR).intValue();
            int day = newContext.getParsed(DAY_OF_MONTH).intValue();
            int hour = newContext.getParsed(HOUR_OF_DAY).intValue();
            int min = newContext.getParsed(MINUTE_OF_HOUR).intValue();
            Long secVal = newContext.getParsed(SECOND_OF_MINUTE);
            Long nanoVal = newContext.getParsed(NANO_OF_SECOND);
            int sec = (secVal != null ? secVal.intValue() : 0);
            int nano = (nanoVal != null ? nanoVal.intValue() : 0);
            int year = (int) yearParsed % 10_000;
            long instantSecs;
            try {
                LocalDateTime ldt = LocalDateTime.of(year, month, day, hour, min, sec, 0);
                instantSecs = ldt.toEpochSecond(ZoneOffset.UTC);
                instantSecs += Math.multiplyExact(yearParsed / 10_000L, SECONDS_PER_10000_YEARS);
            } catch (RuntimeException ex) {
                return ~position;
            }
            int successPos = text.length();
            successPos = context.setParsedField(INSTANT_SECONDS, instantSecs, position, successPos);
            return context.setParsedField(NANO_OF_SECOND, nano, position, successPos);
        }

        @Override
        public String toString() {
            return "Instant()";
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Prints or parses an offset ID.
     */
    static final class OffsetIdPrinterParser implements DateTimePrinterParser {
        static final String[] PATTERNS = new String[] {
            "+HH", "+HHmm", "+HH:mm", "+HHMM", "+HH:MM", "+HHMMss", "+HH:MM:ss", "+HHMMSS", "+HH:MM:SS",
        };  // order used in pattern builder
        static final OffsetIdPrinterParser INSTANCE_ID_Z = new OffsetIdPrinterParser("+HH:MM:ss", "Z");
        static final OffsetIdPrinterParser INSTANCE_ID_ZERO = new OffsetIdPrinterParser("+HH:MM:ss", "0");

        private final String noOffsetText;
        private final int type;

        /**
         * Constructor.
         *
         * @param pattern  the pattern
         * @param noOffsetText  the text to use for UTC, not null
         */
        OffsetIdPrinterParser(String pattern, String noOffsetText) {
            Objects.requireNonNull(pattern, "pattern");
            Objects.requireNonNull(noOffsetText, "noOffsetText");
            this.type = checkPattern(pattern);
            this.noOffsetText = noOffsetText;
        }

        private int checkPattern(String pattern) {
            for (int i = 0; i < PATTERNS.length; i++) {
                if (PATTERNS[i].equals(pattern)) {
                    return i;
                }
            }
            throw new IllegalArgumentException("Invalid zone offset pattern: " + pattern);
        }

        @Override
        public boolean format(DateTimePrintContext context, StringBuilder buf) {
            Long offsetSecs = context.getValue(OFFSET_SECONDS);
            if (offsetSecs == null) {
                return false;
            }
            int totalSecs = Math.toIntExact(offsetSecs);
            if (totalSecs == 0) {
                buf.append(noOffsetText);
            } else {
                int absHours = Math.abs((totalSecs / 3600) % 100);  // anything larger than 99 silently dropped
                int absMinutes = Math.abs((totalSecs / 60) % 60);
                int absSeconds = Math.abs(totalSecs % 60);
                int bufPos = buf.length();
                int output = absHours;
                buf.append(totalSecs < 0 ? "-" : "+")
                    .append((char) (absHours / 10 + '0')).append((char) (absHours % 10 + '0'));
                if (type >= 3 || (type >= 1 && absMinutes > 0)) {
                    buf.append((type % 2) == 0 ? ":" : "")
                        .append((char) (absMinutes / 10 + '0')).append((char) (absMinutes % 10 + '0'));
                    output += absMinutes;
                    if (type >= 7 || (type >= 5 && absSeconds > 0)) {
                        buf.append((type % 2) == 0 ? ":" : "")
                            .append((char) (absSeconds / 10 + '0')).append((char) (absSeconds % 10 + '0'));
                        output += absSeconds;
                    }
                }
                if (output == 0) {
                    buf.setLength(bufPos);
                    buf.append(noOffsetText);
                }
            }
            return true;
        }

        @Override
        public int parse(DateTimeParseContext context, CharSequence text, int position) {
            int length = text.length();
            int noOffsetLen = noOffsetText.length();
            if (noOffsetLen == 0) {
                if (position == length) {
                    return context.setParsedField(OFFSET_SECONDS, 0, position, position);
                }
            } else {
                if (position == length) {
                    return ~position;
                }
                if (context.subSequenceEquals(text, position, noOffsetText, 0, noOffsetLen)) {
                    return context.setParsedField(OFFSET_SECONDS, 0, position, position + noOffsetLen);
                }
            }

            // parse normal plus/minus offset
            char sign = text.charAt(position);  // IOOBE if invalid position
            if (sign == '+' || sign == '-') {
                // starts
                int negative = (sign == '-' ? -1 : 1);
                int[] array = new int[4];
                array[0] = position + 1;
                if ((parseNumber(array, 1, text, true) ||
                        parseNumber(array, 2, text, type >=3) ||
                        parseNumber(array, 3, text, false)) == false) {
                    // success
                    long offsetSecs = negative * (array[1] * 3600L + array[2] * 60L + array[3]);
                    return context.setParsedField(OFFSET_SECONDS, offsetSecs, position, array[0]);
                }
            }
            // handle special case of empty no offset text
            if (noOffsetLen == 0) {
                return context.setParsedField(OFFSET_SECONDS, 0, position, position + noOffsetLen);
            }
            return ~position;
        }

        /**
         * Parse a two digit zero-prefixed number.
         *
         * @param array  the array of parsed data, 0=pos,1=hours,2=mins,3=secs, not null
         * @param arrayIndex  the index to parse the value into
         * @param parseText  the offset ID, not null
         * @param required  whether this number is required
         * @return true if an error occurred
         */
        private boolean parseNumber(int[] array, int arrayIndex, CharSequence parseText, boolean required) {
            if ((type + 3) / 2 < arrayIndex) {
                return false;  // ignore seconds/minutes
            }
            int pos = array[0];
            if ((type % 2) == 0 && arrayIndex > 1) {
                if (pos + 1 > parseText.length() || parseText.charAt(pos) != ':') {
                    return required;
                }
                pos++;
            }
            if (pos + 2 > parseText.length()) {
                return required;
            }
            char ch1 = parseText.charAt(pos++);
            char ch2 = parseText.charAt(pos++);
            if (ch1 < '0' || ch1 > '9' || ch2 < '0' || ch2 > '9') {
                return required;
            }
            int value = (ch1 - 48) * 10 + (ch2 - 48);
            if (value < 0 || value > 59) {
                return required;
            }
            array[arrayIndex] = value;
            array[0] = pos;
            return false;
        }

        @Override
        public String toString() {
            String converted = noOffsetText.replace("'", "''");
            return "Offset(" + PATTERNS[type] + ",'" + converted + "')";
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Prints or parses a zone ID.
     */
    static final class ZoneTextPrinterParser extends ZoneIdPrinterParser {

        /** The text style to output. */
        private final TextStyle textStyle;

        /** The preferred zoneid map */
        private Set<String> preferredZones;

        ZoneTextPrinterParser(TextStyle textStyle, Set<ZoneId> preferredZones) {
            super(Queries.zone(), "ZoneText(" + textStyle + ")");
            this.textStyle = Objects.requireNonNull(textStyle, "textStyle");
            if (preferredZones != null && preferredZones.size() != 0) {
                this.preferredZones = new HashSet<>();
                for (ZoneId id : preferredZones) {
                    this.preferredZones.add(id.getId());
                }
            }
        }

        private static final int STD = 0;
        private static final int DST = 1;
        private static final int GENERIC = 2;
        private static final Map<String, SoftReference<Map<Locale, String[]>>> cache =
            new ConcurrentHashMap<>();

        private String getDisplayName(String id, int type, Locale locale) {
            if (textStyle == TextStyle.NARROW) {
                return null;
            }
            String[] names;
            SoftReference<Map<Locale, String[]>> ref = cache.get(id);
            Map<Locale, String[]> perLocale = null;
            if (ref == null || (perLocale = ref.get()) == null ||
                (names = perLocale.get(locale)) == null) {
                names = TimeZoneNameUtility.retrieveDisplayNames(id, locale);
                if (names == null) {
                    return null;
                }
                names = Arrays.copyOfRange(names, 0, 7);
                names[5] =
                    TimeZoneNameUtility.retrieveGenericDisplayName(id, TimeZone.LONG,locale);
                if (names[5] == null) {
                    names[5] = names[0]; // use the id
                }
                names[6] =
                    TimeZoneNameUtility.retrieveGenericDisplayName(id, TimeZone.SHORT,locale);
                if (names[6] == null) {
                    names[6] = names[0];
                }
                if (perLocale == null) {
                    perLocale = new ConcurrentHashMap<>();
                }
                perLocale.put(locale, names);
                cache.put(id, new SoftReference<>(perLocale));
            }
            switch (type) {
            case STD:
                return names[textStyle.ordinal() + 1];
            case DST:
                return names[textStyle.ordinal() + 3];
            }
            return names[textStyle.ordinal() + 5];
        }

        @Override
        public boolean format(DateTimePrintContext context, StringBuilder buf) {
            ZoneId zone = context.getValue(Queries.zoneId());
            if (zone == null) {
                return false;
            }
            String zname = zone.getId();
            if (!(zone instanceof ZoneOffset)) {
                TemporalAccessor dt = context.getTemporal();
                String name = getDisplayName(zname,
                                             dt.isSupported(ChronoField.INSTANT_SECONDS)
                                             ? (zone.getRules().isDaylightSavings(Instant.from(dt)) ? DST : STD)
                                             : GENERIC,
                                             context.getLocale());
                if (name != null) {
                    zname = name;
                }
            }
            buf.append(zname);
            return true;
        }

        // cache per instance for now
        private final Map<Locale, Entry<Integer, SoftReference<PrefixTree>>>
            cachedTree = new HashMap<>();
        private final Map<Locale, Entry<Integer, SoftReference<PrefixTree>>>
            cachedTreeCI = new HashMap<>();

        @Override
        protected PrefixTree getTree(DateTimeParseContext context) {
            if (textStyle == TextStyle.NARROW) {
                return super.getTree(context);
            }
            Locale locale = context.getLocale();
            boolean isCaseSensitive = context.isCaseSensitive();
            Set<String> regionIds = ZoneRulesProvider.getAvailableZoneIds();
            int regionIdsSize = regionIds.size();

            Map<Locale, Entry<Integer, SoftReference<PrefixTree>>> cached =
                isCaseSensitive ? cachedTree : cachedTreeCI;

            Entry<Integer, SoftReference<PrefixTree>> entry = null;
            PrefixTree tree = null;
            String[][] zoneStrings = null;
            if ((entry = cached.get(locale)) == null ||
                (entry.getKey() != regionIdsSize ||
                (tree = entry.getValue().get()) == null)) {
                tree = PrefixTree.newTree(context);
                zoneStrings = TimeZoneNameUtility.getZoneStrings(locale);
                for (String[] names : zoneStrings) {
                    String zid = names[0];
                    if (!regionIds.contains(zid)) {
                        continue;
                    }
                    tree.add(zid, zid);    // don't convert zid -> metazone
                    zid = ZoneName.toZid(zid, locale);
                    int i = textStyle == TextStyle.FULL ? 1 : 2;
                    for (; i < names.length; i += 2) {
                        tree.add(names[i], zid);
                    }
                }
                // if we have a set of preferred zones, need a copy and
                // add the preferred zones again to overwrite
                if (preferredZones != null) {
                    for (String[] names : zoneStrings) {
                        String zid = names[0];
                        if (!preferredZones.contains(zid) || !regionIds.contains(zid)) {
                            continue;
                        }
                        int i = textStyle == TextStyle.FULL ? 1 : 2;
                        for (; i < names.length; i += 2) {
                            tree.add(names[i], zid);
                       }
                    }
                }
                cached.put(locale, new SimpleImmutableEntry<>(regionIdsSize, new SoftReference<>(tree)));
            }
            return tree;
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Prints or parses a zone ID.
     */
    static class ZoneIdPrinterParser implements DateTimePrinterParser {
        private final TemporalQuery<ZoneId> query;
        private final String description;

        ZoneIdPrinterParser(TemporalQuery<ZoneId> query, String description) {
            this.query = query;
            this.description = description;
        }

        @Override
        public boolean format(DateTimePrintContext context, StringBuilder buf) {
            ZoneId zone = context.getValue(query);
            if (zone == null) {
                return false;
            }
            buf.append(zone.getId());
            return true;
        }

        /**
         * The cached tree to speed up parsing.
         */
        private static volatile Entry<Integer, PrefixTree> cachedPrefixTree;
        private static volatile Entry<Integer, PrefixTree> cachedPrefixTreeCI;

        protected PrefixTree getTree(DateTimeParseContext context) {
            // prepare parse tree
            Set<String> regionIds = ZoneRulesProvider.getAvailableZoneIds();
            final int regionIdsSize = regionIds.size();
            Entry<Integer, PrefixTree> cached = context.isCaseSensitive()
                                                ? cachedPrefixTree : cachedPrefixTreeCI;
            if (cached == null || cached.getKey() != regionIdsSize) {
                synchronized (this) {
                    cached = context.isCaseSensitive() ? cachedPrefixTree : cachedPrefixTreeCI;
                    if (cached == null || cached.getKey() != regionIdsSize) {
                        cached = new SimpleImmutableEntry<>(regionIdsSize, PrefixTree.newTree(regionIds, context));
                        if (context.isCaseSensitive()) {
                            cachedPrefixTree = cached;
                        } else {
                            cachedPrefixTreeCI = cached;
                        }
                    }
                }
            }
            return cached.getValue();
        }

        /**
         * This implementation looks for the longest matching string.
         * For example, parsing Etc/GMT-2 will return Etc/GMC-2 rather than just
         * Etc/GMC although both are valid.
         */
        @Override
        public int parse(DateTimeParseContext context, CharSequence text, int position) {
            int length = text.length();
            if (position > length) {
                throw new IndexOutOfBoundsException();
            }
            if (position == length) {
                return ~position;
            }

            // handle fixed time-zone IDs
            char nextChar = text.charAt(position);
            if (nextChar == '+' || nextChar == '-') {
                return parseOffsetBased(context, text, position, OffsetIdPrinterParser.INSTANCE_ID_Z);
            } else if (length >= position + 2) {
                char nextNextChar = text.charAt(position + 1);
                if (context.charEquals(nextChar, 'U') && context.charEquals(nextNextChar, 'T')) {
                    if (length >= position + 3 && context.charEquals(text.charAt(position + 2), 'C')) {
                        return parseOffsetBased(context, text, position + 3, OffsetIdPrinterParser.INSTANCE_ID_ZERO);
                    }
                    return parseOffsetBased(context, text, position + 2, OffsetIdPrinterParser.INSTANCE_ID_ZERO);
                } else if (context.charEquals(nextChar, 'G') && length >= position + 3 &&
                        context.charEquals(nextNextChar, 'M') && context.charEquals(text.charAt(position + 2), 'T')) {
                    return parseOffsetBased(context, text, position + 3, OffsetIdPrinterParser.INSTANCE_ID_ZERO);
                }
            }

            // parse
            PrefixTree tree = getTree(context);
            ParsePosition ppos = new ParsePosition(position);
            String parsedZoneId = tree.match(text, ppos);
            if (parsedZoneId == null) {
                if (context.charEquals(nextChar, 'Z')) {
                    context.setParsed(ZoneOffset.UTC);
                    return position + 1;
                }
                return ~position;
            }
            context.setParsed(ZoneId.of(parsedZoneId));
            return ppos.getIndex();
        }

        private int parseOffsetBased(DateTimeParseContext context, CharSequence text, int position, OffsetIdPrinterParser parser) {
            DateTimeParseContext newContext = context.copy();
            int endPos = parser.parse(newContext, text, position);
            if (endPos < 0) {
                if (parser == OffsetIdPrinterParser.INSTANCE_ID_Z) {
                    return ~position;
                }
                context.setParsed(ZoneOffset.UTC);
                return position;
            }
            int offset = (int) newContext.getParsed(OFFSET_SECONDS).longValue();
            ZoneId zone = ZoneOffset.ofTotalSeconds(offset);
            context.setParsed(zone);
            return endPos;
        }

        @Override
        public String toString() {
            return description;
        }
    }

    //-----------------------------------------------------------------------
    /**
     * A String based prefix tree for parsing time-zone names.
     */
    static class PrefixTree {
        protected String key;
        protected String value;
        protected char c0;    // performance optimization to avoid the
                              // boundary check cost of key.charat(0)
        protected PrefixTree child;
        protected PrefixTree sibling;

        private PrefixTree(String k, String v, PrefixTree child) {
            this.key = k;
            this.value = v;
            this.child = child;
            if (k.length() == 0){
                c0 = 0xffff;
            } else {
                c0 = key.charAt(0);
            }
        }

        /**
         * Creates a new prefix parsing tree based on parse context.
         *
         * @param context  the parse context
         * @return the tree, not null
         */
        public static PrefixTree newTree(DateTimeParseContext context) {
            //if (!context.isStrict()) {
            //    return new LENIENT("", null, null);
            //}
            if (context.isCaseSensitive()) {
                return new PrefixTree("", null, null);
            }
            return new CI("", null, null);
        }

        /**
         * Creates a new prefix parsing tree.
         *
         * @param keys  a set of strings to build the prefix parsing tree, not null
         * @param context  the parse context
         * @return the tree, not null
         */
        public static  PrefixTree newTree(Set<String> keys, DateTimeParseContext context) {
            PrefixTree tree = newTree(context);
            for (String k : keys) {
                tree.add0(k, k);
            }
            return tree;
        }

        /**
         * Clone a copy of this tree
         */
        public PrefixTree copyTree() {
            PrefixTree copy = new PrefixTree(key, value, null);
            if (child != null) {
                copy.child = child.copyTree();
            }
            if (sibling != null) {
                copy.sibling = sibling.copyTree();
            }
            return copy;
        }


        /**
         * Adds a pair of {key, value} into the prefix tree.
         *
         * @param k  the key, not null
         * @param v  the value, not null
         * @return  true if the pair is added successfully
         */
        public boolean add(String k, String v) {
            return add0(k, v);
        }

        private boolean add0(String k, String v) {
            k = toKey(k);
            int prefixLen = prefixLength(k);
            if (prefixLen == key.length()) {
                if (prefixLen < k.length()) {  // down the tree
                    String subKey = k.substring(prefixLen);
                    PrefixTree c = child;
                    while (c != null) {
                        if (isEqual(c.c0, subKey.charAt(0))) {
                            return c.add0(subKey, v);
                        }
                        c = c.sibling;
                    }
                    // add the node as the child of the current node
                    c = newNode(subKey, v, null);
                    c.sibling = child;
                    child = c;
                    return true;
                }
                // have an existing <key, value> already, overwrite it
                // if (value != null) {
                //    return false;
                //}
                value = v;
                return true;
            }
            // split the existing node
            PrefixTree n1 = newNode(key.substring(prefixLen), value, child);
            key = k.substring(0, prefixLen);
            child = n1;
            if (prefixLen < k.length()) {
                PrefixTree n2 = newNode(k.substring(prefixLen), v, null);
                child.sibling = n2;
                value = null;
            } else {
                value = v;
            }
            return true;
        }

        /**
         * Match text with the prefix tree.
         *
         * @param text  the input text to parse, not null
         * @param off  the offset position to start parsing at
         * @param end  the end position to stop parsing
         * @return the resulting string, or null if no match found.
         */
        public String match(CharSequence text, int off, int end) {
            if (!prefixOf(text, off, end)){
                return null;
            }
            if (child != null && (off += key.length()) != end) {
                PrefixTree c = child;
                do {
                    if (isEqual(c.c0, text.charAt(off))) {
                        String found = c.match(text, off, end);
                        if (found != null) {
                            return found;
                        }
                        return value;
                    }
                    c = c.sibling;
                } while (c != null);
            }
            return value;
        }

        /**
         * Match text with the prefix tree.
         *
         * @param text  the input text to parse, not null
         * @param pos  the position to start parsing at, from 0 to the text
         *  length. Upon return, position will be updated to the new parse
         *  position, or unchanged, if no match found.
         * @return the resulting string, or null if no match found.
         */
        public String match(CharSequence text, ParsePosition pos) {
            int off = pos.getIndex();
            int end = text.length();
            if (!prefixOf(text, off, end)){
                return null;
            }
            off += key.length();
            if (child != null && off != end) {
                PrefixTree c = child;
                do {
                    if (isEqual(c.c0, text.charAt(off))) {
                        pos.setIndex(off);
                        String found = c.match(text, pos);
                        if (found != null) {
                            return found;
                        }
                        break;
                    }
                    c = c.sibling;
                } while (c != null);
            }
            pos.setIndex(off);
            return value;
        }

        protected String toKey(String k) {
            return k;
        }

        protected PrefixTree newNode(String k, String v, PrefixTree child) {
            return new PrefixTree(k, v, child);
        }

        protected boolean isEqual(char c1, char c2) {
            return c1 == c2;
        }

        protected boolean prefixOf(CharSequence text, int off, int end) {
            if (text instanceof String) {
                return ((String)text).startsWith(key, off);
            }
            int len = key.length();
            if (len > end - off) {
                return false;
            }
            int off0 = 0;
            while (len-- > 0) {
                if (!isEqual(key.charAt(off0++), text.charAt(off++))) {
                    return false;
                }
            }
            return true;
        }

        private int prefixLength(String k) {
            int off = 0;
            while (off < k.length() && off < key.length()) {
                if (!isEqual(k.charAt(off), key.charAt(off))) {
                    return off;
                }
                off++;
            }
            return off;
        }

        /**
         * Case Insensitive prefix tree.
         */
        private static class CI extends PrefixTree {

            private CI(String k, String v, PrefixTree child) {
                super(k, v, child);
            }

            @Override
            protected CI newNode(String k, String v, PrefixTree child) {
                return new CI(k, v, child);
            }

            @Override
            protected boolean isEqual(char c1, char c2) {
                return DateTimeParseContext.charEqualsIgnoreCase(c1, c2);
            }

            @Override
            protected boolean prefixOf(CharSequence text, int off, int end) {
                int len = key.length();
                if (len > end - off) {
                    return false;
                }
                int off0 = 0;
                while (len-- > 0) {
                    if (!isEqual(key.charAt(off0++), text.charAt(off++))) {
                        return false;
                    }
                }
                return true;
            }
        }

        /**
         * Lenient prefix tree. Case insensitive and ignores characters
         * like space, underscore and slash.
         */
        private static class LENIENT extends CI {

            private LENIENT(String k, String v, PrefixTree child) {
                super(k, v, child);
            }

            @Override
            protected CI newNode(String k, String v, PrefixTree child) {
                return new LENIENT(k, v, child);
            }

            private boolean isLenientChar(char c) {
                return c == ' ' || c == '_' || c == '/';
            }

            protected String toKey(String k) {
                for (int i = 0; i < k.length(); i++) {
                    if (isLenientChar(k.charAt(i))) {
                        StringBuilder sb = new StringBuilder(k.length());
                        sb.append(k, 0, i);
                        i++;
                        while (i < k.length()) {
                            if (!isLenientChar(k.charAt(i))) {
                                sb.append(k.charAt(i));
                            }
                            i++;
                        }
                        return sb.toString();
                    }
                }
                return k;
            }

            @Override
            public String match(CharSequence text, ParsePosition pos) {
                int off = pos.getIndex();
                int end = text.length();
                int len = key.length();
                int koff = 0;
                while (koff < len && off < end) {
                    if (isLenientChar(text.charAt(off))) {
                        off++;
                        continue;
                    }
                    if (!isEqual(key.charAt(koff++), text.charAt(off++))) {
                        return null;
                    }
                }
                if (koff != len) {
                    return null;
                }
                if (child != null && off != end) {
                    int off0 = off;
                    while (off0 < end && isLenientChar(text.charAt(off0))) {
                        off0++;
                    }
                    if (off0 < end) {
                        PrefixTree c = child;
                        do {
                            if (isEqual(c.c0, text.charAt(off0))) {
                                pos.setIndex(off0);
                                String found = c.match(text, pos);
                                if (found != null) {
                                    return found;
                                }
                                break;
                            }
                            c = c.sibling;
                        } while (c != null);
                    }
                }
                pos.setIndex(off);
                return value;
            }
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Prints or parses a chronology.
     */
    static final class ChronoPrinterParser implements DateTimePrinterParser {
        /** The text style to output, null means the ID. */
        private final TextStyle textStyle;

        ChronoPrinterParser(TextStyle textStyle) {
            // validated by caller
            this.textStyle = textStyle;
        }

        @Override
        public boolean format(DateTimePrintContext context, StringBuilder buf) {
            Chronology chrono = context.getValue(Queries.chronology());
            if (chrono == null) {
                return false;
            }
            if (textStyle == null) {
                buf.append(chrono.getId());
            } else {
                buf.append(chrono.getId());  // TODO: Use symbols
            }
            return true;
        }

        @Override
        public int parse(DateTimeParseContext context, CharSequence text, int position) {
            // simple looping parser to find the chronology
            if (position < 0 || position > text.length()) {
                throw new IndexOutOfBoundsException();
            }
            Set<Chronology> chronos = Chronology.getAvailableChronologies();
            Chronology bestMatch = null;
            int matchLen = -1;
            for (Chronology chrono : chronos) {
                String id = chrono.getId();
                int idLen = id.length();
                if (idLen > matchLen && context.subSequenceEquals(text, position, id, 0, idLen)) {
                    bestMatch = chrono;
                    matchLen = idLen;
                }
            }
            if (bestMatch == null) {
                return ~position;
            }
            context.setParsed(bestMatch);
            return position + matchLen;
        }
    }

    //-----------------------------------------------------------------------
    /**
     * Prints or parses a localized pattern.
     */
    static final class LocalizedPrinterParser implements DateTimePrinterParser {
        private final FormatStyle dateStyle;
        private final FormatStyle timeStyle;

        /**
         * Constructor.
         *
         * @param dateStyle  the date style to use, may be null
         * @param timeStyle  the time style to use, may be null
         */
        LocalizedPrinterParser(FormatStyle dateStyle, FormatStyle timeStyle) {
            // validated by caller
            this.dateStyle = dateStyle;
            this.timeStyle = timeStyle;
        }

        @Override
        public boolean format(DateTimePrintContext context, StringBuilder buf) {
            Chronology chrono = Chronology.from(context.getTemporal());
            return formatter(context.getLocale(), chrono).toPrinterParser(false).format(context, buf);
        }

        @Override
        public int parse(DateTimeParseContext context, CharSequence text, int position) {
            Chronology chrono = context.getEffectiveChronology();
            return formatter(context.getLocale(), chrono).toPrinterParser(false).parse(context, text, position);
        }

        /**
         * Gets the formatter to use.
         *
         * @param locale  the locale to use, not null
         * @param chrono  the chronology to use, not null
         * @return the formatter, not null
         * @throws IllegalArgumentException if the formatter cannot be found
         */
        private DateTimeFormatter formatter(Locale locale, Chronology chrono) {
            return DateTimeFormatStyleProvider.getInstance()
                                              .getFormatter(dateStyle, timeStyle, chrono, locale);
        }

        @Override
        public String toString() {
            return "Localized(" + (dateStyle != null ? dateStyle : "") + "," +
                (timeStyle != null ? timeStyle : "") + ")";
        }
    }


    //-----------------------------------------------------------------------
    /**
     * Prints or parses a localized pattern from a localized field.
     * The specific formatter and parameters is not selected until the
     * the field is to be printed or parsed.
     * The locale is needed to select the proper WeekFields from which
     * the field for day-of-week, week-of-month, or week-of-year is selected.
     */
    static final class WeekBasedFieldPrinterParser implements DateTimePrinterParser {
        private char chr;
        private int count;

        /**
         * Constructor.
         *
         * @param chr the pattern format letter that added this PrinterParser.
         * @param count the repeat count of the format letter
         */
        WeekBasedFieldPrinterParser(char chr, int count) {
            this.chr = chr;
            this.count = count;
        }

        @Override
        public boolean format(DateTimePrintContext context, StringBuilder buf) {
            return printerParser(context.getLocale()).format(context, buf);
        }

        @Override
        public int parse(DateTimeParseContext context, CharSequence text, int position) {
            return printerParser(context.getLocale()).parse(context, text, position);
        }

        /**
         * Gets the printerParser to use based on the field and the locale.
         *
         * @param locale  the locale to use, not null
         * @return the formatter, not null
         * @throws IllegalArgumentException if the formatter cannot be found
         */
        private DateTimePrinterParser printerParser(Locale locale) {
            WeekFields weekDef = WeekFields.of(locale);
            TemporalField field = null;
            switch (chr) {
                case 'e':
                    field = weekDef.dayOfWeek();
                    break;
                case 'w':
                    field = weekDef.weekOfMonth();
                    break;
                case 'W':
                    field = weekDef.weekOfYear();
                    break;
                default:
                    throw new IllegalStateException("unreachable");
            }
            return new NumberPrinterParser(field, (count == 2 ? 2 : 1), 2, SignStyle.NOT_NEGATIVE);
        }

        @Override
        public String toString() {
            return String.format("WeekBased(%c%d)", chr, count);
        }
    }


    //-------------------------------------------------------------------------
    /**
     * Length comparator.
     */
    static final Comparator<String> LENGTH_SORT = new Comparator<String>() {
        @Override
        public int compare(String str1, String str2) {
            return str1.length() == str2.length() ? str1.compareTo(str2) : str1.length() - str2.length();
        }
    };

}
