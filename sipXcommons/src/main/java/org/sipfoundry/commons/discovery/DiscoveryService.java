/**
 * Copyright (C) 2007 - 2008 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 */
package org.sipfoundry.commons.discovery;

import javax.sip.*;
import javax.sip.address.*;
import javax.sip.header.*;
import javax.sip.message.*;

import org.sipfoundry.commons.ao.ActiveObjectGroupImpl;
import org.sipfoundry.commons.util.JournalService;

import java.util.*;
import java.text.*;

/**
 * @author Mardy Marshall
 */

public class DiscoveryService extends ActiveObjectGroupImpl<String> implements SipListener {
    private String localHostAddress;
    private SipFactory sipFactory;
    private SipProvider sipProvider;
    private AddressFactory addressFactory;
    private MessageFactory messageFactory;
    private HeaderFactory headerFactory;
    private SipStack sipStack;
    private ContactHeader contactHeader;
    private ListeningPoint listeningPoint;
    private CallIdHeader callIdHeader;
    private CSeqHeader cSeqHeader;
    private FromHeader fromHeader;
    private ArrayList<ViaHeader> viaHeaders;
    private MaxForwardsHeader maxForwards;
    private final String localHostTransport = "udp";
    private final JournalService journalService;
    private LinkedList<Device> devices;

    private final int discoveryBlockSize = 64;

    private class UAVendor {
        public final String name;
        public final String oui;

        public UAVendor(String name, String oui) {
            this.name = name;
            this.oui = oui;
        }
    }

        private final UAVendor[] UAVendorList = {

            new UAVendor("Aastra", "00:08:5D"),
            new UAVendor("Aastra", "00:10:BC"),
            new UAVendor("AudioCodes", "00:90:8F"),
            new UAVendor("ClearOne", "00:90:79"),
            new UAVendor("Gentner", "00:06:24"),
            new UAVendor("Grandstream", "00:0B:82"),
            new UAVendor("ipDialog", "00:04:1C"),
            new UAVendor("LG-Nortel", "00:40:5A"),
            new UAVendor("LG-Nortel", "00:1A:7E"),
            new UAVendor("Linksys", "00:0E:08"),
            new UAVendor("MITEL", "08:00:0F"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:00:75"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:01:81"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:02:5F"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:03:42"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:03:4B"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:04:38"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:04:DC"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:09:97"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:0C:F7"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:0C:F8"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:0E:40"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:0E:62"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:0E:C0"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:0F:06"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:0F:6A"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:0F:CD"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:10:45"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:11:58"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:11:F9"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:12:83"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:13:0A"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:13:65"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:14:0D"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:14:0E"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:14:C7"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:15:40"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:15:9B"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:15:EB"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:16:60"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:16:CA"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:17:65"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:17:D1"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:18:B0"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:19:69"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:19:E1"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:1A:8F"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:1B:25"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:1B:BA"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:1C:17"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:1C:9C"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:1C:EB"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:1D:42"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:1D:AF"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:1E:1F"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:1E:7E"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:1E:CA"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:1F:0A"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:1F:46"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:1F:9A"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:1F:DA"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:20:D8"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:21:62"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:21:E1"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:22:67"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:23:0D"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:24:00"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:60:38"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:90:8E"),
            new UAVendor("Nortel IP Phone 1210/1220/1230", "00:90:CF"),
            new UAVendor("Pingtel Xpressa", "00:D0:1E"),
            new UAVendor("Polycom", "00:04:F2"),
            new UAVendor("SNOM", "00:04:13"),

    };

                                                                           
    public static Random rand = new Random();
    
    public DiscoveryService(String localHostAddress, int localHostPort) {
        this(localHostAddress, localHostPort, null);
    }

