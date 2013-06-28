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

package java.util;

import java.io.FilterOutputStream;
import java.io.InputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;

/**
 * This class consists exclusively of static methods for obtaining
 * encoders and decoders for the Base64 encoding scheme. The
 * implementation of this class supports the following types of Base64
 * as specified in
 * <a href="http://www.ietf.org/rfc/rfc4648.txt">RFC 4648</a> and
 * <a href="http://www.ietf.org/rfc/rfc2045.txt">RFC 2045</a>.
 *
 * <p>
 * <ul>
 * <a name="basic">
 * <li><b>Basic</b>
 * <p> Uses "The Base64 Alphabet" as specified in Table 1 of
 *     RFC 4648 and RFC 2045 for encoding and decoding operation.
 *     The encoder does not add any line feed (line separator)
 *     character. The decoder rejects data that contains characters
 *     outside the base64 alphabet.</p></li>
 *
 * <a name="url">
 * <li><b>URL and Filename safe</b>
 * <p> Uses the "URL and Filename safe Base64 Alphabet" as specified
 *     in Table 2 of RFC 4648 for encoding and decoding. The
 *     encoder does not add any line feed (line separator) character.
 *     The decoder rejects data that contains characters outside the
 *     base64 alphabet.</p></li>
 *
 * <a name="mime">
 * <li><b>MIME</b>
 * <p> Uses the "The Base64 Alphabet" as specified in Table 1 of
 *     RFC 2045 for encoding and decoding operation. The encoded output
 *     must be represented in lines of no more than 76 characters each
 *     and uses a carriage return {@code '\r'} followed immediately by
 *     a linefeed {@code '\n'} as the line separator. No line separator
 *     is added to the end of the encoded output. All line separators
 *     or other characters not found in the base64 alphabet table are
 *     ignored in decoding operation.</p></li>
 * </ul>
 *
 * <p> Unless otherwise noted, passing a {@code null} argument to a
 * method of this class will cause a {@link java.lang.NullPointerException
 * NullPointerException} to be thrown.
 *
 * @author  Xueming Shen
 * @since   1.8
 */

public class Base64 {

    private Base64() {}

    /**
     * Returns a {@link Encoder} that encodes using the
     * <a href="#basic">Basic</a> type base64 encoding scheme.
     *
     * @return  A Base64 encoder.
     */
    public static Encoder getEncoder() {
         return Encoder.RFC4648;
    }

    /**
     * Returns a {@link Encoder} that encodes using the
     * <a href="#url">URL and Filename safe</a> type base64
     * encoding scheme.
     *
     * @return  A Base64 encoder.
     */
    public static Encoder getUrlEncoder() {
         return Encoder.RFC4648_URLSAFE;
    }

    /**
     * Returns a {@link Encoder} that encodes using the
     * <a href="#mime">MIME</a> type base64 encoding scheme.
     *
     * @return  A Base64 encoder.
     */
    public static Encoder getMimeEncoder() {
        return Encoder.RFC2045;
    }

    /**
     * Returns a {@link Encoder} that encodes using the
     * <a href="#mime">MIME</a> type base64 encoding scheme
     * with specified line length and line separators.
     *
     * @param   lineLength
     *          the length of each output line (rounded down to nearest multiple
     *          of 4). If {@code lineLength <= 0} the output will not be separated
     *          in lines
     * @param   lineSeparator
     *          the line separator for each output line
     *
     * @return  A Base64 encoder.
     *
     * @throws  IllegalArgumentException if {@code lineSeparator} includes any
     *          character of "The Base64 Alphabet" as specified in Table 1 of
     *          RFC 2045.
     */
    public static Encoder getEncoder(int lineLength, byte[] lineSeparator) {
         Objects.requireNonNull(lineSeparator);
         int[] base64 = Decoder.fromBase64;
         for (byte b : lineSeparator) {
             if (base64[b & 0xff] != -1)
                 throw new IllegalArgumentException(
                     "Illegal base64 line separator character 0x" + Integer.toString(b, 16));
         }
         return new Encoder(false, lineSeparator, lineLength >> 2 << 2);
    }

    /**
     * Returns a {@link Decoder} that decodes using the
     * <a href="#basic">Basic</a> type base64 encoding scheme.
     *
     * @return  A Base64 decoder.
     */
    public static Decoder getDecoder() {
         return Decoder.RFC4648;
    }

    /**
     * Returns a {@link Decoder} that decodes using the
     * <a href="#url">URL and Filename safe</a> type base64
     * encoding scheme.
     *
     * @return  A Base64 decoder.
     */
    public static Decoder getUrlDecoder() {
         return Decoder.RFC4648_URLSAFE;
    }

    /**
     * Returns a {@link Decoder} that decodes using the
     * <a href="#mime">MIME</a> type base64 decoding scheme.
     *
     * @return  A Base64 decoder.
     */
    public static Decoder getMimeDecoder() {
         return Decoder.RFC2045;
    }

    /**
     * This class implements an encoder for encoding byte data using
     * the Base64 encoding scheme as specified in RFC 4648 and RFC 2045.
     *
     * <p> Instances of {@link Encoder} class are safe for use by
     * multiple concurrent threads.
     *
     * <p> Unless otherwise noted, passing a {@code null} argument to
     * a method of this class will cause a
     * {@link java.lang.NullPointerException NullPointerException} to
     * be thrown.
     *
     * @see     Decoder
     * @since   1.8
     */
    public static class Encoder {

        private final byte[] newline;
        private final int linemax;
        private final boolean isURL;

        private Encoder(boolean isURL, byte[] newline, int linemax) {
            this.isURL = isURL;
            this.newline = newline;
            this.linemax = linemax;
        }

