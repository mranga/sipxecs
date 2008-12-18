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
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;

import junit.framework.JUnit4TestAdapter;

import org.apache.commons.io.IOUtils;
import org.custommonkey.xmlunit.XMLAssert;
import org.custommonkey.xmlunit.XMLUnit;
import org.dom4j.Document;
import org.dom4j.Element;
import org.easymock.EasyMock;
import org.easymock.IMocksControl;
import org.junit.Assert;
import org.junit.Test;
import org.sipfoundry.sipxconfig.XmlUnitHelper;
import org.sipfoundry.sipxconfig.admin.dialplan.CallDigits;
import org.sipfoundry.sipxconfig.admin.dialplan.CallPattern;
import org.sipfoundry.sipxconfig.admin.dialplan.CustomDialingRule;
import org.sipfoundry.sipxconfig.admin.dialplan.DialPattern;
import org.sipfoundry.sipxconfig.admin.dialplan.IDialingRule;
import org.sipfoundry.sipxconfig.domain.Domain;
import org.sipfoundry.sipxconfig.domain.DomainManager;
import org.sipfoundry.sipxconfig.gateway.Gateway;
import org.sipfoundry.sipxconfig.permission.PermissionName;

public class AuthRulesTest {
    private static final int GATEWAYS_LEN = 5;

    public static junit.framework.Test suite() {
        return new JUnit4TestAdapter(AuthRulesTest.class);
      }

    public AuthRulesTest() {
        XmlUnitHelper.setNamespaceAware(false);
        XMLUnit.setIgnoreWhitespace(true);
    }

    @Test
    public void testGetDoc() throws Exception {
        AuthRules rules = new AuthRules();
        rules.begin();
        Document doc = rules.getDocument();

        String xml = XmlUnitHelper.asString(doc);
        XMLAssert.assertXMLEqual(
                "<mappings xmlns=\"http://www.sipfoundry.org/sipX/schema/xml/urlauth-00-00\"/>",
                xml);
    }

    @Test
    public void testGenerate() throws Exception {
        Gateway gateway = new Gateway();
        gateway.setUniqueId();
        gateway.setAddress("10.1.2.3");

        List gateways = new ArrayList();
        gateways.add(gateway);

        IMocksControl control = EasyMock.createControl();
        IDialingRule rule = control.createMock(IDialingRule.class);
        rule.getDescription();
        control.andReturn("test rule description");
        rule.getTransformedPatterns(gateway);
        control.andReturn(new String[] {
            "555", "666", "777"
        });
        rule.getPermissionNames();
        control.andReturn(Arrays.asList(new String[] {
            PermissionName.VOICEMAIL.getName()
        }));
        rule.getGateways();
        control.andReturn(gateways);
        rule.getName();
        control.andReturn("testrule");
        control.replay();

        MockAuthRules authRules = new MockAuthRules();
        authRules.begin();
        authRules.generate(rule);
        authRules.end();

        Document document = authRules.getDocument();
        String domDoc = XmlUnitHelper.asString(document);

        XMLAssert.assertXpathEvaluatesTo("test rule description", "/mappings/hostMatch/description", domDoc);
        XMLAssert.assertXpathEvaluatesTo(gateway.getGatewayAddress(), "/mappings/hostMatch/hostPattern",
                domDoc);
        XMLAssert.assertXpathEvaluatesTo("555", "/mappings/hostMatch/userMatch/userPattern", domDoc);
        XMLAssert.assertXpathEvaluatesTo("666", "/mappings/hostMatch/userMatch/userPattern[2]", domDoc);
        XMLAssert.assertXpathEvaluatesTo("777", "/mappings/hostMatch/userMatch/userPattern[3]", domDoc);
        XMLAssert.assertXpathEvaluatesTo("Voicemail",
                "/mappings/hostMatch/userMatch/permissionMatch/permission", domDoc);

        // check if generate no access has been called properly
        Assert.assertEquals(1, authRules.uniqueGateways);

        control.verify();
    }