    public DiscoveryService(String localHostAddress, int localHostPort, JournalService journalService) {
        super("PreflightDiscovery", Thread.NORM_PRIORITY, 64, 3000);

        this.localHostAddress = localHostAddress;
        this.journalService = journalService;

        sipFactory = SipFactory.getInstance();
        sipFactory.setPathName("gov.nist");

        devices = new LinkedList<Device>();

        Properties properties = new Properties();
        properties.setProperty("javax.sip.STACK_NAME", "Preflight");
        properties.setProperty("gov.nist.javax.sip.MAX_MESSAGE_SIZE", "1048576");
        properties.setProperty("gov.nist.javax.sip.DEBUG_LOG", "PreflightDebug.txt");
        properties.setProperty("gov.nist.javax.sip.SERVER_LOG", "PreflightLog.txt");
        properties.setProperty("gov.nist.javax.sip.TRACE_LEVEL", "0");
        // Drop the client connection after we are done with the transaction.
        properties.setProperty("gov.nist.javax.sip.CACHE_CLIENT_CONNECTIONS", "false");

        try {
            // Create SipStack object
            sipStack = sipFactory.createSipStack(properties);
        } catch (PeerUnavailableException e) {
            // could not find gov.nist.jain.protocol.ip.sip.SipStackImpl in the
            // classpath.
            e.printStackTrace();
            System.err.println(e.getMessage());
            System.exit(0);
        }
        try {
            headerFactory = sipFactory.createHeaderFactory();
            addressFactory = sipFactory.createAddressFactory();
            messageFactory = sipFactory.createMessageFactory();
            listeningPoint = sipStack.createListeningPoint(localHostAddress, localHostPort, localHostTransport);
            sipProvider = sipStack.createSipProvider(listeningPoint);
            DiscoveryService listener = this;
            sipProvider.addSipListener(listener);

        } catch (PeerUnavailableException e) {
            e.printStackTrace();
            System.err.println(e.getMessage());
            System.exit(0);
        } catch (Exception e) {
            e.printStackTrace();
        }

        String fromName = "preflight";
        String fromSipAddress = "pingtel.com";
        String fromDisplayName = "Preflight";

        try {
            // create From Header
            SipURI fromAddress;
            fromAddress = addressFactory.createSipURI(fromName, fromSipAddress);

            Address fromNameAddress = addressFactory.createAddress(fromAddress);
            fromNameAddress.setDisplayName(fromDisplayName);
            fromHeader = headerFactory.createFromHeader(fromNameAddress, Long.toHexString(System.currentTimeMillis()));

            // Create ViaHeaders
            viaHeaders = new ArrayList<ViaHeader>();

            // Create a new CallId header
            callIdHeader = sipProvider.getNewCallId();

            // Create a new Cseq header
            cSeqHeader = headerFactory.createCSeqHeader(1L, Request.OPTIONS);

            // Create contact headers
            SipURI contactUrl = addressFactory.createSipURI(fromName, localHostAddress);
            contactUrl.setPort(localHostPort);

            // Create the contact name address.
            SipURI contactURI = addressFactory.createSipURI(fromName, localHostAddress);
            contactURI.setPort(localHostPort);

            Address contactAddress = addressFactory.createAddress(contactURI);

            // Add the contact address.
            contactAddress.setDisplayName(fromName);

            contactHeader = headerFactory.createContactHeader(contactAddress);

            // Create a new MaxForwardsHeader
            maxForwards = headerFactory.createMaxForwardsHeader(70);
        } catch (ParseException e) {
            // TODO Auto-generated catch block
            e.printStackTrace();
        } catch (InvalidArgumentException e) {
            // TODO Auto-generated catch block
            e.printStackTrace();
        }

    }

    public SipProvider getSipProvider() {
        return sipProvider;
    }

    public void processRequest(RequestEvent requestEvent) {
    }

    public void processResponse(ResponseEvent responseEvent) {
        ClientTransaction tid = responseEvent.getClientTransaction();
        if (tid == null) {
            return;
        }

        DiscoveryAgent discoveryAgent = (DiscoveryAgent) tid.getApplicationData();
        discoveryAgent.processSIPResponse(responseEvent);

    }

    public void processTimeout(javax.sip.TimeoutEvent timeoutEvent) {
        ClientTransaction tid = timeoutEvent.getClientTransaction();
        if (tid != null) {
            DiscoveryAgent discoveryAgent = (DiscoveryAgent) tid.getApplicationData();
            discoveryAgent.processSIPTimeout(timeoutEvent);
        }
    }

    public void processIOException(IOExceptionEvent exceptionEvent) {
        DiscoveryAgent discoveryAgent = (DiscoveryAgent) findInstance(exceptionEvent.getHost());
        discoveryAgent.terminate();
    }

    public void processTransactionTerminated(TransactionTerminatedEvent transactionTerminatedEvent) {
        ClientTransaction tid = transactionTerminatedEvent.getClientTransaction();
        if (tid != null) {
            DiscoveryAgent discoveryAgent = (DiscoveryAgent) tid.getApplicationData();
            discoveryAgent.processTransactionTerminated(transactionTerminatedEvent);
        }
    }

    public void processDialogTerminated(DialogTerminatedEvent dialogTerminatedEvent) {
        System.err.println("dialogTerminatedEvent");

    }

