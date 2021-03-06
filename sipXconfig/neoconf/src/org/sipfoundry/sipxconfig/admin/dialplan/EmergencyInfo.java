/*
 *
 *
 * Copyright (C) 2008 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 * $
 */
package org.sipfoundry.sipxconfig.admin.dialplan;

public class EmergencyInfo {
    private String m_number;
    private String m_address;
    private Integer m_port;
    
    protected EmergencyInfo(String address, Integer port, String number) {
        m_address = address;
        m_number = number;
        m_port = port;
    }
    
    public String getNumber() {
        return m_number;
    }
    public String getAddress() {
        return m_address;
    }
    public Integer getPort() {
        return m_port;
    }
}
