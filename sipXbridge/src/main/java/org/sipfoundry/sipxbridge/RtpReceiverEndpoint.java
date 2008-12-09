/*
 *  Copyright (C) 2008 Pingtel Corp., certain elements licensed under a Contributor Agreement.
 *  Contributors retain copyright to elements licensed under a Contributor Agreement.
 *  Licensed to the User under the LGPL license.
 *
 */
package org.sipfoundry.sipxbridge;

import java.io.IOException;
import java.util.Random;
import java.util.Vector;

import javax.sdp.Connection;
import javax.sdp.MediaDescription;
import javax.sdp.Origin;
import javax.sdp.SdpFactory;
import javax.sdp.SessionDescription;

import org.apache.log4j.Logger;
import org.sipfoundry.sipxbridge.symmitron.SymEndpointInterface;

class RtpReceiverEndpoint implements SymEndpointInterface {

    private Origin origin;

    private SessionDescription sessionDescription;

    private static Logger logger = Logger.getLogger(RtpReceiverEndpoint.class);


    private long sessionId;

    private int sessionVersion;

    private SymEndpointInterface symReceiverEndpoint;

    private String ipAddress;

    private String globalAddress;

    private boolean useGlobalAddressing;

    RtpReceiverEndpoint(SymEndpointInterface symReceiverEndpoint) {
        this.symReceiverEndpoint = symReceiverEndpoint;
        this.ipAddress = symReceiverEndpoint.getIpAddress();
        try {
            this.sessionId = Math.abs(new Random().nextLong());
            this.sessionVersion = 1;

            String address = this.getIpAddress();
            origin = SdpFactory.getInstance().createOrigin(Gateway.SIPXBRIDGE_USER, sessionId,
                    sessionVersion, "IN", "IP4", address);
        } catch (Exception ex) {
            throw new RuntimeException("Unexpected exception creating origin ", ex);
        }
    }

    public String getIpAddress() {
        return this.ipAddress;
    }

    public int getPort() {
        return this.symReceiverEndpoint.getPort();
    }

    public void setIpAddress(String ipAddress) {
        this.ipAddress = ipAddress;
    }

    SessionDescription getSessionDescription() {
        return this.sessionDescription;
    }
    
    /**
     * Set the session description for this receiver. 
     * 
     * @param sessionDescription -- the session description to set. Warning - this method will update
     * the sessionDescriptor and set its addresses to the address assigned to the endpoint.
     * 
     */

    void setSessionDescription(SessionDescription sessionDescription) {
        if (this.sessionDescription != null && logger.isDebugEnabled()) {
            logger.debug("Old SD  = " + this.sessionDescription);
            logger.debug("newSD = " + sessionDescription);
        }
        
        
      

        try {
           
            String address = useGlobalAddressing ? getGlobalAddress() : getIpAddress();

            this.sessionDescription = sessionDescription;
            /*
             * draft-ietf-sipping-sip-offeranswer-08 section 5.2.5 makes it clear that a UA cannot
             * change the session id field of the o-line when making a subsequent offer/answer.
             * We use the same Origin field for all interactions.
             */
            this.sessionDescription.setOrigin(origin);
            SipUtilities.fixupSdpMediaAddresses(sessionDescription, address, this.getPort());
            

            if ( logger.isDebugEnabled() ) {
                logger.debug("sessionDescription after fixup : " + sessionDescription);
            }

        } catch (Exception ex) {
            logger.error("Unexpected exception ", ex);
            throw new RuntimeException("Unexpected exception", ex);
        }
    }

    public void setGlobalAddress(String publicAddress) {
        this.globalAddress = publicAddress;

    }

    public String getGlobalAddress() {
        return this.globalAddress;
    }

    /**
     * A flag that controls whether or not to assign global addresses to the RTP descriptor that
     * is assigned to this rtp receiver.
     * 
     * @param globalAddressingUsed
     */

    void setUseGlobalAddressing(boolean globalAddressingUsed) {
        this.useGlobalAddressing = globalAddressingUsed;
    }

}