        /**
         * This array is a lookup table that translates 6-bit positive integer
         * index values into their "Base64 Alphabet" equivalents as specified
         * in "Table 1: The Base64 Alphabet" of RFC 2045 (and RFC 4648).
         */
        private static final char[] toBase64 = {
            'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
            'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
            'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
            'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
        };

        /**
         * It's the lookup table for "URL and Filename safe Base64" as specified
         * in Table 2 of the RFC 4648, with the '+' and '/' changed to '-' and
         * '_'. This table is used when BASE64_URL is specified.
         */
        private static final char[] toBase64URL = {
            'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
            'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
            'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
            'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '-', '_'
        };

        private static final int MIMELINEMAX = 76;
        private static final byte[] CRLF = new byte[] {'\r', '\n'};

        static final Encoder RFC4648 = new Encoder(false, null, -1);
        static final Encoder RFC4648_URLSAFE = new Encoder(true, null, -1);
        static final Encoder RFC2045 = new Encoder(false, CRLF, MIMELINEMAX);

        /**
         * Encodes all bytes from the specified byte array into a newly-allocated
         * byte array using the {@link Base64} encoding scheme. The returned byte
         * array is of the length of the resulting bytes.
         *
         * @param   src
         *          the byte array to encode
         * @return  A newly-allocated byte array containing the resulting
         *          encoded bytes.
         */
        public byte[] encode(byte[] src) {
            int len = 4 * ((src.length + 2) / 3);    // dst array size
            if (linemax > 0)                          // line separators
                len += (len - 1) / linemax * newline.length;
            byte[] dst = new byte[len];
            int ret = encode0(src, 0, src.length, dst);
            if (ret != dst.length)
                 return Arrays.copyOf(dst, ret);
            return dst;
        }

        /**
         * Encodes all bytes from the specified byte array using the
         * {@link Base64} encoding scheme, writing the resulting bytes to the
         * given output byte array, starting at offset 0.
         *
         * <p> It is the responsibility of the invoker of this method to make
         * sure the output byte array {@code dst} has enough space for encoding
         * all bytes from the input byte array. No bytes will be written to the
         * output byte array if the output byte array is not big enough.
         *
         * @param   src
         *          the byte array to encode
         * @param   dst
         *          the output byte array
         * @return  The number of bytes written to the output byte array
         *
         * @throws  IllegalArgumentException if {@code dst} does not have enough
         *          space for encoding all input bytes.
         */
        public int encode(byte[] src, byte[] dst) {
            int len = 4 * ((src.length + 2) / 3);    // dst array size
            if (linemax > 0) {
                len += (len - 1) / linemax * newline.length;
            }
            if (dst.length < len)
                throw new IllegalArgumentException(
                    "Output byte array is too small for encoding all input bytes");
            return encode0(src, 0, src.length, dst);
        }

        /**
         * Encodes the specified byte array into a String using the {@link Base64}
         * encoding scheme.
         *
         * <p> This method first encodes all input bytes into a base64 encoded
         * byte array and then constructs a new String by using the encoded byte
         * array and the {@link java.nio.charset.StandardCharsets#ISO_8859_1
         * ISO-8859-1} charset.
         *
         * <p> In other words, an invocation of this method has exactly the same
         * effect as invoking
         * {@code new String(encode(src), StandardCharsets.ISO_8859_1)}.
         *
         * @param   src
         *          the byte array to encode
         * @return  A String containing the resulting Base64 encoded characters
         */
        @SuppressWarnings("deprecation")
        public String encodeToString(byte[] src) {
            byte[] encoded = encode(src);
            return new String(encoded, 0, 0, encoded.length);
        }

        /**
         * Encodes all remaining bytes from the specified byte buffer into
         * a newly-allocated ByteBuffer using the {@link Base64} encoding
         * scheme.
         *
         * Upon return, the source buffer's position will be updated to
         * its limit; its limit will not have been changed. The returned
         * output buffer's position will be zero and its limit will be the
         * number of resulting encoded bytes.
         *
         * @param   buffer
         *          the source ByteBuffer to encode
         * @return  A newly-allocated byte buffer containing the encoded bytes.
         */
        public ByteBuffer encode(ByteBuffer buffer) {
            int len = 4 * ((buffer.remaining() + 2) / 3);
            if (linemax > 0)
                len += (len - 1) / linemax * newline.length;
            byte[] dst = new byte[len];
            int ret = 0;
            if (buffer.hasArray()) {
                ret = encode0(buffer.array(),
                              buffer.arrayOffset() + buffer.position(),
                              buffer.arrayOffset() + buffer.limit(),
                              dst);
                buffer.position(buffer.limit());
            } else {
                byte[] src = new byte[buffer.remaining()];
                buffer.get(src);
                ret = encode0(src, 0, src.length, dst);
            }
            if (ret != dst.length)
                 dst = Arrays.copyOf(dst, ret);
            return ByteBuffer.wrap(dst);
        }

