/*
 *  Copyright (C) 2008 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 *  Contributors retain copyright to elements licensed under a Contributor Agreement.
 *  Licensed to the User under the LGPL license.
 *
 */
package org.sipfoundry.sipxbridge.symmitron;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Properties;
import java.util.Random;
import java.util.Timer;
import java.util.TimerTask;
import java.util.concurrent.ConcurrentHashMap;

import javax.net.ssl.SSLServerSocket;

import net.java.stun4j.StunAddress;
import net.java.stun4j.client.NetworkConfigurationDiscoveryProcess;
import net.java.stun4j.client.StunDiscoveryReport;

import org.apache.log4j.Level;
import org.apache.log4j.Logger;
import org.mortbay.http.HttpContext;
import org.mortbay.http.HttpListener;
import org.mortbay.http.HttpServer;
import org.mortbay.http.SocketListener;
import org.mortbay.http.SslListener;
import org.mortbay.jetty.servlet.ServletHandler;
import org.mortbay.util.InetAddrPort;
import org.mortbay.util.ThreadedServer;
import org.sipfoundry.commons.log4j.SipFoundryAppender;
import org.sipfoundry.commons.log4j.SipFoundryLayout;
import org.sipfoundry.sipxbridge.SipXbridgeException;

/**
 * The SIPXbridge XML RPC handler.
 * 
 * @author M. Ranganathan
 * 
 */
@SuppressWarnings("unchecked")
public class SymmitronServer implements Symmitron {

    private static Logger logger = Logger.getLogger(SymmitronServer.class.getPackage().getName());

    protected static Timer timer = new Timer();

    private static String status;

    /*
     * Map that pairs the SymSession id with the SymSession
     */
    private static Map<String, Sym> sessionMap = new ConcurrentHashMap<String, Sym>();

    /*
     * Map that pairs the rtpSesion owner with the rtp session
     */
    private static Map<String, HashSet<Sym>> sessionResourceMap = new ConcurrentHashMap<String, HashSet<Sym>>();

    /*
     * A map that pairs the RtpBridge id with the rtp Bridge.
     */
    private static Map<String, Bridge> bridgeMap = new ConcurrentHashMap<String, Bridge>();

    /*
     * A map that pairs the owner with the RtpBridge
     */
    private static Map<String, HashSet<Bridge>> bridgeResourceMap = new ConcurrentHashMap<String, HashSet<Bridge>>();

    /*
     * A map of component name to instance handle.
     */
    private static Map<String, String> instanceTable = new ConcurrentHashMap<String, String>();

    private static PortRangeManager portRangeManager;

    /*
     * My Instance handle
     */
    private static String myHandle = "sipxbridge:" + Math.abs(new Random().nextLong());

    private static boolean isWebServerRunning;

    private static HttpServer webServer;

    private static InetAddress localAddressByName;

    private static InetAddress publicAddress;

    private static SymmitronConfig symmitronConfig;

    private static final int STUN_PORT = 3478;

   
    // /////////////////////////////////////////////////////////////

    /**
     * Discover our address using stun.
     * 
     * @throws SipXbridgeException
     */
    static void discoverAddress() throws Exception {
        try {

            String stunServerAddress = symmitronConfig.getStunServerAddress();

            if (stunServerAddress != null && symmitronConfig.isUseStun()) {

                // Todo -- deal with the situation when this port may be taken.
                // The port may be taken by sipxbridge.
                StunAddress localStunAddress = new StunAddress(symmitronConfig.getLocalAddress(),
                        STUN_PORT + 1);

                StunAddress serverStunAddress = new StunAddress(stunServerAddress, STUN_PORT);

                NetworkConfigurationDiscoveryProcess addressDiscovery = new NetworkConfigurationDiscoveryProcess(
                        localStunAddress, serverStunAddress);
                java.util.logging.LogManager logManager = java.util.logging.LogManager
                        .getLogManager();
                java.util.logging.Logger log = logManager
                        .getLogger(NetworkConfigurationDiscoveryProcess.class.getName());
                log.setLevel(java.util.logging.Level.OFF);

                addressDiscovery.start();
                StunDiscoveryReport report = addressDiscovery.determineAddress();
                if (report == null) {
                    logger.error("No stun report - could not do address discovery");
                    return;
                }
                StunAddress stunAddress = report.getPublicAddress();
                if (stunAddress == null) {
                    logger.error("No stun address - could not do address discovery");
                    return;
                }

                publicAddress = stunAddress.getSocketAddress().getAddress();
                logger.debug("Stun report = " + report);
                String publicAddr = publicAddress.getHostAddress();
                logger.debug("publicAddress = " + publicAddr);
                symmitronConfig.setPublicAddress(publicAddr);

            }
        } catch (Exception ex) {

            logger.error("Error discovering  address -- could be networking is bad", ex);
            throw ex;
        }
    }

