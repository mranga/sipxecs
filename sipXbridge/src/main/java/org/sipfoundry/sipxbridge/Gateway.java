/*
 *  Copyright (C) 2008 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 *  Contributors retain copyright to elements licensed under a Contributor Agreement.
 *  Licensed to the User under the LGPL license.
 *
 */

package org.sipfoundry.sipxbridge;

import gov.nist.javax.sip.SipStackExt;
import gov.nist.javax.sip.clientauthutils.AuthenticationHelper;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.net.InetAddress;
import java.security.KeyStore;
import java.security.SecureRandom;
import java.util.HashSet;
import java.util.Properties;
import java.util.Timer;
import java.util.TimerTask;
import java.util.concurrent.ConcurrentHashMap;
import java.util.logging.Handler;
import java.util.logging.Level;

import javax.net.ssl.HostnameVerifier;
import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSession;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManagerFactory;
import javax.sip.ListeningPoint;
import javax.sip.SipProvider;
import javax.sip.address.Address;
import javax.sip.address.SipURI;
import javax.sip.message.Request;

import net.java.stun4j.StunAddress;
import net.java.stun4j.client.NetworkConfigurationDiscoveryProcess;
import net.java.stun4j.client.StunDiscoveryReport;

import org.apache.log4j.Logger;
import org.sipfoundry.commons.log4j.SipFoundryAppender;
import org.sipfoundry.commons.log4j.SipFoundryLayout;
import org.sipfoundry.sipxbridge.symmitron.SymmitronClient;
import org.sipfoundry.sipxbridge.symmitron.SymmitronConfig;
import org.sipfoundry.sipxbridge.symmitron.SymmitronConfigParser;
import org.sipfoundry.sipxbridge.xmlrpc.SipXbridgeXmlRpcClient;
import org.xbill.DNS.Lookup;
import org.xbill.DNS.Record;
import org.xbill.DNS.SRVRecord;
import org.xbill.DNS.Type;

/**
 * The main class
 * 
 * @author M. Ranganathan
 * 
 */
public class Gateway {

	private static Logger logger = Logger.getLogger(Gateway.class);

	private static String configurationFile = "file:///etc/sipxpbx/sipxbridge.xml";

	/*
	 * This is a reserved name for the sipxbridge user.
	 */
	static final String SIPXBRIDGE_USER = "~~id~bridge";
	/*
	 * The account manager -- tracks user accounts. This is populated by reading
	 * the sipxbridge.xml configuration file.
	 */
	private static AccountManagerImpl accountManager;

	/*
	 * The security manager - handles wrapping credentials etc.
	 */
	private static AuthenticationHelper authenticationHelper;

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
	 * External
	 */
	private static SipProvider externalTlsProvider;

	/*
	 * CallControl router.
	 */
	private static CallControlManager callControlManager;

	/*
	 * The allocator for back to back user agents.
	 */

	private static BackToBackUserAgentFactory backToBackUserAgentFactory;

	/*
	 * This is a placeholder - to be replaced by STUN
	 */
	private static String globalAddress;

	/*
	 * The stun port
	 */
	private static final int STUN_PORT = 3478;

	/*
	 * A table of timers for re-registration
	 */
	private static Timer timer = new Timer();

	/*
	 * The Music on hold URL
	 */
	private static Address musicOnHoldAddress;

	/*
	 * The From address of the gateway.
	 */
	private static Address gatewayFromAddress;

	
	/*
	 * The Gateway state.
	 */
	static GatewayState state = GatewayState.STOPPED;

	/*
	 * The time for REGISTER requests.
	 */
	private static final int MAX_REGISTRATION_TIMER = 10000;

	/*
	 * Default transport to talk to ITSP
	 */
	protected static final String DEFAULT_ITSP_TRANSPORT = "udp";

	/*
	 * Min value for session timer ( seconds ).
	 */
	protected static final int MIN_EXPIRES = 60;

	/*
	 * Session expires interval (initial value)
	 */
	protected static final int SESSION_EXPIRES = 30 * 60;

	/*
	 * Advance timer by 10 seconds for session timer.
	 */
	protected static final int TIMER_ADVANCE = 10;

	/*
	 * set to true to enable tls for sip signaling (untested).
	 */

	private static boolean isTlsSupportEnabled = false;

	private static int callCount;

	private static ConcurrentHashMap<String, SymmitronClient> symmitronClients = new ConcurrentHashMap<String, SymmitronClient>();