        /**
         * Encodes as many bytes as possible from the input byte buffer
         * using the {@link Base64} encoding scheme, writing the resulting
         * bytes to the given output byte buffer.
         *
         * <p>The buffers are read from, and written to, starting at their
         * current positions. Upon return, the input and output buffers'
         * positions will be advanced to reflect the bytes read and written,
         * but their limits will not be modified.
         *
         * <p>The encoding operation will stop and return if either all
         * remaining bytes in the input buffer have been encoded and written
         * to the output buffer, or the output buffer has insufficient space
         * to encode any more input bytes. The encoding operation can be
         * continued, if there is more bytes in input buffer to be encoded,
         * by invoking this method again with an output buffer that has more
         * {@linkplain java.nio.Buffer#remaining remaining} bytes. This is
         * typically done by draining any encoded bytes from the output buffer.
         * The value returned from last invocation needs to be passed in as the
         * third parameter {@code bytesOut} if it is to continue an unfinished
         * encoding, 0 otherwise.
         *
         * <p><b>Recommended Usage Example</b>
         * <pre>
         *    ByteBuffer src = ...;
         *    ByteBuffer dst = ...;
         *    Base64.Encoder enc = Base64.getMimeDecoder();
         *
         *    int bytesOut = 0;
         *    while (src.hasRemaining()) {
         *        // clear output buffer for decoding
         *        dst.clear();
         *        bytesOut = enc.encode(src, dst, bytesOut);
         *
         *        // read encoded bytes out of "dst"
         *        dst.flip();
         *        ...
         *    }
         * </pre>
         *
         * @param   src
         *          the input byte buffer to encode
         * @param   dst
         *          the output byte buffer
         * @param   bytesOut
         *          the return value of last invocation if this is to continue
         *          an unfinished encoding operation, 0 otherwise
         * @return  The sum total of {@code bytesOut} and the number of bytes
         *          written to the output ByteBuffer during this invocation.
         */
        public int encode(ByteBuffer src, ByteBuffer dst, int bytesOut) {
            if (src.hasArray() && dst.hasArray())
                return encodeArray(src, dst, bytesOut);
            return encodeBuffer(src, dst, bytesOut);
        }

        /**
         * Wraps an output stream for encoding byte data using the {@link Base64}
         * encoding scheme.
         *
         * <p> It is recommended to promptly close the returned output stream after
         * use, during which it will flush all possible leftover bytes to the underlying
         * output stream. Closing the returned output stream will close the underlying
         * output stream.
         *
         * @param   os
         *          the output stream.
         * @return  the output stream for encoding the byte data into the
         *          specified Base64 encoded format
         */
        public OutputStream wrap(OutputStream os) {
            Objects.requireNonNull(os);
            return new EncOutputStream(os, isURL ? toBase64URL : toBase64,
                                       newline, linemax);
        }

        private int encodeArray(ByteBuffer src, ByteBuffer dst, int bytesOut) {
            char[] base64 = isURL? toBase64URL : toBase64;
            byte[] sa = src.array();
            int    sp = src.arrayOffset() + src.position();
            int    sl = src.arrayOffset() + src.limit();
            byte[] da = dst.array();
            int    dp = dst.arrayOffset() + dst.position();
            int    dl = dst.arrayOffset() + dst.limit();
            int    dp00 = dp;
            int    dpos = 0;        // dp of each line
            if (linemax > 0 && bytesOut > 0)
                dpos = bytesOut % (linemax + newline.length);
            try {
                if (dpos == linemax && sp < src.limit()) {
                    if (dp + newline.length > dl)
                        return  dp - dp00 + bytesOut;
                    for (byte b : newline){
                        dst.put(dp++, b);
                    }
                    dpos = 0;
                }
                sl = sp + (sl - sp) / 3 * 3;
                while (sp < sl) {
                    int slen = (linemax > 0) ? (linemax - dpos) / 4 * 3
                                             : sl - sp;
                    int sl0 = Math.min(sp + slen, sl);
                    for (int sp0 = sp, dp0 = dp ; sp0 < sl0; ) {
                        if (dp0 + 4 > dl) {
                            sp = sp0; dp = dp0;
                            return  dp0 - dp00 + bytesOut;
                        }
                        int bits = (sa[sp0++] & 0xff) << 16 |
                                   (sa[sp0++] & 0xff) <<  8 |
                                   (sa[sp0++] & 0xff);
                        da[dp0++] = (byte)base64[(bits >>> 18) & 0x3f];
                        da[dp0++] = (byte)base64[(bits >>> 12) & 0x3f];
                        da[dp0++] = (byte)base64[(bits >>> 6)  & 0x3f];
                        da[dp0++] = (byte)base64[bits & 0x3f];
                    }
                    int n = (sl0 - sp) / 3 * 4;
                    dpos += n;
                    dp += n;
                    sp = sl0;
                    if (dpos == linemax && sp < src.limit()) {
                        if (dp + newline.length > dl)
                            return  dp - dp00 + bytesOut;
                        for (byte b : newline){
                            da[dp++] = b;
                        }
                        dpos = 0;
                    }
                }
                sl = src.arrayOffset() + src.limit();
                if (sp < sl && dl >= dp + 4) {       // 1 or 2 leftover bytes
                    int b0 = sa[sp++] & 0xff;
                    da[dp++] = (byte)base64[b0 >> 2];
                    if (sp == sl) {
                        da[dp++] = (byte)base64[(b0 << 4) & 0x3f];
                        da[dp++] = '=';
                        da[dp++] = '=';
                    } else {
                        int b1 = sa[sp++] & 0xff;
                        da[dp++] = (byte)base64[(b0 << 4) & 0x3f | (b1 >> 4)];
                        da[dp++] = (byte)base64[(b1 << 2) & 0x3f];
                        da[dp++] = '=';
                    }
                }
                return dp - dp00 + bytesOut;
            } finally {
                src.position(sp - src.arrayOffset());
                dst.position(dp - dst.arrayOffset());
            }
        }

