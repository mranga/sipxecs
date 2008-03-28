/*
 *  Copyright (C) 2008 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 *  Contributors retain copyright to elements licensed under a Contributor Agreement.
 *  Licensed to the User under the LGPL license.
 *
 */

package org.sipfoundry.sipxbridge;

import java.io.File;
import java.io.FileInputStream;
import java.util.HashSet;
import java.util.Hashtable;
import java.util.Properties;
import java.util.Timer;
import java.util.TimerTask;

import javax.sip.ClientTransaction;
import javax.sip.DialogTerminatedEvent;
import javax.sip.IOExceptionEvent;
import javax.sip.ListeningPoint;
import javax.sip.RequestEvent;
import javax.sip.ResponseEvent;
import javax.sip.SipException;
import javax.sip.SipListener;
import javax.sip.SipProvider;
import javax.sip.TimeoutEvent;
import javax.sip.TransactionTerminatedEvent;
import javax.sip.address.Hop;
import javax.sip.message.Response;

import net.java.stun4j.StunAddress;
import net.java.stun4j.client.NetworkConfigurationDiscoveryProcess;
import net.java.stun4j.client.ResponseSequenceServer;
import net.java.stun4j.client.StunDiscoveryReport;

import org.apache.log4j.FileAppender;
import org.apache.log4j.Level;
import org.apache.log4j.Logger;
import org.apache.log4j.PropertyConfigurator;
import org.apache.log4j.SimpleLayout;
import org.sipfoundry.log4j.SipFoundryAppender;
import org.sipfoundry.log4j.SipFoundryLayout;
import org.xbill.DNS.Lookup;
import org.xbill.DNS.Record;
import org.xbill.DNS.SRVRecord;
import org.xbill.DNS.TextParseException;
import org.xbill.DNS.Type;

import gov.nist.javax.sip.clientauthutils.*;

import junit.framework.TestCase;

/**
 * The main class
 * 
 * @author M. Ranganathan
 * 
 */
public class Gateway {

    private static Logger logger = Logger.getLogger(Gateway.class);

    private static String configurationFile = "/etc/sipxpbx/sipxbridge.xml";

    /*
     * The account manager -- tracks user accounts. This is populated by reading
     * the sipxbridge.xml configuration file.
     */
    private static AccountManagerImpl accountManager;

    /*
     * The security manager - handles wrapping credentials etc.
     */
    private static SipSecurityManager sipSecurityManager;

    /*
     * The registration manager.
     */
    private static RegistrationManager registrationManager;

    /*
     * Internal SIp Provider
     */
    private static SipProvider internalProvider;

    /*
     * External provider.
     */
    private static SipProvider externalProvider;

    /*
     * Back to back user agent manager.
     */
    private static CallControlManager backToBackUserAgentManager;
    
    

    /*
     * The Sipx proxy port.
     */
    private static int sipxProxyPort = 5060;

    /*
     * The sipx proxy address
     */
    private static String sipxProxyAddress;

    /*
     * The SIPX proxy transport.
     */
    private static String sipxProxyTransport = "udp";

    /*
     * This is a placeholder - to be replaced by STUN
     */
    private static String globalAddress;

    /*
     * The log file
     */
    protected static final String logFile = "/var/log/sipxpbx/sipxbridge.log";

    /*
     * The stun port
     */
    private static final int STUN_PORT = 3478;

    /*
     * A table of timers for re-registration
     */
    public static final Timer timer = new Timer();
    
    

    static {
        init();
    }
    
   

