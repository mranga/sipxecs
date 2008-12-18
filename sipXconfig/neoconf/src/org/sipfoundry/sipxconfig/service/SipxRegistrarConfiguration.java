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

import org.apache.velocity.VelocityContext;
import org.sipfoundry.sipxconfig.admin.commserver.Location;

public class SipxRegistrarConfiguration extends SipxServiceConfiguration {
    @Override
    protected VelocityContext setupContext(Location location) {
        VelocityContext context = super.setupContext(location);
        SipxService registrarService = getService(SipxRegistrarService.BEAN_ID);
        SipxService proxyService = getService(SipxProxyService.BEAN_ID);
        context.put("settings", registrarService.getSettings());
        context.put("registrarService", registrarService);
        context.put("proxyService", proxyService);

        return context;
    }

}
