/*
 * Copyright (c) 1997, 2012, Oracle and/or its affiliates. All rights reserved.
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
 * (C) Copyright Taligent, Inc. 1996, 1997 - All Rights Reserved
 * (C) Copyright IBM Corp. 1996 - 1998 - All Rights Reserved
 *
 * The original version of this source code and documentation
 * is copyrighted and owned by Taligent, Inc., a wholly-owned
 * subsidiary of IBM. These materials are provided under terms
 * of a License Agreement between Taligent and Sun. This technology
 * is protected by multiple US and International patents.
 *
 * This notice and attribution to Taligent may not be removed.
 * Taligent is a registered trademark of Taligent, Inc.
 *
 */

package sun.text.resources.hr;

import java.util.ListResourceBundle;

public class FormatData_hr extends ListResourceBundle {
    /**
     * Overrides ListResourceBundle
     */
    protected final Object[][] getContents() {
        return new Object[][] {
            { "MonthNames",
                new String[] {
                    "sije\u010dnja",
                    "velja\u010de",
                    "o\u017eujka",
                    "travnja",
                    "svibnja",
                    "lipnja",
                    "srpnja",
                    "kolovoza",
                    "rujna",
                    "listopada",
                    "studenoga",
                    "prosinca",
                    "",
                }
            },
            { "standalone.MonthNames",
                new String[] {
                    "sije\u010danj", // january
                    "velja\u010da", // february
                    "o\u017eujak", // march
                    "travanj", // april
                    "svibanj", // may
                    "lipanj", // june
                    "srpanj", // july
                    "kolovoz", // august
                    "rujan", // september
                    "listopad", // october
                    "studeni", // november
                    "prosinac", // december
                    "" // month 13 if applicable
                }
            },
            { "MonthAbbreviations",
                new String[] {
                    "sij",
                    "velj",
                    "o\u017eu",
                    "tra",
                    "svi",
                    "lip",
                    "srp",
                    "kol",
                    "ruj",
                    "lis",
                    "stu",
                    "pro",
                    "",
                }
            },
            { "standalone.MonthAbbreviations",
                new String[] {
                    "sij", // abb january
                    "vel", // abb february
                    "o\u017eu", // abb march
                    "tra", // abb april
                    "svi", // abb may
                    "lip", // abb june
                    "srp", // abb july
                    "kol", // abb august
                    "ruj", // abb september
                    "lis", // abb october
                    "stu", // abb november
                    "pro", // abb december
                    "" // abb month 13 if applicable
                }
            },
            { "standalone.MonthNarrows",
                new String[] {
                    "1.",
                    "2.",
                    "3.",
                    "4.",
                    "5.",
                    "6.",
                    "7.",
                    "8.",
                    "9.",
                    "10.",
                    "11.",
                    "12.",
                    "",
                }
            },
            { "DayNames",
                new String[] {
                    "nedjelja", // Sunday
                    "ponedjeljak", // Monday
                    "utorak", // Tuesday
                    "srijeda", // Wednesday
                    "\u010detvrtak", // Thursday
                    "petak", // Friday
                    "subota" // Saturday
                }
            },
            { "DayAbbreviations",
                new String[] {
                    "ned", // abb Sunday
                    "pon", // abb Monday
                    "uto", // abb Tuesday
                    "sri", // abb Wednesday
                    "\u010det", // abb Thursday
                    "pet", // abb Friday
                    "sub" // abb Saturday
                }
            },
            { "DayNarrows",
                new String[] {
                    "N",
                    "P",
                    "U",
                    "S",
                    "\u010c",
                    "P",
                    "S",
                }
            },
            { "standalone.DayNarrows",
                new String[] {
                    "n",
                    "p",
                    "u",
                    "s",
                    "\u010d",
                    "p",
                    "s",
                }
            },
            { "NumberElements",
                new String[] {
                    ",", // decimal separator
                    ".", // group (thousands) separator
                    ";", // list separator
                    "%", // percent sign
                    "0", // native 0 digit
                    "#", // pattern digit
                    "-", // minus sign
                    "E", // exponential
                    "\u2030", // per mille
                    "\u221e", // infinity
                    "\ufffd" // NaN
                }
            },
            { "TimePatterns",
                new String[] {
                    "HH:mm:ss z", // full time pattern
                    "HH:mm:ss z", // long time pattern
                    "HH:mm:ss", // medium time pattern
                    "HH:mm", // short time pattern
                }
            },
            { "DatePatterns",
                new String[] {
                    "yyyy. MMMM dd", // full date pattern
                    "yyyy. MMMM dd", // long date pattern
                    "yyyy.MM.dd", // medium date pattern
                    "yyyy.MM.dd", // short date pattern
                }
            },
            { "DateTimePatterns",
                new String[] {
                    "{1} {0}" // date-time pattern
                }
            },
            { "DateTimePatternChars", "GanjkHmsSEDFwWxhKzZ" },
        };
    }
}