    private static void init() {
        try {

            logger.addAppender(new SipFoundryAppender(new SipFoundryLayout(),
                    logFile));

            ConfigurationParser parser = new ConfigurationParser();
            accountManager = parser.createAccountManager(configurationFile);

            sipSecurityManager = new gov.nist.javax.sip.clientauthutils.SipSecurityManager(
                    accountManager, ProtocolObjects.headerFactory);

            BridgeConfiguration bridgeConfiguration = accountManager
                    .getBridgeConfiguration();
            int externalPort = bridgeConfiguration.getExternalPort();
            String externalAddress = bridgeConfiguration.getExternalAddress();
            System.out.println("External Address:port = " + externalAddress
                    + ":" + externalPort);

            ListeningPoint externalUdpListeningPoint = ProtocolObjects.sipStack
                    .createListeningPoint(externalAddress, externalPort, "udp");
            ListeningPoint externalTcpListeningPoint = ProtocolObjects.sipStack
                    .createListeningPoint(externalAddress, externalPort, "tcp");

            externalProvider = ProtocolObjects.sipStack
                    .createSipProvider(externalUdpListeningPoint);

            int localPort = bridgeConfiguration.getLocalPort();
            String localIpAddress = bridgeConfiguration.getLocalAddress();
            System.out.println("Local Address:port = " + localIpAddress + ":"
                    + localPort);

            ListeningPoint internalUdpListeningPoint = ProtocolObjects.sipStack
                    .createListeningPoint(localIpAddress, localPort, "udp");

            ListeningPoint internalTcpListeningPoint = ProtocolObjects.sipStack
                    .createListeningPoint(localIpAddress, localPort, "tcp");

            internalProvider = ProtocolObjects.sipStack
                    .createSipProvider(internalUdpListeningPoint);

            registrationManager = new RegistrationManager(getWanProvider());

            backToBackUserAgentManager = new CallControlManager();

            String stunServerAddress = bridgeConfiguration
                    .getStunServerAddress();

            if (stunServerAddress != null) {

                // Todo -- deal with the situation when this port may be taken.
                StunAddress localStunAddress = new StunAddress(localIpAddress,
                        5091);

                StunAddress serverStunAddress = new StunAddress(
                        stunServerAddress, STUN_PORT);

                NetworkConfigurationDiscoveryProcess addressDiscovery = new NetworkConfigurationDiscoveryProcess(
                        localStunAddress, serverStunAddress);
                addressDiscovery.start();
                StunDiscoveryReport report = addressDiscovery
                        .determineAddress();
                System.out.println("Stun report = " + report);
                globalAddress = report.getPublicAddress().getSocketAddress()
                        .getAddress().getHostAddress();
                if (report.getPublicAddress().getPort() != 5091) {
                    System.out
                            .println("WARNING External port != internal port ");
                }

            }
            Hop hop = getSipxProxyHop();
            if (hop == null) {
                System.out
                        .println("Cannot resolve sipx proxy address check config ");
            }
            System.out.println("Proxy assumed to be running at 	" + hop);
            System.out.println("------- SIPXBRIDGE READY --------");

        } catch (Exception ex) {
            throw new RuntimeException("Cannot initialize gateway", ex);
        }

    }

    private Gateway() {

    }

    public static AccountManagerImpl getAccountManager() {
        return Gateway.accountManager;

    }

    public static RegistrationManager getRegistrationManager() {
        return registrationManager;
    }

    public static SipSecurityManager getSipSecurityManager() {
        return sipSecurityManager;
    }

    public static SipProvider getWanProvider() {
        return externalProvider;
    }

    public static SipProvider getLanProvider() {
        return internalProvider;
    }

    /**
     * @return the backToBackUserAgentManager
     */
    public static CallControlManager getCallControlManager() {
        return backToBackUserAgentManager;
    }

    /**
     * The local address of the gateway.
     * 
     * @return
     */

    public static String getLocalAddress() {
        return accountManager.getBridgeConfiguration().getLocalAddress();
    }