    private Map<String, Object> createErrorMap(int errorCode, String reason) {
        Map<String, Object> retval = new HashMap<String, Object>();
        retval.put(STATUS_CODE, ERROR);
        retval.put(ERROR_CODE, new Integer(errorCode).toString());
        retval.put(ERROR_INFO, reason);

        retval.put(INSTANCE_HANDLE, myHandle);

        return retval;
    }

    private Map<String, Object> createSuccessMap() {
        Map<String, Object> retval = new HashMap<String, Object>();
        retval.put(STATUS_CODE, OK);
        retval.put(INSTANCE_HANDLE, myHandle);
        return retval;
    }

    private void addSymResource(String controllerId, Sym sym) {
        sessionMap.put(sym.getId(), sym);
        HashSet<Sym> syms = sessionResourceMap.get(controllerId);
        if (syms != null) {
            syms.add(sym);
        } else {
            syms = new HashSet<Sym>();
            syms.add(sym);
            sessionResourceMap.put(controllerId, syms);
        }
    }

    /**
     * Get the port range manager.
     * 
     */
    public static PortRangeManager getPortManager() {
        return portRangeManager;
    }

    public static String getLocalAddress() {
        return symmitronConfig.getLocalAddress();
    }

    public static InetAddress getLocalInetAddress() throws UnknownHostException {
        if (SymmitronServer.localAddressByName == null) {
            SymmitronServer.localAddressByName = InetAddress.getByName(getLocalAddress());
        }
        return localAddressByName;
    }

    public static int getRangeLowerBound() {

        return symmitronConfig.getPortRangeLowerBound();
    }

    public static int getRangeUpperBound() {

        return symmitronConfig.getPortRangeUpperBound();
    }

    public static void setSymmitronConfig(SymmitronConfig symmitronConfig) throws IOException {
        SymmitronServer.symmitronConfig = symmitronConfig;
        if (symmitronConfig.getPublicAddress() != null) {
            publicAddress = InetAddress.getByName(symmitronConfig.getPublicAddress());
        }
        String logFileName = symmitronConfig.getLogFileName();
        if (logFileName != null) {
            String dirName = symmitronConfig.getLogFileDirectory() + "/sipxrelay.log";
            Logger logger = Logger.getLogger(SymmitronServer.class.getPackage().getName());

            SipFoundryAppender sfa = new SipFoundryAppender(new SipFoundryLayout(), dirName);
            logger.addAppender(sfa);

            logger.setLevel(Level.toLevel(symmitronConfig.getLogLevel()));
        }
        portRangeManager = new PortRangeManager(symmitronConfig.getPortRangeLowerBound(),
                symmitronConfig.getPortRangeUpperBound());
    }

    public static void startWebServer() throws Exception {

        if (!isWebServerRunning) {

            isWebServerRunning = true;
            webServer = new HttpServer();
  
             InetAddrPort inetAddrPort = new InetAddrPort(symmitronConfig.getLocalAddress(),symmitronConfig.getXmlRpcPort() );
            inetAddrPort.setInetAddress(InetAddress.getByName(symmitronConfig.getLocalAddress()));
           
            if (symmitronConfig.getUseHttps()) { 
            	logger.info("Starting  secure xml rpc server on inetAddr:port " + symmitronConfig.getLocalAddress() + ":" +  symmitronConfig.getXmlRpcPort());
                  
                SslListener sslListener = new SslListener(inetAddrPort);
                inetAddrPort.setInetAddress(InetAddress.getByName(symmitronConfig.getLocalAddress()));
                
                String keystore = System.getProperties().getProperty("javax.net.ssl.keyStore");
                logger.info("keystore = " + keystore);
                sslListener.setKeystore(keystore);
                String keystoreType = System.getProperty("javax.net.ssl.trustStoreType");  
                logger.info("keyStoreType = "  + keystoreType); 
                sslListener.setKeystoreType(keystoreType);
                
                String algorithm =  System.getProperties().getProperty(
                        "jetty.x509.algorithm") ;
                logger.info("algorithm = " + algorithm);
                sslListener.setAlgorithm(algorithm);  
                String password = System.getProperties().getProperty("jetty.ssl.password");
                sslListener.setPassword(password);
                
                String keypassword = System.getProperties().getProperty("jetty.ssl.keypassword");
                
                sslListener.setKeyPassword(keypassword);
                  
                sslListener.setMaxThreads(32);
                sslListener.setMinThreads(4);
                sslListener.setLingerTimeSecs(30000);
              
                ((ThreadedServer)sslListener).open();
                
                String[] cypherSuites = ((SSLServerSocket)sslListener.getServerSocket()).getSupportedCipherSuites();
                
                for ( String suite: cypherSuites) {
                	logger.info("Cypher Suites enabled : " + suite);
                }
              
                ((SSLServerSocket)sslListener.getServerSocket()).setEnabledCipherSuites(cypherSuites);
                
                String[] protocols = ((SSLServerSocket)sslListener.getServerSocket()).getSupportedProtocols();
                
                for ( String protocol: protocols ) {
                	logger.info("Supported protocol = " + protocol);
                }
                
                ((SSLServerSocket)sslListener.getServerSocket()).setEnabledProtocols(protocols);
                
                webServer.setListeners(new HttpListener[] { sslListener });
                
                for ( HttpListener listener : webServer.getListeners() ) {
                    logger.debug("Listener = " + listener);
                    
                    listener.start();
                }
                
            } else {
            	logger.info("Starting unsecure xml rpc server on inetAddr:port " + symmitronConfig.getLocalAddress() + ":" +  symmitronConfig.getXmlRpcPort());
                
                SocketListener socketListener = new SocketListener(inetAddrPort);
                socketListener.setMaxThreads(32);
                socketListener.setMinThreads(4);
                socketListener.setLingerTimeSecs(30000);
                webServer.addListener(socketListener);
            }
           

        
            
            HttpContext httpContext = new HttpContext();
           
            httpContext.setContextPath("/");
            ServletHandler servletHandler = new ServletHandler();
            servletHandler.addServlet("symmitron", "/*", SymmitronServlet.class.getName());
            httpContext.addHandler(servletHandler);

            webServer.addContext(httpContext);

            webServer.start();

            logger.debug("Web server started.");

        }
    }