        private int encodeBuffer(ByteBuffer src, ByteBuffer dst, int bytesOut) {
            char[] base64 = isURL? toBase64URL : toBase64;
            int sp = src.position();
            int sl = src.limit();
            int dp = dst.position();
            int dl = dst.limit();
            int dp00 = dp;

            int dpos = 0;        // dp of each line
            if (linemax > 0 && bytesOut > 0)
                dpos = bytesOut % (linemax + newline.length);
            try {
                if (dpos == linemax && sp < src.limit()) {
                    if (dp + newline.length > dl)
                        return  dp - dp00 + bytesOut;
                    for (byte b : newline){
                        dst.put(dp++, b);
                    }
                    dpos = 0;
                }
                sl = sp + (sl - sp) / 3 * 3;
                while (sp < sl) {
                    int slen = (linemax > 0) ? (linemax - dpos) / 4 * 3
                                             : sl - sp;
                    int sl0 = Math.min(sp + slen, sl);
                    for (int sp0 = sp, dp0 = dp ; sp0 < sl0; ) {
                        if (dp0 + 4 > dl) {
                            sp = sp0; dp = dp0;
                            return  dp0 - dp00 + bytesOut;
                        }
                        int bits = (src.get(sp0++) & 0xff) << 16 |
                                   (src.get(sp0++) & 0xff) <<  8 |
                                   (src.get(sp0++) & 0xff);
                        dst.put(dp0++, (byte)base64[(bits >>> 18) & 0x3f]);
                        dst.put(dp0++, (byte)base64[(bits >>> 12) & 0x3f]);
                        dst.put(dp0++, (byte)base64[(bits >>> 6)  & 0x3f]);
                        dst.put(dp0++, (byte)base64[bits & 0x3f]);
                    }
                    int n = (sl0 - sp) / 3 * 4;
                    dpos += n;
                    dp += n;
                    sp = sl0;
                    if (dpos == linemax && sp < src.limit()) {
                        if (dp + newline.length > dl)
                            return  dp - dp00 + bytesOut;
                        for (byte b : newline){
                            dst.put(dp++, b);
                        }
                        dpos = 0;
                    }
                }
                if (sp < src.limit() && dl >= dp + 4) {       // 1 or 2 leftover bytes
                    int b0 = src.get(sp++) & 0xff;
                    dst.put(dp++, (byte)base64[b0 >> 2]);
                    if (sp == src.limit()) {
                        dst.put(dp++, (byte)base64[(b0 << 4) & 0x3f]);
                        dst.put(dp++, (byte)'=');
                        dst.put(dp++, (byte)'=');
                    } else {
                        int b1 = src.get(sp++) & 0xff;
                        dst.put(dp++, (byte)base64[(b0 << 4) & 0x3f | (b1 >> 4)]);
                        dst.put(dp++, (byte)base64[(b1 << 2) & 0x3f]);
                        dst.put(dp++, (byte)'=');
                    }
                }
                return dp - dp00 + bytesOut;
            } finally {
                src.position(sp);
                dst.position(dp);
            }
        }

        private int encode0(byte[] src, int off, int end, byte[] dst) {
            char[] base64 = isURL ? toBase64URL : toBase64;
            int sp = off;
            int slen = (end - off) / 3 * 3;
            int sl = off + slen;
            if (linemax > 0 && slen  > linemax / 4 * 3)
                slen = linemax / 4 * 3;
            int dp = 0;
            while (sp < sl) {
                int sl0 = Math.min(sp + slen, sl);
                for (int sp0 = sp, dp0 = dp ; sp0 < sl0; ) {
                    int bits = (src[sp0++] & 0xff) << 16 |
                               (src[sp0++] & 0xff) <<  8 |
                               (src[sp0++] & 0xff);
                    dst[dp0++] = (byte)base64[(bits >>> 18) & 0x3f];
                    dst[dp0++] = (byte)base64[(bits >>> 12) & 0x3f];
                    dst[dp0++] = (byte)base64[(bits >>> 6)  & 0x3f];
                    dst[dp0++] = (byte)base64[bits & 0x3f];
                }
                int dlen = (sl0 - sp) / 3 * 4;
                dp += dlen;
                sp = sl0;
                if (dlen == linemax && sp < end) {
                    for (byte b : newline){
                        dst[dp++] = b;
                    }
                }
            }
            if (sp < end) {               // 1 or 2 leftover bytes
                int b0 = src[sp++] & 0xff;
                dst[dp++] = (byte)base64[b0 >> 2];
                if (sp == end) {
                    dst[dp++] = (byte)base64[(b0 << 4) & 0x3f];
                    dst[dp++] = '=';
                    dst[dp++] = '=';
                } else {
                    int b1 = src[sp++] & 0xff;
                    dst[dp++] = (byte)base64[(b0 << 4) & 0x3f | (b1 >> 4)];
                    dst[dp++] = (byte)base64[(b1 << 2) & 0x3f];
                    dst[dp++] = '=';
                }
            }
            return dp;
        }
    }

    /**
     * This class implements a decoder for decoding byte data using the
     * Base64 encoding scheme as specified in RFC 4648 and RFC 2045.
     *
     * <p> The Base64 padding character {@code '='} is accepted and
     * interpreted as the end of the encoded byte data, but is not
     * required. So if the final unit of the encoded byte data only has
     * two or three Base64 characters (without the corresponding padding
     * character(s) padded), they are decoded as if followed by padding
     * character(s).
     *
     * <p> Instances of {@link Decoder} class are safe for use by
     * multiple concurrent threads.
     *
     * <p> Unless otherwise noted, passing a {@code null} argument to
     * a method of this class will cause a
     * {@link java.lang.NullPointerException NullPointerException} to
     * be thrown.
     *
     * @see     Encoder
     * @since   1.8
     */
    public static class Decoder {

        private final boolean isURL;
        private final boolean isMIME;

        private Decoder(boolean isURL, boolean isMIME) {
            this.isURL = isURL;
            this.isMIME = isMIME;
        }