	private static String configurationPath;

	private static HashSet<String> proxyAddressTable = new HashSet<String>();

	private static HashSet<Integer> parkServerCodecs = new HashSet<Integer>();

	static SipFoundryAppender logAppender;

	private static SymmitronConfig symconfig;

	private Gateway() {

	}

	static void setConfigurationFileName(String configFileName) {
		Gateway.configurationFile = configFileName;
	}

	static void parseConfigurationFile() {
		ConfigurationParser parser = new ConfigurationParser();
		accountManager = parser.createAccountManager(configurationFile);

	}

	static SymmitronClient initializeSymmitron(String address) {
		/*
		 * Looks up a symmitron for a given address.
		 */
		SymmitronClient symmitronClient = symmitronClients.get(address);
		if (symmitronClient == null) {
			int symmitronPort;
			boolean isSecure = false;
			if (Gateway.getBridgeConfiguration().getSymmitronXmlRpcPort() != 0) {
				symmitronPort = Gateway.getBridgeConfiguration()
						.getSymmitronXmlRpcPort();
			} else {	
				symmitronPort = symconfig.getXmlRpcPort();
				isSecure = symconfig.getUseHttps();
			}
			
			symmitronClient = new SymmitronClient(address, symmitronPort,
					isSecure, callControlManager);
			symmitronClients.put(address, symmitronClient);
		}

		return symmitronClient;
	}

	
	

	/**
	 * Initialize the loggers for the libraries used.
	 * 
	 * @throws IOException
	 */
	static void initializeLogging() throws IOException {

		BridgeConfiguration bridgeConfiguration = Gateway
				.getBridgeConfiguration();
		Level level = Level.OFF;
		String logLevel = bridgeConfiguration.getLogLevel();

		if (logLevel.equals("INFO"))
			level = Level.INFO;
		else if (logLevel.equals("DEBUG"))
			level = Level.FINE;
		else if (logLevel.equals("TRACE"))
			level = Level.FINER;
		else if (logLevel.equals("WARN"))
			level = Level.WARNING;

		/*
		 * BUGBUG For now turn off Logging on STUN4j. It writes to stdout.
		 */
		level = Level.OFF;

		java.util.logging.Logger log = java.util.logging.Logger
				.getLogger("net.java.stun4j");
		log.setLevel(level);
		java.util.logging.FileHandler fileHandler = new java.util.logging.FileHandler(
				Gateway.getLogFile());

		/*
		 * Remove all existing handlers.
		 */
		for (Handler handler : log.getHandlers()) {
			log.removeHandler(handler);
		}

		/*
		 * Add the file handler.
		 */
		log.addHandler(fileHandler);

		Gateway.logAppender = new SipFoundryAppender(new SipFoundryLayout(),
				Gateway.getLogFile());
		Logger applicationLogger = Logger.getLogger(Gateway.class.getPackage()
				.getName());

		/*
		 * Set the log level.
		 */
		if (Gateway.getLogLevel().equals("TRACE")) {
			applicationLogger.setLevel(org.apache.log4j.Level.DEBUG);
		} else {
			applicationLogger.setLevel(org.apache.log4j.Level.toLevel(Gateway
					.getLogLevel()));
		}

		applicationLogger.addAppender(logAppender);

	}

	/**
	 * Discover our address using stun.
	 * 
	 * @throws SipXbridgeException
	 */
	static void discoverAddress() throws SipXbridgeException {
		try {

			BridgeConfiguration bridgeConfiguration = accountManager
					.getBridgeConfiguration();
			String stunServerAddress = bridgeConfiguration
					.getStunServerAddress();

			if (stunServerAddress != null) {
				// Todo -- deal with the situation when this port may be taken.
				StunAddress localStunAddress = new StunAddress(Gateway
						.getLocalAddress(), STUN_PORT);

				StunAddress serverStunAddress = new StunAddress(
						stunServerAddress, STUN_PORT);

				NetworkConfigurationDiscoveryProcess addressDiscovery = new NetworkConfigurationDiscoveryProcess(
						localStunAddress, serverStunAddress);

				addressDiscovery.start();
				StunDiscoveryReport report = addressDiscovery
						.determineAddress();
				globalAddress = report.getPublicAddress().getSocketAddress()
						.getAddress().getHostAddress();
				logger.debug("Stun report = " + report);

				if (report.getPublicAddress().getPort() != STUN_PORT) {
					logger
							.warn("WARNING External port != internal port your NAT may not be symmetric.");
				}

			}
		} catch (Exception ex) {
			throw new SipXbridgeException(
					"Error discovering  address", ex);
		}
	}

