/*
 *
 *
 * Copyright (C) 2008 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 * $
 */
package org.sipfoundry.sipxconfig.site.admin.commserver;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.apache.commons.lang.ArrayUtils;
import org.apache.tapestry.BaseComponent;
import org.apache.tapestry.IAsset;
import org.apache.tapestry.IPage;
import org.apache.tapestry.IRequestCycle;
import org.apache.tapestry.annotations.Asset;
import org.apache.tapestry.annotations.Bean;
import org.apache.tapestry.annotations.InjectObject;
import org.apache.tapestry.annotations.Parameter;
import org.apache.tapestry.services.ExpressionEvaluator;
import org.apache.tapestry.valid.IValidationDelegate;
import org.apache.tapestry.valid.ValidatorException;
import org.sipfoundry.sipxconfig.acd.AcdContext;
import org.sipfoundry.sipxconfig.acd.AcdServer;
import org.sipfoundry.sipxconfig.admin.commserver.Location;
import org.sipfoundry.sipxconfig.admin.commserver.LocationsManager;
import org.sipfoundry.sipxconfig.admin.commserver.ServiceStatus;
import org.sipfoundry.sipxconfig.admin.commserver.SipxProcessContext;
import org.sipfoundry.sipxconfig.common.CoreContext;
import org.sipfoundry.sipxconfig.common.UserException;
import org.sipfoundry.sipxconfig.components.ObjectSelectionModel;
import org.sipfoundry.sipxconfig.components.PageWithCallback;
import org.sipfoundry.sipxconfig.components.SelectMap;
import org.sipfoundry.sipxconfig.service.LocationSpecificService;
import org.sipfoundry.sipxconfig.service.SipxAcdService;
import org.sipfoundry.sipxconfig.service.SipxCallResolverService;
import org.sipfoundry.sipxconfig.service.SipxFreeswitchService;
import org.sipfoundry.sipxconfig.service.SipxIvrService;
import org.sipfoundry.sipxconfig.service.SipxMediaService;
import org.sipfoundry.sipxconfig.service.SipxPageService;
import org.sipfoundry.sipxconfig.service.SipxParkService;
import org.sipfoundry.sipxconfig.service.SipxPresenceService;
import org.sipfoundry.sipxconfig.service.SipxProxyService;
import org.sipfoundry.sipxconfig.service.SipxRegistrarService;
import org.sipfoundry.sipxconfig.service.SipxRlsService;
import org.sipfoundry.sipxconfig.service.SipxService;
import org.sipfoundry.sipxconfig.service.SipxServiceManager;
import org.sipfoundry.sipxconfig.service.SipxStatusService;
import org.sipfoundry.sipxconfig.site.acd.AcdServerPage;
import org.sipfoundry.sipxconfig.site.service.EditCallResolverService;
import org.sipfoundry.sipxconfig.site.service.EditFreeswitchService;
import org.sipfoundry.sipxconfig.site.service.EditIvrService;
import org.sipfoundry.sipxconfig.site.service.EditMediaService;
import org.sipfoundry.sipxconfig.site.service.EditPageService;
import org.sipfoundry.sipxconfig.site.service.EditParkService;
import org.sipfoundry.sipxconfig.site.service.EditPresenceService;
import org.sipfoundry.sipxconfig.site.service.EditProxyService;
import org.sipfoundry.sipxconfig.site.service.EditRegistrarService;
import org.sipfoundry.sipxconfig.site.service.EditResourceListService;
import org.sipfoundry.sipxconfig.site.service.EditStatusService;

import static org.sipfoundry.sipxconfig.admin.commserver.ServiceStatus.Status.Undefined;
import static org.sipfoundry.sipxconfig.components.TapestryUtils.getMessage;
import static org.sipfoundry.sipxconfig.components.TapestryUtils.getValidator;

public abstract class ServicesTable extends BaseComponent {

    public static final Map<String, String> SERVICE_MAP = new HashMap<String, String>();
    static {
        SERVICE_MAP.put(SipxProxyService.BEAN_ID, EditProxyService.PAGE);
        SERVICE_MAP.put(SipxRegistrarService.BEAN_ID, EditRegistrarService.PAGE);
        SERVICE_MAP.put(SipxParkService.BEAN_ID, EditParkService.PAGE);
        SERVICE_MAP.put(SipxPresenceService.BEAN_ID, EditPresenceService.PAGE);
        SERVICE_MAP.put(SipxCallResolverService.BEAN_ID, EditCallResolverService.PAGE);
        SERVICE_MAP.put(SipxRlsService.BEAN_ID, EditResourceListService.PAGE);
        SERVICE_MAP.put(SipxStatusService.BEAN_ID, EditStatusService.PAGE);
        SERVICE_MAP.put(SipxPageService.BEAN_ID, EditPageService.PAGE);
        SERVICE_MAP.put(SipxFreeswitchService.BEAN_ID, EditFreeswitchService.PAGE);
        SERVICE_MAP.put(SipxAcdService.BEAN_ID, AcdServerPage.PAGE);
        SERVICE_MAP.put(SipxIvrService.BEAN_ID, EditIvrService.PAGE);
        SERVICE_MAP.put(SipxMediaService.BEAN_ID, EditMediaService.PAGE);
    }

