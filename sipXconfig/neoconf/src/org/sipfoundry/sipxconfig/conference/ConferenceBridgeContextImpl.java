/*
 *
 *
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 * $
 */
package org.sipfoundry.sipxconfig.conference;

import java.io.Serializable;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Set;

import org.hibernate.Criteria;
import org.hibernate.Session;
import org.hibernate.criterion.Order;
import org.hibernate.criterion.Projections;
import org.hibernate.criterion.Restrictions;
import org.sipfoundry.sipxconfig.admin.ExtensionInUseException;
import org.sipfoundry.sipxconfig.admin.NameInUseException;
import org.sipfoundry.sipxconfig.admin.commserver.Location;
import org.sipfoundry.sipxconfig.alias.AliasManager;
import org.sipfoundry.sipxconfig.common.BeanId;
import org.sipfoundry.sipxconfig.common.CoreContext;
import org.sipfoundry.sipxconfig.common.SipUri;
import org.sipfoundry.sipxconfig.common.SipxCollectionUtils;
import org.sipfoundry.sipxconfig.common.User;
import org.sipfoundry.sipxconfig.common.UserException;
import org.sipfoundry.sipxconfig.common.event.DaoEventListener;
import org.sipfoundry.sipxconfig.domain.DomainManager;
import org.sipfoundry.sipxconfig.service.LocationSpecificService;
import org.sipfoundry.sipxconfig.service.SipxFreeswitchService;
import org.sipfoundry.sipxconfig.service.SipxService;
import org.springframework.beans.factory.BeanFactory;
import org.springframework.beans.factory.BeanFactoryAware;
import org.springframework.dao.support.DataAccessUtils;
import org.springframework.orm.hibernate3.HibernateCallback;
import org.springframework.orm.hibernate3.HibernateTemplate;
import org.springframework.orm.hibernate3.support.HibernateDaoSupport;