    @Test
    public void testGenerateMultipleGateways() throws Exception {
        Gateway[] gateways = new Gateway[GATEWAYS_LEN];
        StringBuilder prefixBuilder = new StringBuilder();
        for (int i = 0; i < gateways.length; i++) {
            gateways[i] = new Gateway();
            gateways[i].setUniqueId();
            gateways[i].setAddress("10.1.2." + i);
            gateways[i].setAddressPort(i);
            gateways[i].setPrefix(prefixBuilder.toString());
            prefixBuilder.append("2");
        }

        IMocksControl control = EasyMock.createControl();
        IDialingRule rule = control.createMock(IDialingRule.class);
        rule.getGateways();
        control.andReturn(Arrays.asList(gateways));
        rule.getName();
        control.andReturn("testrule").times(gateways.length);
        rule.getDescription();
        control.andReturn(null).times(gateways.length);
        for (int i = 0; i < gateways.length; i++) {
            rule.getTransformedPatterns(gateways[i]);
            String prefix = gateways[i].getPrefix();
            control.andReturn(new String[] {
                prefix + "555", prefix + "666", prefix + "777"
            });
        }
        rule.getPermissionNames();
        control.andReturn(Arrays.asList(new String[] {
            PermissionName.VOICEMAIL.getName()
        }));
        control.replay();

        AuthRules authRules = new AuthRules();
        authRules.begin();
        authRules.generate(rule);
        authRules.end();

        Document document = authRules.getDocument();
        String domDoc = XmlUnitHelper.asString(document);

        String hostMatchFormat = "/mappings/hostMatch[%d]/";
        prefixBuilder = new StringBuilder();
        for (int i = 0; i < gateways.length; i++) {
            String hostMatch = String.format(hostMatchFormat, i + 1);
            XMLAssert.assertXpathEvaluatesTo(gateways[i].getGatewayAddress(), hostMatch + "hostPattern",
                    domDoc);

            String prefix = prefixBuilder.toString();
            XMLAssert.assertXpathEvaluatesTo(prefix + "555", hostMatch + "userMatch/userPattern", domDoc);
            XMLAssert.assertXpathEvaluatesTo(prefix + "666", hostMatch + "userMatch/userPattern[2]", domDoc);
            XMLAssert.assertXpathEvaluatesTo(prefix + "777", hostMatch + "userMatch/userPattern[3]", domDoc);
            XMLAssert.assertXpathEvaluatesTo("Voicemail", hostMatch
                    + "/userMatch/permissionMatch/permission", domDoc);
            prefixBuilder.append("2");
        }

        String lastHostMatch = "/mappings/hostMatch[6]/";
        // "no access" match at the end of the file - just checks if paths are that
        // testGenerateNoAccessRule tests if values are correct
        for (int i = 0; i < gateways.length; i++) {
            XMLAssert.assertXpathExists(lastHostMatch + "hostPattern[" + (i + 1) + "]", domDoc);
        }

        XMLAssert.assertXpathEvaluatesTo(".", lastHostMatch + "userMatch/userPattern", domDoc);
        XMLAssert.assertXpathEvaluatesTo("NoAccess",
                lastHostMatch + "userMatch/permissionMatch/permission", domDoc);

        control.verify();
    }

    @Test
    public void testGenerateNoPermissionRequiredRule() throws Exception {
        Gateway[] gateways = new Gateway[GATEWAYS_LEN];
        for (int i = 0; i < gateways.length; i++) {
            gateways[i] = new Gateway();
            gateways[i].setUniqueId();
            gateways[i].setAddress("10.1.2." + i);
        }

        IMocksControl control = EasyMock.createControl();
        IDialingRule rule = control.createMock(IDialingRule.class);
        rule.getName();
        control.andReturn("testrule").times(gateways.length);
        rule.getDescription();
        control.andReturn(null).times(gateways.length);
        for (int i = 0; i < gateways.length; i++) {
            rule.getTransformedPatterns(gateways[i]);
            control.andReturn(new String[] {
                "555", "666", "777"
            });
        }
        rule.getPermissionNames();
        control.andReturn(Arrays.asList(new String[] {}));
        rule.getGateways();
        control.andReturn(Arrays.asList(gateways));
        control.replay();

        MockAuthRules authRules = new MockAuthRules();
        authRules.begin();
        authRules.generate(rule);
        authRules.end();

        Document document = authRules.getDocument();
        String domDoc = XmlUnitHelper.asString(document);

        String hostMatchFormat = "/mappings/hostMatch[%d]/";
        for (int i = 0; i < gateways.length; i++) {
            String hostMatch = String.format(hostMatchFormat, i + 1);
            XMLAssert.assertXpathEvaluatesTo(gateways[i].getGatewayAddress(), hostMatch + "hostPattern",
                    domDoc);
            XMLAssert.assertXpathEvaluatesTo("555", hostMatch + "userMatch/userPattern", domDoc);
            XMLAssert.assertXpathEvaluatesTo("666", hostMatch + "userMatch/userPattern[2]", domDoc);
            XMLAssert.assertXpathEvaluatesTo("777", hostMatch + "userMatch/userPattern[3]", domDoc);
            XMLAssert.assertXpathEvaluatesTo("", hostMatch + "/userMatch/permissionMatch", domDoc);
        }

        // check if generate no access has been called properly
        Assert.assertEquals(GATEWAYS_LEN, authRules.uniqueGateways);

        control.verify();
    }