    @InjectObject("service:tapestry.ognl.ExpressionEvaluator")
    public abstract ExpressionEvaluator getExpressionEvaluator();

    @InjectObject("spring:sipxProcessContext")
    public abstract SipxProcessContext getSipxProcessContext();

    @InjectObject("spring:locationsManager")
    public abstract LocationsManager getLocationsManager();

    @InjectObject("spring:sipxServiceManager")
    public abstract SipxServiceManager getSipxServiceManager();

    @InjectObject("spring:acdContext")
    public abstract AcdContext getAcdContext();

    @InjectObject("spring:coreContext")
    public abstract CoreContext getCoreContext();

    @Bean
    public abstract SelectMap getSelections();

    @Bean
    public abstract ServerStatusSqueezeAdapter getServerStatusConverter();

    @Bean(initializer = "array=sipxProcessContext.locations,labelExpression=id")
    public abstract ObjectSelectionModel getLocationModel();

    public abstract ServiceStatus getCurrentRow();

    public abstract int getCurrentRowId();
    
    @Parameter(required = true)
    public abstract Location getServiceLocation();

    public abstract void setServiceLocation(Location location);

    public abstract void setServiceStatusCached(Object[] serviceStatus);

    public abstract Object[] getServiceStatusCached();

    @Asset("/images/cog.png")
    public abstract IAsset getServiceIcon();
    
    public String getServiceLabel() {
        String serviceBeanId = getCurrentRow().getServiceBeanId();
        String key = "label." + serviceBeanId;
        return getMessage(getMessages(), key, serviceBeanId);
    }

    public Object[] getServiceStatus() {
        Object[] serviceStatus = getServiceStatusCached();
        if (serviceStatus == null) {
            serviceStatus = retrieveServiceStatus(getServiceLocation());
            setServiceStatusCached(serviceStatus);
        }
        return serviceStatus;
    }

    public Object[] retrieveServiceStatus(Location location) {
        if (location == null || location.getServices() == null) {
            return ArrayUtils.EMPTY_OBJECT_ARRAY;
        }
        try {
            return getSipxProcessContext().getStatus(location, true);

        } catch (UserException e) {
            IValidationDelegate validator = getValidator(this);
            validator.record(new ValidatorException(e.getMessage()));

            Collection<ServiceStatus> serviceStatusList = new ArrayList<ServiceStatus>();
            for (LocationSpecificService lss : location.getServices()) {
                SipxService service = lss.getSipxService();
                String serviceName = service.getProcessName();
                if (serviceName != null) {
                    serviceStatusList.add(new ServiceStatus(service.getBeanId(), Undefined));
                }
            }
            return serviceStatusList.toArray();
        }
    }

    public boolean getCurrentRowHasServiceLink() {
        return SERVICE_MAP.containsKey(getCurrentRow().getServiceBeanId());
    }

    public IPage editService(IRequestCycle cycle, String serviceBeanId, Integer locationId) {
        PageWithCallback page = (PageWithCallback) cycle.getPage(SERVICE_MAP.get(serviceBeanId));
        page.setReturnPage(EditLocationPage.PAGE);
        if (page instanceof AcdServerPage) {
            AcdServer acdServer = getAcdContext().getAcdServerForLocationId(locationId);
            if (acdServer != null) {
                ((AcdServerPage) page).setAcdServerId(acdServer.getId());
            }
        }
        return page;
    }

    /**
     * Registered as form listener - forces service status update every time form is submitted
     */
    public void refresh() {
        setServiceStatusCached(null);
    }

    public void removeService() {
        manageServices(SipxProcessContext.Command.STOP);
        Collection<String> selectedBeanIds = getSelections().getAllSelected();
        for (String beanId : selectedBeanIds) {
            getServiceLocation().removeServiceByBeanId(beanId);
        }
        getLocationsManager().storeLocation(getServiceLocation());
        refresh();
    }

    public void start() {
        manageServices(SipxProcessContext.Command.START);
        refresh();
    }

    public void stop() {
        manageServices(SipxProcessContext.Command.STOP);
        refresh();
    }

    public void restart() {
        manageServices(SipxProcessContext.Command.RESTART);
        refresh();
    }

    private void manageServices(SipxProcessContext.Command operation) {
        Collection<String> serviceBeanIds = getSelections().getAllSelected();
        if (serviceBeanIds == null) {
            return;
        }
        try {
            SipxServiceManager sipxServiceManager = getSipxServiceManager();
            List<SipxService> services = new ArrayList<SipxService>(serviceBeanIds.size());
            for (String beanId : serviceBeanIds) {
                SipxService service = sipxServiceManager.getServiceByBeanId(beanId);
                if (service != null) {
                    services.add(service);
                }
            }
            getSipxProcessContext().manageServices(getServiceLocation(), services, operation);
        } catch (UserException e) {
            IValidationDelegate validator = getValidator(this);
            validator.record(new ValidatorException(e.getMessage()));
        }
    }
}