	/**
	 * Start timer to rediscover our address.
	 * 
	 */
	static void startRediscoveryTimer() {
		int rediscoveryTime = Gateway.accountManager.getBridgeConfiguration()
				.getGlobalAddressRediscoveryPeriod();
		TimerTask ttask = new TimerTask() {

			@Override
			public void run() {
				try {
					Gateway.discoverAddress();
				} catch (Exception ex) {
					logger.error("Error re-discovering  address");
				}

			}

		};
		Gateway.getTimer().schedule(ttask, rediscoveryTime * 1000,
				rediscoveryTime * 1000);
	}

	/**
	 * Get the proxy addresses. This is done once at startup. We cannot accecpt
	 * inbound INVITE from other addresses than the ones on this list.
	 */

	static void getSipxProxyAddresses() throws SipXbridgeException {
		try {

			Record[] records = new Lookup("_sip._" + getSipxProxyTransport()
					+ "." + getSipxProxyDomain(), Type.SRV).run();

			/* repopulate the proxy address table in case of reconfiguration */
			Gateway.proxyAddressTable.clear();
			if (records == null) {
				logger.debug("SRV record lookup returned null");
				String sipxProxyAddress = getSipxProxyDomain();
				Gateway.proxyAddressTable.add(InetAddress.getByName(
						sipxProxyAddress).getHostAddress());
			} else {
				for (Record rec : records) {
					SRVRecord record = (SRVRecord) rec;
					String resolvedName = record.getTarget().toString();
					/*
					 * Sometimes the DNS (on linux) appends a "." to the end of
					 * the record. The library ought to strip this.
					 */
					if (resolvedName.endsWith(".")) {
						resolvedName = resolvedName.substring(0, resolvedName
								.lastIndexOf('.'));
					}
					String ipAddress = InetAddress.getByName(resolvedName)
							.getHostAddress();

					Gateway.proxyAddressTable.add(ipAddress);
				}
			}

			logger.debug("proxy address table = " + proxyAddressTable);
		} catch (Exception ex) {
			logger.error("Cannot do address lookup ", ex);
			throw new SipXbridgeException(
					"Could not do dns lookup for " + getSipxProxyDomain(), ex);
		}
	}

	static boolean isAddressFromProxy(String address) {

		return proxyAddressTable.contains(address);

	}

	/**
	 * Initialize the bridge.
	 * 
	 */
	static void initializeSipListeningPoints() {
		try {

			BridgeConfiguration bridgeConfiguration = accountManager
					.getBridgeConfiguration();

			authenticationHelper = ((SipStackExt) ProtocolObjects.sipStack)
					.getAuthenticationHelper(accountManager,
							ProtocolObjects.headerFactory);

			int externalPort = bridgeConfiguration.getExternalPort();
			String externalAddress = bridgeConfiguration.getExternalAddress();
			logger.debug("External Address:port = " + externalAddress + ":"
					+ externalPort);
			ListeningPoint externalUdpListeningPoint = ProtocolObjects.sipStack
					.createListeningPoint(externalAddress, externalPort, "udp");
			ListeningPoint externalTcpListeningPoint = ProtocolObjects.sipStack
					.createListeningPoint(externalAddress, externalPort, "tcp");
			if (Gateway.isTlsSupportEnabled) {
				ListeningPoint externalTlsListeningPoint = ProtocolObjects.sipStack
						.createListeningPoint(externalAddress,
								externalPort + 1, "tls");
				externalTlsProvider = ProtocolObjects.sipStack
						.createSipProvider(externalTlsListeningPoint);
			}
			externalProvider = ProtocolObjects.sipStack
					.createSipProvider(externalUdpListeningPoint);
			externalProvider.addListeningPoint(externalTcpListeningPoint);

			int localPort = bridgeConfiguration.getLocalPort();
			String localIpAddress = bridgeConfiguration.getLocalAddress();

			SipURI mohUri = accountManager.getBridgeConfiguration()
					.getMusicOnHoldName() == null ? null
					: ProtocolObjects.addressFactory.createSipURI(
							accountManager.getBridgeConfiguration()
									.getMusicOnHoldName(), Gateway
									.getSipxProxyDomain());
			if (mohUri != null) {
				musicOnHoldAddress = ProtocolObjects.addressFactory
						.createAddress(mohUri);
			}

			String domain = Gateway.getSipxProxyDomain();
			gatewayFromAddress = ProtocolObjects.addressFactory
					.createAddress(ProtocolObjects.addressFactory.createSipURI(
							SIPXBRIDGE_USER, domain));
			logger.debug("Local Address:port " + localIpAddress + ":"
					+ localPort);

			ListeningPoint internalUdpListeningPoint = ProtocolObjects.sipStack
					.createListeningPoint(localIpAddress, localPort, "udp");

			ListeningPoint internalTcpListeningPoint = ProtocolObjects.sipStack
					.createListeningPoint(localIpAddress, localPort, "tcp");

			internalProvider = ProtocolObjects.sipStack
					.createSipProvider(internalUdpListeningPoint);

			internalProvider.addListeningPoint(internalTcpListeningPoint);

			registrationManager = new RegistrationManager(getWanProvider("udp"));

			callControlManager = new CallControlManager();

			backToBackUserAgentFactory = new BackToBackUserAgentFactory();

		} catch (Throwable ex) {
			ex.printStackTrace();
			logger.error("Cannot initialize gateway", ex);
			throw new SipXbridgeException(
					"Cannot initialize gateway", ex);
		}

	}

