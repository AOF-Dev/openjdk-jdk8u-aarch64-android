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
 * @test
 * @bug     8002070
 * @summary Remove the stack search for a resource bundle Logger to use
 * @author  Jim Gish
 * @build  ResourceBundleSearchTest IndirectlyLoadABundle LoadItUp
 * @run main ResourceBundleSearchTest
 */
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.MissingResourceException;
import java.util.ResourceBundle;
import java.util.logging.Logger;

public class ResourceBundleSearchTest {

    private final static boolean DEBUG = false;
    private final static String LOGGER_PREFIX = "myLogger.";
    private static int loggerNum = 0;
    private final static String PROP_RB_NAME = "ClassPathTestBundle";
    private final static String TCCL_TEST_BUNDLE = "ContextClassLoaderTestBundle";

    private static int numPass = 0;
    private static int numFail = 0;
    private static List<String> msgs = new ArrayList<>();

    public static void main(String[] args) throws Throwable {
        ResourceBundleSearchTest test = new ResourceBundleSearchTest();
        test.runTests();
    }

    private void runTests() throws Throwable {
        // ensure we are using en as the default Locale so we can find the resource
        Locale.setDefault(Locale.ENGLISH);

        String testClasses = System.getProperty("test.classes");
        System.out.println( "test.classes = " + testClasses );

        ClassLoader myClassLoader = ClassLoader.getSystemClassLoader();

        // Find out where we are running from so we can setup the URLClassLoader URL
        String userDir = System.getProperty("user.dir");
        String testDir = System.getProperty("test.src", userDir);
        String sep = System.getProperty("file.separator");

        URL[] urls = new URL[1];

        urls[0] = Paths.get(testDir, "resources").toUri().toURL();
        URLClassLoader rbClassLoader = new URLClassLoader(urls);

        // Test 1 - can we find a Logger bundle from doing a stack search?
        // We shouldn't be able to
        assertFalse(testGetBundleFromStackSearch(), "testGetBundleFromStackSearch");

        // Test 2 - can we find a Logger bundle off of the Thread context class
        // loader? We should be able to.
        assertTrue(
                testGetBundleFromTCCL(TCCL_TEST_BUNDLE, rbClassLoader),
                "testGetBundleFromTCCL");

        // Test 3 - Can we find a Logger bundle from the classpath?  We should be
        // able to, but ....
        // We check to see if the bundle is on the classpath or not so that this
        // will work standalone.  In the case of jtreg/samevm,
        // the resource bundles are not on the classpath.  Running standalone
        // (or othervm), they are
        if (isOnClassPath(PROP_RB_NAME, myClassLoader)) {
            debug("We should be able to see " + PROP_RB_NAME + " on the classpath");
            assertTrue(testGetBundleFromSystemClassLoader(PROP_RB_NAME),
                    "testGetBundleFromSystemClassLoader");
        } else {
            debug("We should not be able to see " + PROP_RB_NAME + " on the classpath");
            assertFalse(testGetBundleFromSystemClassLoader(PROP_RB_NAME),
                    "testGetBundleFromSystemClassLoader");
        }

        report();
    }

    private void report() throws Exception {
        System.out.println("Num passed = " + numPass + " Num failed = " + numFail);
        if (numFail > 0) {
            // We only care about the messages if they were errors
            for (String msg : msgs) {
                System.out.println(msg);
            }
            throw new Exception(numFail + " out of " + (numPass + numFail)
                    + " tests failed.");
        }
    }

    public void assertTrue(boolean testResult, String testName) {
        if (testResult) {
            numPass++;
        } else {
            numFail++;
            System.out.println("FAILED: " + testName
                    + " was supposed to return true but did NOT!");
        }
    }

    public void assertFalse(boolean testResult, String testName) {
        if (!testResult) {
            numPass++;
        } else {
            numFail++;
            System.out.println("FAILED: " + testName
                    + " was supposed to return false but did NOT!");
        }
    }

    public boolean testGetBundleFromStackSearch() throws Throwable {
        // This should fail.  This was the old functionality to search up the
        // caller's call stack
        IndirectlyLoadABundle indirectLoader = new IndirectlyLoadABundle();
        return indirectLoader.loadAndTest();
    }

    public boolean testGetBundleFromTCCL(String bundleName,
            ClassLoader setOnTCCL) throws InterruptedException {
        // This should succeed.  We should be able to get the bundle from the
        // thread context class loader
        debug("Looking for " + bundleName + " using TCCL");
        LoggingThread lr = new LoggingThread(bundleName, setOnTCCL);
        lr.start();
        synchronized (lr) {
            try {
                lr.wait();
            } catch (InterruptedException ex) {
                throw ex;
            }
        }
        msgs.add(lr.msg);
        return lr.foundBundle;
    }

    /*
     * @param String bundleClass
     * @param ClassLoader to use for search
     * @return true iff bundleClass is on system classpath
     */
    public static boolean isOnClassPath(String baseName, ClassLoader cl) {
        ResourceBundle rb = null;
        try {
            rb = ResourceBundle.getBundle(baseName, Locale.getDefault(), cl);
            System.out.println("INFO: Found bundle " + baseName + " on " + cl);
        } catch (MissingResourceException e) {
            System.out.println("INFO: Could not find bundle " + baseName + " on " + cl);
            return false;
        }
        return (rb != null);
    }

    private static String newLoggerName() {
        // we need a new logger name every time we attempt to find a bundle via
        // the Logger.getLogger call, so we'll simply tack on an integer which
        // we increment each time this is called
        loggerNum++;
        return LOGGER_PREFIX + loggerNum;
    }

    public boolean testGetBundleFromSystemClassLoader(String bundleName) {
        // this should succeed if the bundle is on the system classpath.
        try {
            Logger aLogger = Logger.getLogger(ResourceBundleSearchTest.newLoggerName(),
                    bundleName);
        } catch (MissingResourceException re) {
            msgs.add("INFO: testGetBundleFromSystemClassLoader() did not find bundle "
                    + bundleName);
            return false;
        }
        msgs.add("INFO: testGetBundleFromSystemClassLoader() found the bundle "
                + bundleName);
        return true;
    }

    public static class LoggingThread extends Thread {

        boolean foundBundle = false;
        String msg = null;
        ClassLoader clToSetOnTCCL = null;
        String bundleName = null;

        public LoggingThread(String bundleName) {
            this.bundleName = bundleName;
        }

        public LoggingThread(String bundleName, ClassLoader setOnTCCL) {
            this.clToSetOnTCCL = setOnTCCL;
            this.bundleName = bundleName;
        }

        public void run() {
            boolean setTCCL = false;
            try {
                if (clToSetOnTCCL != null) {
                    Thread.currentThread().setContextClassLoader(clToSetOnTCCL);
                    setTCCL = true;
                }
                // this should succeed if the bundle is on the system classpath.
                try {
                    Logger aLogger = Logger.getLogger(ResourceBundleSearchTest.newLoggerName(),
                            bundleName);
                    msg = "INFO: LoggingRunnable() found the bundle " + bundleName
                            + (setTCCL ? " with " : " without ") + "setting the TCCL";
                    foundBundle = true;
                } catch (MissingResourceException re) {
                    msg = "INFO: LoggingRunnable() did not find the bundle " + bundleName
                            + (setTCCL ? " with " : " without ") + "setting the TCCL";
                    foundBundle = false;
                }
            } catch (Throwable e) {
                e.printStackTrace();
                System.exit(1);
            }
        }
    }

    private void debug(String msg) {
        if (DEBUG) {
            System.out.println(msg);
        }
    }
}
