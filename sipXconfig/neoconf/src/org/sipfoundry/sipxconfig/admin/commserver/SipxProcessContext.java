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

import java.util.Collection;
import java.util.List;

import org.sipfoundry.sipxconfig.service.SipxService;

public interface SipxProcessContext {
    public static enum Command {
        START, STOP, RESTART;
    }

    /**
     * Return an array containing a ServiceStatus entry for each process on the first server
     * machine. This is a first step towards providing status for all server machines.
     *
     * @param onlyActiveServices If true, only return status information for the services that the
     *        location parameter lists in its services list. If false, return all service status
     *        information available.
     */
    public ServiceStatus[] getStatus(Location location, boolean onlyActiveServices);

    
    /**
     * Return a list of status messages for a given service on a given server.
     */
    public List<String> getStatusMessages(Location location, SipxService service);
    
    /**
     * Apply the specified command to the named services. This method handles only commands that
     * don't need output, which excludes the "status" command.
     *
     * @param services list of services that will receive the command
     * @param command command to send
     * @param location information about the host on which services are running
     */
    public void manageServices(Location location, Collection< ? extends SipxService> services, Command command);

    public void manageServices(Collection< ? extends SipxService> services, Command command);

    /**
     * Modifies list of services running an a specified location to reflect the bundles selection.
     *
     * If the list of processes running on the server correctly reflects the selected bundle none
     * of them will be started (or stopped).
     *
     * @param location server on which to start/stop services
     */
    public void enforceRole(Location location);

    /**
     * Delayed version of manageServices strictly for restarting services. Restart commands is not
     * send until the event of the specific class is received.
     *
     * @param services list of services that will receive the command
     * @param eventClass class of event that will trigger the command
     */
    public void restartOnEvent(Collection< ? extends SipxService> services, Class eventClass);
}