	static AccountManagerImpl getAccountManager() {
		return Gateway.accountManager;

	}

	static BridgeConfiguration getBridgeConfiguration() {
		return accountManager.getBridgeConfiguration();
	}

	static RegistrationManager getRegistrationManager() {
		return registrationManager;
	}

	static AuthenticationHelper getAuthenticationHelper() {
		return authenticationHelper;
	}

	static SipProvider getWanProvider(String transport) {
		if (transport.equalsIgnoreCase("tls"))
			return externalTlsProvider;
		else
			return externalProvider;
	}

	static SipProvider getLanProvider() {
		return internalProvider;
	}

	/**
	 * @return the call control router.
	 */
	static CallControlManager getCallControlManager() {
		return callControlManager;
	}

	/**
	 * Get the back to back user agent factory.
	 * 
	 * @return the back to back user agent factory.
	 */
	static BackToBackUserAgentFactory getBackToBackUserAgentFactory() {
		return backToBackUserAgentFactory;
	}

	/**
	 * The local address of the gateway.
	 * 
	 * @return
	 */

	static String getLocalAddress() {
		return accountManager.getBridgeConfiguration().getLocalAddress();
	}

	/**
	 * The transport to use to talk to sipx proxy. This is registered in the DNS
	 * srv.
	 * 
	 * @return the proxy transport
	 */
	static String getSipxProxyTransport() {
		return accountManager.getBridgeConfiguration().getSipxProxyTransport();
	}

	/**
	 * The Sipx proxy domain.
	 * 
	 * @return the sipx proxy domain name.
	 */
	static String getSipxProxyDomain() {
		return accountManager.getBridgeConfiguration().getSipxProxyDomain();
	}

	/**
	 * 
	 * @return the bridge log level.
	 */
	static String getLogLevel() {
		return accountManager.getBridgeConfiguration().getLogLevel();
	}

	/**
	 * Get the timeout for media.
	 * 
	 * @return
	 */
	static int getMediaKeepaliveMilisec() {

		return Gateway.accountManager.getBridgeConfiguration()
				.getMediaKeepalive();
	}

	/**
	 * Get the sip keepalive
	 * 
	 * @return
	 */
	static int getSipKeepaliveSeconds() {
		return Gateway.accountManager.getBridgeConfiguration()
				.getSipKeepalive();
	}

	/**
	 * Get the MOH server Request URI.
	 */
	static SipURI getMusicOnHoldUri() {
		try {
			return accountManager.getBridgeConfiguration().getMusicOnHoldName() == null ? null
					: ProtocolObjects.addressFactory.createSipURI(
							accountManager.getBridgeConfiguration()
									.getMusicOnHoldName(), Gateway
									.getSipxProxyDomain());
		} catch (Exception ex) {
			logger.error("Unexpected exception creating Music On Hold URI", ex);
			throw new SipXbridgeException("Unexpected exception", ex);
		}
	}

	/**
	 * @return the Music On Hold server Address.
	 */
	static Address getMusicOnHoldAddress() {
		return musicOnHoldAddress;
	}