    @Test
    public void testGenerateNoAccessRule() throws Exception {
        Gateway[] gateways = new Gateway[GATEWAYS_LEN];
        for (int i = 0; i < gateways.length; i++) {
            gateways[i] = new Gateway();
            gateways[i].setUniqueId();
            gateways[i].setAddress("10.1.2." + i);
            gateways[i].setAddressPort(i);
        }
        AuthRules rules = new AuthRules();
        rules.begin();
        rules.generateNoAccess(Arrays.asList(gateways));
        String lastHostMatch = "/mappings/hostMatch/";
        Document document = rules.getDocument();
        String domDoc = XmlUnitHelper.asString(document);
        // "no access" match at the end of the file
        for (int i = 0; i < gateways.length; i++) {
            XMLAssert.assertXpathEvaluatesTo(gateways[i].getGatewayAddress(), lastHostMatch
                    + "hostPattern[" + (i + 1) + "]", domDoc);
        }

        XMLAssert.assertXpathEvaluatesTo(".", lastHostMatch + "userMatch/userPattern", domDoc);
        XMLAssert.assertXpathEvaluatesTo("NoAccess",
                lastHostMatch + "userMatch/permissionMatch/permission", domDoc);
    }

    @Test
    public void testNamespace() {
        AuthRules rules = new AuthRules();
        rules.begin();
        Document doc = rules.getDocument();

        Element rootElement = doc.getRootElement();
        XmlUnitHelper.assertElementInNamespace(rootElement,
                "http://www.sipfoundry.org/sipX/schema/xml/urlauth-00-00");
    }

    @Test
    public void testGenerateInternalRuleWithTargetPermission() throws Exception {
        CustomDialingRule rule = new CustomDialingRule();
        rule.setName("test");
        rule.setDescription("Calls to internal extensions");
        DialPattern pattern1 = new DialPattern("12", 3);
        DialPattern pattern2 = new DialPattern("13", 4);
        rule.setDialPatterns(Arrays.asList(pattern1, pattern2));
        CallPattern callPattern = new CallPattern("7", CallDigits.VARIABLE_DIGITS);
        rule.setCallPattern(callPattern);
        rule.setPermissionNames(Arrays.asList("LocalDialing"));

        DomainManager domainManager = EasyMock.createMock(DomainManager.class);
        domainManager.getDomain();
        EasyMock.expectLastCall().andReturn(new Domain("example.org")).anyTimes();
        EasyMock.replay(domainManager);

        MockAuthRules authRules = new MockAuthRules();
        authRules.setDomainManager(domainManager);
        authRules.begin();
        authRules.generate(rule);
        authRules.end();

        Document document = authRules.getDocument();
        String domDoc = XmlUnitHelper.asString(document);

        InputStream controlXml = getClass().getResourceAsStream("authrules-internal-target-perm.test.xml");
        XMLAssert.assertXMLEqual(IOUtils.toString(controlXml), domDoc);
    }

    private class MockAuthRules extends AuthRules {
        public int uniqueGateways = 0;

        @Override
        void generateNoAccess(Collection<Gateway> gateways) {
            uniqueGateways = gateways.size();
        }
    }
}