    /**
     * @return the sipxProxyAddress
     */
    public static HopImpl getSipxProxyHop() {
        try {
            /*
             * No caching of results here as the failover mechanism depends upon
             * DNS.
             */
            if (logger.isDebugEnabled())
                logger
                        .debug("getSipxProxyHop() : Looking up the following address "
                                + "_sip._"
                                + getSipxProxyTransport()
                                + "."
                                + getSipxProxyDomain());

            Record[] records = new Lookup("_sip._" + getSipxProxyTransport()
                    + "." + getSipxProxyDomain(), Type.SRV).run();
            if (records == null) {
                logger.debug("SRV record lookup returned null");
                Gateway.sipxProxyAddress = getSipxProxyDomain();
                // Could not look up the SRV record. Try the A record.
                return new HopImpl(getSipxProxyDomain(), getSipxProxyPort(),
                        getSipxProxyTransport());

            } else {
                SRVRecord record = (SRVRecord) records[0];
                int port = record.getPort();
                String resolvedName = record.getTarget().toString();
                // Sometimes the DNS (on linux) appends a "." to the end of the
                // record.
                // The library ought to strip this.
                if (resolvedName.endsWith(".")) {
                    resolvedName = resolvedName.substring(0, resolvedName
                            .lastIndexOf('.'));
                }
                Gateway.sipxProxyAddress = resolvedName;
                Gateway.sipxProxyPort = port;
                return new HopImpl(resolvedName, port, getSipxProxyTransport());
            }

        } catch (TextParseException ex) {
            logger
                    .error("Problem looking up proxy address -- stopping gateway");
            Gateway.stop();
            throw new RuntimeException("Problem looking up proxy address");
        }

    }

    /**
     * Get the sipx proxy address from the domain. This is determined by using
     * the DNS query above.
     * 
     * @return the sipx proxy address ( determined from DNS)
     */
    public static String getSipxProxyAddress() {
        return sipxProxyAddress;
    }

    /**
     * Get the sipx proxy port from the domain. This is determined by using the
     * DNS query above.
     * 
     * @return the sipx proxy domain.
     */
    public static int getSipxProxyPort() {
        return sipxProxyPort;
    }

    /**
     * The transport to use to talk to sipx proxy. This is registered in the DNS
     * srv.
     * 
     * @return the proxy transport
     */
    public static String getSipxProxyTransport() {
        return sipxProxyTransport;
    }

    /**
     * The Sipx proxy domain.
     * 
     * @return the sipx proxy domain name.
     */
    public static String getSipxProxyDomain() {
        return accountManager.getBridgeConfiguration().getSipxProxyDomain();
    }

    /**
     * 
     * @return the bridge log level.
     */
    public static String getLogLevel() {
        return accountManager.getBridgeConfiguration().getLogLevel();
    }
    
    /**
     * @return the upper bound of the port range we are allowing for RTP.
     * 
     */
    public static int getRtpPortRangeUpperBound() {
        
        return accountManager.getBridgeConfiguration().getRtpPortUpperBound();
    }
    
    /**
     * @return the lower bound of the port range for RTP.
     */
    public static int getRtpPortRangeLowerBound() {
        return accountManager.getBridgeConfiguration().getRtpPortLowerBound();
    }

    
    /**
     * @return the Music On Hold server User Name.
     */
    public static String getMusicOnHoldName() {
        return accountManager.getBridgeConfiguration().getMusicOnHoldName();
    }
  
    /**
     * Registers all listeners and starts everything.
     * 
     * @throws Exception
     */
    public static void start() throws GatewayConfigurationException {
        try {
            SipListenerImpl listener = new SipListenerImpl();
            getWanProvider().addSipListener(listener);
            getLanProvider().addSipListener(listener);
            ProtocolObjects.start();
          
        } catch (Exception ex) {
            throw new GatewayConfigurationException(
                    "Could not start gateway -- check configuration", ex);
        }

    }

    public static void stop() {
        backToBackUserAgentManager.stop();
        timer.cancel();
        ProtocolObjects.stop();

    }

    /**
     * Get the global address of igate ( this should be determined by Stun but
     * for now we hard code it ).
     * 
     * @return
     */
    public static String getGlobalAddress() {
        return globalAddress;
    }

    

    /**
     * The main method for the Bridge.
     * 
     * @param args
     */
    public static void main(String[] args) throws Exception {

        for (int i = 0; i < args.length; i++) {
            if ("-config".equals(args[i])) {
                Gateway.configurationFile = args[++i];
            }
        }
        start();

        for (ItspAccountInfo itspAccount : Gateway.accountManager
                .getItspAccounts()) {
            if (itspAccount.isRegisterOnInitialization())
                Gateway.registrationManager.sendRegistrer(itspAccount);
        }

    }

   

}