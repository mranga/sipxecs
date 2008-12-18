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
import org.sipfoundry.sipxconfig.common.UserException;
import org.sipfoundry.sipxconfig.components.ObjectSelectionModel;
import org.sipfoundry.sipxconfig.components.PageWithCallback;
import org.sipfoundry.sipxconfig.components.SelectMap;
import org.sipfoundry.sipxconfig.components.TapestryUtils;
import org.sipfoundry.sipxconfig.service.LocationSpecificService;
import org.sipfoundry.sipxconfig.service.SipxService;
import org.sipfoundry.sipxconfig.service.SipxServiceManager;
import org.sipfoundry.sipxconfig.site.acd.AcdServerPage;
import org.sipfoundry.sipxconfig.site.service.EditCallResolverService;
import org.sipfoundry.sipxconfig.site.service.EditFreeswitchService;
import org.sipfoundry.sipxconfig.site.service.EditPageService;
import org.sipfoundry.sipxconfig.site.service.EditParkService;
import org.sipfoundry.sipxconfig.site.service.EditPresenceService;
import org.sipfoundry.sipxconfig.site.service.EditProxyService;
import org.sipfoundry.sipxconfig.site.service.EditRegistrarService;
import org.sipfoundry.sipxconfig.site.service.EditResourceListService;
import org.sipfoundry.sipxconfig.site.service.EditStatusService;

import static org.sipfoundry.sipxconfig.admin.commserver.ServiceStatus.Status.Undefined;

public abstract class ServicesTable extends BaseComponent {

    public static final Map<String, String> SERVICE_MAP = new HashMap<String, String>();
    static {
        SERVICE_MAP.put("SIPXProxy", EditProxyService.PAGE);
        SERVICE_MAP.put("SIPRegistrar", EditRegistrarService.PAGE);
        SERVICE_MAP.put("ParkServer", EditParkService.PAGE);
        SERVICE_MAP.put("PresenceServer", EditPresenceService.PAGE);
        SERVICE_MAP.put("CallResolver", EditCallResolverService.PAGE);
        SERVICE_MAP.put("ResourceListServer", EditResourceListService.PAGE);
        SERVICE_MAP.put("SIPStatus", EditStatusService.PAGE);
        SERVICE_MAP.put("PageServer", EditPageService.PAGE);
        SERVICE_MAP.put("FreeSWITCH", EditFreeswitchService.PAGE);
        SERVICE_MAP.put("ACDServer", AcdServerPage.PAGE);
    }

    @InjectObject(value = "service:tapestry.ognl.ExpressionEvaluator")
    public abstract ExpressionEvaluator getExpressionEvaluator();

    @InjectObject(value = "spring:sipxProcessContext")
    public abstract SipxProcessContext getSipxProcessContext();

    @InjectObject(value = "spring:locationsManager")
    public abstract LocationsManager getLocationsManager();

    @InjectObject(value = "spring:sipxServiceManager")
    public abstract SipxServiceManager getSipxServiceManager();

    @InjectObject(value = "spring:acdContext")
    public abstract AcdContext getAcdContext();

    @Bean
    public abstract SelectMap getSelections();

    @Bean
    public abstract ServerStatusSqueezeAdapter getServerStatusConverter();

    @Bean(initializer = "array=sipxProcessContext.locations,labelExpression=id")
    public abstract ObjectSelectionModel getLocationModel();

    public abstract ServiceStatus getCurrentRow();

    @Parameter(required = true)
    public abstract Location getServiceLocation();

    public abstract void setServiceLocation(Location location);

    public abstract void setServiceStatus(Object[] serviceStatus);

    public abstract Object[] getServiceStatus();

    @Asset("/images/cog.png")
    public abstract IAsset getServiceIcon();

    @Asset("/images/error.png")
    public abstract IAsset getErrorIcon();

    @Asset("/images/unknown.png")
    public abstract IAsset getUnknownIcon();

    @Asset("/images/running.png")
    public abstract IAsset getRunningIcon();

    @Asset("/images/disabled.png")
    public abstract IAsset getDisabledIcon();

    @Asset("/images/loading.gif")
    public abstract IAsset getLoadingIcon();

    public IAsset getStatusIcon(ServiceStatus status) {
        switch (status.getStatus()) {
        case ConfigurationMismatch:
        case ResourceRequired:
        case ConfigurationTestFailed:
        case Failed:
            return getErrorIcon();
        case Running:
            return getRunningIcon();
        case Testing:
        case Starting:
        case Stopping:
        case ShuttingDown:
            return getLoadingIcon();
        case Disabled:
        case ShutDown:
            return getDisabledIcon();
        default:
            return getUnknownIcon();
        }
    }

    public String getLabelClass(ServiceStatus status) {
        switch (status.getStatus()) {
        case Disabled:
            return "service-disabled";
        case ConfigurationTestFailed:
        case Failed:
        case ResourceRequired:
        case ConfigurationMismatch:
            return "service-error";
        default:
            return "";
        }
    }

    @Override
    protected void prepareForRender(IRequestCycle cycle) {
        super.prepareForRender(cycle);

        Object[] serviceStatus = getServiceStatus();
        if (serviceStatus == null) {
            serviceStatus = retrieveServiceStatus(getServiceLocation());
            setServiceStatus(serviceStatus);
        }
    }

    public Object[] retrieveServiceStatus(Location location) {
        if (location == null || location.getServices() == null) {
            return ArrayUtils.EMPTY_OBJECT_ARRAY;
        }
        try {
            return getSipxProcessContext().getStatus(location, true);

        } catch (UserException e) {
            IValidationDelegate validator = TapestryUtils.getValidator(this);
            validator.record(new ValidatorException(e.getMessage()));

            Collection<ServiceStatus> serviceStatusList = new ArrayList<ServiceStatus>();
            for (LocationSpecificService service : location.getServices()) {
                String serviceName = service.getSipxService().getProcessName();
                if (serviceName != null) {
                    serviceStatusList.add(new ServiceStatus(serviceName, Undefined));
                }
            }
            return serviceStatusList.toArray();
        }
    }

    public boolean getCurrentRowHasServiceLink() {
        return SERVICE_MAP.containsKey(getCurrentRow().getServiceName());
    }

    public IPage editService(IRequestCycle cycle, String serviceName, Integer locationId) {
        PageWithCallback page = (PageWithCallback) cycle.getPage(SERVICE_MAP.get(serviceName));
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
        setServiceStatus(null);
    }

    public void removeService() {
        manageServices(SipxProcessContext.Command.STOP);
        Collection<String> selectedNames = getSelections().getAllSelected();
        for (String name : selectedNames) {
            SipxService service = getSipxServiceManager().getServiceByName(name);
            getServiceLocation().removeServiceByBeanId(service.getBeanId());
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
        Collection<String> serviceNames = getSelections().getAllSelected();
        if (serviceNames == null) {
            return;
        }
        try {
            SipxServiceManager sipxServiceManager = getSipxServiceManager();
            List<SipxService> services = new ArrayList<SipxService>(serviceNames.size());
            for (String name : serviceNames) {
                SipxService service = sipxServiceManager.getServiceByName(name);
                if (service != null) {
                    services.add(service);
                }
            }
            getSipxProcessContext().manageServices(getServiceLocation(), services, operation);
        } catch (UserException e) {
            IValidationDelegate validator = TapestryUtils.getValidator(this);
            validator.record(new ValidatorException(e.getMessage()));
        }
    }
}
