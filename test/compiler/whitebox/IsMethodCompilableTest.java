/*
 * Copyright (c) 2013, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
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
 * @test IsMethodCompilableTest
 * @bug 8007270
 * @library /testlibrary /testlibrary/whitebox
 * @build IsMethodCompilableTest
 * @run main ClassFileInstaller sun.hotspot.WhiteBox
 * @run main/othervm/timeout=600 -Xbootclasspath/a:. -XX:+UnlockDiagnosticVMOptions -XX:+WhiteBoxAPI IsMethodCompilableTest
 * @author igor.ignatyev@oracle.com
 */
public class IsMethodCompilableTest extends CompilerWhiteBoxTest {
    protected static final long PER_METHOD_RECOMPILATION_CUTOFF;

    static {
        long tmp = Long.parseLong(
                getVMOption("PerMethodRecompilationCutoff", "400"));
        if (tmp == -1) {
            PER_METHOD_RECOMPILATION_CUTOFF = -1 /* Inf */;
        } else {
            PER_METHOD_RECOMPILATION_CUTOFF = 1 + (0xFFFFFFFFL & tmp);
        }
    }

    public static void main(String[] args) throws Exception {
        new IsMethodCompilableTest().runTest();
    }

    protected void test() throws Exception {
        if (!WHITE_BOX.isMethodCompilable(METHOD)) {
            throw new RuntimeException(METHOD + " must be compilable");
        }
        System.out.println("PerMethodRecompilationCutoff = "
                + PER_METHOD_RECOMPILATION_CUTOFF);
        if (PER_METHOD_RECOMPILATION_CUTOFF == -1) {
            System.err.println(
                    "Warning: test is not applicable if PerMethodRecompilationCutoff == Inf");
            return;
        }
        // to prevent inlining #method into #compile()
        WHITE_BOX.setDontInlineMethod(METHOD, true);
        boolean madeNotCompilable = false;

        for (long i = 0; i < PER_METHOD_RECOMPILATION_CUTOFF; ++i) {
            compile();
            waitBackgroundCompilation(METHOD);
            WHITE_BOX.deoptimizeMethod(METHOD);
            if (!WHITE_BOX.isMethodCompilable(METHOD)) {
                madeNotCompilable = true;
                break;
            }
        }
        if (!madeNotCompilable) {
            throw new RuntimeException(METHOD + " is still compilable after "
                    + PER_METHOD_RECOMPILATION_CUTOFF + " iterations");
        }
        compile();
        if (WHITE_BOX.isMethodCompiled(METHOD)) {
            printInfo(METHOD);
            throw new RuntimeException(
                    METHOD + " is not compilable but compiled");
        }
    }
}