        /**
         * Lookup table for decoding unicode characters drawn from the
         * "Base64 Alphabet" (as specified in Table 1 of RFC 2045) into
         * their 6-bit positive integer equivalents.  Characters that
         * are not in the Base64 alphabet but fall within the bounds of
         * the array are encoded to -1.
         *
         */
        private static final int[] fromBase64 = new int[256];
        static {
            Arrays.fill(fromBase64, -1);
            for (int i = 0; i < Encoder.toBase64.length; i++)
                fromBase64[Encoder.toBase64[i]] = i;
            fromBase64['='] = -2;
        }

        /**
         * Lookup table for decoding "URL and Filename safe Base64 Alphabet"
         * as specified in Table2 of the RFC 4648.
         */
        private static final int[] fromBase64URL = new int[256];

        static {
            Arrays.fill(fromBase64URL, -1);
            for (int i = 0; i < Encoder.toBase64URL.length; i++)
                fromBase64URL[Encoder.toBase64URL[i]] = i;
            fromBase64URL['='] = -2;
        }

        static final Decoder RFC4648         = new Decoder(false, false);
        static final Decoder RFC4648_URLSAFE = new Decoder(true, false);
        static final Decoder RFC2045         = new Decoder(false, true);

        /**
         * Decodes all bytes from the input byte array using the {@link Base64}
         * encoding scheme, writing the results into a newly-allocated output
         * byte array. The returned byte array is of the length of the resulting
         * bytes.
         *
         * @param   src
         *          the byte array to decode
         *
         * @return  A newly-allocated byte array containing the decoded bytes.
         *
         * @throws  IllegalArgumentException
         *          if {@code src} is not in valid Base64 scheme
         */
        public byte[] decode(byte[] src) {
            byte[] dst = new byte[outLength(src, 0, src.length)];
            int ret = decode0(src, 0, src.length, dst);
            if (ret != dst.length) {
                dst = Arrays.copyOf(dst, ret);
            }
            return dst;
        }

        /**
         * Decodes a Base64 encoded String into a newly-allocated byte array
         * using the {@link Base64} encoding scheme.
         *
         * <p> An invocation of this method has exactly the same effect as invoking
         * {@code decode(src.getBytes(StandardCharsets.ISO_8859_1))}
         *
         * @param   src
         *          the string to decode
         *
         * @return  A newly-allocated byte array containing the decoded bytes.
         *
         * @throws  IllegalArgumentException
         *          if {@code src} is not in valid Base64 scheme
         */
        public byte[] decode(String src) {
            return decode(src.getBytes(StandardCharsets.ISO_8859_1));
        }

        /**
         * Decodes all bytes from the input byte array using the {@link Base64}
         * encoding scheme, writing the results into the given output byte array,
         * starting at offset 0.
         *
         * <p> It is the responsibility of the invoker of this method to make
         * sure the output byte array {@code dst} has enough space for decoding
         * all bytes from the input byte array. No bytes will be be written to
         * the output byte array if the output byte array is not big enough.
         *
         * <p> If the input byte array is not in valid Base64 encoding scheme
         * then some bytes may have been written to the output byte array before
         * IllegalargumentException is thrown.
         *
         * @param   src
         *          the byte array to decode
         * @param   dst
         *          the output byte array
         *
         * @return  The number of bytes written to the output byte array
         *
         * @throws  IllegalArgumentException
         *          if {@code src} is not in valid Base64 scheme, or {@code dst}
         *          does not have enough space for decoding all input bytes.
         */
        public int decode(byte[] src, byte[] dst) {
            int len = outLength(src, 0, src.length);
            if (dst.length < len)
                throw new IllegalArgumentException(
                    "Output byte array is too small for decoding all input bytes");
            return decode0(src, 0, src.length, dst);
        }

        /**
         * Decodes all bytes from the input byte buffer using the {@link Base64}
         * encoding scheme, writing the results into a newly-allocated ByteBuffer.
         *
         * <p> Upon return, the source buffer's position will be updated to
         * its limit; its limit will not have been changed. The returned
         * output buffer's position will be zero and its limit will be the
         * number of resulting decoded bytes
         *
         * @param   buffer
         *          the ByteBuffer to decode
         *
         * @return  A newly-allocated byte buffer containing the decoded bytes
         *
         * @throws  IllegalArgumentException
         *          if {@code src} is not in valid Base64 scheme.
         */
        public ByteBuffer decode(ByteBuffer buffer) {
            int pos0 = buffer.position();
            try {
                byte[] src;
                int sp, sl;
                if (buffer.hasArray()) {
                    src = buffer.array();
                    sp = buffer.arrayOffset() + buffer.position();
                    sl = buffer.arrayOffset() + buffer.limit();
                    buffer.position(buffer.limit());
                } else {
                    src = new byte[buffer.remaining()];
                    buffer.get(src);
                    sp = 0;
                    sl = src.length;
                }
                byte[] dst = new byte[outLength(src, sp, sl)];
                return ByteBuffer.wrap(dst, 0, decode0(src, sp, sl, dst));
            } catch (IllegalArgumentException iae) {
                buffer.position(pos0);
                throw iae;
            }
        }

