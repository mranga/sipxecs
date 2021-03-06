/*
 * 
 * 
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.  
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 * 
 * $
 */
package org.sipfoundry.sipxconfig.upload;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import org.apache.commons.io.FileUtils;
import org.apache.commons.io.IOUtils;
import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.sipfoundry.sipxconfig.device.ModelSource;
import org.sipfoundry.sipxconfig.setting.AbstractSettingVisitor;
import org.sipfoundry.sipxconfig.setting.BeanWithSettings;
import org.sipfoundry.sipxconfig.setting.Setting;
import org.sipfoundry.sipxconfig.setting.type.FileSetting;
import org.sipfoundry.sipxconfig.setting.type.SettingType;

/**
 * Describing the files required to track and manage a vendor's firmware files
 */
public class Upload extends BeanWithSettings {
    private static final Log LOG = LogFactory.getLog(Upload.class);
    private String m_name;
    private String m_description;
    private UploadSpecification m_specification;
    private String m_specificationId;
    private String m_beanId;
    private String m_uploadRootDirectory;
    private String m_destinationDirectory;
    private boolean m_deployed;
    private String m_directoryId;
    private ModelSource<UploadSpecification> m_specificationSource;

    public Upload() {
    }

    protected Upload(String beanId) {
        m_beanId = beanId;
    }

    public Upload(UploadSpecification specification) {
        m_beanId = specification.getBeanId();
        m_specification = specification;
    }
    
    void setDirectoryId(String directory) {
        m_directoryId = directory;
    }
    
    String getDirectoryId() {
        return m_directoryId != null ? m_directoryId : getId().toString();
    }

    public boolean isDeployed() {
        return m_deployed;
    }

    /**
     * Should only be called by DB marshalling and subclasses. See deploy and undeploy 
     */
    public void setDeployed(boolean deployed) {
        m_deployed = deployed;        
    }

    public UploadSpecification getSpecification() {
        if (m_specification != null) {
            return m_specification;
        }
        if (m_specificationId == null) {
            throw new IllegalStateException("Model ID not set");
        }
        if (m_specificationSource == null) {
            throw new IllegalStateException("ModelSource not set");
        }
        m_specification = m_specificationSource.getModel(m_specificationId);
        return m_specification;
    }

    public String getBeanId() {
        return m_beanId;
    }

    /**
     * Internal, do not call this method. Hibnerate property declared update=false, but still
     * required method be defined.
     */
    @SuppressWarnings("unused")    
    public void setBeanId(String illegal_) {
    }

    public String getSpecificationId() {
        return m_specificationId;
    }
    
    public void setSpecification(UploadSpecification specification) {
        m_specification = specification;
    }
    
    public void setSpecificationId(String specificationId) {        
        m_specificationId = specificationId;
        m_specification = null;
    }

    @Override
    protected Setting loadSettings() {
        String modelFile = getSpecification().getModelFilePath();
        Setting settings = getModelFilesContext().loadModelFile(modelFile);

        // Hack, bean id should be valid 
        settings.acceptVisitor(new UploadDirectorySetter());        
        
        return settings;
    }

    private class UploadDirectorySetter extends AbstractSettingVisitor {
        public void visitSetting(Setting setting) {
            SettingType type = setting.getType();
            if (type instanceof FileSetting) {
                FileSetting fileType = (FileSetting) type;
                fileType.setDirectory(getUploadDirectory());
            }
        }
    }
    
    private class FileDeployer extends AbstractSettingVisitor {
        public void visitSetting(Setting setting) {
            SettingType type = setting.getType();
            if (type instanceof FileSetting) {
                String filename = setting.getValue();
                if (filename != null) {
                    deployFile(filename);
                }
            }
        }
    }

    private class FileUndeployer extends AbstractSettingVisitor {
        public void visitSetting(Setting setting) {
            SettingType type = setting.getType();
            if (type instanceof FileSetting) {
                String filename = setting.getValue();
                if (filename != null) {
                    File f = new File(getDestinationDirectory(), filename);
                    f.delete();
                }
            }
        }
    }
    
    private void deployFile(String file) {
        InputStream from;
        try {
            from = new FileInputStream(new File(getUploadDirectory(), file));
            File destDir = new File(getDestinationDirectory());
            destDir.mkdirs();
            OutputStream to = new FileOutputStream(new File(destDir, file));
            IOUtils.copy(from, to);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }
    
            
    public String getName() {
        return m_name;
    }

    public void setName(String name) {
        m_name = name;
    }

    public String getDescription() {
        return m_description;
    }
    
    public void setDescription(String description) {
        m_description = description;
    }
    
    public void setDestinationDirectory(String destinationDirectory) {
        m_destinationDirectory = destinationDirectory;
    }
    
    public String getDestinationDirectory() {
        return m_destinationDirectory;
    }

    /**
     * delete all files
     */       
    public void remove() {
        undeploy();
        File uploadDirectory = new File(getUploadDirectory());
        try {
            FileUtils.deleteDirectory(uploadDirectory);
        } catch (IOException cantDelete) {
            LOG.error("Could not remove uploaded files", cantDelete);
        }
    }
    
    public void setUploadRootDirectory(String uploadDirectory) {
        m_uploadRootDirectory = uploadDirectory;
    }
    
    public String getUploadDirectory() {
        return m_uploadRootDirectory + '/' + getDirectoryId();
    }

    public void deploy() {
        getSettings().acceptVisitor(new FileDeployer());        
        m_deployed = true;
    }
    
    public void undeploy() {
        getSettings().acceptVisitor(new FileUndeployer());        
        m_deployed = false;
    }

    public ModelSource<UploadSpecification> getUploadSpecificationSource() {
        return m_specificationSource;
    }

    public void setUploadSpecificationSource(ModelSource<UploadSpecification> specificationSource) {
        m_specificationSource = specificationSource;
    }
}