	/**
	 * Get the Gateway Address ( used in From Header ) of requests that
	 * originate from the Gateway.
	 * 
	 * @return an address that can be used in the From Header of request that
	 *         originate from the Gateway.
	 */
	static Address getGatewayFromAddress() {
		return Gateway.gatewayFromAddress;

	}

	static void registerWithItsp() throws SipXbridgeException {
		logger.info("------- REGISTERING--------");
		try {
			Gateway.accountManager.lookupItspAccountAddresses();
			Gateway.accountManager.startAuthenticationFailureTimers();

			for (ItspAccountInfo itspAccount : Gateway.accountManager
					.getItspAccounts()) {

				if (itspAccount.isRegisterOnInitialization()
						&& itspAccount.getState() != AccountState.INVALID) {
					Gateway.registrationManager.sendRegistrer(itspAccount);
				}

			}

			/*
			 * Wait for successful registration.
			 */
			for (int i = 0; i < MAX_REGISTRATION_TIMER / 1000; i++) {

				try {
					Thread.sleep(1000);
				} catch (InterruptedException ex) {
					throw new SipXbridgeException(
							"Unexpected exception registering", ex);
				}

				/*
				 * Check all accounts - if they are Authenticated we are done.
				 */
				boolean allAuthenticated = true;
				for (ItspAccountInfo itspAccount : Gateway.accountManager
						.getItspAccounts()) {
					if (itspAccount.isRegisterOnInitialization()
							&& itspAccount.getState() == AccountState.AUTHENTICATING) {
						allAuthenticated = false;
						break;
					}
				}
				if (allAuthenticated)
					break;

			}

			/*
			 * For all those who ask for keepalive and don't need registration,
			 * kick off their timers.
			 */
			for (ItspAccountInfo itspAccount : Gateway.getAccountManager()
					.getItspAccounts()) {
				if (!itspAccount.isRegisterOnInitialization()
						&& itspAccount.getSipKeepaliveMethod().equals("CR-LF")
						&& itspAccount.getState() != AccountState.INVALID) {
					itspAccount.startCrLfTimerTask();

				}
			}

		} catch (SipXbridgeException ex) {
			logger.fatal(ex);
			throw ex;

		} catch (Exception ex) {
			logger.fatal(ex);
			throw new SipXbridgeException(ex.getMessage());
		}
	}

	/**
	 * Registers all listeners and starts everything.
	 * 
	 * @throws Exception
	 */
	static void startSipListener() throws SipXbridgeException {

		try {
			SipListenerImpl listener = new SipListenerImpl();
			getWanProvider("udp").addSipListener(listener);
			if (Gateway.isTlsSupportEnabled)
				getWanProvider("tls").addSipListener(listener);
			getLanProvider().addSipListener(listener);
			ProtocolObjects.start();
		} catch (Exception ex) {
			throw new SipXbridgeException(
					"Could not start gateway -- check configuration", ex);
		}

	}

	static void startAddressDiscovery() {

		if (Gateway.getGlobalAddress() == null
				&& Gateway.accountManager.getBridgeConfiguration()
						.getStunServerAddress() == null) {
			throw new SipXbridgeException(
					"Gateway address or stun server required. ");
		}

		if (Gateway.getGlobalAddress() == null) {
			discoverAddress();
			startRediscoveryTimer();
		} else {
			Gateway.accountManager.getBridgeConfiguration()
					.setStunServerAddress(null);
		}

	}

	static void start() throws SipXbridgeException {
		if (Gateway.getState() != GatewayState.STOPPED) {
			return;
		}

		Gateway.state = GatewayState.INITIALIZING;

		/*
		 * Initialize the JAIN-SIP listening points.
		 */
		initializeSipListeningPoints();

		/*
		 * Lookup the proxy addresses and keep them cached. Note that this is
		 * done just once. sipxbridge will need to be restarted if the DNS
		 * records are re-configured. We relup upon sipxsupervisor restarting
		 * sipxbridge when dns addresses are reconfigured.
		 */

		getSipxProxyAddresses();

		/*
		 * Start up the STUN address discovery.
		 */
		startAddressDiscovery();

		/*
		 * Register the sip listener with the provider.
		 */
		startSipListener();

		/*
		 * Can start sending outbound calls. Cannot yet gake inbound calls.
		 */
		Gateway.state = GatewayState.INITIALIZED;

		/*
		 * Register with ITSPs. Now we can take inbound calls
		 */
		registerWithItsp();
		

	}

