/*
 * 
 * 
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.  
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 * 
 * $
 */
package org.sipfoundry.sipxconfig.site.upload;

import java.util.Collection;

import org.apache.tapestry.IPage;
import org.apache.tapestry.annotations.Bean;
import org.apache.tapestry.annotations.InjectObject;
import org.apache.tapestry.annotations.InjectPage;
import org.apache.tapestry.event.PageBeginRenderListener;
import org.apache.tapestry.event.PageEvent;
import org.apache.tapestry.html.BasePage;
import org.apache.tapestry.valid.ValidatorException;
import org.sipfoundry.sipxconfig.common.DaoUtils;
import org.sipfoundry.sipxconfig.components.SelectMap;
import org.sipfoundry.sipxconfig.components.SipxValidationDelegate;
import org.sipfoundry.sipxconfig.components.TapestryContext;
import org.sipfoundry.sipxconfig.device.ModelSource;
import org.sipfoundry.sipxconfig.upload.Upload;
import org.sipfoundry.sipxconfig.upload.UploadManager;
import org.sipfoundry.sipxconfig.upload.UploadSpecification;

public abstract class ManageUploads extends BasePage implements PageBeginRenderListener {

    public static final String PAGE = "upload/ManageUploads";

    @InjectObject(value = "spring:tapestry")
    public abstract TapestryContext getTapestry();

    @InjectObject(value = "spring:uploadManager")
    public abstract UploadManager getUploadManager();

    @InjectObject(value = "spring:uploadSpecificationSource")
    public abstract ModelSource<UploadSpecification> getUploadSpecificationSource();

    @InjectPage(value = EditUpload.PAGE)
    public abstract EditUpload getEditUploadPage();

    @Bean
    public abstract SipxValidationDelegate getValidator();

    @Bean
    public abstract SelectMap getSelections();

    public abstract Upload getCurrentRow();

    public abstract void setUpload(Collection upload);

    public abstract Collection getUpload();

    public abstract UploadSpecification getSelectedSpecification();

    public IPage editUpload(Integer uploadId) {
        EditUpload page = getEditUploadPage();
        page.setUploadId(uploadId);
        page.setReturnPage(PAGE);
        return page;
    }

    public IPage addUpload() {
        if (getSelectedSpecification() == null) {
            return null;
        }

        EditUpload page = getEditUploadPage();
        page.setUploadId(null);
        page.setUploadSpecification(getSelectedSpecification());
        page.setReturnPage(PAGE);
        return page;
    }

    public void deleteUpload() {
        getUploadManager().deleteUploads(getSelections().getAllSelected());
        // force reload
        setUpload(null);
    }

    public void activate() {
        setDeployed(true);
    }

    public void inactivate() {
        setDeployed(false);
    }

    private void setDeployed(boolean deploy) {
        Upload[] uploads = DaoUtils.loadBeansArrayByIds(getUploadManager(), Upload.class,
                getSelections().getAllSelected());
        for (int i = 0; i < uploads.length; i++) {
            if (deploy) {
                if (getUploadManager().isActiveUploadById(uploads[i].getSpecification())) {
                    getValidator().record(new ValidatorException(getMessages().getMessage("error.alreadyActivated")));
                } else {
                    getUploadManager().deploy(uploads[i]);
                }
            } else {
                getUploadManager().undeploy(uploads[i]);
            }
        }
        // force reload
        setUpload(null);
    }

    /** stub: side-effect of PageBeginRenderListener */
    public void pageBeginRender(PageEvent event_) {
        if (getUpload() == null) {
            setUpload(getUploadManager().getUpload());
        }
    }
}
