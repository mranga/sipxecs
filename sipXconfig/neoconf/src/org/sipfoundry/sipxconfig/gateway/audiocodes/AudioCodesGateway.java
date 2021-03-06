/*
 * 
 * 
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.  
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 * 
 * $
 */
package org.sipfoundry.sipxconfig.gateway.audiocodes;

import java.io.File;
import java.util.Arrays;
import java.util.List;

import org.sipfoundry.sipxconfig.device.DeviceVersion;
import org.sipfoundry.sipxconfig.device.ProfileContext;
import org.sipfoundry.sipxconfig.device.ProfileLocation;
import org.sipfoundry.sipxconfig.gateway.Gateway;
import org.sipfoundry.sipxconfig.setting.AbstractSettingVisitor;
import org.sipfoundry.sipxconfig.setting.Setting;
import org.sipfoundry.sipxconfig.setting.type.FileSetting;
import org.sipfoundry.sipxconfig.setting.type.SettingType;

public abstract class AudioCodesGateway extends Gateway {
    public static final String BEAN_ID = "audiocodes";
    protected static final List PARAMETER_TABLE_SETTINGS = Arrays.asList("CoderName");
    private static final String CALL_PROGRESS_TONES_FILE = "Media_RTP_RTPC/Telephony/CallProgressTonesFilename";
    private static final String FXS_LOOP_CHARACTERISTICS_FILE =
        "Media_RTP_RTPC/Telephony/FXSLoopCharacteristicsFilename";
    private static final String[] COPY_FILES = {CALL_PROGRESS_TONES_FILE, FXS_LOOP_CHARACTERISTICS_FILE};
    
    public AudioCodesGateway() {
        setDeviceVersion(AudioCodesModel.REL_5_0);
    }    
    
    public void setDefaultVersionId(String defaultVersionId) {
        setDeviceVersion(DeviceVersion.getDeviceVersion(BEAN_ID + defaultVersionId));
    }    
    
    @Override
    public void initialize() {
        AudioCodesGatewayDefaults defaults = new AudioCodesGatewayDefaults(this, getDefaults());
        addDefaultBeanSettingHandler(defaults);
    }

    @Override
    public String getProfileFilename() {
        String serialNumber = getSerialNumber();
        if (serialNumber == null) {
            return null;
        }
        return serialNumber.toUpperCase() + ".ini";
    }

    @Override
    protected ProfileContext createContext() {
        return new AudioCodesContext(this, getModel().getProfileTemplate());
    }

    @Override
    protected Setting loadSettings() {
        Setting setting = getModelFilesContext().loadDynamicModelFile("gateway.xml",
                getModel().getModelDir(), getSettingsEvaluator());
        String configDir = new File(((AudioCodesModel) getModel()).getConfigDirectory(),
                getModel().getModelDir()).getAbsolutePath();
        GatewayDirectorySetter gatewayDirectorySetter = new GatewayDirectorySetter(configDir);
        setting.acceptVisitor(gatewayDirectorySetter);
        return setting;
    }

    private void copyGatewayFiles(ProfileLocation location) {
        String name;
        Setting settings = getSettings();
        String sourceDir = new File(((AudioCodesModel) getModel()).getConfigDirectory(),
                getModel().getModelDir()).getAbsolutePath();
        for (String file : COPY_FILES) {
            name = settings.getSetting(file).getValue();
            getProfileGenerator().copy(location, sourceDir, name, name);
        }
    }

    protected void copyFiles(ProfileLocation location) {
        super.copyFiles(location);
        copyGatewayFiles(location);
    }

    private static class GatewayDirectorySetter extends AbstractSettingVisitor {
        private String m_gatewayDirectory;

        public GatewayDirectorySetter(String directory) {
            m_gatewayDirectory = directory;
        }

        public void visitSetting(Setting setting) {
            SettingType type = setting.getType();
            if (type instanceof FileSetting) {
                FileSetting fileType = (FileSetting) type;
                fileType.setDirectory(m_gatewayDirectory);
            }
        }
    }

    abstract int getMaxCalls();
}
