/*
 *
 *
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 * $
 */
package org.sipfoundry.sipxconfig.site.user;

import java.util.ArrayList;
import java.util.List;

import org.apache.commons.lang.RandomStringUtils;
import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.tapestry.IPage;
import org.apache.tapestry.IRequestCycle;
import org.apache.tapestry.callback.ICallback;
import org.apache.tapestry.event.PageBeginRenderListener;
import org.apache.tapestry.event.PageEvent;
import org.sipfoundry.sipxconfig.common.CoreContext;
import org.sipfoundry.sipxconfig.common.User;
import org.sipfoundry.sipxconfig.components.FormActions;
import org.sipfoundry.sipxconfig.components.PageWithCallback;
import org.sipfoundry.sipxconfig.components.TapestryUtils;
import org.sipfoundry.sipxconfig.conference.Bridge;
import org.sipfoundry.sipxconfig.conference.Conference;
import org.sipfoundry.sipxconfig.conference.ConferenceBridgeContext;
import org.sipfoundry.sipxconfig.setting.Group;
import org.sipfoundry.sipxconfig.setting.Setting;
import org.sipfoundry.sipxconfig.setting.SettingDao;
import org.sipfoundry.sipxconfig.setting.SettingImpl;
import org.sipfoundry.sipxconfig.setting.SettingValue;
import org.sipfoundry.sipxconfig.site.admin.ExtensionPoolsPage;
import org.sipfoundry.sipxconfig.site.setting.EditGroup;
import org.sipfoundry.sipxconfig.vm.Mailbox;
import org.sipfoundry.sipxconfig.vm.MailboxManager;
import org.sipfoundry.sipxconfig.vm.MailboxPreferences;
import org.springframework.orm.hibernate3.HibernateObjectRetrievalFailureException;

public abstract class NewUser extends PageWithCallback implements PageBeginRenderListener {

    public static final String PAGE = "user/NewUser";
    private static final int SIP_PASSWORD_LEN = 8;
    private static final String CONFERENCE_SETTING_PATH = "conference" + Setting.PATH_DELIM;

    private static final Log LOG = LogFactory.getLog(NewUser.class);

    public abstract ConferenceBridgeContext getConferenceBridgeContext();

    public abstract CoreContext getCoreContext();

    public abstract SettingDao getSettingDao();

    public abstract User getUser();

    public abstract void setUser(User user);

    public abstract boolean isStay();

    public abstract String getButtonPressed();

    public abstract MailboxManager getMailboxManager();

    public IPage onCommit(IRequestCycle cycle) {
        if (!TapestryUtils.isValid(this)) {
            return null;
        }
        // Save the user
        CoreContext core = getCoreContext();
        User user = getUser();
        EditGroup.saveGroups(getSettingDao(), user.getGroups());
        core.saveUser(user);

        // If necessary, create a conference for this user.
        Group conferenceGroup = getConferenceGroup(user);
        if (conferenceGroup != null) {
            createUserConference(user, conferenceGroup);
        }

        MailboxManager mmgr = getMailboxManager();
        if (mmgr.isEnabled()) {
            String userName = user.getUserName();
            mmgr.deleteMailbox(userName);
            Mailbox mailbox = mmgr.getMailbox(userName);
            mmgr.saveMailboxPreferences(mailbox, getMailboxPreferences());
        }

        // On saving the user, transfer control to edituser page.
        if (FormActions.APPLY.equals(getButtonPressed())) {
            EditUser edit = (EditUser) cycle.getPage(EditUser.PAGE);
            edit.setUserId(user.getId());
            return edit;
        }

        return null;
    }