public class ConferenceBridgeContextImpl extends HibernateDaoSupport implements BeanFactoryAware,
        ConferenceBridgeContext, DaoEventListener {
    private static final String CONFERENCE = "conference";
    private static final String VALUE = "value";
    private static final String CONFERENCE_IDS_WITH_ALIAS = "conferenceIdsWithAlias";
    private static final String CONFERENCE_BY_NAME = "conferenceByName";
    private static final String OWNER = "owner";

    private AliasManager m_aliasManager;
    private BeanFactory m_beanFactory;
    private ConferenceBridgeProvisioning m_provisioning;
    private CoreContext m_coreContext;
    private DomainManager m_domainManager;

    public List getBridges() {
        return getHibernateTemplate().loadAll(Bridge.class);
    }

    public void store(Bridge bridge) {
        getHibernateTemplate().saveOrUpdate(bridge);
        if (bridge.isNew()) {
            // need to make sure that ID is set
            getHibernateTemplate().flush();
        }
        m_provisioning.deploy(bridge.getId());
    }

    public void store(Conference conference) {
        validate(conference);
        getHibernateTemplate().saveOrUpdate(conference);
        m_provisioning.deploy(conference.getBridge().getId());
    }

    public void validate(Conference conference) {
        String name = conference.getName();
        String extension = conference.getExtension();
        if (name == null) {
            throw new UserException("A conference must have a name");
        }
        if (extension == null) {
            throw new UserException("A conference must have an extension");
        }

        final String conferenceTypeName = CONFERENCE;
        if (!m_aliasManager.canObjectUseAlias(conference, name)) {
            throw new NameInUseException(conferenceTypeName, name);
        }
        if (!m_aliasManager.canObjectUseAlias(conference, extension)) {
            throw new ExtensionInUseException(conferenceTypeName, extension);
        }
    }

    public Bridge newBridge() {
        return (Bridge) m_beanFactory.getBean(Bridge.BEAN_NAME, Bridge.class);
    }

    public Conference newConference() {
        Conference conference = (Conference) m_beanFactory.getBean(Conference.BEAN_NAME,
                Conference.class);
        conference.generateAccessCodes();
        return conference;
    }

    public void removeBridges(Collection bridgesIds) {
        List bridges = new ArrayList(bridgesIds.size());
        for (Iterator i = bridgesIds.iterator(); i.hasNext();) {
            Serializable id = (Serializable) i.next();
            Bridge bridge = loadBridge(id);
            bridges.add(bridge);
        }
        getHibernateTemplate().deleteAll(bridges);
    }

    public void removeConferences(Collection conferencesIds) {
        Set bridges = new HashSet();
        for (Iterator i = conferencesIds.iterator(); i.hasNext();) {
            Serializable id = (Serializable) i.next();
            Conference conference = loadConference(id);
            Bridge bridge = conference.getBridge();
            bridge.removeConference(conference);
            bridges.add(bridge);
        }
        getHibernateTemplate().saveOrUpdateAll(bridges);
    }

    public Bridge loadBridge(Serializable id) {
        return (Bridge) getHibernateTemplate().load(Bridge.class, id);
    }


    public Bridge getBridgeByServer(String hostname) {
          // TODO JPA This is temporarily commented out until I can figure out why loading the object in this way
          // does not load dependent objects like the service and location...
//        List<Bridge> bridges = getHibernateTemplate().findByNamedQueryAndNamedParam(
//                "bridgeByHost", VALUE, hostname);
//        return (Bridge) DataAccessUtils.singleResult(bridges2);
        
        Bridge bridgeForServer = null;
        List<Bridge> bridges = getHibernateTemplate().loadAll(Bridge.class);
        for (Bridge b : bridges) {
            if (b.getService().getLocation().getFqdn().equalsIgnoreCase(hostname)) {
                bridgeForServer = b;
                break;
            }
        }
        
        return bridgeForServer;
    }

    public Conference loadConference(Serializable id) {
        return (Conference) getHibernateTemplate().load(Conference.class, id);
    }

    public Conference findConferenceByName(String name) {
        List<Conference> conferences = getHibernateTemplate().findByNamedQueryAndNamedParam(
                CONFERENCE_BY_NAME, VALUE, name);
        return (Conference) DataAccessUtils.singleResult(conferences);
    }

    public void clear() {
        List bridges = getBridges();
        getHibernateTemplate().deleteAll(bridges);
    }

    // trivial get/set
    public void setBeanFactory(BeanFactory beanFactory) {
        m_beanFactory = beanFactory;
    }

    public void setAliasManager(AliasManager aliasManager) {
        m_aliasManager = aliasManager;
    }

    public void setProvisioning(ConferenceBridgeProvisioning provisioning) {
        m_provisioning = provisioning;
    }

    public void setCoreContext(CoreContext coreContext) {
        m_coreContext = coreContext;
    }

    public void setDomainManager(DomainManager domainManager) {
        m_domainManager = domainManager;
    }

    public boolean isAliasInUse(String alias) {
        List confIds = getHibernateTemplate().findByNamedQueryAndNamedParam(
                CONFERENCE_IDS_WITH_ALIAS, VALUE, alias);
        return SipxCollectionUtils.safeSize(confIds) > 0;
    }

    public Collection getBeanIdsOfObjectsWithAlias(String alias) {
        Collection ids = getHibernateTemplate().findByNamedQueryAndNamedParam(
                CONFERENCE_IDS_WITH_ALIAS, VALUE, alias);
        Collection bids = BeanId.createBeanIdCollection(ids, Conference.class);
        return bids;
    }

    public Collection getAliasMappings() {
        List conferences = getHibernateTemplate().loadAll(Conference.class);
        final ArrayList list = new ArrayList();
        for (Iterator i = conferences.iterator(); i.hasNext();) {
            Conference conference = (Conference) i.next();
            list.addAll(conference.generateAliases(m_coreContext.getDomainName()));
        }
        return list;
    }

    public List<Conference> findConferencesByOwner(User owner) {
        List<Conference> conferences = getHibernateTemplate().findByNamedQueryAndNamedParam(
                "conferencesByOwner", OWNER, owner);
        return conferences;
    }

    private Criteria filterConferencesCriteria(final Integer bridgeId, final Integer  ownerGroupId, Session session) {
        Criteria criteria = session.createCriteria(Conference.class);
        criteria.createCriteria("bridge", "b").
            add(Restrictions.eq("b.id", bridgeId));
        if (ownerGroupId != null) {
            criteria.createCriteria(OWNER, "o").
                createCriteria("groups" , "g").
                    add(Restrictions.eq("g.id", ownerGroupId));
        }
        return criteria;
    }

    public List<Conference> filterConferences(final Integer bridgeId, final Integer  ownerGroupId) {
        HibernateCallback callback = new HibernateCallback() {
            public Object doInHibernate(Session session) {
                Criteria criteria = filterConferencesCriteria(bridgeId, ownerGroupId, session);
                return criteria.list();
            }
        };
        return getHibernateTemplate().executeFind(callback);
    }

    public int countFilterConferences(final Integer bridgeId, final Integer  ownerGroupId) {
        HibernateCallback callback = new HibernateCallback() {
            public Object doInHibernate(Session session) {
                Criteria criteria = filterConferencesCriteria(bridgeId, ownerGroupId, session);
                criteria.setProjection(Projections.rowCount());
                List results = criteria.list();
                return results;
            }
        };
        List list = getHibernateTemplate().executeFind(callback);
        Integer count = (Integer) list.get(0);
        return count.intValue();
    }

    public List<Conference> filterConferencesByPage(final Integer bridgeId, final Integer  ownerGroupId,
        final int firstRow, final int pageSize, final String[] orderBy, final boolean orderAscending) {

        HibernateCallback callback = new HibernateCallback() {
            public Object doInHibernate(Session session) {
                Criteria criteria = filterConferencesCriteria(bridgeId, ownerGroupId, session);
                criteria.setFirstResult(firstRow);
                criteria.setMaxResults(pageSize);
                for (int i = 0; i < orderBy.length; i++) {
                    Order order = orderAscending ? Order.asc(orderBy[i]) : Order.desc(orderBy[i]);
                    criteria.addOrder(order);
                }
                return criteria.list();
            }
        };
        return getHibernateTemplate().executeFind(callback);
    }

    public String getAddressSpec(Conference conference) {
        String domain = m_domainManager.getDomain().getName();
        return SipUri.fix(conference.getExtension(), domain);
    }

    private void onLocationSpecificServiceDelete(LocationSpecificService locationService) {
        SipxService service = locationService.getSipxService();
        if (service instanceof SipxFreeswitchService) {
            Bridge bridge = getBridgeForLocationId(locationService.getLocation().getId());
            if (bridge != null) {
                getHibernateTemplate().delete(bridge);
            }
        }
    }    
        
    private void onLocationDelete(Location location) {
        getHibernateTemplate().update(location);
        Bridge bridge = getBridgeForLocationId(location.getId());
        if (bridge != null) {
            getHibernateTemplate().delete(bridge);
        }
    }    
    
    public void onDelete(Object entity) {
        if (entity instanceof LocationSpecificService) {
            onLocationSpecificServiceDelete((LocationSpecificService) entity);
        } else if (entity instanceof Location) {
            onLocationDelete((Location) entity);
        }
    }

    public Bridge getBridgeForLocationId(Integer locationId) {
        HibernateTemplate hibernate = getHibernateTemplate();
        List<Bridge> servers = hibernate.findByNamedQueryAndNamedParam(
                "bridgeForLocationId", "locationId", locationId);

        return (Bridge) DataAccessUtils.singleResult(servers);
    }
    
    public void onSave(Object entity) { }
}
