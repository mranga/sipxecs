/*
 *
 *
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 * $
 */
package org.sipfoundry.sipxconfig.device;

import java.util.Comparator;
import java.util.HashSet;
import java.util.Set;

import org.apache.commons.lang.ArrayUtils;
import org.apache.commons.lang.StringUtils;

public abstract class DeviceDescriptor {

    public static final LabelComparator LABEL_COMPARATOR = new LabelComparator();

    private String m_modelFilePath;

    private String m_beanId;

    private String m_label;

    private String m_modelId;

    private String m_vendor;

    private Set<String> m_supportedFeatures = new HashSet<String>();

    /**
     * Directory under 'etc/sipxpbx' directory where model files and templates are kept
     */
    private String m_modelDir;

    /**
     * Names of the files that should be copied from model directory to profile location during
     * profile generation
     */
    private String[] m_staticProfileFilenames = ArrayUtils.EMPTY_STRING_ARRAY;

    private ProfileLocation m_defaultProfileLocation;

    /**
     * By default we accept MAC address as serial number. Device plugin developers can define
     * other regular expressions to accept serial number in a format specific for a device.
     */
    private String m_serialNumberPattern = "^[a-f\\d]{12}$";

    private DeviceVersion[] m_versions = new DeviceVersion[0];

    private String m_profileTemplate;

    /**
     * Downloadable files or utilities - used by UI
     */
    private Resource[] m_resources;

    public DeviceDescriptor() {
    }

    public DeviceDescriptor(String beanId) {
        setBeanId(beanId);
    }

    public DeviceDescriptor(String beanId, String modelId) {
        this(beanId);
        setModelId(modelId);
    }

    public void setBeanId(String beanId) {
        m_beanId = beanId;
    }

    public void setLabel(String label) {
        m_label = label;
    }

    public void setModelId(String modelId) {
        m_modelId = modelId;
    }

    /**
     * Spring bean name, NOTE: You cannot reuse java class to multiple spring beans. One
     * class/bean but a spring bean can handle multiple models
     */
    public String getBeanId() {
        return m_beanId;
    }

    /**
     * User identifiable label for this model
     */
    public String getLabel() {
        return m_label;
    }

    public String getModelId() {
        return m_modelId;
    }

    /**
     * If non-empty pattern is specified than assume serial number is needed.
     */
    public boolean getHasSerialNumber() {
        return StringUtils.isNotBlank(getSerialNumberPattern());
    }

    public void setVersions(DeviceVersion[] versions) {
        m_versions = versions;
    }

    public DeviceVersion[] getVersions() {
        return m_versions;
    }

    public String getSerialNumberPattern() {
        return m_serialNumberPattern;
    }

    public void setSerialNumberPattern(String serialNumberPattern) {
        m_serialNumberPattern = serialNumberPattern;
    }

    public void setSupportedFeatures(Set<String> supportedFeatures) {
        m_supportedFeatures = supportedFeatures;
    }

    /**
     * This function is called to transform serial number entered by the user to the format that
     * is accepted by sipXconfig. It is called before the serial number is verified against
     * regular expression pattern.
     *
     * @param raw serial number as entered by the user or
     * @return cleaned serial number
     */
    public String cleanSerialNumber(String raw) {
        if (raw == null) {
            return null;
        }
        String clean = raw.toLowerCase();
        // remove spaces, dashes and colons
        return clean.replaceAll("[-:\\s]*", "");
    }

    /**
     * @return File name with upload settings describing files to upload. Relative to /etc/sipxpbx
     */
    public String getModelFilePath() {
        return m_modelFilePath;
    }

    public void setModelFilePath(String modelFilePath) {
        m_modelFilePath = modelFilePath;
    }

    /**
     * When loading the settings model.
     *
     * Example If you can add "not-extinct" the following setting will not be loaded. Phone model
     * id and version are added by default.
     *
     * &lt;setting name="dinosaur" unless="not-extinct"/&gt;
     */
    public Set<String> getDefinitions() {
        Set<String> definitions = new HashSet<String>();
        definitions.add(getModelId());
        definitions.addAll(m_supportedFeatures);
        return definitions;
    }

    public void setDefaultProfileLocation(ProfileLocation defaultProfileLocation) {
        m_defaultProfileLocation = defaultProfileLocation;
    }

    public ProfileLocation getDefaultProfileLocation() {
        return m_defaultProfileLocation;
    }

    /**
     * Return the name of the directory (under etc)
     *
     * For most models model files are located in the directory identified by bean_id. Howewer it
     * is possible have multiple bean_ids sharing model in the same directory.
     */
    public String getModelDir() {
        if (m_modelDir != null) {
            return m_modelDir;
        }
        return m_beanId;
    }

    public void setModelDir(String modelDir) {
        m_modelDir = modelDir;
    }

    public String[] getStaticProfileNames() {
        return m_staticProfileFilenames;
    }

    public void setStaticProfileFilenames(String[] staticProfileFilenames) {
        m_staticProfileFilenames = staticProfileFilenames;
    }

    public void setProfileTemplate(String profileTemplate) {
        m_profileTemplate = profileTemplate;
    }

    public String getProfileTemplate() {
        return m_profileTemplate;
    }

    public Resource[] getResources() {
        return m_resources;
    }

    public void setResources(Resource[] resources) {
        m_resources = resources;
    }

    public String getVendor() {
        return m_vendor;
    }

    public void setVendor(String vendor) {
        m_vendor = vendor;
    }

    private static class LabelComparator implements Comparator<DeviceDescriptor> {
        public int compare(DeviceDescriptor o1, DeviceDescriptor o2) {
            if (o1 == null && o2 == null) {
                return 0;
            } else if (o1 == null) {
                return -1;
            } else if (o2 == null) {
                return 1;
            } else {
                return o1.getLabel().compareToIgnoreCase(o2.getLabel());
            }
        }
    }
}
