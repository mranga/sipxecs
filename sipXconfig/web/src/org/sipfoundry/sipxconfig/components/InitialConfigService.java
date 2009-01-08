/*
 *
 *
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 * $
 */
package org.sipfoundry.sipxconfig.components;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import org.apache.commons.io.IOUtils;
import org.apache.tapestry.IRequestCycle;
import org.apache.tapestry.engine.IEngineService;
import org.apache.tapestry.engine.ILink;
import org.apache.tapestry.services.ServiceConstants;
import org.apache.tapestry.util.ContentType;
import org.apache.tapestry.web.WebResponse;
import org.sipfoundry.sipxconfig.admin.commserver.InitialConfig;

public class InitialConfigService implements IEngineService {
    public static final String SERVICE_NAME = "initial-config";
    private WebResponse m_response;
    private InitialConfig m_initialConfig;

    public String getName() {
        return SERVICE_NAME;
    }

    public void service(IRequestCycle cycle) throws IOException {

        String[] parameterValues = cycle.getParameters(ServiceConstants.PARAMETER);
        if (parameterValues == null || parameterValues.length > 1) {
            return;
        }
        String location = parameterValues[0];
        m_response.setHeader("Expires", "0");
        m_response.setHeader("Cache-Control", "must-revalidate, post-check=0, pre-check=0");
        m_response.setHeader("Pragma", "public");
        m_response.setHeader("Content-Disposition", "attachment; filename=\"" + location + ".tar.gz"
                + "\"");

        OutputStream responseOutputStream = m_response.getOutputStream(new ContentType(
                "literal:tar/x-gzip"));
        InputStream stream = m_initialConfig.getArchiveStream(location);
        IOUtils.copy(stream, responseOutputStream);
        m_initialConfig.deleteInitialConfigDirectory();

    }

    public ILink getLink(boolean post, Object parameter) {

        return null;
    }

    public void setResponse(WebResponse response) {
        m_response = response;
    }

    public void setInitialConfig(InitialConfig initialConfig) {
        m_initialConfig = initialConfig;
    }

}