	/**
	 * Stop the gateway. Release any port resources associated with ongoing
	 * dialogs and tear down ongoing Music oh
	 */
	static synchronized void stop() {
		Gateway.state = GatewayState.STOPPING;
		
		logger.debug("Stopping Gateway");
		// Purge the timer.
		getTimer().purge();
		try {
			/*
			 * De-register from all ITSP accounts.
			 */
			for ( ItspAccountInfo itspAccount : Gateway.getAccountManager().getItspAccounts()) {
				if ( itspAccount.isRegisterOnInitialization() ) {
					registrationManager.sendDeregister(itspAccount);
				}
			}
			
			/*
			 * Tear down all ongoing calls.
			 */
			
			for ( BackToBackUserAgent b2bua : backToBackUserAgentFactory.getBackToBackUserAgents()) {
				b2bua.tearDown(Gateway.SIPXBRIDGE_USER, ReasonCode.BRIDGE_STOPPING, "Bridge Stopping");
			}
			
			Gateway.callCount = 0;
			
			for ( SymmitronClient client : Gateway.symmitronClients.values() ) {
				client.signOut();
			}
			
			
		} catch (Exception ex) {
			logger.error("Unexepcted exception occured while stopping bridge",
					ex);

		}
		// Tear down the sip stack.
		ProtocolObjects.stop();
		Gateway.state = GatewayState.STOPPED;
	}

	/**
	 * Get the global address of bridge. This is the publicly routable address
	 * of the bridge.
	 * 
	 * @return
	 */
	static String getGlobalAddress() {

		return Gateway.accountManager.getBridgeConfiguration()
				.getGlobalAddress() == null ? globalAddress
				: Gateway.accountManager.getBridgeConfiguration()
						.getGlobalAddress();
	}

	/**
	 * Get the global port of the bridge. This is the publicly routable port of
	 * the bridge.
	 * 
	 * @return
	 */
	static int getGlobalPort() {
		return Gateway.accountManager.getBridgeConfiguration().getGlobalPort() != -1 ? Gateway.accountManager
				.getBridgeConfiguration().getGlobalPort()
				: Gateway.accountManager.getBridgeConfiguration()
						.getExternalPort();
	}

	/**
	 * Get the web server port.
	 * 
	 * @return the web server port.
	 */
	static int getXmlRpcWebServerPort() {
		return Gateway.accountManager.getBridgeConfiguration().getXmlRpcPort();
	}

	/**
	 * Gets the current bridge status.
	 * 
	 * @return
	 */
	static GatewayState getState() {

		return Gateway.state;

	}

	/**
	 * Get the log file name.
	 * 
	 * @return the log file name
	 * 
	 */
	static String getLogFile() {
		return Gateway.getAccountManager().getBridgeConfiguration()
				.getLogFileDirectory()
				+ "/sipxbridge.log";
	}

	/**
	 * Get the default codec name. Returns null if Re-Invite is supported.
	 * 
	 */
	static String getCodecName() {
		if (Gateway.isReInviteSupported())
			return null;
		else
			return Gateway.getAccountManager().getBridgeConfiguration()
					.getCodecName();
	}

	/**
	 * Get the call limit ( number of concurrent calls)
	 */
	static int getCallLimit() {
		return Gateway.getAccountManager().getBridgeConfiguration()
				.getMaxCalls();
	}

	/**
	 * Get the call count.
	 * 
	 * @return -- the call count of the gateway ( number of active calls )
	 * 
	 */
	static int getCallCount() {

		return Gateway.callCount;
	}

	/**
	 * Decrement the call count.
	 */
	static void decrementCallCount() {
		if (Gateway.callCount > 0)
			Gateway.callCount--;
	}

	static void incrementCallCount() {
		Gateway.callCount++;

	}

	/**
	 * Whether or not delayed offer answer model is supported on Re-INVITE
	 * 
	 * @return true if supported.
	 */
	static boolean isReInviteSupported() {

		return accountManager.getBridgeConfiguration().isReInviteSupported();
	}

	static int getSessionExpires() {
		return SESSION_EXPIRES;
	}

	/**
	 * @return the timer
	 */
	static Timer getTimer() {
		return timer;
	}

	/**
	 * Session keepalive timer.
	 * 
	 * @return
	 */
	static String getSessionTimerMethod() {
		return Request.INVITE;
	}

