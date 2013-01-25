#!/bin/sh

#
# Copyright (c) 2005, 2012, Oracle and/or its affiliates. All rights reserved.
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# This code is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 only, as
# published by the Free Software Foundation.  Oracle designates this
# particular file as subject to the "Classpath" exception as provided
# by Oracle in the LICENSE file that accompanied this code.
#
# This code is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# version 2 for more details (a copy is included in the LICENSE file that
# accompanied this code).
#
# You should have received a copy of the GNU General Public License version
# 2 along with this work; if not, write to the Free Software Foundation,
# Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
# or visit www.oracle.com if you need additional information or have any
# questions.
#

#
# This script is to generate the supported locale list string and replace the
# LocaleDataMetaInfo-XLocales.java.template in <ws>/src/share/classes/sun/util
# 
# SORT, NAWK & SED is passed in as environment variables.
#

# A list of resource base name list;
RESOURCE_NAMES=$1

# A list of US resources;
US_FILES_LIST=$2

# A list of non-US resources;
NONUS_FILES_LIST=$3

INPUT_FILE=$4
OUTPUT_FILE=$5

localelist=
getlocalelist() {
    localelist=""
    localelist=`$NAWK -F$1_ '{print $2}' $2 | $SORT | $SED -e 's/_/-/g'`
}

sed_script="$SED -e \"s@^#warn .*@// -- This file was mechanically generated: Do not edit! -- //@\" "

# ja-JP-JP and th-TH-TH need to be manually added, as they don't have any resource files.
nonusall=" ja-JP-JP th-TH-TH "

for FILE in $RESOURCE_NAMES
do
    getlocalelist $FILE $US_FILES_LIST
    sed_script=$sed_script"-e \"s@#"$FILE"_USLocales#@$localelist@g\" "
    usall=$usall" "$localelist
    getlocalelist $FILE $NONUS_FILES_LIST
    sed_script=$sed_script"-e \"s@#"$FILE"_NonUSLocales#@$localelist@g\" "
    nonusall=$nonusall" "$localelist
done

usall=`(for LOC in $usall; do echo $LOC;done) |$SORT -u`
nonusall=`(for LOC in $nonusall; do echo $LOC;done) |$SORT -u`

sed_script=$sed_script"-e \"s@#AvailableLocales_USLocales#@$usall@g\" "
sed_script=$sed_script"-e \"s@#AvailableLocales_NonUSLocales#@$nonusall@g\" "

sed_script=$sed_script"$INPUT_FILE > $OUTPUT_FILE"
eval $sed_script
