/*
 * 
 * 
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.  
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 * 
 * $
 */
package org.sipfoundry.sipxconfig.paging;

import java.util.Collection;
import java.util.List;

import org.sipfoundry.sipxconfig.admin.dialplan.DialingRuleProvider;

public interface PagingContext extends DialingRuleProvider {
    static final String CONTEXT_BEAN_NAME = "pagingContext";
    
    String getAudioDirectory();
      
    String getPagingPrefix();

    List<PagingGroup> getPagingGroups();

    PagingGroup getPagingGroupById(Integer id);

    void deletePagingGroupsById(Collection<Integer> groupsIds);
    
    void savePagingPrefix(String prefix);
    
    void savePagingGroup(PagingGroup group);
    
    void clear();
}