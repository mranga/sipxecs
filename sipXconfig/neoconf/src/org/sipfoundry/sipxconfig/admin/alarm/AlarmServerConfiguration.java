/*
 *
 *
 * Copyright (C) 2008 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 * $
 */
package org.sipfoundry.sipxconfig.admin.alarm;

import org.apache.velocity.VelocityContext;
import org.sipfoundry.sipxconfig.admin.TemplateConfigurationFile;
import org.sipfoundry.sipxconfig.admin.commserver.Location;

import static org.apache.commons.lang.StringUtils.defaultIfEmpty;

public class AlarmServerConfiguration extends TemplateConfigurationFile {
    private boolean m_emailNotificationEnabled;

    private AlarmServerContacts m_contacts;

    private String m_fromEmailAddress;

    private String m_logDirectory;

    private String m_hostName;

    public void generate(AlarmServer alarmServer, String logDirectory, String hostName) {
        m_emailNotificationEnabled = alarmServer.isEmailNotificationEnabled();
        m_fromEmailAddress = defaultIfEmpty(alarmServer.getFromEmailAddress(), "postmaster@" + hostName);
        m_contacts = alarmServer.getContacts();
        m_logDirectory = logDirectory;
        m_hostName = hostName;
    }

    @Override
    protected VelocityContext setupContext(Location location) {
        VelocityContext context = super.setupContext(location);
        context.put("email", m_emailNotificationEnabled);
        context.put("fromEmailAddress", m_fromEmailAddress);
        context.put("contacts", m_contacts);
        context.put("logDirectory", m_logDirectory);
        context.put("hostName", m_hostName);

        return context;
    }
}