    /**
     * The RPC handler for sipxbridge.
     */
    public SymmitronServer() {

    }

    /**
     * Current bridge State.
     * 
     * @return the current bridge state.
     */
    public String getStatus() {
        return status;
    }

    /**
     * Start the bridge.
     * 
     */
    public String start() {

        status = "INITIALIZED";
        return status;
    }

    /**
     * Stop the bridge
     */
    public String stop() {

        for (Bridge bridge : ConcurrentSet.getBridges()) {
            bridge.stop();
        }

        timer.purge();

        status = "STOPPED";
        return status;
    }

    /**
     * Get the Port Range that is handled by the Bridge.
     * 
     * @return the port range supported by the bridge.
     */
   
	public Map getRtpPortRange() {

        PortRange portRange = new PortRange(symmitronConfig.getPortRangeLowerBound(),
                symmitronConfig.getPortRangeUpperBound());

        return portRange.toMap();

    }

    public static InetAddress getPublicInetAddress() {
        return publicAddress;
    }

    /**
     * Check for client reboot.
     * 
     * @param controllerHandle - a client handle in the form componentName:instanceId
     */
    public void checkForControllerReboot(String controllerHandle) {
        String[] handleParts = controllerHandle.split(":");
        if (handleParts.length != 2) {
           throw new IllegalArgumentException("Illegal handle format");
        }
        String componentName = handleParts[0];

        String previousInstance = instanceTable.get(componentName);
        if (previousInstance == null) {
            instanceTable.put(componentName, controllerHandle);
        } else if (!previousInstance.equals(controllerHandle)) {
            HashSet<Bridge> rtpBridges = bridgeResourceMap.get(previousInstance);
            if (rtpBridges != null) {
                for (Bridge rtpBridge : rtpBridges) {
                    rtpBridge.stop();
                }
            }
            bridgeResourceMap.remove(previousInstance);
            bridgeResourceMap.put(controllerHandle, new HashSet<Bridge>());
            HashSet<Sym> rtpSessions = sessionResourceMap.get(previousInstance);
            if (rtpSessions != null) {
                for (Sym rtpSession : rtpSessions) {
                    rtpSession.close();
                }
            }
            sessionResourceMap.remove(previousInstance);
            sessionResourceMap.put(controllerHandle, new HashSet<Sym>());

            instanceTable.put(componentName, controllerHandle);
        }
    }

