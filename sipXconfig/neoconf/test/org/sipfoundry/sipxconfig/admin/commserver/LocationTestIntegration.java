package org.sipfoundry.sipxconfig.admin.commserver;

import java.util.ArrayList;
import java.util.Collection;

import org.sipfoundry.sipxconfig.IntegrationTestCase;
import org.sipfoundry.sipxconfig.common.event.DaoEventPublisher;
import org.sipfoundry.sipxconfig.service.LocationSpecificService;
import org.sipfoundry.sipxconfig.service.SipxParkService;
import org.sipfoundry.sipxconfig.service.SipxProxyService;
import org.sipfoundry.sipxconfig.service.SipxRegistrarService;
import org.sipfoundry.sipxconfig.service.SipxService;

public class LocationTestIntegration extends IntegrationTestCase {
    private DaoEventPublisher m_daoEventPublisher;
    
    public void setDaoEventPublisher(DaoEventPublisher daoEventPublisher) {
        m_daoEventPublisher = daoEventPublisher;
    }
    
    public void testGetProcessMonitorUrl() {
        Location out = new Location();
        out.setFqdn("localhost");
        assertEquals("https://localhost:8092/RPC2", out.getProcessMonitorUrl());
        Location out1 = new Location();
        out1.setFqdn("mysystem.europe.pmd.com");
        assertEquals("https://mysystem.europe.pmd.com:8092/RPC2", out1.getProcessMonitorUrl());
    }

    @SuppressWarnings("deprecation")
    public void testParseAddress() {
        // it tests setUrl - which is deprecated, because it's only used when migrating from topology.xml
        Location out = new Location();
        out.setUrl("https://localhost:8091/cgi-bin/replication/replication.cgi");
        assertEquals("localhost", out.getFqdn());
        assertNull(out.getAddress());

        Location out1 = new Location();
        out1.setUrl("https://192.168.1.10:8091/cgi-bin/replication/replication.cgi");
        assertEquals("192.168.1.10", out1.getFqdn());
        assertNull(out.getAddress());
    }

    public void testSetServiceDefinitions() {
        Location out = new Location();
        Collection<SipxService> sipxServices = new ArrayList<SipxService>();
        SipxService proxyService = new SipxProxyService();
        proxyService.setModelId(SipxProxyService.BEAN_ID);
        sipxServices.add(proxyService);
        SipxService registrarService = new SipxRegistrarService();
        registrarService.setModelId(SipxRegistrarService.BEAN_ID);
        sipxServices.add(registrarService);

        out.setServiceDefinitions(sipxServices);

        Collection<LocationSpecificService> servicesFromOut = out.getServices();
        assertNotNull(servicesFromOut);
        assertEquals(2, servicesFromOut.size());
    }

    public void testSetServices() {
        Location out = new Location();
        Collection<LocationSpecificService> services = new ArrayList<LocationSpecificService>();
        SipxService proxyService = new SipxProxyService();
        LocationSpecificService proxyLss= new LocationSpecificService();
        proxyLss.setSipxService(proxyService);
        services.add(proxyLss);
        SipxService registrarService = new SipxRegistrarService();
        LocationSpecificService registrarLss = new LocationSpecificService();
        registrarLss.setSipxService(registrarService);
        services.add(registrarLss);

        out.setServices(services);

        Collection<LocationSpecificService> servicesFromOut = out.getServices();
        assertNotNull(servicesFromOut);
        assertEquals(2, servicesFromOut.size());
    }

    public void testAddService() {
        Location out = new Location();
        Collection<SipxService> sipxServices = new ArrayList<SipxService>();
        SipxService proxyService = new SipxProxyService();
        proxyService.setModelId(SipxProxyService.BEAN_ID);
        sipxServices.add(proxyService);
        SipxService registrarService = new SipxRegistrarService();
        registrarService.setModelId(SipxRegistrarService.BEAN_ID);
        sipxServices.add(registrarService);

        out.setServiceDefinitions(sipxServices);

        SipxService parkService = new SipxParkService();
        parkService.setBeanId(SipxParkService.BEAN_ID);
        out.addService(parkService);

        Collection<LocationSpecificService> servicesFromOut = out.getServices();
        assertNotNull(servicesFromOut);
        assertEquals(3, servicesFromOut.size());
    }

    public void testRemoveService() {
        Location out = new Location();
        out.setDaoEventPublisher(m_daoEventPublisher);
        Collection<SipxService> sipxServices = new ArrayList<SipxService>();
        SipxService proxyService = new SipxProxyService();
        sipxServices.add(proxyService);
        proxyService.setModelId(SipxProxyService.BEAN_ID);
        SipxService registrarService = new SipxRegistrarService();
        registrarService.setModelId(SipxRegistrarService.BEAN_ID);
        sipxServices.add(registrarService);

        out.setServiceDefinitions(sipxServices);

        LocationSpecificService proxyLss = new LocationSpecificService();
        proxyLss.setSipxService(proxyService);
        out.removeService(proxyLss);

        Collection<LocationSpecificService> servicesFromOut = out.getServices();
        assertNotNull(servicesFromOut);
        assertEquals(1, servicesFromOut.size());
    }
    
    public void testGetHostname() {
        Location out = new Location();
        out.setFqdn("sipx.example.org");

        assertEquals("sipx", out.getHostname());
    }

    public void testRemoveServiceByBeanId() {
        Location out = new Location();
        out.setDaoEventPublisher(m_daoEventPublisher);
        Collection<SipxService> sipxServices = new ArrayList<SipxService>();
        SipxService proxyService = new SipxProxyService();
        proxyService.setBeanId("sipxProxyService");
        sipxServices.add(proxyService);
        SipxService registrarService = new SipxRegistrarService();
        registrarService.setBeanId("sipxRegistrarService");
        sipxServices.add(registrarService);

        out.setServiceDefinitions(sipxServices);
        
        out.removeServiceByBeanId("sipxProxyService");
        
        Collection<LocationSpecificService> servicesFromOut = out.getServices();
        assertNotNull(servicesFromOut);
        assertEquals(1, servicesFromOut.size());
        
        assertNotSame(proxyService, servicesFromOut.iterator().next().getSipxService());
    }
    
    public void testGetService() {
        Location out = new Location();
        Collection<SipxService> sipxServices = new ArrayList<SipxService>();
        SipxService proxyService = new SipxProxyService();
        proxyService.setBeanId("sipxProxyService");
        sipxServices.add(proxyService);
        SipxService registrarService = new SipxRegistrarService();
        registrarService.setBeanId("sipxRegistrarService");
        sipxServices.add(registrarService);

        out.setServiceDefinitions(sipxServices);
        
        assertSame(proxyService, out.getService("sipxProxyService").getSipxService());
        assertSame(registrarService, out.getService("sipxRegistrarService").getSipxService());
        assertNull(out.getService("invalidBeanId"));
    }
}
