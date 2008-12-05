/*
 *
 *
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 * $
 */
package org.sipfoundry.sipxconfig.site.admin;

import junit.framework.Test;
import net.sourceforge.jwebunit.junit.WebTestCase;

import org.sipfoundry.sipxconfig.site.SiteTestHelper;

public class StatisticsPageTestUi extends WebTestCase {
    public static Test suite() throws Exception {
        return SiteTestHelper.webTestSuite(StatisticsPageTestUi.class);
    }

    public void setUp() throws Exception {
        getTestContext().setBaseUrl(SiteTestHelper.getBaseUrl());
        SiteTestHelper.home(getTester());
        clickLink("seedLocationsManager");
        clickLink("toggleNavigation");
        clickLink("menu.statistics");
    }

    public void testStatisticsPage() throws Exception {
        // add targets to monitor for host.example.org
        assertLinkPresent("link.configureHosts");
        clickLink("link.configureHosts");
        assertElementPresent("hosts:list");
        assertLinkPresentWithText("host.example.org");
        clickLink("editRowLink");
        setWorkingForm("targets");
        
        // select targets
        SiteTestHelper.selectRow(tester, 1, true);
        SiteTestHelper.selectRow(tester, 2, true);
        SiteTestHelper.selectRow(tester, 3, true);
        SiteTestHelper.selectRow(tester, 4, true);
        SiteTestHelper.selectRow(tester, 5, true);
        clickButton("form:ok");

        // Check to make sure we get a user error for host not found.
        SiteTestHelper.assertUserError(tester);

        clickLink("menu.statistics");
        SiteTestHelper.selectOption(tester, "PropertySelection", "host.example.org");
        assertLinkPresent("link.configureTargets");
        assertLinkPresent("link.configureHosts");
        assertLinkPresent("report0");// summary report
        assertLinkPresent("report1");
        assertLinkPresent("report2");
        assertLinkPresent("report3");
        assertLinkPresent("report4");
        assertLinkPresent("report5");

        assertLinkPresent("image0");
        assertLinkPresent("image1");
        assertLinkPresent("image2");
        assertLinkPresent("image3");
        assertLinkPresent("image4");

        // test empty community string
        clickLink("link.configureTargets");

        // remove first and second targets to monitor
        SiteTestHelper.selectRow(tester, 1, false);
        SiteTestHelper.selectRow(tester, 2, false);
        clickButton("form:ok");
        SiteTestHelper.assertUserError(tester);

        clickLink("menu.statistics");
        SiteTestHelper.selectOption(tester, "PropertySelection", "host.example.org");
        assertLinkPresent("report0");// summary report
        assertLinkPresent("report1");
        assertLinkPresent("report2");
        assertLinkPresent("report3");

        assertLinkPresent("image0");
        assertLinkPresent("image1");
        assertLinkPresent("image2");

        // remove all targets to monitor
        clickLink("menu.statistics");
        SiteTestHelper.selectOption(tester, "PropertySelection", "host.example.org");
        clickLink("link.configureTargets");
        SiteTestHelper.selectRow(tester, 3, false);
        SiteTestHelper.selectRow(tester, 4, false);
        SiteTestHelper.selectRow(tester, 5, false);
        clickButton("form:ok");

        SiteTestHelper.assertUserError(tester);
    }
}
