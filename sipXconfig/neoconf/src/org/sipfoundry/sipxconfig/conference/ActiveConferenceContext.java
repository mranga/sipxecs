/*
 *
 *
 * Copyright (C) 2008 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 *
 */
package org.sipfoundry.sipxconfig.conference;

import java.util.List;

public interface ActiveConferenceContext {

    /**
     * Gets the number of active conferences for a conference bridge.
     *
     * @param bridge A FreeSWITCH conference bridge
     * @return The number of currently active (non-empty) conferences on the bridge.
     */
    public int getActiveConferenceCount(Bridge bridge);

    public List<ActiveConference> getActiveConferences(Bridge bridge);

    public List<ActiveConferenceMember> getConferenceMembers(Conference conference);

    public boolean lockConference(Conference conference);

    public boolean unlockConference(Conference conference);

    public boolean muteUser(Conference conference, ActiveConferenceMember member);

    public boolean unmuteUser(Conference conference, ActiveConferenceMember member);

    public boolean deafUser(Conference conference, ActiveConferenceMember member);

    public boolean undeafUser(Conference conference, ActiveConferenceMember member);

    public boolean kickUser(Conference conference, ActiveConferenceMember member);
}