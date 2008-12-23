/*
 *
 *
 * Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 *
 * $
 */
package org.sipfoundry.sipxconfig.admin.dialplan.config;

import java.io.InputStream;
import java.util.Arrays;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.apache.commons.io.IOUtils;
import org.apache.commons.lang.ArrayUtils;
import org.apache.commons.lang.StringUtils;
import org.custommonkey.xmlunit.XMLTestCase;
import org.custommonkey.xmlunit.XMLUnit;
import org.dom4j.Document;
import org.easymock.EasyMock;
import org.sipfoundry.sipxconfig.admin.dialplan.CallDigits;
import org.sipfoundry.sipxconfig.admin.dialplan.CallPattern;
import org.sipfoundry.sipxconfig.admin.dialplan.CustomDialingRule;
import org.sipfoundry.sipxconfig.admin.dialplan.DialPattern;
import org.sipfoundry.sipxconfig.admin.dialplan.IDialingRule;
import org.sipfoundry.sipxconfig.domain.Domain;
import org.sipfoundry.sipxconfig.domain.DomainManager;
import org.sipfoundry.sipxconfig.gateway.Gateway;
import org.sipfoundry.sipxconfig.service.SipxPageService;
import org.sipfoundry.sipxconfig.service.SipxParkService;
import org.sipfoundry.sipxconfig.service.SipxRlsService;
import org.sipfoundry.sipxconfig.service.SipxServiceManager;
import org.sipfoundry.sipxconfig.setting.Group;
import org.sipfoundry.sipxconfig.test.TestUtil;

import static org.easymock.EasyMock.createMock;
import static org.easymock.EasyMock.createStrictMock;
import static org.easymock.EasyMock.expectLastCall;
import static org.easymock.EasyMock.replay;
import static org.easymock.EasyMock.verify;
import static org.sipfoundry.sipxconfig.XmlUnitHelper.asString;
import static org.sipfoundry.sipxconfig.XmlUnitHelper.assertElementInNamespace;
import static org.sipfoundry.sipxconfig.XmlUnitHelper.setNamespaceAware;

public class FallbackRulesTest extends XMLTestCase {

    // Object Under Test
    private FallbackRules m_out;

    public FallbackRulesTest() {
        setNamespaceAware(false);
        XMLUnit.setIgnoreWhitespace(true);
    }

    @Override
    public void setUp() {
        m_out = new FallbackRules();

        DomainManager domainManager = EasyMock.createMock(DomainManager.class);
        domainManager.getDomain();
        EasyMock.expectLastCall().andReturn(new Domain("example.org")).anyTimes();
        EasyMock.replay(domainManager);

        m_out.setDomainManager(domainManager);

        SipxParkService parkService = new SipxParkService();
        parkService.setModelId(SipxParkService.BEAN_ID);
        parkService.setParkServerSipPort("9905");

        SipxRlsService rlsService = new SipxRlsService();
        rlsService.setRlsPort("9906");
        rlsService.setModelId(SipxRlsService.BEAN_ID);

        SipxPageService pageService = new SipxPageService() {
            @Override
            public String getSipPort() {
                return "9910";
            }
        };
        pageService.setModelId(SipxPageService.BEAN_ID);

        SipxServiceManager sipxServiceManager = TestUtil.getMockSipxServiceManager(parkService, rlsService,
                pageService);
        EasyMock.replay(sipxServiceManager);
        m_out.setSipxServiceManager(sipxServiceManager);
    }

    public void testGenerateRuleWithGateways() throws Exception {
        FullTransform t1 = new FullTransform();
        t1.setUser("333");
        t1.setHost("10.1.1.14");
        t1.setFieldParams("Q=0.97");

        IDialingRule rule = createStrictMock(IDialingRule.class);
        rule.isInternal();
        expectLastCall().andReturn(false);
        rule.getHostPatterns();
        expectLastCall().andReturn(ArrayUtils.EMPTY_STRING_ARRAY);
        rule.getName();
        expectLastCall().andReturn("my test name");
        rule.getDescription();
        expectLastCall().andReturn("my test description");
        rule.getPatterns();
        expectLastCall().andReturn(array("x."));
        rule.getSiteTransforms();
        Map<String, List< ? extends Transform>> siteMap = new HashMap<String, List< ? extends Transform>>();
        siteMap.put(StringUtils.EMPTY, Arrays.asList(t1));
        expectLastCall().andReturn(siteMap);

        replay(rule);

        m_out.begin();
        m_out.generate(rule);
        m_out.end();
        m_out.localizeDocument(TestUtil.createDefaultLocation());

        Document document = m_out.getDocument();

        assertElementInNamespace(document.getRootElement(),
                "http://www.sipfoundry.org/sipX/schema/xml/fallback-00-00");

        String domDoc = asString(document);
        InputStream referenceXmlStream = getClass().getResourceAsStream("fallbackrules.test.xml");
        assertEquals(IOUtils.toString(referenceXmlStream), domDoc);

        verify(rule);
    }

