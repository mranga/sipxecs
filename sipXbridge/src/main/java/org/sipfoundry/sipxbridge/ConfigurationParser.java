/*
 *  Copyright (C) 2008 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 *  Contributors retain copyright to elements licensed under a Contributor Agreement.
 *  Licensed to the User under the LGPL license.
 *
 */
package org.sipfoundry.sipxbridge;

import java.io.File;
import java.io.FileReader;

import org.apache.commons.digester.Digester;

/**
 * The Parser for the configuration file that the bridge will use.
 * 
 * @author M. Ranganathan
 * 
 */
public class ConfigurationParser {
    private static final String BRIDGE_CONFIG = "sipxbridge-config/bridge-configuration";
    private static final String ITSP_CONFIG = "sipxbridge-config/itsp-account";

    /**
     * Add the digester rules.
     * 
     * @param digester
     */
    private static void addRules(Digester digester) {

        digester.addObjectCreate("sipxbridge-config", AccountManagerImpl.class);
        digester.addObjectCreate(BRIDGE_CONFIG, BridgeConfiguration.class);
        digester.addSetNext(BRIDGE_CONFIG, "setBridgeConfiguration");
        digester.addCallMethod(String.format("%s/%s", BRIDGE_CONFIG,
                "external-address"), "setExternalAddress", 0);
        digester.addCallMethod(String.format("%s/%s", BRIDGE_CONFIG,
                "external-port"), "setExternalPort", 0,
                new Class[] { Integer.class });
        digester.addCallMethod(String.format("%s/%s", BRIDGE_CONFIG,
                "local-address"), "setLocalAddress", 0);
        digester
                .addCallMethod(String.format("%s/%s", BRIDGE_CONFIG,
                        "local-port"), "setLocalPort", 0,
                        new Class[] { Integer.class });
        digester.addCallMethod(String.format("%s/%s", BRIDGE_CONFIG,
                "stun-server-address"), "setStunServerAddress", 0);
        digester.addCallMethod(String.format("%s/%s", BRIDGE_CONFIG,
                "sipx-proxy-domain"), "setSipxProxyDomain", 0);
        digester.addCallMethod(String.format("%s/%s", BRIDGE_CONFIG,
        "rtp-port-range"), "setPortRange", 0);
        digester.addCallMethod(String.format("%s/%s", BRIDGE_CONFIG,
        "music-on-hold-support-enabled"), "setMusicOnHoldSupportEnabled",
        0, new Class[] { Boolean.class });
        
        digester.addCallMethod(String.format("%s/%s", BRIDGE_CONFIG,
                "log-level"), "setLogLevel", 0);
        
        /*
         * ITSP configuration support parameters.
         */
        digester.addObjectCreate(ITSP_CONFIG, ItspAccountInfo.class);
        digester.addSetNext(ITSP_CONFIG, "addItspAccount");
        digester.addCallMethod(String.format("%s/%s", ITSP_CONFIG,
                "outbound-proxy"), "setOutboundProxy", 0);
        digester.addCallMethod(String.format("%s/%s", ITSP_CONFIG,
                "outbound-transport"), "setOutboundTransport", 0);
        digester.addCallMethod(String.format("%s/%s", ITSP_CONFIG,
                "proxy-domain"), "setProxyDomain", 0);
        digester.addCallMethod(String.format("%s/%s", ITSP_CONFIG,
                "authentication-realm"), "setAuthenticationRealm", 0);
        digester.addCallMethod(
                String.format("%s/%s", ITSP_CONFIG, "user-name"),
                "setUserName", 0);
        digester.addCallMethod(String.format("%s/%s", ITSP_CONFIG, "password"),
                "setPassword", 0);
        digester.addCallMethod(String.format("%s/%s", ITSP_CONFIG,
                "display-name"), "setDisplayName", 0);
        digester.addCallMethod(String.format("%s/%s", ITSP_CONFIG,
                "use-global-addressing"), "setGlobalAddressingUsed", 0,
                new Class[] { Boolean.class });
        digester.addCallMethod(
                String.format("%s/%s", ITSP_CONFIG, "use-rport"),
                "setRportUsed", 0, new Class[] { Boolean.class });
        digester.addCallMethod(String.format("%s/%s", ITSP_CONFIG,
                "route-inbound-calls-to-auto-attendant"),
                "setInboundCallsRoutedToAutoAttendant", 0,
                new Class[] { Boolean.class });
        digester.addCallMethod(String.format("%s/%s", ITSP_CONFIG,
                "route-inbound-calls-to-auto-attendant"),
                "setInboundCallsRoutedToAutoAttendant", 0,
                new Class[] { Boolean.class });

        
        
        

    }

    public AccountManagerImpl createAccountManager(String fileName) {
        // Create a Digester instance
        Digester d = new Digester();
        d.setSchema("file:schema/sipxbridge.xsd");
        addRules(d);

        // Process the input file.
        try {
            java.io.Reader reader = new FileReader(new File(fileName));
            d.parse(reader);
            return (AccountManagerImpl) d.getRoot();
        } catch (java.io.IOException ioe) {
            System.out.println("Error reading input file:" + ioe.getMessage());
            throw new RuntimeException("Intiialzation exception", ioe);
        } catch (org.xml.sax.SAXException se) {
            System.out.println("Error parsing input file:" + se.getMessage());
            throw new RuntimeException("Intiialzation exception", se);
        }

    }

}