    /**
     * Creates a new conference for a new user.
     *
     * @param user The user that has just been created.
     * @param conferenceGroup The group containing conference settings to use.
     */
    private void createUserConference(User user, Group conferenceGroup) {
        SettingValue bridgeIdValue = conferenceGroup.getSettingValue(new SettingImpl(CONFERENCE_SETTING_PATH
                + "bridgeId"));
        Integer bridgeId = Integer.parseInt(bridgeIdValue.getValue());

        SettingValue conferencePrefixValue = conferenceGroup.getSettingValue(new SettingImpl(CONFERENCE_SETTING_PATH
                + "prefix"));

        ConferenceBridgeContext bridgeContext = getConferenceBridgeContext();

        Bridge bridge = null;
        try {
            bridge = bridgeContext.loadBridge(bridgeId);
        } catch (HibernateObjectRetrievalFailureException horfe) {
            LOG.warn(String.format("Unable to create a conference for new user %s; the user group \"%s\" "
                    + "references a non-existent conference bridge ID: %d", user.getUserName(), conferenceGroup
                    .getName(), bridgeId));
        }

        if (bridge != null) {
            String extension = user.getExtension(true);
            String extensionOrName = extension == null ? user.getUserName() : extension;
            String conferenceExtension = conferencePrefixValue + extensionOrName;

            Conference userConference = bridgeContext.newConference();
            userConference.setExtension(conferenceExtension.toString());
            userConference.setName(user.getUserName() + "-conference");
            userConference.setOwner(user);
            userConference.setEnabled(true);
            userConference.setDescription("Automatically created conference for " + user.getDisplayName());

            LOG.debug(String.format("Creating conference \"%s\", extension %s, for user %s", userConference
                    .getName(), userConference.getExtension(), user.getDisplayName()));

            bridgeContext.validate(userConference);
            bridge.addConference(userConference);
            bridgeContext.store(bridge);
        }
    }

    /**
     * Examines the groups a user belongs to and determines which, if any, group's conference
     * creation settings to use.
     *
     * @param user The user being created.
     * @return A Group whose conference creation settings should be used, or null if none of the
     *         user's groups have conferences enabled.
     */
    private Group getConferenceGroup(User user) {
        // Find the highest weight group that has conferences enabled, if any.
        List<Group> userGroups = new ArrayList<Group>(user.getGroupsAsList()); // cloning it
        // because we may
        // remove items
        Group conferenceGroup = null;
        while (!userGroups.isEmpty() && conferenceGroup == null) {
            Group highestGroup = Group.selectGroupWithHighestWeight(userGroups);
            SettingValue groupValue = highestGroup.getSettingValue(new SettingImpl(CONFERENCE_SETTING_PATH
                    + "enabled"));
            if (groupValue != null && Boolean.valueOf(groupValue.getValue())) {
                conferenceGroup = highestGroup;
            } else {
                userGroups.remove(highestGroup);
            }
        }

        if (conferenceGroup != null) {
            LOG.debug("Using group \"" + conferenceGroup.getName() + "\" for conference settings for new user: "
                    + user.getUserName());
        }

        return conferenceGroup;
    }

    private MailboxPreferences getMailboxPreferences() {
        return (MailboxPreferences) getBeans().getBean("mailboxPreferences");
    }

    public IPage extensionPools(IRequestCycle cycle) {
        ExtensionPoolsPage poolsPage = (ExtensionPoolsPage) cycle.getPage(ExtensionPoolsPage.PAGE);
        poolsPage.setReturnPage(this);
        return poolsPage;
    }

    @Override
    protected ICallback createCallback(String pageName) {
        ICallback createCallback = super.createCallback(pageName);
        return new OptionalStay(createCallback);
    }

    class OptionalStay implements ICallback {
        private final ICallback m_delegate;

        OptionalStay(ICallback delegate) {
            m_delegate = delegate;
        }

        public void performCallback(IRequestCycle cycle) {
            if (isStay() && FormActions.OK.equals(getButtonPressed())) {
                // Explicitly null out information that should not be used for multiple users,
                // otherwise keep form values as is theory that creating users in bulk will want
                // all the same settings by default
                setUser(null);

                MailboxPreferences mailboxPrefs = getMailboxPreferences();
                // Clear the email addresses
                mailboxPrefs.setEmailAddress(null); // XCF-1523
                mailboxPrefs.setAlternateEmailAddress(null);

                // Reset (clear) the voicemail checkboxes
                mailboxPrefs.setAttachVoicemailToEmail(false);
                mailboxPrefs.setAttachVoicemailToAlternateEmail(false);

                cycle.activate(PAGE);
            } else if (m_delegate != null) {
                m_delegate.performCallback(cycle);
            }
        }
    }

    public void pageBeginRender(PageEvent event) {
        User user = getUser();
        if (user == null) {
            user = getCoreContext().newUser();
            user.setSipPassword(RandomStringUtils.randomAlphanumeric(SIP_PASSWORD_LEN));
            setUser(user);
        }
    }
}