    public synchronized Request createOptionsRequest(String destinationAddress, int destinationPort) {
        Request request = null;
        String destinationAddressPort = new String(destinationAddress + ":" + new Integer(destinationPort).toString());

        try {
            // Create To Header.
            Address toAddress = addressFactory.createAddress(destinationAddressPort);
            ToHeader toHeader = headerFactory.createToHeader(toAddress, null);

            // Create Request URI.
            SipURI requestURI = addressFactory.createSipURI("", destinationAddressPort);

            // Create the request.
            request = messageFactory.createRequest(requestURI, Request.OPTIONS, callIdHeader, cSeqHeader, fromHeader, toHeader,
                    viaHeaders, maxForwards);

            request.addHeader(contactHeader);
        } catch (ParseException e) {
            e.printStackTrace();
        }

        return request;
    }

    public synchronized boolean isSIPVendor(String networkAddress) {
        String hardwareAddress = ArpTable.lookup(networkAddress);
        if (hardwareAddress != null) {
            String vendor = null;
            for (int x = 0; x < UAVendorList.length; x++) {
                if (hardwareAddress.startsWith(UAVendorList[x].oui)) {
                    vendor = UAVendorList[x].name;
                }
            }
            if (vendor != null) {
            	return true;
            } else {
            	return false;
            }
        } else {
        	return false;
        }
    }
    
    public synchronized void addDiscovered(String networkAddress, String userAgentInfo) {
        String hardwareAddress = ArpTable.lookup(networkAddress);
        if (hardwareAddress != null) {
            String vendor = null;
            for (int x = 0; x < UAVendorList.length; x++) {
                if (hardwareAddress.startsWith(UAVendorList[x].oui)) {
                    vendor = UAVendorList[x].name;
                }
            }
            if (vendor != null) {
                devices.add(new Device(hardwareAddress, networkAddress, vendor, userAgentInfo));

                if (journalService != null) {
                    String output = hardwareAddress + ", " + networkAddress + ", " + vendor;
                    if (userAgentInfo.compareTo("") != 0) {
                        output += ", " + userAgentInfo;
                    }
                    journalService.println(output);
                }
            }
        }
    }

    public LinkedList<Device> discover(String network, String networkMask, boolean reportProgress) {
        int range;
        int discoveryCount;
        int percentageCompleted = 0;
        int[] addr = new int[4];
        int[] mask = new int[4];

        // Break up the network address and mask into sub-fields and then determine the
        // starting address and range.
        String[] fields = network.split("[.]");
        if (fields.length == 4) {
            for (int i = 0; i < 4; i++) {
                addr[i] = Integer.parseInt(fields[i]) & 0xFF;
            }
        }
        fields = networkMask.split("[.]");
        if (fields.length == 4) {
            for (int i = 0; i < 4; i++) {
                mask[i] = Integer.parseInt(fields[i]) & 0xFF;
            }
        }

        // Convert the mask to CIDR format.
        int cidr = 32;
        for (int i = 3; i >= 0; i--) {
            int test = 0;
            for (int bitField = 1; bitField < 256; bitField <<= 1) {
                test = mask[i] & bitField;
                if (test == 0) {
                    cidr -= 1;
                } else {
                    break;
                }
            }
            if (test != 0) {
                break;
            }
        }

        // Calculate range based upon the CIDR.
        range = (int) Math.pow(2, (32 - cidr)) - 2;

        ++addr[3];
        discoveryCount = range;

        // Walk through the range of IP addresses, creating DiscoveryAgents for each address.
        while (discoveryCount > 0) {
            // So as not to flood the network, only send out a small number of requests at a time.
            try {
                String target = String.format("%d.%d.%d.%d", addr[0], addr[1], addr[2], addr[3]);
                if (target.compareTo(localHostAddress) != 0) {
                    // Otherwise skip ourself.
                    newActiveObject(new DiscoveryAgentImpl(target, this), target);
                }
                // Calculate the next IP address.
                if (++addr[3] == 256) {
                    addr[3] = 0;
                    if (++addr[2] == 256) {
                        addr[2] = 0;
                        ++addr[1];
                    }
                }
            } catch (Exception e) {
                e.printStackTrace();
            }
            discoveryCount -= 1;

            // Wait for some of the discovery requests to finish before sending more.
            try {
                while (size() == discoveryBlockSize) {
                    if (reportProgress) {
                        // Calculate and report percentage of completion.
                        int percent = ((range - discoveryCount) * 100) / range;
                        if (percent != percentageCompleted) {
                            percentageCompleted = percent;
                            System.err.println(percentageCompleted);
                        }
                    }
                    Thread.sleep(10);
                }
            } catch (InterruptedException e) {
            }
        }

        // Wait for the remaining discovery requests to finish.
        try {
            while (size() > 0) {
                Thread.sleep(100);
            }
        } catch (InterruptedException e) {
        }

        return devices;

    }

}