        /**
         * Decodes as many bytes as possible from the input byte buffer
         * using the {@link Base64} encoding scheme, writing the resulting
         * bytes to the given output byte buffer.
         *
         * <p>The buffers are read from, and written to, starting at their
         * current positions. Upon return, the input and output buffers'
         * positions will be advanced to reflect the bytes read and written,
         * but their limits will not be modified.
         *
         * <p> If the input buffer is not in valid Base64 encoding scheme
         * then some bytes may have been written to the output buffer
         * before IllegalArgumentException is thrown. The positions of
         * both input and output buffer will not be advanced in this case.
         *
         * <p>The decoding operation will end and return if all remaining
         * bytes in the input buffer have been decoded and written to the
         * output buffer.
         *
         * <p> The decoding operation will stop and return if the output
         * buffer has insufficient space to decode any more input bytes.
         * The decoding operation can be continued, if there is more bytes
         * in input buffer to be decoded, by invoking this method again with
         * an output buffer that has more {@linkplain java.nio.Buffer#remaining
         * remaining} bytes. This is typically done by draining any decoded
         * bytes from the output buffer.
         *
         * <p><b>Recommended Usage Example</b>
         * <pre>
         *    ByteBuffer src = ...;
         *    ByteBuffer dst = ...;
         *    Base64.Decoder dec = Base64.getDecoder();
         *
         *    while (src.hasRemaining()) {
         *
         *        // prepare the output byte buffer
         *        dst.clear();
         *        dec.decode(src, dst);
         *
         *        // read bytes from the output buffer
         *        dst.flip();
         *        ...
         *    }
         * </pre>
         *
         * @param   src
         *          the input byte buffer to decode
         * @param   dst
         *          the output byte buffer
         *
         * @return  The number of bytes written to the output byte buffer during
         *          this decoding invocation
         *
         * @throws  IllegalArgumentException
         *          if {@code src} is not in valid Base64 scheme.
         */
        public int decode(ByteBuffer src, ByteBuffer dst) {
            int sp0 = src.position();
            int dp0 = dst.position();
            try {
                if (src.hasArray() && dst.hasArray())
                    return decodeArray(src, dst);
                return decodeBuffer(src, dst);
            } catch (IllegalArgumentException iae) {
                src.position(sp0);
                dst.position(dp0);
                throw iae;
            }
        }

        /**
         * Returns an input stream for decoding {@link Base64} encoded byte stream.
         *
         * <p> The {@code read}  methods of the returned {@code InputStream} will
         * throw {@code IOException} when reading bytes that cannot be decoded.
         *
         * <p> Closing the returned input stream will close the underlying
         * input stream.
         *
         * @param   is
         *          the input stream
         *
         * @return  the input stream for decoding the specified Base64 encoded
         *          byte stream
         */
        public InputStream wrap(InputStream is) {
            Objects.requireNonNull(is);
            return new DecInputStream(is, isURL ? fromBase64URL : fromBase64, isMIME);
        }

        private int decodeArray(ByteBuffer src, ByteBuffer dst) {
            int[] base64 = isURL ? fromBase64URL : fromBase64;
            int   bits = 0;
            int   shiftto = 18;       // pos of first byte of 4-byte atom
            byte[] sa = src.array();
            int    sp = src.arrayOffset() + src.position();
            int    sl = src.arrayOffset() + src.limit();
            byte[] da = dst.array();
            int    dp = dst.arrayOffset() + dst.position();
            int    dl = dst.arrayOffset() + dst.limit();
            int    dp0 = dp;
            int    mark = sp;
            try {
                while (sp < sl) {
                    int b = sa[sp++] & 0xff;
                    if ((b = base64[b]) < 0) {
                        if (b == -2) {   // padding byte
                            if (shiftto == 6 && (sp == sl || sa[sp++] != '=') ||
                                shiftto == 18) {
                                throw new IllegalArgumentException(
                                     "Input byte array has wrong 4-byte ending unit");
                            }
                            break;
                        }
                        if (isMIME)     // skip if for rfc2045
                            continue;
                        else
                            throw new IllegalArgumentException(
                                "Illegal base64 character " +
                                Integer.toString(sa[sp - 1], 16));
                    }
                    bits |= (b << shiftto);
                    shiftto -= 6;
                    if (shiftto < 0) {
                        if (dl < dp + 3)
                            return dp - dp0;
                        da[dp++] = (byte)(bits >> 16);
                        da[dp++] = (byte)(bits >>  8);
                        da[dp++] = (byte)(bits);
                        shiftto = 18;
                        bits = 0;
                        mark = sp;
                    }
                }
                if (shiftto == 6) {
                    if (dl - dp < 1)
                        return dp - dp0;
                    da[dp++] = (byte)(bits >> 16);
                } else if (shiftto == 0) {
                    if (dl - dp < 2)
                        return dp - dp0;
                    da[dp++] = (byte)(bits >> 16);
                    da[dp++] = (byte)(bits >>  8);
                } else if (shiftto == 12) {
                    throw new IllegalArgumentException(
                        "Last unit does not have enough valid bits");
                }
                while (sp < sl) {
                    if (isMIME && base64[sa[sp++]] < 0)
                        continue;
                    throw new IllegalArgumentException(
                        "Input byte array has incorrect ending byte at " + sp);
                }
                mark = sp;
                return dp - dp0;
            } finally {
                src.position(mark);
                dst.position(dp);
            }
        }