	/**
	 * Returns true if inbound calls go to an auto attendant.
	 * 
	 * @return
	 */
	static boolean isInboundCallsRoutedToAutoAttendant() {
		return accountManager.getBridgeConfiguration()
				.isInboundCallsRoutedToAutoAttendant();
	}

	/**
	 * Place where you want to direct calls ( assuming that you have such a
	 * place ).
	 * 
	 * @return - the address of auto attendant.
	 */
	static String getAutoAttendantName() {
		return accountManager.getBridgeConfiguration().getAutoAttendantName();
	}

	/**
	 * The XML rpc client connection to the symmitron.
	 * 
	 * @param address
	 *            - the address ( extracted from the Via header).
	 * @return -- the client to talk to the symmitron.
	 */
	static SymmitronClient getSymmitronClient(String address) {
		String lookupAddress = address;
		if (Gateway.getBridgeConfiguration().getSymmitronHost() != null) {
			lookupAddress = Gateway.getBridgeConfiguration().getSymmitronHost();
		}

		SymmitronClient symmitronClient = symmitronClients.get(lookupAddress);
		if (symmitronClient == null) {
			symmitronClient = initializeSymmitron(lookupAddress);
			try {
				Thread.sleep(100);
			} catch (Exception ex) {
			}
		}
		return symmitronClient;
	}

	/**
	 * The set of codecs handled by the park server.
	 * 
	 * @return
	 */
	static HashSet<Integer> getParkServerCodecs() {

		return parkServerCodecs;
	}

	/**
	 * Log an internal error and potentially throw a runtime exception ( if
	 * debug is enabled).
	 * 
	 * @param errorString
	 */
	static void logInternalError(String errorString, Exception exception) {
		if (logger.isDebugEnabled()) {
			logger.fatal(errorString, exception);
			throw new SipXbridgeException(errorString, exception);
		} else {
			logger.fatal(errorString, exception);
		}

	}

	static void logInternalError(String errorString) {
		if (logger.isDebugEnabled()) {
			logger.fatal(errorString);
			throw new SipXbridgeException(errorString);
		} else {
			/*
			 * Log our stack trace for analysis.
			 */
			logger.fatal(errorString, new Exception());
		}
	}
	/**
	 * 
	 * This method must be called before SSL connection is initialized.
	 */
	private static void initHttpsClient() {
		try {
			// Create empty HostnameVerifier
			HostnameVerifier hv = new HostnameVerifier() {
				public boolean verify(String arg0, SSLSession arg1) {
					return true;
				}
			};

			HttpsURLConnection.setDefaultHostnameVerifier(hv);

			String trustStoreType = System
					.getProperty("javax.net.ssl.trustStoreType");

			logger.debug("trustStoreType = " + trustStoreType);

			KeyStore ks = KeyStore.getInstance(trustStoreType);

			String pathToTrustStore = System
					.getProperty("javax.net.ssl.trustStore");

			logger.debug("pathToTrustStore = " + pathToTrustStore);

			logger.debug("passwKey = "
					+ System.getProperty("javax.net.ssl.trustStorePassword"));

			char[] passwKey = System.getProperty(
					"javax.net.ssl.trustStorePassword").toCharArray();

			ks.load(new FileInputStream(pathToTrustStore), passwKey);

			String x509Algorithm = System.getProperty("jetty.x509.algorithm");

			logger.debug("X509Algorithm = " + x509Algorithm);

			TrustManagerFactory tmf = TrustManagerFactory
					.getInstance(x509Algorithm);

			tmf.init(ks);

			SSLContext sslContext = SSLContext.getInstance("SSL");

			sslContext.init(null, tmf.getTrustManagers(), new SecureRandom());

			SSLSocketFactory factory = sslContext.getSocketFactory();
			SSLSocket socket = ((SSLSocket) factory.createSocket());
			String[] suites = socket.getEnabledCipherSuites();
			socket.setEnabledCipherSuites(suites);
			for (String suite : factory.getDefaultCipherSuites()) {
				logger.debug("Supported Suite = " + suite);
			}

			HttpsURLConnection.setDefaultSSLSocketFactory(factory);

		} catch (Exception ex) {
			logger.fatal("Unexpected exception initializing HTTPS client", ex);
			throw new SipXbridgeException(ex);
		}
	}
	
	
	
	
	
