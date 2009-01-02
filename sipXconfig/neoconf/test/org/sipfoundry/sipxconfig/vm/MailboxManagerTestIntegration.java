/*
 * 
 * 
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.  
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 * 
 * $
 */
package org.sipfoundry.sipxconfig.vm;

import org.sipfoundry.sipxconfig.IntegrationTestCase;
import org.sipfoundry.sipxconfig.common.CoreContext;
import org.sipfoundry.sipxconfig.common.User;
import org.sipfoundry.sipxconfig.vm.attendant.PersonalAttendant;

public class MailboxManagerTestIntegration extends IntegrationTestCase {
    private MailboxManager m_mailboxManager;

    private CoreContext m_coreContext;

    public void testLoadPersonalAttendantPerUser() throws Exception {
        loadDataSetXml("admin/dialplan/sbc/domain.xml");
        
        assertEquals(0, countRowsInTable("personal_attendant"));

        User newUser = m_coreContext.newUser();
        m_coreContext.saveUser(newUser);

        PersonalAttendant pa = m_mailboxManager.loadPersonalAttendantForUser(newUser);
        assertNotNull(pa);
        flush();
        assertEquals(1, countRowsInTable("personal_attendant"));
        assertSame(newUser, pa.getUser());

        pa.setOperator("150");
        m_mailboxManager.storePersonalAttendant(pa);

        flush();
        assertEquals(1, countRowsInTable("personal_attendant"));

        m_coreContext.deleteUser(newUser);

        flush();
        assertEquals(0, countRowsInTable("personal_attendant"));
    }

    public void setMailboxManager(MailboxManager mailboxManager) {
        m_mailboxManager = mailboxManager;
    }

    public void setCoreContext(CoreContext coreContext) {
        m_coreContext = coreContext;
    }
}
