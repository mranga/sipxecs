/*
 *
 *
 * Copyright (C) 2008 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 * $
 */
package org.sipfoundry.sipxconfig.components;

import java.util.ArrayList;
import java.util.Collection;

import org.apache.commons.lang.ObjectUtils;
import org.apache.tapestry.BaseComponent;
import org.apache.tapestry.IActionListener;
import org.apache.tapestry.IComponent;
import org.apache.tapestry.IForm;
import org.apache.tapestry.IMarkupWriter;
import org.apache.tapestry.IRequestCycle;
import org.apache.tapestry.annotations.Bean;
import org.apache.tapestry.annotations.InjectObject;
import org.apache.tapestry.annotations.Parameter;
import org.apache.tapestry.form.IPropertySelectionModel;
import org.sipfoundry.sipxconfig.admin.dialplan.sbc.SbcDescriptor;
import org.sipfoundry.sipxconfig.admin.dialplan.sbc.SbcDevice;
import org.sipfoundry.sipxconfig.admin.dialplan.sbc.SbcDeviceManager;
import org.sipfoundry.sipxconfig.components.selection.AdaptedSelectionModel;
import org.sipfoundry.sipxconfig.components.selection.OptGroup;
import org.sipfoundry.sipxconfig.components.selection.OptGroupPropertySelectionRenderer;
import org.sipfoundry.sipxconfig.components.selection.OptionAdapter;
import org.sipfoundry.sipxconfig.device.ModelSource;
import org.sipfoundry.sipxconfig.site.sbc.EditSbcDevice;
import org.sipfoundry.sipxconfig.site.setting.SbcDeviceAction;

public abstract class SbcDeviceSelect extends BaseComponent {
    @InjectObject(value = "spring:sbcDeviceManager")
    public abstract SbcDeviceManager getSbcDeviceManager();

    @InjectObject(value = "spring:sbcModelSource")
    public abstract ModelSource<SbcDescriptor> getSbcModelSource();

    @InjectObject(value = "spring:tapestry")
    public abstract TapestryContext getTapestry();

    @Bean
    public abstract OptGroupPropertySelectionRenderer getRender();

    @Parameter(required = true)
    public abstract void setSelectedSbcDevice(SbcDevice selectedSbcDevice);

    public abstract SbcDevice getSelectedSbcDevice();

    public abstract void setSelectedAction(IActionListener selectedAction);

    public abstract IActionListener getSelectedAction();

    @Parameter(required = false)
    public abstract void setEnforceInternetCallingSupport(boolean enforce);

    public abstract boolean getEnforceInternetCallingSupport();

    protected void renderComponent(IMarkupWriter writer, IRequestCycle cycle) {
        if (getSelectedSbcDevice() != null) {
            setSelectedAction(new AddExistingSbcDeviceAction(getSelectedSbcDevice()));
        } else {
            setSelectedAction(null);
        }

        super.renderComponent(writer, cycle);
        if (TapestryUtils.isRewinding(cycle, this) && TapestryUtils.isValid(this)) {
            triggerAction(cycle);
        }
    }

    private void triggerAction(IRequestCycle cycle) {
        IActionListener a = getSelectedAction();
        if (!(a instanceof SbcDeviceAction)) {
            return;
        }

        SbcDeviceAction action = (SbcDeviceAction) a;
        SbcDevice device = action.getSbcDevice();
        if (device != null) {
            action.setId(device.getId());
        }
        action.actionTriggered(this, cycle);
    }

    public IPropertySelectionModel decorateModel(IPropertySelectionModel model) {
        return getTapestry().addExtraOption(model, getMessages(), "label.select");
    }

    private boolean checkIfAllowedToAddAction(SbcDescriptor model) {
        boolean enforceInternetCallingSupport = getEnforceInternetCallingSupport();
        if (enforceInternetCallingSupport) {
            if (!model.isInternetCallingSupported()) {
                return false;
            }
        }
        return true;
    }

    public IPropertySelectionModel getModel() {
        SbcDeviceManager deviceManager = getSbcDeviceManager();
        Collection<OptionAdapter> actions = new ArrayList<OptionAdapter>();

        Collection<SbcDevice> sbcDevices = deviceManager.getSbcDevices();
        if (!sbcDevices.isEmpty()) {
            actions.add(new OptGroup(getMessages().getMessage("label.addSbc")));
            for (SbcDevice sbcDevice : sbcDevices) {
                if (checkIfAllowedToAddAction(sbcDevice.getModel())) {
                    AddExistingSbcDeviceAction action = new AddExistingSbcDeviceAction(sbcDevice);
                    actions.add(action);
                }
            }
        }

        Collection<SbcDescriptor> models = getSbcModelSource().getModels();
        actions.add(new OptGroup(getMessages().getMessage("label.createSbc")));
        for (SbcDescriptor model : models) {
            boolean maxAllowedReached = deviceManager.maxAllowedLimitReached(model);
            if (checkIfAllowedToAddAction(model) && !maxAllowedReached) {
                AddNewSbcDeviceAction action = new AddNewSbcDeviceAction(model);
                actions.add(action);
            }
        }

        AdaptedSelectionModel model = new AdaptedSelectionModel();
        model.setCollection(actions);
        return model;
    }

    private class AddNewSbcDeviceAction extends SbcDeviceAction {
        private SbcDescriptor m_model;

        AddNewSbcDeviceAction(SbcDescriptor model) {
            super(null);
            m_model = model;
        }

        public void actionTriggered(IComponent component, final IRequestCycle cycle) {
            Runnable action = new Runnable() {
                public void run() {
                    EditSbcDevice page = EditSbcDevice
                            .getAddPage(cycle, m_model, cycle.getPage());
                    cycle.activate(page);
                }
            };

            IForm form = org.apache.tapestry.TapestryUtils.getForm(cycle, component);
            form.addDeferredRunnable(action);
        }

        public String getLabel(Object option, int index) {
            return m_model.getLabel();
        }

        public Object getValue(Object option, int index) {
            return m_model;
        }

        public String squeezeOption(Object option, int index) {
            return m_model.getModelId() + m_model.getBeanId();
        }
    }

    private class AddExistingSbcDeviceAction extends SbcDeviceAction {

        public AddExistingSbcDeviceAction(SbcDevice sbcDevice) {
            super(sbcDevice);
        }

        public void actionTriggered(IComponent component, final IRequestCycle cycle) {
            setSelectedSbcDevice(getSbcDevice());
        }

        public Object getValue(Object option, int index) {
            return this;
        }

        public String squeezeOption(Object option, int index) {
            return getSbcDevice().getId().toString();
        }

        @Override
        public boolean equals(Object obj) {
            return ObjectUtils.equals(this.getSbcDevice(), ((AddExistingSbcDeviceAction) obj)
                    .getSbcDevice());
        }

        public int hashCode() {
            return this.getSbcDevice().hashCode();
        }
    }
}
