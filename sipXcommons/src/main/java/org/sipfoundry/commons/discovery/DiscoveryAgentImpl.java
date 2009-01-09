/**
 * Copyright (C) 2007 - 2008 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 * Contributors retain copyright to elements licensed under a Contributor Agreement.
 * Licensed to the User under the LGPL license.
 */
package org.sipfoundry.commons.discovery;

import java.util.ListIterator;

import javax.sip.*;
import javax.sip.header.*;
import javax.sip.message.*;

import org.sipfoundry.commons.ao.*;

/**
 * [Enter descriptive text here]
 * <p>
 * 
 * @author Mardy Marshall
 */
public class DiscoveryAgentImpl extends ActiveObject implements DiscoveryAgent {
    private final String targetAddress;
    private final DiscoveryService discoveryService;
    private final SipProvider sipProvider;
    private boolean terminated = false;
    private Pinger pinger;
        

    public DiscoveryAgentImpl(String targetAddress, DiscoveryService discoveryService) {
        this.targetAddress = targetAddress;
        this.discoveryService = discoveryService;
        this.sipProvider = discoveryService.getSipProvider();
    }

    @Startup
    public void discover() {
        pinger = new Pinger(DiscoveryService.rand.nextInt(1234), targetAddress, 500);
        if (pinger.ping()) {
            processPingResponse();
        } else {
            processPingTimeout();
        }
    }

    /*
     * (non-Javadoc)
     * 
     * @see org.sipfoundry.preflight.discovery.DiscoveryAgent#processPingResponse()
     */
    @Synchronous
    public void processPingResponse() {
        // discoveryService.addDiscovered(targetAddress, "");
        // ActiveObjectGroup activeObjectGroup = getActiveObjectGroup();
        // activeObjectGroup.deleteInstance(this);
        
    	if (discoveryService.isSIPVendor(targetAddress)) {
            try {
                ClientTransaction clientTransaction;

                Request request = discoveryService.createOptionsRequest(targetAddress, 5060);
                // Create the client transaction.
                clientTransaction = sipProvider.getNewClientTransaction(request);
                clientTransaction.setApplicationData(this);
                // Send the request out.
                clientTransaction.sendRequest();
            } catch (TransactionUnavailableException e) {
                e.printStackTrace();
            } catch (SipException e) {
                e.printStackTrace();
            }
    	} else {
            if (!terminated) {
        	    ActiveObjectGroup activeObjectGroup = getActiveObjectGroup();
        	    activeObjectGroup.deleteInstance(this);
            }
    	}
    }

    /*
     * (non-Javadoc)
     * 
     * @see org.sipfoundry.preflight.discovery.DiscoveryAgent#processSIPResponse(javax.sip.ResponseEvent)
     */
    @SuppressWarnings("unchecked")
    @Synchronous
    public void processSIPResponse(ResponseEvent responseEvent) {
        String userAgentInfo = "";
        Response response = (Response) responseEvent.getResponse();
        UserAgentHeader userAgentHeader = (UserAgentHeader) response.getHeader(UserAgentHeader.NAME);
        if (userAgentHeader != null) {
            ListIterator<String> headerValues = userAgentHeader.getProduct();
            while (headerValues.hasNext()) {
                userAgentInfo += headerValues.next();
                if (headerValues.hasNext()) {
                    userAgentInfo += ", ";
                }
            }
        }
        
        if (!terminated) {
            discoveryService.addDiscovered(targetAddress, userAgentInfo);
            ActiveObjectGroup activeObjectGroup = getActiveObjectGroup();
            activeObjectGroup.deleteInstance(this);
        }
    }

    /*
     * (non-Javadoc)
     * 
     * @see org.sipfoundry.preflight.discovery.DiscoveryAgent#processPingTimeout(javax.sip.TimeoutEvent)
     */
    @Synchronous
    public void processPingTimeout() {
        if (!terminated) {
        	ActiveObjectGroup activeObjectGroup = getActiveObjectGroup();
        	activeObjectGroup.deleteInstance(this);
        }
    }

    /*
     * (non-Javadoc)
     * 
     * @see org.sipfoundry.preflight.discovery.DiscoveryAgent#processSIPTimeout(javax.sip.TimeoutEvent)
     */
    @Synchronous
    public void processSIPTimeout(TimeoutEvent timeoutEvent) {
        if (!terminated) {
        	ActiveObjectGroup activeObjectGroup = getActiveObjectGroup();
        	activeObjectGroup.deleteInstance(this);
        }
    }

    /*
     * (non-Javadoc)
     * 
     * @see org.sipfoundry.preflight.discovery.DiscoveryAgent#processTransactionTerminated(javax.sip.TransactionTerminatedEvent)
     */
    @Synchronous
    public void processTransactionTerminated(TransactionTerminatedEvent transactionTerminatedEvent) {
    }

    @Terminate
    @Synchronous
    public void terminate() {
        terminated = true;
        ActiveObjectGroup activeObjectGroup = getActiveObjectGroup();
        activeObjectGroup.deleteInstance(this);
    }
}