	/**
	 * The main method for the Bridge.
	 * 
	 * @param args
	 */
	public static void main(String[] args) throws Exception {
		try {

			/*
			 * The codecs supported by our park server.
			 */
			parkServerCodecs.add(RtpPayloadTypes.getPayloadType("PCMU"));
			parkServerCodecs.add(RtpPayloadTypes.getPayloadType("PCMA"));

			Gateway.isTlsSupportEnabled = System.getProperty(
					"sipxbridge.enableTls", "false").equals("true");

			Gateway.configurationPath = System.getProperty("conf.dir",
					"/etc/sipxpbx");
			Gateway.configurationFile = System.getProperty("conf.dir",
					"/etc/sipxpbx")
					+ "/sipxbridge.xml";
			String command = System.getProperty("sipxbridge.command", "start");
			String log4jPropertiesFile = Gateway.configurationPath
					+ "/log4j.properties";

			if (command.equals("start")) {
				// Wait for the configuration file to become available.
				while (!new File(Gateway.configurationFile).exists()) {
					Thread.sleep(5 * 1000);
				}
				Gateway.parseConfigurationFile();
				if (new File(log4jPropertiesFile).exists()) {
					/*
					 * Override the file configuration setting.
					 */
					Properties props = new Properties();
					props.load(new FileInputStream(log4jPropertiesFile));
					BridgeConfiguration configuration = Gateway.accountManager
							.getBridgeConfiguration();
					String level = props
							.getProperty("log4j.category.org.sipfoundry.sipxbridge");
					if (level != null) {
						configuration.setLogLevel(level);
					}

				}

				Gateway.initializeLogging();

				Gateway.start();

				SipXbridgeXmlRpcServerImpl.startXmlRpcServer();
				
				Gateway.symconfig = new SymmitronConfigParser().parse(Gateway.configurationPath+ "/nattraversalrules.xml");
				
				if ( symconfig.getUseHttps() ) {
					Gateway.initHttpsClient();
				}

			} else if (command.equals("configtest")) {
				try {
					if (!new File(Gateway.configurationFile).exists()) {
						System.err
								.println("sipxbridge.xml does not exist - please check configuration.");
						System.exit(-1);
					}
					Gateway.parseConfigurationFile();
					BridgeConfiguration configuration = Gateway.accountManager
							.getBridgeConfiguration();

					if (configuration.getGlobalAddress() == null
							&& configuration.getStunServerAddress() == null) {
						logger
								.error("Configuration error -- no global address or stun server");
						System.err
								.println("sipxbridge.xml: Configuration error: no global address specified and no stun server specified.");
						System.exit(-1);
					}

					if (Gateway.accountManager.getBridgeConfiguration()
							.getExternalAddress().equals(
									Gateway.accountManager
											.getBridgeConfiguration()
											.getLocalAddress())
							&& Gateway.accountManager.getBridgeConfiguration()
									.getExternalPort() == Gateway.accountManager
									.getBridgeConfiguration().getLocalPort()) {
						logger
								.error("Configuration error -- external address == internal address && external port == internal port");
						System.err
								.println("sipxbridge.xml: Configuration error: external address == internal address && external port == internal port");

						System.exit(-1);
					}
					
					Gateway.symconfig = new SymmitronConfigParser().parse(Gateway.configurationPath+ "/nattraversalrules.xml");
					
					/*
					 * Make sure we can initialize the keystores etc.
					 */
					if ( symconfig.getUseHttps() ) {
						initHttpsClient();
					}
					System.exit(0);
				} catch (Exception ex) {
					System.exit(-1);
				}
			} else if (command.equals("stop")){
				
				/*
				 * Stop bridge, release all resources and exit.
				 */
				
				Gateway.parseConfigurationFile();
				Gateway.initializeLogging();
				logger.debug("command " + command);
				/*
				 * Initialize the HTTPS client.
				 */
				if ( Gateway.getBridgeConfiguration().isSecure()) {
					Gateway.initHttpsClient();
				}
				
				/*
				 * Connect to the sipxbridge server and ask him to exit.
				 */
				SipXbridgeXmlRpcClient client = new SipXbridgeXmlRpcClient(Gateway.getBridgeConfiguration().getExternalAddress(),
						Gateway.getBridgeConfiguration().getXmlRpcPort(), Gateway.getBridgeConfiguration().isSecure());
				client.exit();
				System.exit(0);
				
			} else {
				logger.error("Bad option ");
			}

		} catch (Throwable ex) {
			ex.printStackTrace();
			logger.fatal("Unexpected exception starting", ex);
			System.exit(-1);

		}

	}

}
