// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.multiwindow;

import static org.chromium.chrome.browser.multiwindow.MultiWindowUtilsTest.createSecondChromeTabbedActivity;
import static org.chromium.chrome.browser.multiwindow.MultiWindowUtilsTest.moveActivityToFront;
import static org.chromium.chrome.browser.multiwindow.MultiWindowUtilsTest.waitForSecondChromeTabbedActivity;

import android.annotation.TargetApi;
import android.os.Build;
import android.support.test.InstrumentationRegistry;
import android.support.test.filters.MediumTest;

import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Feature;
import org.chromium.base.test.util.MinAndroidSdkLevel;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.ChromeTabbedActivity;
import org.chromium.chrome.browser.ChromeTabbedActivity2;
import org.chromium.chrome.browser.firstrun.FirstRunStatus;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabWindowManager;
import org.chromium.chrome.test.ChromeActivityTestRule;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.ChromeTabbedActivityTestRule;
import org.chromium.chrome.test.util.MenuUtils;
import org.chromium.content.browser.test.util.Criteria;
import org.chromium.content.browser.test.util.CriteriaHelper;
import org.chromium.net.test.EmbeddedTestServer;

import java.util.Locale;
import java.util.concurrent.Callable;

/**
 * Integration testing for Android's N+ MultiWindow.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({
        ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE,
        ChromeActivityTestRule.DISABLE_NETWORK_PREDICTION_FLAG,
})
@MinAndroidSdkLevel(Build.VERSION_CODES.N)
public class MultiWindowIntegrationTest {
    @Rule
    public ChromeTabbedActivityTestRule mActivityTestRule = new ChromeTabbedActivityTestRule();

    private EmbeddedTestServer mTestServer;

    @Before
    public void setUp() throws InterruptedException {
        mTestServer = EmbeddedTestServer.createAndStartServer(
                InstrumentationRegistry.getInstrumentation().getContext());
        mActivityTestRule.startMainActivityOnBlankPage();
    }

    @After
    public void tearDown() throws Exception {
        mTestServer.stopAndDestroyServer();
    }

    @Test
    @MediumTest
    @Feature("MultiWindow")
    @TargetApi(Build.VERSION_CODES.N)
    @CommandLineFlags.Add(ChromeSwitches.DISABLE_TAB_MERGING_FOR_TESTING)
    public void testIncognitoNtpHandledCorrectly() throws InterruptedException {
        try {
            ThreadUtils.runOnUiThreadBlocking(new Runnable() {
                @Override
                public void run() {
                    FirstRunStatus.setFirstRunFlowComplete(true);
                }
            });

            mActivityTestRule.newIncognitoTabFromMenu();
            Assert.assertTrue(mActivityTestRule.getActivity().getActivityTab().isIncognito());
            final int incognitoTabId = mActivityTestRule.getActivity().getActivityTab().getId();
            final ChromeTabbedActivity2 cta2 =
                    createSecondChromeTabbedActivity(mActivityTestRule.getActivity());

            moveActivityToFront(mActivityTestRule.getActivity());

            MenuUtils.invokeCustomMenuActionSync(InstrumentationRegistry.getInstrumentation(),
                    mActivityTestRule.getActivity(), R.id.move_to_other_window_menu_id);

            CriteriaHelper.pollUiThread(Criteria.equals(1,
                    new Callable<Integer>() {
                        @Override
                        public Integer call() throws Exception {
                            return cta2.getTabModelSelector().getModel(true).getCount();
                        }
                    }));

            ThreadUtils.runOnUiThreadBlocking(new Runnable() {
                @Override
                public void run() {
                    Assert.assertEquals(1, TabWindowManager.getInstance().getIncognitoTabCount());

                    // Ensure the same tab exists in the new activity.
                    Assert.assertEquals(incognitoTabId, cta2.getActivityTab().getId());
                }
            });
        } finally {
            ThreadUtils.runOnUiThreadBlocking(new Runnable() {
                @Override
                public void run() {
                    FirstRunStatus.setFirstRunFlowComplete(false);
                }
            });
        }
    }

    @Test
    @MediumTest
    @Feature("MultiWindow")
    @TargetApi(Build.VERSION_CODES.N)
    @CommandLineFlags.Add({ChromeSwitches.DISABLE_TAB_MERGING_FOR_TESTING,
            ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
    public void testMoveTabTwice() throws InterruptedException {
        // Load 'google' in separate tab.
        int googleTabId = mActivityTestRule
                                  .loadUrlInNewTab(mTestServer.getURL(
                                          "/chrome/test/data/android/google.html"))
                                  .getId();

        final ChromeTabbedActivity cta = mActivityTestRule.getActivity();

        // Move 'google' tab to cta2.
        MenuUtils.invokeCustomMenuActionSync(InstrumentationRegistry.getInstrumentation(), cta,
                R.id.move_to_other_window_menu_id);

        final ChromeTabbedActivity2 cta2 = waitForSecondChromeTabbedActivity();

        // At this point cta should have NTP tab, and cta2 should have 'google' tab.
        waitForTabs("CTA", cta, 1, Tab.INVALID_TAB_ID);
        waitForTabs("CTA2", cta2, 1, googleTabId);

        // Move 'google' tab back to cta.
        moveActivityToFront(cta2);
        MenuUtils.invokeCustomMenuActionSync(InstrumentationRegistry.getInstrumentation(), cta2,
                R.id.move_to_other_window_menu_id);

        // At this point cta2 should have zero tabs, and cta should have 2 tabs (NTP, 'google').
        waitForTabs("CTA2", cta2, 0, Tab.INVALID_TAB_ID);
        waitForTabs("CTA", cta, 2, googleTabId);
    }

    /**
     * Waits until 'activity' has 'expectedTotalTabCount' total tabs, and its active tab has
     * 'expectedActiveTabId' ID. 'expectedActiveTabId' is optional and can be Tab.INVALID_TAB_ID.
     * 'tag' is an arbitrary string that is prepended to failure reasons.
     */
    private static void waitForTabs(final String tag, final ChromeTabbedActivity activity,
            final int expectedTotalTabCount, final int expectedActiveTabId) {
        CriteriaHelper.pollUiThread(new Criteria() {
            @Override
            public boolean isSatisfied() {
                int actualTotalTabCount = activity.getTabModelSelector().getTotalTabCount();
                if (expectedTotalTabCount != actualTotalTabCount) {
                    updateFailureReason(String.format((Locale) null,
                            "%s: total tab count is wrong: %d (expected %d)", tag,
                            actualTotalTabCount, expectedTotalTabCount));
                    return false;
                }
                return true;
            }
        });

        if (expectedActiveTabId == Tab.INVALID_TAB_ID) return;

        CriteriaHelper.pollUiThread(new Criteria() {
            @Override
            public boolean isSatisfied() {
                int actualActiveTabId = activity.getActivityTab().getId();
                if (expectedActiveTabId != actualActiveTabId) {
                    updateFailureReason(String.format((Locale) null,
                            "%s: active tab ID is wrong: %d (expected %d)", tag, actualActiveTabId,
                            expectedActiveTabId));
                    return false;
                }
                return true;
            }
        });
    }
}
