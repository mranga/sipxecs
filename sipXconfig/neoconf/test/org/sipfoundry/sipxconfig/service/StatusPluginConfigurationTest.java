/*
 *
 *
 * Copyright (C) 2008 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 * $
 */
package org.sipfoundry.sipxconfig.service;

import static org.easymock.EasyMock.replay;
import static org.easymock.EasyMock.verify;

import org.sipfoundry.sipxconfig.test.TestUtil;

public class StatusPluginConfigurationTest extends SipxServiceTestBase {

    public void testWrite() throws Exception {
        SipxStatusService statusService = new SipxStatusService();
        initCommonAttributes(statusService);
        statusService.setBeanId(SipxStatusService.BEAN_ID);

        SipxMediaService mediaService = new SipxMediaService();
        mediaService.setVoicemailHttpsPort(9905);
        mediaService.setBeanId(SipxMediaService.BEAN_ID);

        SipxServiceManager sipxServiceManager = TestUtil.getMockSipxServiceManager(statusService,
                mediaService);
        replay(sipxServiceManager);

        StatusPluginConfiguration out = new StatusPluginConfiguration();
        out.setSipxServiceManager(sipxServiceManager);

        out.setTemplate("sipxstatus/status-plugin.vm");

        assertCorrectFileGeneration(out, "expected-status-plugin-config");
        verify(sipxServiceManager);
    }
}