    public void testGenerateRuleWithGatewaysAndSite() throws Exception {
        Transform t1 = createTransform("444", "montreal.example.org", "q=0.95");
        Transform t2 = createTransform("9444", "lisbon.example.org", "q=0.95");

        Map<String, List<Transform>> siteTr = new LinkedHashMap<String, List<Transform>>();
        siteTr.put("Montreal", Arrays.asList(t1));
        siteTr.put("Lisbon", Arrays.asList(t2));

        IDialingRule rule = createStrictMock(IDialingRule.class);
        rule.isInternal();
        expectLastCall().andReturn(false);
        rule.getHostPatterns();
        expectLastCall().andReturn(ArrayUtils.EMPTY_STRING_ARRAY);
        rule.getName();
        expectLastCall().andReturn("my test name");
        rule.getDescription();
        expectLastCall().andReturn("my test description");
        rule.getPatterns();
        expectLastCall().andReturn(array("x."));
        rule.getSiteTransforms();
        expectLastCall().andReturn(siteTr);

        replay(rule);

        m_out.begin();
        m_out.generate(rule);
        m_out.end();
        m_out.localizeDocument(TestUtil.createDefaultLocation());

        Document document = m_out.getDocument();
        assertElementInNamespace(document.getRootElement(),
                "http://www.sipfoundry.org/sipX/schema/xml/fallback-00-00");

        String domDoc = asString(document);

        InputStream referenceXmlStream = getClass().getResourceAsStream("fallbackrules-sites.test.xml");
        assertEquals(IOUtils.toString(referenceXmlStream), domDoc);

        verify(rule);
    }

    private Transform createTransform(String user, String host, String q) {
        FullTransform t1 = new FullTransform();
        t1.setUser(user);
        t1.setHost(host);
        t1.setFieldParams(q);
        return t1;
    }

    public void testGenerateRuleWithGatewaysAndShared() throws Exception {
        Group montrealSite = new Group();
        montrealSite.setName("Montreal");

        Group lisbonSite = new Group();
        lisbonSite.setName("Lisbon");

        Gateway montreal = new Gateway();
        montreal.setUniqueId();
        montreal.setAddress("montreal.example.org");
        montreal.setSite(montrealSite);

        Gateway lisbon = new Gateway();
        lisbon.setUniqueId();
        lisbon.setAddress("lisbon.example.org");
        lisbon.setSite(lisbonSite);
        lisbon.setShared(true);
        lisbon.setPrefix("9");

        Gateway shared = new Gateway();
        shared.setUniqueId();
        shared.setAddress("example.org");
        shared.setPrefix("8");

        CustomDialingRule rule = new CustomDialingRule();
        rule.setName("my test name");
        rule.setDescription("my test description");
        rule.addGateway(shared);
        rule.addGateway(montreal);
        rule.addGateway(lisbon);
        rule.setCallPattern(new CallPattern("444", CallDigits.NO_DIGITS));
        rule.setDialPatterns(Arrays.asList(new DialPattern("x", DialPattern.VARIABLE_DIGITS)));

        m_out.begin();
        m_out.generate(rule);
        m_out.end();
        m_out.localizeDocument(TestUtil.createDefaultLocation());

        Document document = m_out.getDocument();

        assertElementInNamespace(document.getRootElement(),
                "http://www.sipfoundry.org/sipX/schema/xml/fallback-00-00");

        String domDoc = asString(document);

        InputStream referenceXmlStream = getClass().getResourceAsStream("fallbackrules-shared-gateway.test.xml");
        assertEquals(IOUtils.toString(referenceXmlStream), domDoc);
    }

    public void testGenerateRuleWithoutGateways() throws Exception {
        IDialingRule rule = createMock(IDialingRule.class);
        rule.isInternal();
        expectLastCall().andReturn(true);
        replay(rule);

        m_out.begin();
        m_out.generate(rule);
        m_out.end();
        m_out.localizeDocument(TestUtil.createDefaultLocation());

        Document document = m_out.getDocument();

        assertElementInNamespace(document.getRootElement(),
                "http://www.sipfoundry.org/sipX/schema/xml/fallback-00-00");

        String domDoc = asString(document);

        InputStream referenceXmlStream = getClass().getResourceAsStream("fallbackrules-no-gateway.test.xml");
        assertEquals(IOUtils.toString(referenceXmlStream), domDoc);
        verify(rule);
    }

    private static <T> T[] array(T... items) {
        return items;
    }
}