        private int decodeBuffer(ByteBuffer src, ByteBuffer dst) {
            int[] base64 = isURL ? fromBase64URL : fromBase64;
            int   bits = 0;
            int   shiftto = 18;       // pos of first byte of 4-byte atom
            int    sp = src.position();
            int    sl = src.limit();
            int    dp = dst.position();
            int    dl = dst.limit();
            int    dp0 = dp;
            int    mark = sp;
            try {
                while (sp < sl) {
                    int b = src.get(sp++) & 0xff;
                    if ((b = base64[b]) < 0) {
                        if (b == -2) {  // padding byte
                            if (shiftto == 6 && (sp == sl || src.get(sp++) != '=') ||
                                shiftto == 18) {
                                throw new IllegalArgumentException(
                                     "Input byte array has wrong 4-byte ending unit");
                            }
                            break;
                        }
                        if (isMIME)     // skip if for rfc2045
                            continue;
                        else
                            throw new IllegalArgumentException(
                                "Illegal base64 character " +
                                Integer.toString(src.get(sp - 1), 16));
                    }
                    bits |= (b << shiftto);
                    shiftto -= 6;
                    if (shiftto < 0) {
                        if (dl < dp + 3)
                            return dp - dp0;
                        dst.put(dp++, (byte)(bits >> 16));
                        dst.put(dp++, (byte)(bits >>  8));
                        dst.put(dp++, (byte)(bits));
                        shiftto = 18;
                        bits = 0;
                        mark = sp;
                    }
                }
                if (shiftto == 6) {
                    if (dl - dp < 1)
                        return dp - dp0;
                     dst.put(dp++, (byte)(bits >> 16));
                } else if (shiftto == 0) {
                    if (dl - dp < 2)
                        return dp - dp0;
                    dst.put(dp++, (byte)(bits >> 16));
                    dst.put(dp++, (byte)(bits >>  8));
                } else if (shiftto == 12) {
                    throw new IllegalArgumentException(
                        "Last unit does not have enough valid bits");
                }
                while (sp < sl) {
                    if (isMIME && base64[src.get(sp++)] < 0)
                        continue;
                    throw new IllegalArgumentException(
                        "Input byte array has incorrect ending byte at " + sp);
                }
                mark = sp;
                return dp - dp0;
            } finally {
                src.position(mark);
                dst.position(dp);
            }
        }

        private int outLength(byte[] src, int sp, int sl) {
            int[] base64 = isURL ? fromBase64URL : fromBase64;
            int paddings = 0;
            int len = sl - sp;
            if (len == 0)
                return 0;
            if (len < 2) {
                if (isMIME && base64[0] == -1)
                    return 0;
                throw new IllegalArgumentException(
                    "Input byte[] should at least have 2 bytes for base64 bytes");
            }
            if (src[sl - 1] == '=') {
                paddings++;
                if (src[sl - 2] == '=')
                    paddings++;
            }
            if (isMIME) {
                // scan all bytes to fill out all non-alphabet. a performance
                // trade-off of pre-scan or Arrays.copyOf
                int n = 0;
                while (sp < sl) {
                    int b = src[sp++] & 0xff;
                    if (b == '=')
                        break;
                    if ((b = base64[b]) == -1)
                        n++;
                }
                len -= n;
            }
            if (paddings == 0 && (len & 0x3) !=  0)
                paddings = 4 - (len & 0x3);
            return 3 * ((len + 3) / 4) - paddings;
        }

        private int decode0(byte[] src, int sp, int sl, byte[] dst) {
            int[] base64 = isURL ? fromBase64URL : fromBase64;
            int dp = 0;
            int bits = 0;
            int shiftto = 18;       // pos of first byte of 4-byte atom
            while (sp < sl) {
                int b = src[sp++] & 0xff;
                if ((b = base64[b]) < 0) {
                    if (b == -2) {     // padding byte '='
                        // xx=   shiftto==6&&sp==sl missing last =
                        // xx=y  shiftto==6 last is not =
                        // =     shiftto==18 unnecessary padding
                        // x=    shiftto==12 be taken care later
                        //       together with single x, invalid anyway
                        if (shiftto == 6 && (sp == sl || src[sp++] != '=') ||
                            shiftto == 18) {
                            throw new IllegalArgumentException(
                                "Input byte array has wrong 4-byte ending unit");
                        }
                        break;
                    }
                    if (isMIME)    // skip if for rfc2045
                        continue;
                    else
                        throw new IllegalArgumentException(
                            "Illegal base64 character " +
                            Integer.toString(src[sp - 1], 16));
                }
                bits |= (b << shiftto);
                shiftto -= 6;
                if (shiftto < 0) {
                    dst[dp++] = (byte)(bits >> 16);
                    dst[dp++] = (byte)(bits >>  8);
                    dst[dp++] = (byte)(bits);
                    shiftto = 18;
                    bits = 0;
                }
            }
            // reached end of byte array or hit padding '=' characters.
            if (shiftto == 6) {
                dst[dp++] = (byte)(bits >> 16);
            } else if (shiftto == 0) {
                dst[dp++] = (byte)(bits >> 16);
                dst[dp++] = (byte)(bits >>  8);
            } else if (shiftto == 12) {
                throw new IllegalArgumentException(
                    "Last unit does not have enough valid bits");
            }
            // anything left is invalid, if is not MIME.
            // if MIME, ignore all non-base64 character
            while (sp < sl) {
                if (isMIME && base64[src[sp++]] < 0)
                    continue;
                throw new IllegalArgumentException(
                    "Input byte array has incorrect ending byte at " + sp);
            }
            return dp;
        }
    }

    /*
     * An output stream for encoding bytes into the Base64.
     */
    private static class EncOutputStream extends FilterOutputStream {

        private int leftover = 0;
        private int b0, b1, b2;
        private boolean closed = false;

        private final char[] base64;    // byte->base64 mapping
        private final byte[] newline;   // line separator, if needed
        private final int linemax;
        private int linepos = 0;

        EncOutputStream(OutputStream os,
                        char[] base64, byte[] newline, int linemax) {
            super(os);
            this.base64 = base64;
            this.newline = newline;
            this.linemax = linemax;
        }

        @Override
        public void write(int b) throws IOException {
            byte[] buf = new byte[1];
            buf[0] = (byte)(b & 0xff);
            write(buf, 0, 1);
        }

        private void checkNewline() throws IOException {
            if (linepos == linemax) {
                out.write(newline);
                linepos = 0;
            }
        }

