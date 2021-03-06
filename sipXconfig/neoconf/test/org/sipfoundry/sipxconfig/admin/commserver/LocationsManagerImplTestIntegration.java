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

import static java.util.Arrays.*;
import java.util.Collection;
import java.util.Collections;

import org.sipfoundry.sipxconfig.IntegrationTestCase;
import org.sipfoundry.sipxconfig.acd.AcdContext;
import org.sipfoundry.sipxconfig.conference.ConferenceBridgeContext;
import org.sipfoundry.sipxconfig.service.LocationSpecificService;
import org.sipfoundry.sipxconfig.service.SipxFreeswitchService;
import org.sipfoundry.sipxconfig.service.SipxService;

public class LocationsManagerImplTestIntegration extends IntegrationTestCase {
    private LocationsManager m_out;
    private AcdContext m_acdContext;
    private ConferenceBridgeContext m_conferenceBridgeContext;

    public void testGetLocations() throws Exception {
        loadDataSetXml("admin/commserver/clearLocations.xml");
        Location[] emptyLocations = m_out.getLocations();
        assertEquals(0, emptyLocations.length);

        loadDataSetXml("admin/commserver/seedLocationsAndServices.xml");
        Location[] locations = m_out.getLocations();
        assertEquals(2, locations.length);
        assertEquals("https://localhost:8092/RPC2", locations[0].getProcessMonitorUrl());
        assertEquals("https://remotehost.example.org:8092/RPC2", locations[1].getProcessMonitorUrl());
    }

    public void testFindById() throws Exception {
        loadDataSetXml("admin/commserver/seedLocationsAndServices.xml");
        Location[] locations = m_out.getLocations();

        Location firstLocation = locations[0];
        int locationId = firstLocation.getId();

        Location locationById = m_out.getLocation(locationId);
        assertNotNull(locationById);
        assertEquals(firstLocation.getName(), locationById.getName());

        Collection<LocationSpecificService> services = locationById.getServices();
        assertNotNull(services);
        assertEquals(3, services.size());
    }

    public void testGetPrimaryLocation() throws Exception {
        loadDataSetXml("admin/commserver/seedLocations.xml");
        Location location = m_out.getPrimaryLocation();
        assertNotNull(location);
        assertEquals(101, (int) location.getId());
    }

    public void testStore() throws Exception {
        loadDataSetXml("admin/commserver/clearLocations.xml");
        Location location = new Location();
        location.setName("test location");
        location.setAddress("192.168.1.2");
        location.setFqdn("localhost");

        m_out.storeLocation(location);

        Location[] dbLocations = m_out.getLocations();
        assertEquals(1, dbLocations.length);
        assertEquals("test location", dbLocations[0].getName());
        assertEquals("192.168.1.2", dbLocations[0].getAddress());
        assertEquals("localhost", dbLocations[0].getFqdn());
    }

    public void testDelete() throws Exception {
        loadDataSetXml("admin/commserver/seedLocations.xml");
        Location[] locationsBeforeDelete = m_out.getLocations();
        assertEquals(2, locationsBeforeDelete.length);

        Location locationToDelete = m_out.getLocation(101);

        m_out.deleteLocation(locationToDelete);

        Location[] locationsAfterDelete = m_out.getLocations();
        assertEquals(1, locationsAfterDelete.length);
        assertEquals("remotehost.example.org", locationsAfterDelete[0].getFqdn());
    }

    public void testDeleteWithServices() throws Exception {
        loadDataSetXml("admin/commserver/seedLocationsAndServices.xml");
        Location[] locationsBeforeDelete = m_out.getLocations();
        assertEquals(2, locationsBeforeDelete.length);

        Location locationToDelete = m_out.getLocation(101);

        m_out.deleteLocation(locationToDelete);

        Location[] locationsAfterDelete = m_out.getLocations();
        assertEquals(1, locationsAfterDelete.length);
        assertEquals("remotehost.example.org", locationsAfterDelete[0].getFqdn());
    }

    public void testAcdBridgePublishSaveDelete() throws Exception {
        loadDataSetXml("admin/commserver/clearLocations.xml");
        Location[] emptyLocations = m_out.getLocations();
        assertEquals(0, emptyLocations.length);
        assertEquals(0, m_acdContext.getServers().size());

        Location location = new Location();
        location.setName("test location");
        location.setAddress("192.168.1.2");
        location.setFqdn("location1");
        location.setInstalledBundles(asList("callCenterBundle"));
        m_out.storeLocation(location);
        assertEquals(1, m_acdContext.getServers().size());
        assertEquals(0, m_conferenceBridgeContext.getBridges().size());

        SipxService freeswitchService = new SipxFreeswitchService();
        freeswitchService.setBeanId(SipxFreeswitchService.BEAN_ID);
        LocationSpecificService service = new LocationSpecificService();
        service.setSipxService(freeswitchService);
        service.setLocation(location);
        location.addService(freeswitchService);
        location.setInstalledBundles(asList("callCenterBundle", "conferenceBundle"));
        m_out.storeLocation(location);
        assertEquals(1, m_acdContext.getServers().size());
        assertEquals(1, m_conferenceBridgeContext.getBridges().size());

        location.setInstalledBundles(asList("callCenterBundle"));
        m_out.storeLocation(location);
        assertEquals(1, m_acdContext.getServers().size());
        assertEquals(0, m_conferenceBridgeContext.getBridges().size());

        location.setInstalledBundles(Collections.<String> emptyList());
        m_out.storeLocation(location);
        assertEquals(0, m_acdContext.getServers().size());
        assertEquals(0, m_conferenceBridgeContext.getBridges().size());
    }

    public void setLocationsManager(LocationsManager locationsManager) {
        m_out = locationsManager;
    }

    public void setAcdContext(AcdContext acdContext) {
        m_acdContext = acdContext;
    }

    public void setConferenceBridgeContext(ConferenceBridgeContext conferenceBridgeContext) {
        m_conferenceBridgeContext = conferenceBridgeContext;
    }
}
