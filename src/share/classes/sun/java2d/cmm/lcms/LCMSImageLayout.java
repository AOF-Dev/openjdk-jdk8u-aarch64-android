/*
 * Copyright (c) 2007, Oracle and/or its affiliates. All rights reserved.
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
package sun.java2d.cmm.lcms;

import java.awt.image.BufferedImage;
import java.awt.image.ComponentColorModel;
import java.awt.image.ComponentSampleModel;
import java.awt.image.DataBuffer;
import java.awt.image.ColorModel;
import java.awt.image.Raster;
import java.awt.image.SampleModel;
import sun.awt.image.ByteComponentRaster;
import sun.awt.image.ShortComponentRaster;
import sun.awt.image.IntegerComponentRaster;

class LCMSImageLayout {

    public static int BYTES_SH(int x) {
        return x;
    }

    public static int EXTRA_SH(int x) {
        return x << 7;
    }

    public static int CHANNELS_SH(int x) {
        return x << 3;
    }
    public static final int SWAPFIRST = 1 << 14;
    public static final int DOSWAP = 1 << 10;
    public static final int PT_RGB_8 =
            CHANNELS_SH(3) | BYTES_SH(1);
    public static final int PT_GRAY_8 =
            CHANNELS_SH(1) | BYTES_SH(1);
    public static final int PT_GRAY_16 =
            CHANNELS_SH(1) | BYTES_SH(2);
    public static final int PT_RGBA_8 =
            EXTRA_SH(1) | CHANNELS_SH(3) | BYTES_SH(1);
    public static final int PT_ARGB_8 =
            EXTRA_SH(1) | CHANNELS_SH(3) | BYTES_SH(1) | SWAPFIRST;
    public static final int PT_BGR_8 =
            DOSWAP | CHANNELS_SH(3) | BYTES_SH(1);
    public static final int PT_ABGR_8 =
            DOSWAP | EXTRA_SH(1) | CHANNELS_SH(3) | BYTES_SH(1);
    public static final int PT_BGRA_8 = EXTRA_SH(1) | CHANNELS_SH(3)
            | BYTES_SH(1) | DOSWAP | SWAPFIRST;
    public static final int DT_BYTE = 0;
    public static final int DT_SHORT = 1;
    public static final int DT_INT = 2;
    public static final int DT_DOUBLE = 3;
    boolean isIntPacked = false;
    int pixelType;
    int dataType;
    int width;
    int height;
    int nextRowOffset;
    int offset;

    /* This flag indicates whether the image can be processed
     * at once by doTransfrom() native call. Otherwise, the
     * image is processed scan by scan.
     */
    private boolean imageAtOnce = false;
    Object dataArray;

    private LCMSImageLayout(int np, int pixelType, int pixelSize) {
        this.pixelType = pixelType;
        width = np;
        height = 1;
        nextRowOffset = np * pixelSize;
        offset = 0;
    }

    private LCMSImageLayout(int width, int height, int pixelType,
            int pixelSize) {
        this.pixelType = pixelType;
        this.width = width;
        this.height = height;
        nextRowOffset = width * pixelSize;
        offset = 0;
    }

    public LCMSImageLayout(byte[] data, int np, int pixelType, int pixelSize) {
        this(np, pixelType, pixelSize);
        dataType = DT_BYTE;
        dataArray = data;
    }

    public LCMSImageLayout(short[] data, int np, int pixelType, int pixelSize) {
        this(np, pixelType, pixelSize);
        dataType = DT_SHORT;
        dataArray = data;
    }

    public LCMSImageLayout(int[] data, int np, int pixelType, int pixelSize) {
        this(np, pixelType, pixelSize);
        dataType = DT_INT;
        dataArray = data;
    }

    public LCMSImageLayout(double[] data, int np, int pixelType, int pixelSize) {
        this(np, pixelType, pixelSize);
        dataType = DT_DOUBLE;
        dataArray = data;
    }

    private LCMSImageLayout() {
    }

    /* This method creates a layout object for given image.
     * Returns null if the image is not supported by current implementation.
     */
    public static LCMSImageLayout createImageLayout(BufferedImage image) {
        LCMSImageLayout l = new LCMSImageLayout();

        switch (image.getType()) {
            case BufferedImage.TYPE_INT_RGB:
                l.pixelType = PT_ARGB_8;
                l.isIntPacked = true;
                break;
            case BufferedImage.TYPE_INT_ARGB:
                l.pixelType = PT_ARGB_8;
                l.isIntPacked = true;
                break;
            case BufferedImage.TYPE_INT_BGR:
                l.pixelType = PT_ABGR_8;
                l.isIntPacked = true;
                break;
            case BufferedImage.TYPE_3BYTE_BGR:
                l.pixelType = PT_BGR_8;
                break;
            case BufferedImage.TYPE_4BYTE_ABGR:
                l.pixelType = PT_ABGR_8;
                break;
            case BufferedImage.TYPE_BYTE_GRAY:
                l.pixelType = PT_GRAY_8;
                break;
            case BufferedImage.TYPE_USHORT_GRAY:
                l.pixelType = PT_GRAY_16;
                break;
            default:
                /* ColorConvertOp creates component images as
                 * default destination, so this kind of images
                 * has to be supported.
                 */
                ColorModel cm = image.getColorModel();
                if (cm instanceof ComponentColorModel) {
                    ComponentColorModel ccm = (ComponentColorModel) cm;

                    // verify whether the component size is fine
                    int[] cs = ccm.getComponentSize();
                    for (int s : cs) {
                        if (s != 8) {
                            return null;
                        }
                    }

                    return createImageLayout(image.getRaster());

                }
                return null;
        }

        l.width = image.getWidth();
        l.height = image.getHeight();

        switch (image.getType()) {
            case BufferedImage.TYPE_INT_RGB:
            case BufferedImage.TYPE_INT_ARGB:
            case BufferedImage.TYPE_INT_BGR:
                do {
                    IntegerComponentRaster intRaster = (IntegerComponentRaster)
                            image.getRaster();
                    l.nextRowOffset = intRaster.getScanlineStride() * 4;
                    l.offset = intRaster.getDataOffset(0) * 4;
                    l.dataArray = intRaster.getDataStorage();
                    l.dataType = DT_INT;

                    if (l.nextRowOffset == l.width * 4 * intRaster.getPixelStride()) {
                        l.imageAtOnce = true;
                    }
                } while (false);
                break;

            case BufferedImage.TYPE_3BYTE_BGR:
            case BufferedImage.TYPE_4BYTE_ABGR:
                do {
                    ByteComponentRaster byteRaster = (ByteComponentRaster)
                            image.getRaster();
                    l.nextRowOffset = byteRaster.getScanlineStride();
                    int firstBand = image.getSampleModel().getNumBands() - 1;
                    l.offset = byteRaster.getDataOffset(firstBand);
                    l.dataArray = byteRaster.getDataStorage();
                    l.dataType = DT_BYTE;
                    if (l.nextRowOffset == l.width * byteRaster.getPixelStride()) {
                        l.imageAtOnce = true;
                    }
                } while (false);
                break;

            case BufferedImage.TYPE_BYTE_GRAY:
                do {
                    ByteComponentRaster byteRaster = (ByteComponentRaster)
                            image.getRaster();
                    l.nextRowOffset = byteRaster.getScanlineStride();
                    l.offset = byteRaster.getDataOffset(0);
                    l.dataArray = byteRaster.getDataStorage();
                    l.dataType = DT_BYTE;

                    if (l.nextRowOffset == l.width * byteRaster.getPixelStride()) {
                        l.imageAtOnce = true;
                    }
                } while (false);
                break;

            case BufferedImage.TYPE_USHORT_GRAY:
                do {
                    ShortComponentRaster shortRaster = (ShortComponentRaster)
                            image.getRaster();
                    l.nextRowOffset = shortRaster.getScanlineStride() * 2;
                    l.offset = shortRaster.getDataOffset(0) * 2;
                    l.dataArray = shortRaster.getDataStorage();
                    l.dataType = DT_SHORT;

                    if (l.nextRowOffset == l.width * 2 * shortRaster.getPixelStride()) {
                        l.imageAtOnce = true;
                    }
                } while (false);
                break;
            default:
                return null;
        }
        return l;
    }

    private static enum BandOrder {
        DIRECT,
        INVERTED,
        ARBITRARY,
        UNKNOWN;

        public static BandOrder getBandOrder(int[] bandOffsets) {
            BandOrder order = UNKNOWN;

            int numBands = bandOffsets.length;

            for (int i = 0; (order != ARBITRARY) && (i < bandOffsets.length); i++) {
                switch (order) {
                    case UNKNOWN:
                        if (bandOffsets[i] == i) {
                            order = DIRECT;
                        } else if (bandOffsets[i] == (numBands - 1 - i)) {
                            order = INVERTED;
                        } else {
                            order = ARBITRARY;
                        }
                        break;
                    case DIRECT:
                        if (bandOffsets[i] != i) {
                            order = ARBITRARY;
                        }
                        break;
                    case INVERTED:
                        if (bandOffsets[i] != (numBands - 1 - i)) {
                            order = ARBITRARY;
                        }
                        break;
                }
            }
            return order;
        }
    }

    public static LCMSImageLayout createImageLayout(Raster r) {
        LCMSImageLayout l = new LCMSImageLayout();
        if (r instanceof ByteComponentRaster) {
            ByteComponentRaster br = (ByteComponentRaster)r;

            ComponentSampleModel csm = (ComponentSampleModel)r.getSampleModel();

            l.pixelType = CHANNELS_SH(br.getNumBands()) | BYTES_SH(1);

            int[] bandOffsets = csm.getBandOffsets();
            BandOrder order = BandOrder.getBandOrder(bandOffsets);

            int firstBand = 0;
            switch (order) {
                case INVERTED:
                    l.pixelType |= DOSWAP;
                    firstBand  = csm.getNumBands() - 1;
                    break;
                case DIRECT:
                    // do nothing
                    break;
                default:
                    // unable to create the image layout;
                    return null;
            }

            l.nextRowOffset = br.getScanlineStride();
            l.offset = br.getDataOffset(firstBand);
            l.dataArray = br.getDataStorage();
            l.dataType = DT_BYTE;

            l.width = br.getWidth();
            l.height = br.getHeight();

            if (l.nextRowOffset == l.width * br.getPixelStride()) {
                l.imageAtOnce = true;
            }
            return l;
        }
        return null;
    }
}