        @Override
        public void write(byte[] b, int off, int len) throws IOException {
            if (closed)
                throw new IOException("Stream is closed");
            if (off < 0 || len < 0 || off + len > b.length)
                throw new ArrayIndexOutOfBoundsException();
            if (len == 0)
                return;
            if (leftover != 0) {
                if (leftover == 1) {
                    b1 = b[off++] & 0xff;
                    len--;
                    if (len == 0) {
                        leftover++;
                        return;
                    }
                }
                b2 = b[off++] & 0xff;
                len--;
                checkNewline();
                out.write(base64[b0 >> 2]);
                out.write(base64[(b0 << 4) & 0x3f | (b1 >> 4)]);
                out.write(base64[(b1 << 2) & 0x3f | (b2 >> 6)]);
                out.write(base64[b2 & 0x3f]);
                linepos += 4;
            }
            int nBits24 = len / 3;
            leftover = len - (nBits24 * 3);
            while (nBits24-- > 0) {
                checkNewline();
                int bits = (b[off++] & 0xff) << 16 |
                           (b[off++] & 0xff) <<  8 |
                           (b[off++] & 0xff);
                out.write(base64[(bits >>> 18) & 0x3f]);
                out.write(base64[(bits >>> 12) & 0x3f]);
                out.write(base64[(bits >>> 6)  & 0x3f]);
                out.write(base64[bits & 0x3f]);
                linepos += 4;
           }
            if (leftover == 1) {
                b0 = b[off++] & 0xff;
            } else if (leftover == 2) {
                b0 = b[off++] & 0xff;
                b1 = b[off++] & 0xff;
            }
        }

        @Override
        public void close() throws IOException {
            if (!closed) {
                closed = true;
                if (leftover == 1) {
                    checkNewline();
                    out.write(base64[b0 >> 2]);
                    out.write(base64[(b0 << 4) & 0x3f]);
                    out.write('=');
                    out.write('=');
                } else if (leftover == 2) {
                    checkNewline();
                    out.write(base64[b0 >> 2]);
                    out.write(base64[(b0 << 4) & 0x3f | (b1 >> 4)]);
                    out.write(base64[(b1 << 2) & 0x3f]);
                    out.write('=');
                }
                leftover = 0;
                out.close();
            }
        }
    }

    /*
     * An input stream for decoding Base64 bytes
     */
    private static class DecInputStream extends InputStream {

        private final InputStream is;
        private final boolean isMIME;
        private final int[] base64;      // base64 -> byte mapping
        private int bits = 0;            // 24-bit buffer for decoding
        private int nextin = 18;         // next available "off" in "bits" for input;
                                         // -> 18, 12, 6, 0
        private int nextout = -8;        // next available "off" in "bits" for output;
                                         // -> 8, 0, -8 (no byte for output)
        private boolean eof = false;
        private boolean closed = false;

        DecInputStream(InputStream is, int[] base64, boolean isMIME) {
            this.is = is;
            this.base64 = base64;
            this.isMIME = isMIME;
        }

        private byte[] sbBuf = new byte[1];

        @Override
        public int read() throws IOException {
            return read(sbBuf, 0, 1) == -1 ? -1 : sbBuf[0] & 0xff;
        }

        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            if (closed)
                throw new IOException("Stream is closed");
            if (eof && nextout < 0)    // eof and no leftover
                return -1;
            if (off < 0 || len < 0 || len > b.length - off)
                throw new IndexOutOfBoundsException();
            int oldOff = off;
            if (nextout >= 0) {       // leftover output byte(s) in bits buf
                do {
                    if (len == 0)
                        return off - oldOff;
                    b[off++] = (byte)(bits >> nextout);
                    len--;
                    nextout -= 8;
                } while (nextout >= 0);
                bits = 0;
            }
            while (len > 0) {
                int v = is.read();
                if (v == -1) {
                    eof = true;
                    if (nextin != 18) {
                        if (nextin == 12)
                            throw new IOException("Base64 stream has one un-decoded dangling byte.");
                        // treat ending xx/xxx without padding character legal.
                        // same logic as v == 'v' below
                        b[off++] = (byte)(bits >> (16));
                        len--;
                        if (nextin == 0) {           // only one padding byte
                            if (len == 0) {          // no enough output space
                                bits >>= 8;          // shift to lowest byte
                                nextout = 0;
                            } else {
                                b[off++] = (byte) (bits >>  8);
                            }
                        }
                    }
                    if (off == oldOff)
                        return -1;
                    else
                        return off - oldOff;
                }
                if (v == '=') {                  // padding byte(s)
                    if (nextin != 6 && nextin != 0) {
                        throw new IOException("Illegal base64 ending sequence:" + nextin);
                    }
                    b[off++] = (byte)(bits >> (16));
                    len--;
                    if (nextin == 0) {           // only one padding byte
                        if (len == 0) {          // no enough output space
                            bits >>= 8;          // shift to lowest byte
                            nextout = 0;
                        } else {
                            b[off++] = (byte) (bits >>  8);
                        }
                    }
                    eof = true;
                    break;
                }
                if ((v = base64[v]) == -1) {
                    if (isMIME)                 // skip if for rfc2045
                        continue;
                    else
                        throw new IOException("Illegal base64 character " +
                            Integer.toString(v, 16));
                }
                bits |= (v << nextin);
                if (nextin == 0) {
                    nextin = 18;    // clear for next
                    nextout = 16;
                    while (nextout >= 0) {
                        b[off++] = (byte)(bits >> nextout);
                        len--;
                        nextout -= 8;
                        if (len == 0 && nextout >= 0) {  // don't clean "bits"
                            return off - oldOff;
                        }
                    }
                    bits = 0;
                } else {
                    nextin -= 6;
                }
            }
            return off - oldOff;
        }

        @Override
        public int available() throws IOException {
            if (closed)
                throw new IOException("Stream is closed");
            return is.available();   // TBD:
        }

        @Override
        public void close() throws IOException {
            if (!closed) {
                closed = true;
                is.close();
            }
        }
    }
}
