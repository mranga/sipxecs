/*
 * 
 * 
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.  
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 * 
 * $
 */
package org.sipfoundry.sipxconfig.site.admin;

import java.util.Collection;
import java.util.List;

import org.apache.commons.lang.StringUtils;
import org.apache.tapestry.IPage;
import org.apache.tapestry.IRequestCycle;
import org.apache.tapestry.annotations.Bean;
import org.apache.tapestry.annotations.InjectObject;
import org.apache.tapestry.event.PageBeginRenderListener;
import org.apache.tapestry.event.PageEvent;
import org.apache.tapestry.html.BasePage;
import org.apache.tapestry.valid.ValidatorException;
import org.sipfoundry.sipxconfig.components.SelectMap;
import org.sipfoundry.sipxconfig.components.SipxValidationDelegate;
import org.sipfoundry.sipxconfig.components.TapestryUtils;
import org.sipfoundry.sipxconfig.paging.PagingContext;
import org.sipfoundry.sipxconfig.paging.PagingGroup;

public abstract class PagingGroupsPage extends BasePage implements PageBeginRenderListener {
    public static final String PAGE = "admin/PagingGroupsPage";

    @InjectObject(value = "spring:pagingContext")
    public abstract PagingContext getPagingContext();

    @Bean
    public abstract SelectMap getSelections();

    @Bean
    public abstract SipxValidationDelegate getValidator();

    public abstract void setGroups(List<PagingGroup> groups);

    public abstract List<PagingGroup> getGroups();

    public abstract void setCurrentRow(PagingGroup group);

    public abstract Collection getSelectedRows();

    public abstract void setPrefix(String prefix);

    public abstract String getPrefix();

    public IPage addPagingGroup(IRequestCycle cycle) {
        EditPagingGroupPage page = (EditPagingGroupPage) cycle.getPage(EditPagingGroupPage.PAGE);
        page.addPagingGroup(getPage().getPageName());
        return page;
    }

    public IPage editPagingGroup(IRequestCycle cycle, Integer groupId) {
        EditPagingGroupPage page = (EditPagingGroupPage) cycle.getPage(EditPagingGroupPage.PAGE);
        page.editPagingGroup(groupId, getPage().getPageName());
        return page;
    }

    public void pageBeginRender(PageEvent event) {
        // load paging prefix
        if (StringUtils.isEmpty(getPrefix())) {
            String prefix = getPagingContext().getPagingPrefix();
            setPrefix(prefix);
        }

        // load paging groups
        List<PagingGroup> groups = getPagingContext().getPagingGroups();
        setGroups(groups);
    }

    public void savePagingPrefix() {
        String prefix = getPrefix();
        if (StringUtils.isEmpty(prefix)) {
            TapestryUtils.getValidator(getPage()).record(
                    new ValidatorException(getMessages().getMessage("error.pagingPrefix")));
            return;
        }

        List<PagingGroup> groups = getGroups();
        if (groups.size() > 0) {
            getPagingContext().savePagingPrefix(prefix);
        }
    }

    public void delete() {
        Collection ids = getSelections().getAllSelected();
        if (ids.isEmpty()) {
            return;
        }
        getPagingContext().deletePagingGroupsById(ids);
    }

    public Collection getAllSelected() {
        return getSelections().getAllSelected();
    }
}