    /**
     * Sign in a controller.
     */
    public Map<String, Object> signIn(String remoteHandle) {

        logger.debug("signIn " + remoteHandle);
        try {
            checkForControllerReboot(remoteHandle);
            return createSuccessMap();
        } catch (Exception ex) {
            logger.error("Error occured during processing ", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }

    }

    public Map<String, Object> getPublicAddress(String controllerHandle) {
        try {
            checkForControllerReboot(controllerHandle);
            Map<String, Object> retval = createSuccessMap();
            logger.debug(controllerHandle + " getPublicAddress : " + symmitronConfig.getPublicAddress());

            retval.put(PUBLIC_ADDRESS, symmitronConfig.getPublicAddress());

            return retval;
        } catch (Exception ex) {
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }
    }

    /*
     * (non-Javadoc)
     * 
     * @see org.sipfoundry.sipxbridge.Symmitron#createSyms(java.lang.String, int, int)
     */
    public Map<String, Object> createSyms(String controllerHandle, int count, int parity) {
        try {
            this.checkForControllerReboot(controllerHandle);

            PortRange portRange = SymmitronServer.getPortManager().allocate(count,
                    parity == ODD ? Parity.ODD : Parity.EVEN);
            if (portRange == null) {
                logger.error("Ports not available " + count + " parity " + parity);
                return createErrorMap(PORTS_NOT_AVAILABLE, "Ports not available");
            }

            Map<String, Object> retval = createSuccessMap();
            HashMap[] hmapArray = new HashMap[count];
            for (int i = 0; i < count; i++) {
                Sym sym = new Sym();
                SymReceiverEndpoint rtpEndpoint = new SymReceiverEndpoint(portRange
                        .getLowerBound()
                        + i);
                sym.setReceiver(rtpEndpoint);
                hmapArray[i] = sym.toMap();
                this.addSymResource(controllerHandle, sym);
                logger.debug(controllerHandle + " createSym : " + sym.getId());
            }

            retval.put(SYM_SESSION, hmapArray);

            return retval;
        } catch (Exception ex) {
            logger.error("error creating syms", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());

        }

    }

    /*
     * (non-Javadoc)
     * 
     * @see org.sipfoundry.sipxbridge.Symmitron#getSym(java.lang.String, java.lang.String)
     */
    public Map<String, Object> getSym(String controllerHandle, String symId) {

        logger.debug("getSym : " + controllerHandle + " symId = " + symId);
        try {
            this.checkForControllerReboot(controllerHandle);

            if (sessionMap.get(symId) != null) {
                Map<String, Object> retval = createSuccessMap();
                Sym sym = sessionMap.get(symId);
                retval.put(SYM_SESSION, sym.toMap());
                logger.debug("returning " + retval);
                return retval;

            } else {
                return createErrorMap(SESSION_NOT_FOUND, "");

            }
        } catch (Exception ex) {
            logger.error("Error processing request " + symId);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }
    }

    /*
     * (non-Javadoc)
     * 
     * @see org.sipfoundry.sipxbridge.Symmitron#setDestination(java.lang.String, java.lang.String,
     *      java.lang.String, int , int, java.lang.String, byte[], boolean)
     */
    public Map<String, Object> setDestination(String controllerHandle, String symId,
            String ipAddress, int port, int keepAliveTime, String keepaliveMethod) {
        try {
            this.checkForControllerReboot(controllerHandle);

            if (logger.isDebugEnabled()) {
                logger.debug(String.format("setDestination : " + " controllerHande %s "
                        + " symId %s " + " ipAddress %s " + " port %d "
                        + " keepAliveMethod = %s " + " keepAliveTime %d ", controllerHandle,
                        symId, ipAddress, port, keepaliveMethod, keepAliveTime));
            }

            Sym sym = sessionMap.get(symId);
            if (sym == null) {
                return createErrorMap(SESSION_NOT_FOUND, "");
            }

            // Check input arguments.
            if (ipAddress.equals("") && port != 0) {
                return createErrorMap(ILLEGAL_ARGUMENT,
                        "Must specify IP address if port is not zero");
            }
            if (ipAddress.equals(""))
                ipAddress = null;

            Map<String, Object> retval = createSuccessMap();

            // Allocate a new session if needed.
            SymTransmitterEndpoint transmitter = sym.getTransmitter() != null ? sym
                    .getTransmitter() : new SymTransmitterEndpoint();

            transmitter.setIpAddressAndPort(ipAddress, port);
            transmitter.computeAutoDiscoveryFlag();
            KeepaliveMethod method = KeepaliveMethod.valueOfString(keepaliveMethod);
            transmitter.setMaxSilence(keepAliveTime, method);

            if (sym.getTransmitter() == null) {
                sym.setTransmitter(transmitter);
            }
            return retval;

        } catch (Exception ex) {

            logger.error("Processing Error", ex);

            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }

    }

    public Map<String, Object> startBridge(String controllerHandle, String bridgeId) {
        try {
            logger.debug("startBridge: " +controllerHandle + " bridgeId = "+ bridgeId);
            this.checkForControllerReboot(controllerHandle);

            Bridge rtpBridge = bridgeMap.get(bridgeId);
            if (rtpBridge == null) {
                return createErrorMap(SESSION_NOT_FOUND, "");
            }
            rtpBridge.start();
            return createSuccessMap();
        } catch (Exception ex) {
            logger.error("Processing Error", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }
    }

    public Map<String, Object> pauseSym(String controllerHandle, String sessionId) {
        try {
            logger.debug("pauseSym: " + controllerHandle + " symId = " + sessionId );
            this.checkForControllerReboot(controllerHandle);
            Sym rtpSession = sessionMap.get(sessionId);
            if (rtpSession == null) {
                return this.createErrorMap(SESSION_NOT_FOUND,
                        "Specified RTP Session was not found " + sessionId);
            }
            if (rtpSession.getTransmitter() == null) {
                return this.createErrorMap(ILLEGAL_STATE,
                        "transmitter is not assigned for rtp session " + sessionId);
            }
            rtpSession.getTransmitter().setOnHold(true);
            return this.createSuccessMap();
        } catch (Exception ex) {
            logger.error("Processing Error", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }
    }

    /*
     * (non-Javadoc)
     * 
     * @see org.sipfoundry.sipxbridge.Symmitron#removeSym(java.lang.String, java.lang.String,
     *      java.lang.String)
     */
    public Map<String, Object> removeSym(String controllerHandle, String bridgeId, String symId) {
        try {
            logger.debug( "removeSym: " + controllerHandle + " bridgeId = "+ bridgeId + " symId = "  + symId);
            
            this.checkForControllerReboot(controllerHandle);
            Bridge rtpBridge = bridgeMap.get(bridgeId);
            if (rtpBridge == null) {
                return this.createErrorMap(SESSION_NOT_FOUND,
                        "Specified RTP Bridge was not found " + bridgeId);
            }

            Sym rtpSession = sessionMap.get(symId);

            if (rtpSession == null) {
                return this.createErrorMap(SESSION_NOT_FOUND,
                        "Specified RTP Session was not found " + symId);
            }

            rtpBridge.removeSym(rtpSession);
            return this.createSuccessMap();
        } catch (Exception ex) {
            logger.error("Processing Error", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }
    }

    /*
     * (non-Javadoc)
     * 
     * @see org.sipfoundry.sipxbridge.Symmitron#addSym(java.lang.String, java.lang.String,
     *      java.lang.String)
     */
    public Map<String, Object> addSym(String controllerHandle, String bridgeId, String symId) {
        try {
            this.checkForControllerReboot(controllerHandle);
            Bridge bridge = bridgeMap.get(bridgeId);
            logger.debug( "addSym: " + controllerHandle + " bridgeId = " +  bridgeId + " symId = " + symId);
            if (bridge == null) {
                return this.createErrorMap(SESSION_NOT_FOUND, "Specified Bridge was not found "
                        + bridgeId);
            }

            Sym sym = sessionMap.get(symId);

            if (sym == null) {
                return this.createErrorMap(SESSION_NOT_FOUND, "Specified sym was not found "
                        + symId);
            }

            bridge.addSym(sym);
            return this.createSuccessMap();
        } catch (Exception ex) {
            logger.error("Processing Error", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }
    }

    /*
     * (non-Javadoc)
     * 
     * @see org.sipfoundry.sipxbridge.Symmitron#createBridge(java.lang.String, boolean)
     */
    public Map<String, Object> createBridge(String controllerHandle) {
        try {
            this.checkForControllerReboot(controllerHandle);
            logger.debug("createBridge: " + controllerHandle );

            Bridge bridge = new Bridge();
            bridgeMap.put(bridge.getId(), bridge);
            HashSet<Bridge> bridges = bridgeResourceMap.get(controllerHandle);
            if (bridges != null) {
                bridges.add(bridge);
            } else {
                bridges = new HashSet<Bridge>();
                bridges.add(bridge);
                bridgeResourceMap.put(controllerHandle, bridges);
            }

            Map<String, Object> retval = this.createSuccessMap();
            logger.debug("createBridge: returning " + bridge.getId());
            retval.put(BRIDGE_ID, bridge.getId());
            return retval;
        } catch (Exception ex) {
            logger.error("Processing Error", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }

    }

    public Map<String, Object> pauseBridge(String controllerHandle, String bridgeId) {
        try {
            logger.debug ( " pauseBridge: " + controllerHandle + " " + bridgeId);
            this.checkForControllerReboot(controllerHandle);
            Bridge bridge = bridgeMap.get(bridgeId);
            if (bridge == null) {
                return this.createErrorMap(SESSION_NOT_FOUND, "Bridge corresponding to "
                        + bridgeId + " not found");
            }
            bridge.pause();
            return this.createSuccessMap();
        } catch (Exception ex) {
            logger.error("Processing Error", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }
    }

    public Map<String, Object> resumeBridge(String controllerHandle, String bridgeId) {
        try {
            this.checkForControllerReboot(controllerHandle);
            Bridge rtpBridge = bridgeMap.get(bridgeId);
            if (rtpBridge == null) {
                return this.createErrorMap(SESSION_NOT_FOUND, "Bridge corresponding to "
                        + bridgeId + " not found");
            }
            rtpBridge.resume();
            return this.createSuccessMap();
        } catch (Exception ex) {
            logger.error("Processing Error", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }
    }

    public Map<String, Object> resumeSym(String controllerHandle, String sessionId) {
        try {
            logger.debug("resumeSym : " + controllerHandle + " symId = " + sessionId);
            this.checkForControllerReboot(controllerHandle);
            Sym rtpSession = sessionMap.get(sessionId);
            if (rtpSession == null) {
                return this.createErrorMap(SESSION_NOT_FOUND, "Specified sym was not found "
                        + sessionId);
            }
            if (rtpSession.getTransmitter() == null) {
                return this.createErrorMap(ILLEGAL_STATE,
                        "transmitter is not assigned for rtp session " + sessionId);
            }
            rtpSession.getTransmitter().setOnHold(false);
            return this.createSuccessMap();
        } catch (Exception ex) {
            logger.error("Processing Error", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }
    }

    public Map<String, Object> getSymStatistics(String controllerHandle, String symId) {
        try {
            logger.debug("getSymStatistics : " + controllerHandle + " symId = " + symId );
            Sym sym = sessionMap.get(symId);
            if (sym == null) {
                return this.createErrorMap(SESSION_NOT_FOUND, "Specified sym was not found "
                        + symId);
            }

            Map<String, Object> stats = sym.getStats();

            Map<String, Object> retval = this.createSuccessMap();
            retval.putAll(stats);
            return retval;
        } catch (Exception ex) {
            logger.error("Processing Error", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }
    }

    public Map<String, Object> getBridgeStatistics(String controllerHandle, String bridgeId) {
        try {
            this.checkForControllerReboot(controllerHandle);

            logger.debug("getBridgeStatistics : " + controllerHandle + " bridgeId " + bridgeId);

            Bridge bridge = bridgeMap.get(bridgeId);
            if (bridge == null) {
                return this.createErrorMap(SESSION_NOT_FOUND, "Specified bridge was not found "
                        + bridgeId);
            }
            Map<String, Object> retval = this.createSuccessMap();
            retval.put(Symmitron.BRIDGE_STATE, bridge.getState().toString());
            retval.put(Symmitron.CREATION_TIME, new Long(bridge.getCreationTime()).toString());
            retval.put(Symmitron.LAST_PACKET_RECEIVED, new Long(bridge.getLastPacketTime())
                    .toString());
            retval.put(Symmitron.CURRENT_TIME_OF_DAY, new Long(System.currentTimeMillis())
                    .toString());
            retval.put(Symmitron.PACKETS_RECEIVED, new Long(bridge.pakcetsReceived).toString());
            retval.put(Symmitron.PACKETS_SENT, new Long(bridge.packetsSent).toString());
            Map[] symStats = new Map[bridge.getSyms().size()];
            int i = 0;
            for (Sym sym : bridge.getSyms()) {
                symStats[i++] = sym.getStats();
            }
            retval.put(Symmitron.SYM_SESSION_STATS, symStats);
            logger.debug("bridgeStats = " + retval);
            return retval;
        } catch (Exception ex) {
            logger.error("Processing Error", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }
    }

    public Map<String, Object> signOut(String controllerHandle) {
        try {
            logger.debug("signOut " + controllerHandle);
            HashSet<Bridge> rtpBridges = bridgeResourceMap.get(controllerHandle);
            if (rtpBridges != null) {
                for (Bridge rtpBridge : rtpBridges) {
                    rtpBridge.stop();
                }
            }
            bridgeResourceMap.remove(controllerHandle);

            HashSet<Sym> rtpSessions = sessionResourceMap.get(controllerHandle);
            if (rtpSessions != null) {
                for (Sym rtpSession : rtpSessions) {
                    rtpSession.close();
                }
            }
            sessionResourceMap.remove(controllerHandle);

            return this.createSuccessMap();
        } catch (Exception ex) {
            logger.error("Processing Error", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }
    }

    public Map<String, Object> destroySym(String controllerHandle, String symId) {

        try {
            logger.debug("destroySym: " + controllerHandle + " symId = " + symId);
            this.checkForControllerReboot(controllerHandle);

            if (sessionMap.containsKey(symId)) {
                Sym sym = sessionMap.get(symId);
                HashSet<Sym> syms = sessionResourceMap.get(controllerHandle);
                syms.remove(sym);
                if (syms.isEmpty()) {
                    sessionResourceMap.remove(controllerHandle);
                }
                sym.close();
                Map<String, Object> retval = this.createSuccessMap();
                return retval;
            } else {

                return this.createErrorMap(SESSION_NOT_FOUND,
                        "Sym with the given Id was not found");
            }

        } catch (Exception ex) {
            logger.error("Processing Error", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }
    }

    public Map<String, Object> ping(String controllerHandle) {
        try {
            logger.debug("ping : " + controllerHandle);
            this.checkForControllerReboot(controllerHandle);
            Map<String, Object> retval = createSuccessMap();
            if (sessionResourceMap.get(controllerHandle) != null) {
                HashSet<String> timedOutSyms = new HashSet<String>();

                for (Sym sym : sessionResourceMap.get(controllerHandle)) {
                    if (sym.isTimedOut()) {
                        timedOutSyms.add(sym.getId());
                    }
                }

                if (timedOutSyms.size() > 0) {
                    String[] symArray = new String[timedOutSyms.size()];
                    timedOutSyms.toArray(symArray);
                    retval.put(SYM_SESSION, symArray);
                }
            }
            return retval;
        } catch (Exception ex) {
            logger.error("Processing Error", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }

    }

    public Map<String, Object> setTimeout(String controllerHandle, String symId,
            int inactivityTimeout) {
        try {
            this.checkForControllerReboot(controllerHandle);

            if (sessionMap.containsKey(symId)) {
                Sym sym = sessionMap.get(symId);
                sym.setInactivityTimeout(inactivityTimeout);
                return this.createSuccessMap();
            } else {
                return this.createErrorMap(SESSION_NOT_FOUND,
                        "Requested SYM Session was not found");
            }
        } catch (Exception ex) {
            logger.error("Processing Error", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }
    }

    public Map<String, Object> destroyBridge(String controllerHandle, String bridgeId) {
        try {
            logger.debug("destroyBridge: " + controllerHandle + " bridgeId " +  bridgeId);
            this.checkForControllerReboot(controllerHandle);
            Bridge bridge = bridgeMap.get(bridgeId);
            if (bridge != null) {
                bridgeMap.remove(bridgeId);
                HashSet<Bridge> bridges = bridgeResourceMap.get(controllerHandle);
                bridges.remove(bridge);
                if (bridges.isEmpty()) {
                    bridgeResourceMap.remove(controllerHandle);
                }
                for (Sym sym : bridge.getSyms()) {
                    sym.close();
                    sessionMap.remove(sym.getId());
                    HashSet<Sym> syms = sessionResourceMap.get(controllerHandle);
                    syms.remove(sym);
                    if (syms.isEmpty()) {
                        sessionResourceMap.remove(controllerHandle);
                    }

                }
                return this.createSuccessMap();
            } else {
                return this.createErrorMap(SESSION_NOT_FOUND,
                        "Bridge with the given ID was not found");

            }
        } catch (Exception ex) {
            logger.error("Processing Error", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }
    }

    /*
     * (non-Javadoc)
     * 
     * @see org.sipfoundry.sipxbridge.symmitron.Symmitron#getReceiverState(java.lang.String,
     *      java.lang.String)
     */
    public Map<String, Object> getReceiverState(String controllerHandle, String symId) {
        try {
            logger.debug("getReceiverState : " + controllerHandle + " symId " + symId);
            this.checkForControllerReboot(controllerHandle);

            if (sessionMap.containsKey(symId)) {
                Sym sym = sessionMap.get(symId);
                String state = sym.getReceiver().getDatagramChannelState();
                Map retval = this.createSuccessMap();
                retval.put(Symmitron.RECEIVER_STATE, state);
                return retval;
            } else {
                return this.createErrorMap(SESSION_NOT_FOUND,
                        "Requested SYM Session was not found");
            }
        } catch (Exception ex) {
            logger.error("Processing Error", ex);
            return createErrorMap(PROCESSING_ERROR, ex.getMessage());
        }
    }

    /**
     * Test method - stop the xml rpc server.
     */
    static void stopXmlRpcServer() {
        try {
            SymmitronServer.webServer.stop();
        } catch (InterruptedException e) {
            logger.error("request processing interrupt", e);
        }
    }

    public static void main(String[] args) throws Exception {
        try {
            String configDir = System.getProperty("conf.dir", "/etc/sipxpbx");
            String configurationFile = configDir + "/nattraversalrules.xml";
            String command = System.getProperty("sipxrelay.command", "start");
            
            String addressToTest = null;

            if (command.equals("configtest")) {
                try {

                    if (!new File(configurationFile).exists()) {
                        System.exit(-1);
                    }
                    SymmitronConfig config = new SymmitronConfigParser().parse("file:"
                            + configurationFile);
                    SymmitronServer.setSymmitronConfig(config);
                    
                    if (config.getLogFileDirectory() == null) {
                        String installRoot = configDir.substring(0, configDir
                                .indexOf("/etc/sipxpbx"));
                        config.setLogFileDirectory(installRoot + "/var/log/sipxpbx");
                    }
                    config.setLogFileName("sipxrelay.log");

                    if (config.getLocalAddress() == null) {
                        System.err.println("Local address not specified");
                        System.exit(-1);
                    }

                    if (config.getPublicAddress() == null
                            && config.getStunServerAddress() == null && config.isBehindNat() ) {
                        System.err
                                .println("Must specify either public address or stun server address");
                        System.exit(-1);
                    }
                    
                    if ( config.getStunServerAddress() == null && config.isUseStun() ) {
                        System.err
                            .println("Must specify STUN server address if address discovery is desired.");
                        System.exit(-1);
                    }

                    if (config.getPublicAddress() != null) {
                        addressToTest = config.getPublicAddress();
                        InetAddress.getByName(config.getPublicAddress());
                    }

                    /*
                     * Test if host names can be resolved.
                     */
                    if (config.getStunServerAddress() != null) {
                        addressToTest = config.getStunServerAddress();
                        InetAddress.getByName(config.getStunServerAddress());
                    }

                    addressToTest = config.getLocalAddress();

                    InetAddress.getByName(config.getLocalAddress());

                    System.exit(0);

                } catch (UnknownHostException ex) {
                    System.err.println("Host name error -- could not resolve " + addressToTest);
                    System.exit(-1);
                } catch (Exception ex) {
                    logger.fatal(
                            "Exception occured while checking configuration" + " -- exiting", ex);
                    ex.printStackTrace();
                    System.exit(-1);
                }
            } else if (command.equals("start")) {
                // Wait for the configuration file to become available.
                while (!new File(configurationFile).exists()) {
                    Thread.sleep(5 * 1000);
                }
                SymmitronConfig config = new SymmitronConfigParser().parse("file:"
                        + configurationFile);

                InetAddress localAddr = InetAddress.getByName(config.getLocalAddress());

                if (config.getLogFileDirectory() == null) {
                    String installRoot = configDir
                            .substring(0, configDir.indexOf("/etc/sipxpbx"));
                    config.setLogFileDirectory(installRoot + "/var/log/sipxpbx");
                }
                config.setLogFileName("sipxrelay.log");

                String log4jProps = configDir + "/log4j.properties";
                /*
                 * Allow override if a log4j properties file exists.
                 */
                if (new File(log4jProps).exists()) {
					/*
					 * Override the file configuration setting.
					 */
					Properties props = new Properties();
					props.load(new FileInputStream(log4jProps));
					String level = props
							.getProperty("log4j.category.org.sipfoundry.sipxbridge.sipxrelay");
					if (level != null) {
						config.setLogLevel(level);
					}
				}
                SymmitronServer.setSymmitronConfig(config);

                logger.info("Checking port range " + config.getPortRangeLowerBound() + ":"
                        + config.getPortRangeUpperBound());
                for (int i = config.getPortRangeLowerBound(); i < config.getPortRangeUpperBound(); i++) {
                    try {
                        DatagramSocket sock = new DatagramSocket(i, localAddr);
                        sock.close();
                    } catch (Exception ex) {
                        logger.error(String.format("Failed to bind to %s:%d", localAddr, i), ex);
                        throw ex;
                    }
                }
                logger.info("Port range checked ");

                if (config.getPublicAddress() == null && config.getStunServerAddress() != null) {
                    /*
                     * Try an address discovery. If it did not work, then exit. This deals with
                     * accidental mis-configurations of the STUN server address.
                     */
                    discoverAddress();
                    timer.schedule(new TimerTask() {

                        @Override
                        public void run() {
                            try {
                                discoverAddress();
                            } catch (Exception ex) {
                                logger.error("Error discovering address - stun server down?");
                            }

                        }

                    }, config.getRediscoveryTime() * 1000, config.getRediscoveryTime() * 1000);
                }

                Runtime.getRuntime().addShutdownHook(new Thread() {
                    public void run() {

                        logger.fatal("RECEIVED SHUTDOWN SIGNAL");

                    }
                });

                SymmitronServer.startWebServer();
            } else {
                System.err.println("unknown start option " + command);
            }

        } catch (Throwable th) {
            logger.fatal("Exitting main! ", th);
            logger.fatal("Cause = " + th.getCause());
            System.exit(-1);
        }
    }

}
