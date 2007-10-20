/*
 * 
 * 
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.  
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 * 
 * $
 */
package org.sipfoundry.sipxconfig.admin.localization;

import java.io.InputStream;

public interface LocalizationContext {
    public String getCurrentRegionId();

    public String getCurrentLanguageId();

    public String[] getInstalledRegions();

    public String[] getInstalledLanguages();

    public Localization getLocalization();

    public int updateRegion(String region);

    public int updateLanguage(String language);

    public int installLocalizationPackage(InputStream stream, String name);
}
