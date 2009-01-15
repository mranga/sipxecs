/*
 *
 *
 * Copyright (C) 2008 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 * $
 */
package org.sipfoundry.sipxconfig.site.service;

import junit.framework.Test;
import net.sourceforge.jwebunit.junit.WebTestCase;

import org.sipfoundry.sipxconfig.site.SiteTestHelper;

public class EditIvrServiceTestUi extends WebTestCase {

    public static Test suite() throws Exception {
        return SiteTestHelper.webTestSuite(EditProxyServiceTestUi.class);
    }

    public void testDisplay() {
        getTestContext().setBaseUrl(SiteTestHelper.getBaseUrl());
        SiteTestHelper.home(tester);
        clickLink("editIvrService");
        SiteTestHelper.assertNoException(tester);
        assertTextPresent("log.level");
    }
}
