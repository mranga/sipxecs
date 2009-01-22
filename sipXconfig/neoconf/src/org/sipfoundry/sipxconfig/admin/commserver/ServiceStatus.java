/*
 *
 *
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 * $
 */
package org.sipfoundry.sipxconfig.admin.commserver;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import org.sipfoundry.sipxconfig.common.PrimaryKeySource;

public class ServiceStatus implements PrimaryKeySource {

    private static final List<Status> ERROR_STATES = Arrays.asList(Status.ConfigurationMismatch, 
            Status.ResourceRequired, Status.ConfigurationTestFailed, Status.Failed);
    
    private final String m_serviceBeanId;
    private final Status m_status;
    private final List<String> m_messages = new ArrayList<String>();

    public static enum Status {
        Undefined,
        Running,
        Disabled,
        ShutDown,
        ConfigurationMismatch,
        ResourceRequired,
        ConfigurationTestFailed,
        Testing,
        Starting,
        Stopping,
        ShuttingDown,
        Failed
    }

    public ServiceStatus(String serviceBeanId, Status status) {
        m_serviceBeanId = serviceBeanId;
        m_status = status;
    }    
    
    public boolean getShowDetails() {
        return ERROR_STATES.contains(m_status);
    }
    
    public void addMessages(List<String> messages) {
        m_messages.addAll(messages);
    }
    
    public List<String> getMessages() {
        return m_messages;
    }
    
    public String getServiceBeanId() {
        return m_serviceBeanId;
    }

    public Status getStatus() {
        return m_status;
    }

    public Object getPrimaryKey() {
        return m_serviceBeanId;
    }
}
