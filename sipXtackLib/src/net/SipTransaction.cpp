//
// Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.
// Contributors retain copyright to elements licensed under a Contributor Agreement.
// Licensed to the User under the LGPL license.
//
//
// $$
////////////////////////////////////////////////////////////////////////
//////



// SYSTEM INCLUDES
#include <stdlib.h>
#include <assert.h>

// APPLICATION INCLUDES
#include <os/OsDateTime.h>
#include <os/OsTimer.h>
#include <os/OsQueuedEvent.h>
#include <os/OsEvent.h>
#include <net/SipTransaction.h>
#include <net/BranchId.h>
#include <net/SipMessage.h>
#include <net/SipUserAgent.h>
#include <net/SipMessageEvent.h>
#include <net/NetMd5Codec.h>
#include <net/SipTransactionList.h>

// EXTERNAL FUNCTIONS
// EXTERNAL VARIABLES
// CONSTANTS
#define SIP_UDP_RESEND_TIMES 7          // Maximum number of times to send/resend messages via UDP
#define MIN_Q_DELTA_SQUARE 0.0000000001 // Smallest Q difference is 0.00001
#define UDP_LARGE_MSG_LIMIT  1200       // spec says 1300, but we may have to add another via

//#define LOG_FORKING
//#define ROUTE_DEBUG
//#define DUMP_TRANSACTIONS   
//#define RESPONSE_DEBUG

//#define LOG_TRANSLOCK

/* //////////////////////////// PUBLIC //////////////////////////////////// */

/* ============================ CREATORS ================================== */

// Constructor
SipTransaction::SipTransaction(SipMessage* initialMsg,
                               UtlBoolean  isOutgoing,
                               UtlBoolean  userAgentTransaction,
                               BranchId*   parentBranch
                               )
   : mpBranchId(NULL)
   , mRequestMethod("")
   , mIsUaTransaction(userAgentTransaction)
   , mSendToPort(PORT_NONE)
   , mSendToProtocol(OsSocket::UNKNOWN)
   , mpDnsDestinations(NULL)
   , mpRequest(NULL)
   , mpLastProvisionalResponse(NULL)
   , mpLastFinalResponse(NULL)
   , mpAck(NULL)
   , mpCancel(NULL)
   , mpCancelResponse(NULL)
   , mpParentTransaction(NULL)
   , mTransactionStartTime(-1)
   , mTransactionState(TRANSACTION_LOCALLY_INIITATED)
   , mDispatchedFinalResponse(FALSE)
   , mProvisionalSdp(FALSE)
   , mIsCanceled(FALSE)
   , mIsRecursing(FALSE)
   , mIsDnsSrvChild(FALSE)
   , mQvalue(1.0)
   , mExpires(-1)
   , mIsBusy(FALSE)
   , mWaitingList(NULL)
{

#  ifdef ROUTE_DEBUG
   {
      OsSysLog::add(FAC_SIP, PRI_DEBUG,
                    "SipTransaction::_ new %p msg %p %s %s",
                    this, &initialMsg,
                    isOutgoing ? "OUTGOING" : "INCOMING",
                    userAgentTransaction ? "UA" : "SERVER"
                    );
   }
#  endif

   if(initialMsg)
   {
       mIsServerTransaction = initialMsg->isServerTransaction(isOutgoing);

       initialMsg->getCallIdField(&mCallId);

       // Set the hash key
       buildHash(*initialMsg, isOutgoing, *this);

       initialMsg->getCSeqField(&mCseq, &mRequestMethod);
       if(!initialMsg->isResponse())
       {
           initialMsg->getRequestUri(&mRequestUri);
           initialMsg->getRequestMethod(&mRequestMethod);

           // Do not attach the request here as it will get passed in
           // later for handleOutgoing or handleIncoming

           if(   0 != mRequestMethod.compareTo(SIP_INVITE_METHOD) // not INVITE
              || !initialMsg->getExpiresField(&mExpires))            // or no Expires header field
           {
               mExpires = -1;
           }
       }
       else // this is a response
       {
           // Do not attach the response here as it will get passed in
           // later for handleOutgoing or handleIncoming
       }

       initialMsg->getToUrl(mToField);
       initialMsg->getFromUrl(mFromField);

       if(!mIsServerTransaction) // is this a new client transaction?
       {
          if (mIsUaTransaction)
          {
             // Yes - create a new branch id
             mpBranchId = new BranchId(*initialMsg);
          }
          else
          {
             if (parentBranch)
             {
                // child transaction - use loop key from parent
                mpBranchId = new BranchId(*parentBranch, *initialMsg);
             }
             else
             {
                // new server transaction
                mpBranchId = new BranchId(*initialMsg);
             }
          }
       }
       else
       {
          // This is a server transaction, so the branch id is
          // created by the client and passed in the via
           UtlString viaField;
           initialMsg->getViaFieldSubField(&viaField, 0);
           UtlString branchValue;
           SipMessage::getViaTag(viaField.data(), "branch", branchValue);
           mpBranchId = new BranchId(branchValue);
       }
   }
   else
   {
       mIsServerTransaction = FALSE;
   }

   touch(); // sets mTimeStamp
   mTransactionCreateTime = mTimeStamp;
#ifdef DUMP_TRANSACTIONS 
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::constructor dumps whole TransactionTree ");
    justDumpTransactionTree();
#endif
}


// Destructor
SipTransaction::~SipTransaction()
{
#ifdef TEST_PRINT
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::~ *******************************");
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::~ Deleting messages at:");
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::~    %p", mpRequest);
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::~    %p", mpLastProvisionalResponse);
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::~    %p", mpLastFinalResponse);
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::~    %p", mpAck);
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::~    %p", mpCancel);
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::~    %p", mpCancelResponse);
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::~ *******************************");
#endif

    // Optimization: stop timers before doing anything else
    deleteTimers();

    if(mpBranchId)
    {
       delete mpBranchId;
       mpBranchId = NULL;
    }

    if(mpRequest)
    {
       delete mpRequest;
       mpRequest = NULL;
    }

    if(mpLastProvisionalResponse)
    {
       delete mpLastProvisionalResponse;
       mpLastProvisionalResponse = NULL;
    }

    if(mpLastFinalResponse)
    {
       delete mpLastFinalResponse;
       mpLastFinalResponse = NULL;
    }

    if(mpAck)
    {
       delete mpAck;
       mpAck = NULL;
    }

    if(mpCancel)
    {
       delete mpCancel;
       mpCancel = NULL;
    }

    if(mpCancelResponse)
    {
       delete mpCancelResponse;
       mpCancelResponse = NULL;
    }

    if(mpDnsDestinations)
    {
       delete[] mpDnsDestinations;
    }

    if(mWaitingList)
    {
        int numEvents = mWaitingList->entries();

        if(mpParentTransaction)
        {
            OsSysLog::add(FAC_SIP, PRI_ERR, "SipTransaction::~SipTransaction"
                          " non parent has %d waiting events",
                          numEvents);
        }

        if(numEvents > 0)
        {
            // Cannot call signalAllAvailable as it traverses what
            // may be a broken (i.e. partially deleted) tree
            UtlVoidPtr* eventNode = NULL;
            while ((eventNode = (UtlVoidPtr*) mWaitingList->get()))
            {
                if(eventNode)
                {
                    OsEvent* waitingEvent = (OsEvent*) eventNode->getValue();
                    if(waitingEvent)
                    {
                        // If it is already signaled, the other side
                        // is no longer waiting for the event, so this
                        // side must delete the event.
                        if(waitingEvent->signal(0) == OS_ALREADY_SIGNALED)
                        {
                            delete waitingEvent;
                            waitingEvent = NULL;
                        }
                    }
                    delete eventNode;
                    eventNode = NULL;
                }
            }

            OsSysLog::add(FAC_SIP, PRI_ERR,
                          "SipTransaction::~ %d waiting events in list",
                          numEvents);
        }

        delete mWaitingList;
        mWaitingList = NULL;
    }
}

/* ============================ MANIPULATORS ============================== */

// Assignment operator
SipTransaction&
SipTransaction::operator=(const SipTransaction& rhs)
{
   if (this == &rhs)            // handle the assignment to self case
      return *this;

   return *this;
}

enum SipTransaction::messageRelationship
SipTransaction::addResponse(SipMessage*& response,
                            UtlBoolean isOutGoing,  // introvert/extrovert
                            enum messageRelationship relationship) // casual/serious
{
    if(relationship == MESSAGE_UNKNOWN)
    {
        relationship = whatRelation(*response, isOutGoing);
    }

    switch(relationship)
    {

    case MESSAGE_REQUEST:
        // I do not know why this should ever occur
        // Typically the transaction will first be created with a request
        if(mpRequest)
        {
           OsSysLog::add(FAC_SIP, PRI_WARNING,
                         "SipTransaction::addResponse"
                         " of request to existing transaction, IGNORED");
            delete response ;
            response = NULL;
        }
        else
        {
            mpRequest = response;
        }

        if(mTransactionState < TRANSACTION_CALLING)
        {
            mTransactionState = TRANSACTION_CALLING;
            OsTime time;
            OsDateTime::getCurTimeSinceBoot(time);
            mTransactionStartTime = time.seconds();
        }
        break;

    case MESSAGE_PROVISIONAL:
        if(mpLastProvisionalResponse)
        {
            delete mpLastProvisionalResponse;
        }
        mpLastProvisionalResponse = response;
        if(mTransactionState < TRANSACTION_PROCEEDING)
        {
           mTransactionState = TRANSACTION_PROCEEDING;
        }
        // If check if there is early media
        // We need this state member as there may be multiple
        // provisional responses and we cannot rely upon the fact
        // that the last one has SDP or not to indicate that there
        // was early media or not.
        if(!mProvisionalSdp)
        {
            if((response->hasSdpBody()))
            {
                mProvisionalSdp = TRUE;
            }
        }
        break;

    case MESSAGE_FINAL:
        if(mpLastFinalResponse)
        {
            delete mpLastFinalResponse;
        }
        mpLastFinalResponse = response;
        if(mTransactionState < TRANSACTION_COMPLETE)
        {
           mTransactionState = TRANSACTION_COMPLETE;
        }
        break;

    case MESSAGE_ACK:
    case MESSAGE_2XX_ACK:
        // relate error ACK or 2xx-ACK-at-the-server to the original INVITE transaction
        // 2xx ACK will be sent to proxy and forwarded to next hop
        if(mpAck)
        {
            OsSysLog::add(FAC_SIP, PRI_WARNING,
                          "SipTransaction::addResponse" 
                          " ACK already exists, IGNORED");
            delete response ;
            response = NULL;
        }
        else
        {
            mpAck = response;
        }
        break;

    case MESSAGE_2XX_ACK_PROXY:
        // special case 2xx-ACK-at-proxy needs to be sent as new SIP request to the next hop
        if(mpRequest)
        {
           OsSysLog::add(FAC_SIP, PRI_WARNING,
                         "SipTransaction::addResponse"
                         " of 2xx ACK to transaction with existing request message, IGNORED");
            delete response ;
            response = NULL;
        }
        else
        {
            mpRequest = response;
        }
        break;


    case MESSAGE_CANCEL:
        if(mpCancel)
        {
            OsSysLog::add(FAC_SIP, PRI_WARNING,
                          "SipTransaction::addResponse CANCEL already exists, IGNORED");
            delete response ;
            response = NULL;
        }
        else
        {
            mpCancel = response;
        }
        break;

    case MESSAGE_CANCEL_RESPONSE:
        if(mpCancelResponse)
        {
            OsSysLog::add(FAC_SIP, PRI_WARNING,
                          "SipTransaction::addResponse CANCEL response already exists, IGNORED");
            delete response ;
            response = NULL;
        }
        else
        {
            mpCancelResponse = response;
        }
        break;

    case MESSAGE_UNKNOWN:
    case MESSAGE_UNRELATED:
    case MESSAGE_DUPLICATE:
    default:
        OsSysLog::add(FAC_SIP, PRI_ERR,
                      "SipTransaction::addResponse message with bad relationship: %d",
                      relationship);
        delete response ;
        response = NULL;
        break;
    }

    return(relationship);
}

UtlBoolean SipTransaction::handleOutgoing(SipMessage& outgoingMessage,
                                          SipUserAgent& userAgent,
                                          SipTransactionList& transactionList,
                                          enum messageRelationship relationship)
{
    UtlBoolean isResponse = outgoingMessage.isResponse();
    SipMessage* message = &outgoingMessage;
    UtlBoolean isOrignalRequest = FALSE;
    UtlBoolean sendSucceeded = FALSE;
    UtlString method;
    int cSeq;
    UtlString seqMethod;

    outgoingMessage.getCSeqField(&cSeq, &seqMethod);
    outgoingMessage.getRequestMethod(&method);

    if (relationship == MESSAGE_UNKNOWN)
    {
       relationship = whatRelation(outgoingMessage, TRUE);
    }

    if (relationship == MESSAGE_DUPLICATE)
    {
        // If this transaction was contructed with this message
        // it will appear as a duplicate
        if(!isResponse &&
            mpRequest &&
            !mIsServerTransaction &&
            mpRequest->getTimesSent() == 0 &&
            mRequestMethod.compareTo(method) == 0)
        {
            isOrignalRequest = TRUE;
            message = mpRequest;
        }
        else
        {
            OsSysLog::add(FAC_SIP, PRI_WARNING,
                          "SipTransaction::handleOutgoing send of duplicate message");
        }

    }

    UtlBoolean addressRequiresDnsSrvLookup(FALSE);
    UtlString toAddress;
    int port = PORT_NONE;
    OsSocket::IpProtocolSocketType protocol = OsSocket::UNKNOWN;

    if(isResponse)
    {
        UtlString protocolString;
        message->getResponseSendAddress(toAddress,
                                        port,
                                        protocolString);
#       ifdef ROUTE_DEBUG
        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                      "SipTransaction::handleOutgoing"
                      " called getResponseSendAddress, "
                      "returned toAddress = '%s', port = %d, protocolString = '%s'",
                      toAddress.data(), port, protocolString.data());
#       endif
        SipMessage::convertProtocolStringToEnum(protocolString.data(),
                                                protocol);
    }
    else
    {
        // Fix the request so that it is ready to send
        prepareRequestForSend(*message,
                              userAgent,
                              addressRequiresDnsSrvLookup,      // decision is returned
                              toAddress,
                              port,
                              protocol);
#       ifdef ROUTE_DEBUG
        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                      "SipTransaction::handleOutgoing"
                      " called prepareRequestForSend, returned toAddress = '%s', port = %d, "
                      "protocol = OsSocket::SocketProtocolTypes(%d), "
                      "addressRequiresDnsSrvLookup = %d",
                      toAddress.data(), port, protocol,
                      addressRequiresDnsSrvLookup);
#       endif

        if (toAddress.isNull())
        {
           UtlString bytes;
           ssize_t length;
           message->getBytes(&bytes, &length);
           OsSysLog::add(FAC_SIP, PRI_ERR,
                         "SipTransaction::handleOutgoing Unable to obtain To address.  Message is '%s'",
                         bytes.data());
        }

        if(mSendToAddress.isNull())
        {
#          ifdef ROUTE_DEBUG
           OsSysLog::add(FAC_SIP, PRI_DEBUG,
                         "SipTransaction::handleOutgoing setting mSendTo* variables");
#          endif
            mSendToAddress = toAddress;
            mSendToPort = port;
            mSendToProtocol = protocol;
        }
    }   // end prep for send

    // Do not send out CANCEL requests on DNS SRV parents.
    // They do not actually send requests and so should not
    // send CANCELs either.
    if(   !isResponse
       && !mIsDnsSrvChild
       && (method.compareTo(SIP_CANCEL_METHOD) == 0))
    {
        if (OsSysLog::willLog(FAC_SIP, PRI_DEBUG))
        {
            UtlString requestString;
            ssize_t len;
            outgoingMessage.getBytes(&requestString, &len);
            UtlString transString;
            toString(transString, TRUE);
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::handleOutgoing "
                          "should not send CANCEL on DNS parent\n%s\n%s",
                requestString.data(),
                transString.data());
        }
    }

    // Request that requires DNS SRV lookup
    else if (   !isResponse                                 // request
             && addressRequiresDnsSrvLookup                 // already decided
             && method.compareTo(SIP_CANCEL_METHOD) != 0    // not CANCEL
             && !mIsDnsSrvChild                             // not already looked up
             )
    {
        if(mpRequest != NULL)
        {
            if (OsSysLog::willLog(FAC_SIP, PRI_WARNING))
            {
                UtlString requestString;
                ssize_t len;
                outgoingMessage.getBytes(&requestString, &len);
                UtlString transString;
                toString(transString, TRUE);
                OsSysLog::add(FAC_SIP, PRI_WARNING,
                    "SipTransaction::handleOutgoing mpRequest should be NULL\n%s\n%s",
                    requestString.data(),
                    transString.data());
            }
        }

        // treat forwarding 2xx ACK the same as other requests
        if(relationship != MESSAGE_REQUEST && relationship != MESSAGE_2XX_ACK_PROXY)
        {
            if (OsSysLog::willLog(FAC_SIP, PRI_WARNING))
            {
                UtlString requestString;
                ssize_t len;
                outgoingMessage.getBytes(&requestString, &len);
                UtlString transString;
                toString(transString, TRUE);
                OsSysLog::add(FAC_SIP, PRI_WARNING,
                    "SipTransaction::handleOutgoing invalid relationship: %s\n%s\n%s",
                    relationshipString(relationship),
                    requestString.data(),
                    transString.data());
            }
        }

        // Make a copy to attach to the transaction
        SipMessage* requestCopy = new SipMessage(outgoingMessage);

        addResponse(requestCopy,
                    TRUE, // outgoing
                    relationship);

        // Look up the DNS SRV records, create the child transactions,
        // and start pursuing the first child.
        sendSucceeded = recurseDnsSrvChildren(userAgent, transactionList);
    }
    else    // It is a response, cancel, dnsChild, or an ack for failure
    {       // these messages do not get dns lookup, do not get protocol change
        sendSucceeded = doFirstSend(*message,
                                    relationship,
                                    userAgent,
                                    toAddress,
                                    port,
                                    protocol);

        touch();
    }
#ifdef DUMP_TRANSACTIONS 
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::handleOutgoing dumps whole TransactionTree %d", sendSucceeded);
    justDumpTransactionTree();
#endif

    return(sendSucceeded);
} // end handleOutgoing

void SipTransaction::prepareRequestForSend(SipMessage& request,
                                           SipUserAgent& userAgent,
                                           UtlBoolean& addressRequiresDnsSrvLookup,
                                           UtlString& toAddress,
                                           int& port,
                                           OsSocket::IpProtocolSocketType& toProtocol)
{
    UtlString protocol;

    // Make sure max-forwards is set and it is not
    // greater than the default value
    int maxForwards;
    int defaultMaxForwards = userAgent.getMaxForwards();
    if(!request.getMaxForwards(maxForwards) ||
        maxForwards > defaultMaxForwards)
    {
        request.setMaxForwards(defaultMaxForwards);
    }

    UtlBoolean ackFor2xx = FALSE;   // ACKs for 200 resp get new routing and URI
    UtlString method;
    request.getRequestMethod(&method);

    if(method.compareTo(SIP_ACK_METHOD) == 0 
       && mpLastFinalResponse)
    {
        int responseCode;
        responseCode = mpLastFinalResponse->getResponseStatusCode();
        if(responseCode >= SIP_2XX_CLASS_CODE &&
           responseCode < SIP_3XX_CLASS_CODE)
        {
            ackFor2xx = TRUE;
        }
    }
    // Conditions which don't need new routing, no DNS lookup
    if(mIsDnsSrvChild               // DNS lookup already done
       && !mSendToAddress.isNull()  // sendTo address is known 
       && !ackFor2xx)               // ACK for error response, follows response rules
    {
        toAddress = mSendToAddress;
        port = mSendToPort;
        toProtocol = mSendToProtocol;
        addressRequiresDnsSrvLookup = FALSE;

#      ifdef ROUTE_DEBUG
        {
           UtlString protoString;
           SipMessage::convertProtocolEnumToString(toProtocol,protoString);
           OsSysLog::add(FAC_SIP, PRI_DEBUG,
                         "SipTransaction::prepareRequestForSend %p - SRV child ready"
                         "   to %s:%d via '%s'",
                         &request,
                         toAddress.data(), port, protoString.data()
                         );
        }
#      endif
    }

    // Requests must be routed. 
    // ACK for a 2xx response follows request rules
    else
    {
        // process header parameters in the request uri, 
        // especially moving any route parameters to route headers
        request.applyTargetUriHeaderParams();

        // Default to use the proxy 
        userAgent.getProxyServer(0, &toAddress, &port, &protocol);
#       ifdef ROUTE_DEBUG
        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                      "SipTransaction::prepareRequestForSend"
                      " %p got proxy toAddress '%s', port %d, protocol '%s'",
                      &request, toAddress.data(), port, protocol.data());
#       endif

        // Try to get top-most route
        UtlString routeUri;
        UtlString routeAddress;
        int routePort;
        UtlString routeProtocol;
        request.getRouteUri(0, &routeUri);
        Url routeUrlParser(routeUri);
        UtlString dummyValue;
        UtlBoolean nextHopLooseRoutes = routeUrlParser.getUrlParameter("lr", dummyValue, 0);
        UtlString maddr;
        routeUrlParser.getUrlParameter("maddr", maddr);

        UtlString routeHost;
        SipMessage::parseAddressFromUri(routeUri.data(),
                                        &routeHost, &routePort, &routeProtocol);

        // All of this URL maipulation should be done via
        // the Url (routeUrlParser) object.  However to
        // be safe, we are only using it to get the maddr.
        // If the maddr is present use it as the address
        if(!maddr.isNull())
        {
            routeAddress = maddr;
        }
        else
        {
            routeAddress = routeHost;
        }

#       ifdef ROUTE_DEBUG
        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                      "SipTransaction::prepareRequestForSend %p getting first route uri: '%s'",
                      &request, routeUri.data());
#       endif

        // If there is no route use the configured outbound proxy
        if(routeAddress.isNull())
        {
            // Set by earlier call to userAgent.getProxyServer
            // this path is for debug purposes only
#       ifdef ROUTE_DEBUG
        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                      "SipTransaction::prepareRequestForSend route address is null");
#       endif
        }
        else // there is a route 
        {
            toAddress = routeAddress;   // from top-most route or maddr
            port = routePort;
            protocol = routeProtocol;   // can be null if not set in header

            // If this is not a loose route set the URI
            UtlString value;
            if(!nextHopLooseRoutes)
            {
               //Change the URI in the first line to the route Uri
               // so pop the first route uri
                request.removeRouteUri(0, &routeUri);
#               ifdef ROUTE_DEBUG
                OsSysLog::add(FAC_SIP, PRI_DEBUG,
                              "SipTransaction::prepareRequestForSend %p"
                              " removing route, no proxy, uri: '%s'",
                              &request, routeUri.data());
#               endif

                // We need to push the URI on the end of the routes
                UtlString uri;
                request.getRequestUri(&uri);
                request.addLastRouteUri(uri.data());

                // Set the URI to the popped route
                UtlString ChangedUri;
                routeUrlParser.getUri(ChangedUri);
                request.changeRequestUri(ChangedUri);
            }
#           ifdef ROUTE_DEBUG
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::prepareRequestForSend %p - using route address"
                          "   to %s:%d via '%s'",
                          &request, toAddress.data(), port, protocol.data()
                          );
#           endif
        }   // end "there is a route"

        // No proxy, no route URI, check for an X-sipX-NAT-Route header then
        // try to use URI from message if none found.
        if(toAddress.isNull())
        {
           UtlString uriString;
           request.getSipXNatRoute(&uriString);
           if( uriString.isNull() )
           {
              request.getRequestUri(&uriString);
           }
           Url requestUri(uriString, TRUE);

           requestUri.getHostAddress(toAddress);
           port = requestUri.getHostPort();
           requestUri.getUrlParameter("transport", protocol);
           if(requestUri.getUrlParameter("maddr", maddr) &&
              !maddr.isNull())
           {
              toAddress = maddr;
           }
#          ifdef ROUTE_DEBUG
           OsSysLog::add(FAC_SIP, PRI_DEBUG,
                         "SipTransaction::prepareRequestForSend %p - "
                         "   using request URI address: %s:%d via '%s'",
                         &request, toAddress.data(), port, protocol.data()
                         );
#          endif
        }

        // No proxy, route URI, temporary sipX route or message URI, use the To field
        if(toAddress.isNull())
        {
           request.getToAddress(&toAddress, &port, &protocol);
#          ifdef ROUTE_DEBUG
           OsSysLog::add(FAC_SIP, PRI_DEBUG,
                         "SipTransaction::prepareRequestForSend %p "
                         "No URI address, using To address"
                         "   to %s:%d via '%s'",
                         &request, toAddress.data(), port, protocol.data()
                         );
#          endif
        }

        UtlString toField;
        request.getToField(&toField);
#ifdef TEST_PRINT
        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                      "SipTransaction::prepareRequestForSend UA Sending SIP REQUEST to: \"%s\" port: %d",
                      toAddress.data(), port);
#endif

        //SDUA
        UtlString sPort;
        UtlString thisMethod;
        request.getRequestMethod(&thisMethod);

        //check if CANCEL method and has corresponding INVITE
        if(strcmp(thisMethod.data(), SIP_CANCEL_METHOD) == 0)
        {
            //Cancel uses same DNS parameters as matching invite transaction
            //find corresponding INVITE request
            //SipMessage * InviteMsg =  sentMessages.getInviteFor( &message);
            if ( !mIsServerTransaction &&
                 mpRequest &&
                 mRequestMethod.compareTo(SIP_INVITE_METHOD) == 0)
            {
                //copy DNS parameters
                if ( mpRequest->getDNSField(&protocol , &toAddress , &sPort))
                {
                    request.setDNSField( protocol , toAddress , sPort);
                }
            }
        }

        //USE CONTACT OR RECORD ROUTE FIELDS FOR 200 OK responses
        //check if ACK method and if it has contact field set
        //if contact field is set then it is a 200 OK response
        //therefore do not set sticky DNS prameters or DNS look up
        //if DNS field is present request
        if (request.getDNSField(&protocol , &toAddress, &sPort))
        {
            port = atoi(sPort.data());
        }
        else
        {
            addressRequiresDnsSrvLookup = TRUE;
        }

        // If no one specified which protocol
        if(protocol.isNull())
        {
            toProtocol = OsSocket::UNKNOWN;
        }
        else
        {
            SipMessage::convertProtocolStringToEnum(protocol.data(),
                                                    toProtocol);
        }
    }
#   ifdef ROUTE_DEBUG
    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                  "SipTransaction::prepareRequestForSend %p prepared SIP REQUEST"
                  "   DNS SRV lookup: %s"
                  "   to %s:%d via '%s'",
                  &request,
                  addressRequiresDnsSrvLookup ? "NEEDED" : "NOT NEEDED",
                  toAddress.data(), port, protocol.data()
                  );
#   endif
}

UtlBoolean SipTransaction::doFirstSend(SipMessage& message,
                                       enum messageRelationship relationship,
                                       SipUserAgent& userAgent,
                                       UtlString& toAddress,
                                       int& port,
                                       OsSocket::IpProtocolSocketType& toProtocol)
{
    UtlBoolean sendSucceeded = FALSE;
    UtlBoolean isResponse = message.isResponse();
    UtlString method;
    UtlString seqMethod;
    int responseCode = -1;

    OsSocket::IpProtocolSocketType sendProtocol = message.getSendProtocol();
    // Time (msec) after which this message should be resent.
    int resendDuration;
    // Time (in microseconds) after which this message should be resent.
    int resendTime;

#   ifdef ROUTE_DEBUG
    {
       UtlString logProtocol;
       SipMessage::convertProtocolEnumToString(toProtocol, logProtocol);
       OsSysLog::add(FAC_SIP, PRI_DEBUG,
                     "SipTransaction::doFirstSend %p %s to %s:%d via '%s'",
                     &message, relationshipString(relationship),
                     toAddress.data(), port, logProtocol.data()
                     );
    }
#   endif

    if (toProtocol == OsSocket::UNKNOWN)
    {
       if (sendProtocol == OsSocket::UNKNOWN)
       {
          /*
           * This problem should be fixed by XECS-414 which sends an 
           * ACK for a 2xx response through the normal routing 
           * to determine the protocol using DNS SRV lookups
           */
          toProtocol = OsSocket::UDP;
          OsSysLog::add(FAC_SIP, PRI_ERR,
                        "SipTransaction::doFirstSend protocol not explicitly set - using UDP"
                        );
       }
       else
       {
          toProtocol = sendProtocol;
       }
    }

    // Responses:
    if(isResponse)
    {
        responseCode = message.getResponseStatusCode();
        int cSeq;
        message.getCSeqField(&cSeq, &seqMethod);

#       ifdef ROUTE_DEBUG
        {
           UtlString protocolStr;
           SipMessage::convertProtocolEnumToString(toProtocol, protocolStr);
           OsSysLog::add(FAC_SIP, PRI_DEBUG,
                         "SipTransaction::doFirstSend %p "
                         "Sending RESPONSE to: '%s':%d via: '%s'",
                         this, toAddress.data(), port, protocolStr.data());
        }
#       endif
        // This is the first send, save the address and port to which it get sent
        message.setSendAddress(toAddress.data(), port);
        message.setFirstSent();
    }
    // Requests:
    else
    {
        // This is the first send, save the address and port to which it gets sent.
        message.setSendAddress(toAddress.data(), port);
        message.setFirstSent();
        message.getRequestMethod(&method);

        // Add a Via header, now that we know the protocol.

        // Get the via info.
        UtlString viaAddress;
        UtlString viaProtocolString;
        SipMessage::convertProtocolEnumToString(toProtocol, viaProtocolString);
        int viaPort;

        userAgent.getViaInfo(toProtocol,
                             viaAddress,
                             viaPort);

        // Add the via field data
        message.addVia(viaAddress.data(),
                       viaPort,
                       viaProtocolString,
                       mpBranchId->data(),
                       (toProtocol == OsSocket::UDP) && userAgent.getUseRport());

#       ifdef ROUTE_DEBUG
        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                      "SipTransaction::doFirstSend message send %s:%d via %s",
                      toAddress.data(), port, viaProtocolString.data());
#       endif
    }

    if(toProtocol == OsSocket::TCP)
    {
        sendProtocol = OsSocket::TCP;
        resendDuration = 0;
        // Set resend timer based on user agent TCP resend time.
        resendTime = userAgent.getReliableTransportTimeout() * 1000;
    }
#   ifdef SIP_TLS
    else if(toProtocol == OsSocket::SSL_SOCKET)
    {
        sendProtocol = OsSocket::SSL_SOCKET;
        resendDuration = 0;
        // Set resend timer based on user agent TCP resend time.
        resendTime = userAgent.getReliableTransportTimeout() * 1000;
    }
#   endif
    else
    {
        if(toProtocol != OsSocket::UDP)
        {
            OsSysLog::add(FAC_SIP, PRI_WARNING,
                "SipTransaction::doFirstSend %p unknown protocol: %d using UDP",
                &message, toProtocol);
        }

        sendProtocol = OsSocket::UDP;
        resendDuration = userAgent.getFirstResendTimeout();
        // Set resend timer based on user agent UDP resend time.
        resendTime = userAgent.getFirstResendTimeout() * 1000;
    }

    // Set the transport information
    message.setResendDuration(resendDuration);
    message.setSendProtocol(sendProtocol);
    message.touchTransportTime();

    SipMessage* transactionMessageCopy = NULL;

    if (relationship == MESSAGE_REQUEST ||
        relationship == MESSAGE_PROVISIONAL ||
        relationship == MESSAGE_FINAL ||
        relationship == MESSAGE_CANCEL ||
        relationship == MESSAGE_CANCEL_RESPONSE ||
        relationship == MESSAGE_ACK ||
        relationship == MESSAGE_2XX_ACK ||
        relationship == MESSAGE_2XX_ACK_PROXY)      // can treat same as any ACK here
    {
        // Make a copy to attach to the transaction
        transactionMessageCopy = new SipMessage(message);

        // Need to add the message to the transaction before it
        // is sent to avoid the race of receiving the response before
        // the request is added to the transaction.
        addResponse(transactionMessageCopy,
                    TRUE, // outgoing
                    relationship);
    }

    // Save the transaction in the message to make
    // it easier to find the transaction on timeout or 
    // transport error, e.g. connect failure, far-end closed
    message.setTransaction(this);

    if (toProtocol == OsSocket::TCP)
    {
       sendSucceeded = userAgent.sendTcp(&message,
                                         toAddress.data(),
                                         port);
    }
    else if (toProtocol == OsSocket::SSL_SOCKET)
    {
       sendSucceeded = userAgent.sendTls(&message,
                                         toAddress.data(),
                                         port);
    }
    else
    {
       sendSucceeded = userAgent.sendUdp(&message,
                                         toAddress.data(),
                                         port);
    }

    if(   MESSAGE_REQUEST == relationship
       && !sendSucceeded
       )
    {
        mTransactionState = TRANSACTION_TERMINATED;
    }

#   ifdef TEST_PRINT
    message.dumpTimeLog();
    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                  "SipTransaction::doFirstSend set Scheduling & resend data");
#   endif

    // Increment the sent counter after the send so the logging messages are acurate.
    message.incrementTimesSent();
    if(transactionMessageCopy) transactionMessageCopy->incrementTimesSent();

    if(sendSucceeded)
    {
        // Schedule a resend timeout for requests and final INVITE failure
        // responses (2xx-class INVITE responses will be resent
        // by user agents only)
        if(   (   ! isResponse
               && strcmp(method.data(), SIP_ACK_METHOD) != 0
               )
           || (   isResponse
               && (   responseCode >= SIP_3XX_CLASS_CODE
                   || (mIsUaTransaction && responseCode >= SIP_OK_CODE)
                   )
               && strcmp(seqMethod.data(), SIP_INVITE_METHOD) == 0
               )
           )
        {
#           ifdef TEST_PRINT
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::doFirstSend Scheduling UDP %s timeout in %d msec",
                          method.data(),
                          userAgent.getFirstResendTimeout());
#           endif

            if(transactionMessageCopy) transactionMessageCopy->setTransaction(this);

            // Make a separate copy for the SIP message resend timer
            SipMessageEvent* resendEvent =
                new SipMessageEvent(new SipMessage(message),
                                    SipMessageEvent::TRANSACTION_RESEND);
#           ifdef TEST_PRINT
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::doFirstSend timer scheduled for: %p",
                          resendEvent->getMessage());
#           endif

            // Set an event timer to resend the message.
            // When it fires, queue a message to the SipUserAgent.
            OsMsgQ* incomingQ = userAgent.getMessageQueue();
            OsTimer* timer = new OsTimer(incomingQ, resendEvent);
            mTimers.append(timer);
            // Set the resend timer based on resendTime.
            OsTime timerTime(0, resendTime);
            timer->oneshotAfter(timerTime);
#ifdef TEST_PRINT
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::doFirstSend added timer %p to timer list, time = %f secs",
                          timer, resendTime / 1000000.0);
#endif

            // If this is a client transaction and we are sending
            // a request, set an expires timer for the transaction
            if(!mIsServerTransaction &&
               !isResponse)
            {
                // Time (sec) after which to expire the transaction.
                // Start with the mExpires value of this SipTransaction object.
                int expireSeconds = mExpires;
                // The upper limit is the user agent's mDefaultExpiresSeconds.
                int maxExpires = userAgent.getDefaultExpiresSeconds();
                // We cancel DNS SRV children after the configured DNS SRV timeout.
                // The timeout is ignored if we receive any response.
                // If this is the only child, do not set a short (DNS SRV) timeout
                if(mIsDnsSrvChild &&
                   mpParentTransaction &&
                   mpParentTransaction->isChildSerial())
                {
                    expireSeconds = userAgent.getDnsSrvTimeout();
                }
                // Normal client transaction
                else if(expireSeconds <= 0)
                {
                    if(mpParentTransaction &&
                        mpParentTransaction->isChildSerial())
                    {
                        // Transactions that fork serially get a different default expiration.
                        expireSeconds = userAgent.getDefaultSerialExpiresSeconds();
                    }
                    else
                    {
                        expireSeconds = maxExpires;
                    }
                }

                // Make sure the expiration is not longer than
                // the maximum length of time we keep a transaction around
                if(expireSeconds > maxExpires)
                {
                    expireSeconds = maxExpires;
                }

                // Make a separate copy of the message for the transaction expires timer.
                SipMessageEvent* expiresEvent =
                    new SipMessageEvent(new SipMessage(message),
                                        SipMessageEvent::TRANSACTION_EXPIRATION);

                OsTimer* expiresTimer = new OsTimer(incomingQ, expiresEvent);
                mTimers.append(expiresTimer);
#ifdef TEST_PRINT
                OsSysLog::add(FAC_SIP, PRI_DEBUG,
                              "SipTransaction::doFirstSend added timer %p to timer list.",
                              expiresTimer);
#endif

                OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::doFirstSend"
                              " transaction %p setting timeout %d secs.",
                              this, expireSeconds
                              );

                OsTime expiresTime(expireSeconds, 0);
                expiresTimer->oneshotAfter(expiresTime);
            }
        }
    }

    return(sendSucceeded);
} // end doFirstSend

void SipTransaction::handleResendEvent(const SipMessage& outgoingMessage,
                                        SipUserAgent& userAgent,
                                        enum messageRelationship relationship,
                                        SipTransactionList& transactionList,
                                        int& nextTimeout,
                                        SipMessage*& delayedDispatchedMessage)
{
#ifdef TEST_PRINT
   OsSysLog::add(FAC_SIP, PRI_DEBUG,
                 "SipTransaction::handleResendEvent %p", this);
#endif
    if(delayedDispatchedMessage)
    {
        OsSysLog::add(FAC_SIP, PRI_WARNING, "SipTransaction::handleResendEvent"
                      " %p delayedDispatchedMessage is not NULL", this);
        delayedDispatchedMessage = NULL;
    }

    // nextTimeout is in msec.
    nextTimeout = 0;

    // If this is not a duplicate, then there is something worng
    if(relationship != MESSAGE_DUPLICATE &&
        relationship != MESSAGE_CANCEL)
    {
        OsSysLog::add(FAC_SIP, PRI_WARNING, "SipTransaction::handleResendEvent"
                      " %p timeout message is not duplicate: %s",
                      this, relationshipString(relationship));
    }

    // Responses
    if(outgoingMessage.isResponse())
    {
        // The only responses which should have a timeout set
        // are INVITE responses for UA server transactions only

        if(mpLastFinalResponse == NULL)
        {
            OsSysLog::add(FAC_SIP, PRI_ERR, "SipTransaction::handleResendEvent"
                          " response timeout with no response");
        }

        // If this is a user agent server transaction and
        // the ACK has not been received yet
        // We should only be getting here for error final responses
        // when the transaction is on a proxy (i.e. !mIsUaTransaction)
        if(//mIsUaTransaction && // vs. proxy
            mIsServerTransaction && // vs. client
            mpAck == NULL &&
            mpLastFinalResponse)
        {
            // We have not yet received the ACK

            // Use mpLastFinalResponse, not outgoingMessage as
            // mpLastFinalResponse may be a newer final response.
            // outgoingMessage is a snapshot that was taken when the
            // timer was set.
            UtlBoolean sentOk = doResend(*mpLastFinalResponse,
                                         userAgent, nextTimeout);
            // doResend() sets nextTimeout.

            if(sentOk)
            {
                // Schedule the next timeout
                // As this is a resend, we should be able to use
                // the same copy of the SIP message for the next timeout
#ifdef TEST_PRINT
                if(outgoingMessage.getSipTransaction() == NULL)
                {
                    UtlString msgString;
                    ssize_t msgLen;
                    outgoingMessage.getBytes(&msgString, &msgLen);
                    OsSysLog::add(FAC_SIP, PRI_WARNING,
                                  "SipTransaction::handleResendEvent reschedule of response resend with NULL transaction, message = '%s'",
                                  msgString.data());
                }
#endif

                // Schedule a timeout for requests which do not
                // receive a response
                SipMessageEvent* resendEvent =
                    new SipMessageEvent(new SipMessage(outgoingMessage),
                                        SipMessageEvent::TRANSACTION_RESEND);

                OsMsgQ* incomingQ = userAgent.getMessageQueue();
                OsTimer* timer = new OsTimer(incomingQ, resendEvent);
                mTimers.append(timer);
#ifdef TEST_PRINT
                OsSysLog::add(FAC_SIP, PRI_DEBUG,
                              "SipTransaction::handleResendEvent added timer %p to timer list.",
                              timer);
#endif

                // Convert from msecs to usecs.
                OsTime lapseTime(0, nextTimeout * 1000);
                timer->oneshotAfter(lapseTime);
            }
            else
            {
                if( MESSAGE_REQUEST == relationship )
                {
                    mTransactionState = TRANSACTION_TERMINATED;
                }

                // Do this outside so that we do not get blocked
                // on locking or delete the transaction out
                // from under ouselves
                // Cleanup the message
                //delete outgoingMessage;
                //outgoingMessage = NULL;
                //userAgent.dispatch(outgoingMessage,
                //                   SipMessageEvent::TRANSPORT_ERROR);
            }

        } // A legal response timeout

        // The ACK was received so we can quit
        // We should only be getting here for error final responses
        // when the transaction is on a proxy (i.e. !mIsUaTransaction)
        else if(// do we care if proxy or UA?? mIsUaTransaction && // vs. proxy
            mIsServerTransaction && // vs. client
            mpAck &&
            mpLastFinalResponse)
        {
            nextTimeout = -1;
        }

    }

    // Requests
    else
    {
        // This should never be the case
        if(outgoingMessage.isFirstSend())
        {
            OsSysLog::add(FAC_SIP, PRI_WARNING, "SipTransaction::handleResendEvent"
                          " %p called for first time send of message", this);
        }
        else if(!mIsCanceled &&
                mpLastFinalResponse == NULL &&
                mpLastProvisionalResponse == NULL &&
                mTransactionState == TRANSACTION_CALLING)
        {
            UtlString method;
            outgoingMessage.getRequestMethod(&method);

            // This is a resend, retrieve the address and port to send the message to.
#ifdef TEST_PRINT
            UtlString toAddress;
            int port;
            outgoingMessage.getSendAddress(&toAddress, &port);
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::handleResendEvent Resend request %s:%d",
                          toAddress.data(), port);
#endif

            SipMessage* resendMessage = NULL;
            if(method.compareTo(SIP_ACK_METHOD) == 0)
            {
                OsSysLog::add(FAC_SIP, PRI_WARNING, "SipTransaction::handleResendEvent"
                              " resend of ACK");
                resendMessage = mpAck;
            }
            else if(method.compareTo(SIP_CANCEL_METHOD) == 0)
            {
                resendMessage = mpCancel;
            }
            else
            {
                resendMessage = mpRequest;
            }
            UtlBoolean sentOk = doResend(*resendMessage,
                                        userAgent, nextTimeout);
            // doResend() sets nextTimeout.

            if(sentOk && nextTimeout > 0)
            {
                // Schedule the next timeout
#ifdef TEST_PRINT
                if(outgoingMessage.getSipTransaction() == NULL)
                {
                    UtlString msgString;
                    ssize_t msgLen;
                    outgoingMessage.getBytes(&msgString, &msgLen);
                    OsSysLog::add(FAC_SIP, PRI_WARNING,
                        "SipTransaction::handleResendEvent reschedule of request resend with NULL transaction, message = '%s'",
                        msgString.data());
                }
#endif
                // As this is a resend, we should be able to use
                // the same copy of the SIP message for the next timeout

                // Schedule a timeout for requests which do not
                // receive a response
                SipMessageEvent* resendEvent =
                    new SipMessageEvent(new SipMessage(outgoingMessage),
                                        SipMessageEvent::TRANSACTION_RESEND);

                OsMsgQ* incomingQ = userAgent.getMessageQueue();
                OsTimer* timer = new OsTimer(incomingQ, resendEvent);
                mTimers.append(timer);
#ifdef TEST_PRINT
                OsSysLog::add(FAC_SIP, PRI_DEBUG,
                              "SipTransaction::handleResendEvent added timer %p to timer list.",
                              timer);
#endif

                // Convert from msecs to usecs.
                OsTime lapseTime(0, nextTimeout * 1000);
                timer->oneshotAfter(lapseTime);
            }
            else
            {
                // Do this outside so that we do not get blocked
                // on locking or delete the transaction out
                // from under ouselves
                // Cleanup the message
                //delete outgoingMessage;
                //outgoingMessage = NULL;
                //userAgent.dispatch(outgoingMessage,
                //                   SipMessageEvent::TRANSPORT_ERROR);
#ifdef TEST_PRINT
                OsSysLog::add(FAC_SIP, PRI_DEBUG,
                              "SipTransaction::handleResendEvent sentOk: %d nextTimeout: %d",
                              sentOk, nextTimeout);
#endif
                if(!sentOk)
                {
                    if ( MESSAGE_REQUEST == relationship )
                    {
                        mTransactionState = TRANSACTION_TERMINATED;
                    }
                    else
                    {
                        mTransactionState = TRANSACTION_COMPLETE;
                    }
                }
                else if ( nextTimeout < 0 )
                {
                    mTransactionState = TRANSACTION_COMPLETE;
#ifdef TEST_PRINT
                    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                  "SipTransaction::handleResendEvent failed to send request, TRANSACTION_COMPLETE");
#endif
                }
                // else nextTimeout == 0, which should mean the
                // final response was received

            }
        }
        else
        {
            // We are all done, do not reschedule and do not
            // send transport error
            nextTimeout = -1;
            if(mTransactionState == TRANSACTION_CALLING)
            {
                    mTransactionState = TRANSACTION_COMPLETE;
            }
            OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::handleResendEvent"
                          " no response, TRANSACTION_COMPLETE");
        }
    }

    if(mpParentTransaction)
    {
        mpParentTransaction->handleChildTimeoutEvent(*this,
                                                     outgoingMessage,
                                                     userAgent,
                                                     relationship,
                                                     transactionList,
                                                     nextTimeout,
                                                     delayedDispatchedMessage);
    }

    touch();
} // end handleResendEvent

void SipTransaction::handleExpiresEvent(const SipMessage& outgoingMessage,
                                        SipUserAgent& userAgent,
                                        enum messageRelationship relationship,
                                        SipTransactionList& transactionList,
                                        int& nextTimeout,
                                        SipMessage*& delayedDispatchedMessage)
{
#ifdef TEST_PRINT
   OsSysLog::add(FAC_SIP, PRI_DEBUG,
                 "SipTransaction::handleExpiresEvent %p", this);
#endif

    if(delayedDispatchedMessage)
    {
#      ifdef TEST_PRINT
       OsSysLog::add(FAC_SIP, PRI_WARNING, "SipTransaction::handleExpiresEvent"
                      " delayedDispatchedMessage not NULL");
#      endif

       delayedDispatchedMessage = NULL;
    }

    // Responses
    if(outgoingMessage.isResponse())
    {
#       ifdef TEST_PRINT
        OsSysLog::add(FAC_SIP, PRI_WARNING, "SipTransaction::handleExpiresEvent"
                      " %p expires event timed out on SIP response", this);
#       endif
    }

    // Requests
    else
    {
#       ifdef TEST_PRINT
        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                      "SipTransaction::handleExpiresEvent %p",
                      this);
#       endif
        // Do not cancel a DNS SRV child that received any response.
        // The parent client transaction may be canceled which will
        // recursively cancel the children.  However if this timeout
        // event is for a DNS SRV child we do not cancel if there was
        // any sort of response.
        if(mIsDnsSrvChild &&
            (mpLastProvisionalResponse ||
            mpLastFinalResponse))
        {
            // no-op
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                "SipTransaction::handleExpiresEvent %p ignoring timeout cancel of DNS SRV child", this);
        }

        // Do not cancel an early dialog with early media if this
        // transaction is a child to a serial search/fork.
        // This may be a gateway sending IVR prompts ala
        // American Airlines, so we do not want to cancel in
        // the middle of the user entering DTMF
        else if(!mIsDnsSrvChild &&
           !mIsServerTransaction &&
           mpParentTransaction &&
           mpParentTransaction->isChildSerial() &&
           mRequestMethod.compareTo(SIP_INVITE_METHOD) == 0 &&
           isChildEarlyDialogWithMedia())
        {
            // no op
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::handleExpiresEvent"
                          " %p ignoring cancel of early media branch of serial search", this);
        }

        // Do not cancel a completed transaction
        else if((mIsRecursing ||
           mTransactionState == TRANSACTION_CALLING ||
           mTransactionState == TRANSACTION_PROCEEDING ||
           mTransactionState == TRANSACTION_LOCALLY_INIITATED))
        {
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::handleExpiresEvent"
                          " %p canceling expired transaction", this);

            // This transaction has expired; cancel it.
            cancel(userAgent,
                   transactionList);

        }

        // Check the parents in the heirarchy to see if there are
        // other branches to pursue
        if(mpParentTransaction)
        {
            mpParentTransaction->handleChildTimeoutEvent(*this,
                                                         outgoingMessage,
                                                         userAgent,
                                                         relationship,
                                                         transactionList,
                                                         nextTimeout,
                                                         delayedDispatchedMessage);
        }
        // This is the top most parent and it is a client transaction
        // we need to find the best result
        else if(!mIsServerTransaction)
        {
            handleChildTimeoutEvent(*this,
                                     outgoingMessage,
                                     userAgent,
                                     relationship,
                                     transactionList,
                                     nextTimeout,
                                     delayedDispatchedMessage);
        }

        touch();
    }
}

UtlBoolean SipTransaction::handleChildIncoming(//SipTransaction& child,
                                     SipMessage& incomingMessage,
                                     SipUserAgent& userAgent,
                                     enum messageRelationship relationship,
                                     SipTransactionList& transactionList,
                                     UtlBoolean childSaysShouldDispatch,
                                     SipMessage*& delayedDispatchedMessage)
{
    UtlBoolean shouldDispatch = childSaysShouldDispatch;

    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                  "SipTransaction::handleChildIncoming %p relationship %s parent %p",
                  this, relationshipString(relationship), mpParentTransaction
                  );

    if(   relationship == MESSAGE_FINAL
       || relationship == MESSAGE_PROVISIONAL
       )
    {
        int responseCode = incomingMessage.getResponseStatusCode();

        // If there is a parent pass it up
        if(mpParentTransaction)
        {
            // May want to short cut this and first get
            // the top most parent.  However if the state
            // change is interesting to intermediate (i.e.
            // not top most parent) transactions we need to
            // do it this way (recursively)
            shouldDispatch =
                mpParentTransaction->handleChildIncoming(//child,
                                            incomingMessage,
                                            userAgent,
                                            relationship,
                                            transactionList,
                                            childSaysShouldDispatch,
                                            delayedDispatchedMessage);
#           ifdef TEST_PRINT
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::handleChildIncoming %p parent says %d",
                          this, shouldDispatch);
#           endif
        }
        else // this is the topmost parent transaction
        {
            // We do not dispatch if this is a server transaction
            // as the server transaction is the consumer of the
            // message.  If there is no server transaction as the
            // top most parent, then we assume the consumer is a
            // local application (i.e. message queue).
            if(mIsServerTransaction)
            {
               // If responseCode > 100 && <= 299
               if (   (responseCode >  SIP_TRYING_CODE)
                   && (responseCode <  SIP_3XX_CLASS_CODE)
                   )
               {
                  OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                "SipTransaction::handleChildIncoming %p "
                                "topmost parent dispatching %d",
                                this, responseCode );
                  shouldDispatch = TRUE;
               }
               else
               {
#                 ifdef DISPATCH_DEBUG
                  OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                "SipTransaction::handleChildIncoming %p "
                                "topmost parent NOT dispatching %d.",
                                this, responseCode );
#                 endif
                  shouldDispatch = FALSE;
               }
            }

            // CANCEL is hop by hop and should not be dispatched unless
            // the parent transaction was the originator of the CANCEL
            // request
            else if(!mIsCanceled)
            {
                int tempCseq;
                UtlString method;
                incomingMessage.getCSeqField(&tempCseq, &method);
                if(method.compareTo(SIP_CANCEL_METHOD) == 0)
                {
                    shouldDispatch = FALSE;
                }
            }
        }   // end top-most parent

#       ifdef TEST_PRINT
        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                      "SipTransaction::handleChildIncoming %p check response %d",
                      this, responseCode);
#       endif

        if(responseCode < SIP_TRYING_CODE)
        {
            // What is this????
            OsSysLog::add(FAC_SIP, PRI_ERR,
                          "SipTransaction::handleChildIncoming"
                          " dropped invalid response code: %d",
                          responseCode);
        }

        // 100 Trying is hop by hop do not forward it
        else if(responseCode == SIP_TRYING_CODE)
        {
            // no op
        }

        // If this is a successful 2XX or provisional response
        // forward it immediately
        // Once a final response is sent we no longer
        // send provisional responses, but we still send 2XX
        // class responses
        else if(   responseCode < SIP_3XX_CLASS_CODE
                && (   mpLastFinalResponse == NULL
                    || responseCode >= SIP_2XX_CLASS_CODE
                    )
                )
        {
            // If this is a server transaction for which the
            // response must be forwarded upstream
            if(mIsServerTransaction)
            {
                // Forward immediately
                SipMessage response(incomingMessage);
                response.removeTopVia();
                response.resetTransport();
                response.clearDNSField();
#ifdef TEST_PRINT
                OsSysLog::add(FAC_SIP, PRI_DEBUG,
                              "SipTransaction::handleChildIncoming immediately forwarding %d response",
                              responseCode);
#endif
                handleOutgoing(response,
                                userAgent,
                                transactionList,
                                relationship);
            }

            // If we got a good response for which forking
            // or recursion should be canceled
            if(mpParentTransaction == NULL)
            {
                // Got a success
                if(responseCode >= SIP_2XX_CLASS_CODE)
                {
                    // We are done - cancel all the outstanding requests
                    cancelChildren(userAgent,
                                   transactionList);
                }
            }

            // Keep track of the fact that we dispatched a final
            // response (but not for 1xx responses where xx > 00)
            if(   shouldDispatch
               && responseCode >= SIP_2XX_CLASS_CODE
               )
            {
#               ifdef TEST_PRINT
                OsSysLog::add(FAC_SIP, PRI_DEBUG,
                              "SipTransaction::handleChildIncoming %p"
                              " should dispatch final response %d",
                              this, __LINE__);
#               endif
                mDispatchedFinalResponse = TRUE;
            }

            // This should not occur.  All 2xx class messages for which
            // there is no parent server transaction should get dispatched
            else if(   mpParentTransaction == NULL
                    && responseCode >= SIP_2XX_CLASS_CODE
                    )
            {
               // xmlscott: despite the comment above,
               //           this happens a lot and seems to not always be bad,
               //           so I'm changing the priority to get it out of the logs.
                OsSysLog::add(FAC_SIP, PRI_DEBUG,
                              "SipTransaction::handleChildIncoming "
                              "%d response with parent client transaction NOT dispatched",
                              responseCode);
            }
        }       // end server response forwarding
        else
        {
            // 3XX class responses
            if(responseCode <= SIP_4XX_CLASS_CODE)
            {
                // Recursion is handled by the child
#               ifdef TEST_PRINT
                OsSysLog::add(FAC_SIP, PRI_DEBUG,
                              "SipTransaction::handleChildIncoming %p"
                              " 3XX response - should not dispatch",
                              this);
#               endif

                // Wait until all the children have been searched
                // before dispatching
                shouldDispatch = FALSE;
            }
            // 4XX, 5XX, and 6XX class responses
            // (See previous version for code that causes 6XX responses
            // to cancel all outstanding forks.)
            else
            {
                // See if there are other outstanding child transactions

                // If there are none and no more to recurse find the
                // best result

                // If there are more to recurse do so.

                // Wait until all the children have been searched
                // before dispatching
                shouldDispatch = FALSE;
            }

#           ifdef TEST_PRINT
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::handleChildIncoming %p"
                          " response=%d parent=%p final=%p dispatched=%d",
                          this, responseCode, mpParentTransaction,
                          mpLastFinalResponse, mDispatchedFinalResponse
               );
#           endif

            // If this is the server transaction and we have not
            // yet sent back a final response check what is next
            if(   (   mIsServerTransaction
                   && mpLastFinalResponse == NULL
                   )
               // or if this is the parent client transaction on a UA
               // and we have not yet dispatched the final response
               || (   mpParentTransaction == NULL
                   && ! mIsServerTransaction
                   && mpLastFinalResponse == NULL
                   && ! mDispatchedFinalResponse
                   )
               )
            {
                OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::handleChildIncoming %p", this );

                if(mpParentTransaction)
                {
                    OsSysLog::add(FAC_SIP, PRI_ERR,
                                  "SipTransaction::handleChildIncoming %p "
                                  "server transaction is not top most parent", this);
                }

                // See if there is anything to sequentially search
                // startSequentialSearch returns TRUE if something
                // is still searching or it starts the next sequential
                // search
                if(startSequentialSearch(userAgent, transactionList))
                {
                }

                // Special case for when there is no server transaction
                // The parent client transaction, when it first gets a
                // 3xx response has no children, so we need to create them
                else if(   mChildTransactions.isEmpty()
                        && recurseChildren(userAgent, transactionList) // true if something started
                        )
                {
#                   ifdef TEST_PRINT
                    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::handleChildIncoming %p creating children for 3XX",
                                  this);
#                   endif
                }

                // Not waiting for outstanding transactions to complete
                else
                {
                    SipMessage bestResponse;
                    if(findBestResponse(bestResponse))
                    {
#                       ifdef TEST_PRINT
                        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                      "SipTransaction::handleChildIncoming"
                                      " %p sending best response",
                                      this);
#                       endif

                        if (OsSysLog::willLog(FAC_SIP, PRI_DEBUG))
                        {
                           int bestResponseCode = bestResponse.getResponseStatusCode();
                           UtlString callId;
                           bestResponse.getCallIdField(&callId);
                           OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                         // Format the Call-Id so it looks like
                                         // a header line in a SIP message,
                                         // so log processors see it.
                                         "SipTransaction::handleChildIncoming "
                                         "response %d for Call-Id '%s'",
                                         bestResponseCode, callId.data());
                        }

                        if(mIsServerTransaction)
                        {
                            handleOutgoing(bestResponse,
                                            userAgent,
                                            transactionList,
                                            MESSAGE_FINAL);
                        }

                        if(!mDispatchedFinalResponse)
                        {
                            // Get the best message out to be dispatched.
                            if(delayedDispatchedMessage)
                            {
                                delete delayedDispatchedMessage;
                                delayedDispatchedMessage = NULL;
                            }
                            delayedDispatchedMessage =
                                new SipMessage(bestResponse);

#                           ifdef DISPATCH_DEBUG
                            int delayedResponseCode =
                               delayedDispatchedMessage->getResponseStatusCode();
                            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                          "SipTransaction::handleChildIncoming %p "
                                          "delayed dispatch of %d\n",
                                          this, delayedResponseCode );
#                           endif
#                           ifdef TEST_PRINT
                            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                          "SipTransaction::handleChildIncoming %p "
                                          "should dispatch delayed message %d",
                                          this, __LINE__);
#                           endif
                            mDispatchedFinalResponse = TRUE;
                        }
                    }
                }
            }
        }       // end 3xx response

        // The response itself is getting dispatched
        if(   shouldDispatch
           && responseCode >= SIP_2XX_CLASS_CODE
           )
        {
            // Keep track of the fact that we dispatched a final
            // response
            mDispatchedFinalResponse = TRUE;
#           ifdef TEST_PRINT
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::handleChildIncoming %p should dispatch final response %d",
                          this, __LINE__);
#           endif

            if(delayedDispatchedMessage)
            {
                // This is probably a bug.  This should not
                // occur.  For now log some noise and
                // drop the delayed response, if this ever
                // occurs
                // xmlscott: lowered priority
                OsSysLog::add(FAC_SIP, PRI_DEBUG,
                              "SipTransaction::handleChildIncoming"
                              " %p dropping delayed response", this);
                delete delayedDispatchedMessage;
                delayedDispatchedMessage = NULL;
            }
        }
    }  // end new responses

    else if(relationship == MESSAGE_DUPLICATE)
    {
        // Proxy client transaction received a duplicate INVITE
        // response
        if(incomingMessage.isResponse() &&
           //mIsUaTransaction &&
           mRequestMethod.compareTo(SIP_INVITE_METHOD) == 0)
        {
            int responseCode = incomingMessage.getResponseStatusCode();

            // The proxy must resend duplicate 2xx class responses
            // for reliability
            if(responseCode >= SIP_2XX_CLASS_CODE &&
                responseCode < SIP_3XX_CLASS_CODE)
            {
                // If there is more than one Via, send it upstream.
                // The calling UAC should resend the ACK, not the
                // proxy.
                UtlString dummyVia;
                if(incomingMessage.getViaField(&dummyVia, 1))
                {
                    SipTransaction* parent = getTopMostParent();
                    if(parent &&
                       parent->mIsServerTransaction)
                    {
                         OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                       "SipTransaction::handleChildIncoming "
                                       "proxy resending server transaction response %d",
                                       responseCode);
                        userAgent.sendStatelessResponse(incomingMessage);
                    }
                }

                // The ACK originated here, resend it
                else
                {
                    if(mpAck)
                    {
                        // Resend the ACK
                        SipMessage ackCopy(*mpAck);
                        ackCopy.removeTopVia();
                        userAgent.sendStatelessRequest(ackCopy,
                                                       mSendToAddress,
                                                       mSendToPort,
                                                       mSendToProtocol,
                                                       *mpBranchId);
                    }

                    // If this is a duplicate 2xx response and there is only
                    // one Via on the reponse, this UA should be the caller UAC.
                    // We should have an ACK that was sent for the original 2xx
                    // response.
                    else
                    {
                        OsSysLog::add(FAC_SIP, PRI_WARNING,
                                      "SipTransaction::handleChildIncoming "
                                      "duplicate 2xx response received on UAC for INVITE "
                                      "with no ACK"
                                      );
                    }

                }

            }

            // INVITE with final response that failed
            else if(responseCode >= SIP_3XX_CLASS_CODE)
            {
                // For failed INVITE transactions, the ACK is
                // sent hop-by-hop
                if(mpAck)
                {
                    // Resend the ACK
                    SipMessage ackCopy(*mpAck);
                    ackCopy.removeTopVia();
                    userAgent.sendStatelessRequest(ackCopy,
                                                   mSendToAddress,
                                                   mSendToPort,
                                                   mSendToProtocol,
                                                   *mpBranchId);
                }

                // No ACK for a duplicate failed response.  Something
                // is wrong.  The ACK should have been created locally
                // for the previous 3xx, 4xx, 5xx or 6xx final response
                else
                {
                    OsSysLog::add(FAC_SIP, PRI_CRIT,
                                  "SipTransaction::handleChildIncoming "
                                  "duplicate final error response rcvd for INVITE with no ACK");
                }
            }
        }
    }

    return(shouldDispatch);
} // end handleChildIncoming

void SipTransaction::handleChildTimeoutEvent(SipTransaction& child,
                                    const SipMessage& outgoingMessage,
                                    SipUserAgent& userAgent,
                                    enum messageRelationship relationship,
                                    SipTransactionList& transactionList,
                                    int& nextTimeout,
                                    SipMessage*& delayedDispatchedMessage)
{
    if(mpParentTransaction)
    {
        // For now recurse.  We might be able to short cut this
        // and go straight to the top-most parent
        mpParentTransaction->handleChildTimeoutEvent(child,
                                                     outgoingMessage,
                                                     userAgent,
                                                     relationship,
                                                     transactionList,
                                                     nextTimeout,
                                                     delayedDispatchedMessage);
    }

    // Top most parent
    else
    {
#       ifdef LOG_FORKING
        OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::handleChildTimeoutEvent"
                      " found top most parent: %p", this);
#       endif
        {
            UtlBoolean isResponse = outgoingMessage.isResponse();
            UtlString method;
            outgoingMessage.getRequestMethod(&method);

            if(   ! isResponse
               && method.compareTo(SIP_ACK_METHOD) == 0
               )
            {
#               ifdef LOG_FORKING
                OsSysLog::add(FAC_SIP, PRI_ERR, "SipTransaction::handleChildTimeoutEvent"
                              " timeout of ACK");
#               endif
            }

            else if(   ! isResponse
                    && method.compareTo(SIP_CANCEL_METHOD) == 0
                    )
            {
            }

            else if(   relationship == MESSAGE_DUPLICATE
                    && ! isResponse
                    )
            {
                // Check if we should still be trying
                if(nextTimeout > 0)
                {
                    // Still trying
                }
                // This transaction is done or has given up
                // See if we should start the next sequential search
                else
                {
                    // We do not dispatch proxy transactions
                    nextTimeout = -1;

#                   ifdef LOG_FORKING
                    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::handleChildTimeoutEvent"
                                  " %p", this);
#                   endif

                    if(startSequentialSearch(userAgent, transactionList))
                    {
#                       ifdef LOG_FORKING
                        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                      "SipTransaction::handleChildTimeoutEvent "
                                      "%p starting/still searching", this);
#                       endif
                    }

                    // Not waiting for outstanding transactions to complete
                    // and we have not yet sent a final response
                    else if(mpLastFinalResponse == NULL)
                    {
                        SipMessage bestResponse;
                        UtlBoolean foundBestResponse = findBestResponse(bestResponse);
                        // 2XX class responses are sent immediately so we
                        //  should not send it again
                        int bestResponseCode = bestResponse.getResponseStatusCode();
                        if (OsSysLog::willLog(FAC_SIP, PRI_DEBUG))
                        {
                           UtlString callId;
                           bestResponse.getCallIdField(&callId);
#                          ifdef LOG_FORKING
                           OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                         // Format the Call-Id so it looks like
                                         // a header line in a SIP message,
                                         // so log processors see it.
                                         "SipTransaction::handleChildTimeoutEvent "
                                         "response %d for Call-Id '%s'",
                                         bestResponseCode, callId.data());
#                          endif
                        }

                        // There is nothing to send if this is not a server transaction
                        // (this is the top most parent, if it is a client transaction
                        // the response gets dispatched).
                        if(   bestResponseCode >= SIP_3XX_CLASS_CODE
                           && mIsServerTransaction
                           && foundBestResponse
                           )
                        {
#                           ifdef LOG_FORKING
                            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                          "SipTransaction::handleChildTimeoutEvent "
                                          "%p sending best response", this);
#                           endif

                            // this is a timeout event, choose 408 over 302 or 404
                            if (  bestResponseCode < SIP_4XX_CLASS_CODE 
                               || bestResponseCode == SIP_NOT_FOUND_CODE)
                            {
                                bestResponseCode = SIP_REQUEST_TIMEOUT_CODE;
                                bestResponse.setResponseData(mpRequest,
                                                             SIP_REQUEST_TIMEOUT_CODE,
                                                             SIP_REQUEST_TIMEOUT_TEXT);

                            }

                            if (   (SIP_REQUEST_TIMEOUT_CODE == bestResponseCode)
                                && (!bestResponse.hasSelfHeader())
                                )
                            {
                               userAgent.setSelfHeader(bestResponse);
                            }

                            handleOutgoing(bestResponse,
                                            userAgent,
                                            transactionList,
                                            MESSAGE_FINAL);
                        }
                        else
                        {
#                           ifdef LOG_FORKING
                            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                          "SipTransaction::handleChildTimeoutEvent "
                                          "%p not sending %d best response",
                                          this, bestResponseCode);

#                           endif
                        }

                        if(foundBestResponse &&
                            !mDispatchedFinalResponse)
                        {
                            if(delayedDispatchedMessage)
                            {
                                delete delayedDispatchedMessage;
                                delayedDispatchedMessage = NULL;
                            }
                            delayedDispatchedMessage = new SipMessage(bestResponse);
#                           ifdef TEST_PRINT
                            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                          "SipTransaction::handleChildIncoming %p should dispatch final response %d",
                                          this, __LINE__);
#                           endif
                            mDispatchedFinalResponse = TRUE;
                        }
                    }

                }
            } // Request timeout

        } // server transaction
    }

}

UtlBoolean SipTransaction::startSequentialSearch(SipUserAgent& userAgent,
                                           SipTransactionList& transactionList)
{
    UtlSListIterator iterator(mChildTransactions);
    SipTransaction* childTransaction = NULL;
    UtlBoolean childStillProceeding = FALSE;
    UtlBoolean startingNewSearch = FALSE;

#ifdef TEST_PRINT
    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                  "SipTransaction::startSequentialSearch %p", this);
#endif

    while ((childTransaction = (SipTransaction*) iterator()))
    {
        if(   ! childTransaction->mIsCanceled
           && (   childTransaction->mTransactionState == TRANSACTION_CALLING
               || childTransaction->mTransactionState == TRANSACTION_PROCEEDING
               )
           )
        {
            // The child is not done
            childStillProceeding = TRUE;
#           ifdef LOG_FORKING
            OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::startSequentialSearch "
                          "%p child: %p still proceeding",
                          this, childTransaction);
#           endif
        }

        else if( childTransaction->mIsRecursing )
        {
            // See if the grand children or decendants are still searching
            if(childTransaction->startSequentialSearch(userAgent,
                transactionList))
            {
                childStillProceeding = TRUE;
#               ifdef LOG_FORKING
                OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::startSequentialSearch "
                              "%p child: %p decendent still proceeding",
                              this, childTransaction);
#               endif
            }
            else
            {
#               ifdef LOG_FORKING
                OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::startSequentialSearch "
                              "%p child: %p no longer proceeding",
                              this, childTransaction);
#               endif
            }
        }

        // A child has completed and may be recursed
        else if(   ! childStillProceeding
                && (   childTransaction->mTransactionState == TRANSACTION_COMPLETE
                    || childTransaction->mTransactionState == TRANSACTION_CONFIRMED
                    )
                && ! mIsCanceled
                && ! childTransaction->mIsCanceled
                )
        {
#           ifdef LOG_FORKING
            OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::startSequentialSearch "
                          "%p child: %p completed, now recursing",
                          this, childTransaction);
#           endif
            UtlBoolean recurseStartedNewSearch = childTransaction->recurseChildren(userAgent,
                    transactionList);
            if(!startingNewSearch)
            {
                startingNewSearch = recurseStartedNewSearch;
            }

            // Do not break out of the loop because we want
            // to check all the currently proceeding transaction
            // to see if we should recurse.
#           ifdef LOG_FORKING
            OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::startSequentialSearch "
                          "%p child: %p startingNewSearch: %s",
                          this, childTransaction, startingNewSearch ? "True" : "False");
#                         endif
        }

        // If there is another sequential search to kick off on this
        // parent
        else if(   ! childStillProceeding
                && ! startingNewSearch
                && childTransaction->mTransactionState == TRANSACTION_LOCALLY_INIITATED
                && ! mIsCanceled
                && ! childTransaction->mIsCanceled
                )
        {
            UtlBoolean recurseStartedNewSearch = FALSE;

            if(mpDnsDestinations)   // tells us DNS lookup was done, check for valid records at lower level
            {
                recurseStartedNewSearch  = recurseDnsSrvChildren(userAgent, transactionList);
            }
            else
            {
                recurseStartedNewSearch = recurseChildren(userAgent, transactionList);
            }

            if(!startingNewSearch)
            {
                startingNewSearch = recurseStartedNewSearch;
            }
#           ifdef LOG_FORKING
            OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::startSequentialSearch "
                          "%p child: %p starting sequential startingNewSearch: %s",
                          this, childTransaction,
                          startingNewSearch ? "True" : "False");
#           endif
            if( recurseStartedNewSearch )
            {
                break;
            }
            else
            {
#               ifdef LOG_FORKING
                OsSysLog::add(FAC_SIP, PRI_DEBUG,
                              "SipTransaction::startSequentialSearch "
                              "%p failed to find child to transaction to pursue", this);
#               endif
            }
        }
    }

    mIsRecursing = childStillProceeding || startingNewSearch;
#   ifdef LOG_FORKING
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::startSequentialSearch "
                  "%p returning: %s childStillProceeding: %s startingNewSearch:%s",
                  this, mIsRecursing ? "True" : "False",
                  childStillProceeding ? "True" : "False",
                  startingNewSearch ? "True" : "False");
#   endif
    return(mIsRecursing);
}

UtlBoolean SipTransaction::recurseDnsSrvChildren(SipUserAgent& userAgent,
                                                 SipTransactionList& transactionList)
{
    // If this is a client transaction requiring DNS SRV lookup
    // and we need to create the children to recurse
    if(!mIsServerTransaction &&         // is client transaction
        !mIsDnsSrvChild &&              // is not traversing DNS record tree
        mpDnsDestinations == NULL &&    // the one required DNS lookup has not yet happened
        mpRequest &&  // only applicable to requests not sent
        mpLastFinalResponse == NULL && // should be no response yet
        mChildTransactions.isEmpty())
    {
        if(mSendToAddress.isNull())
        {
            OsSysLog::add(FAC_SIP, PRI_ERR, "SipTransaction::recurseDnsSrvChildren"
                          " no send address");
        }
        else if(mTransactionState < TRANSACTION_CONFIRMED)
        {
            mTransactionState = TRANSACTION_CONFIRMED;

            // Do the DNS lookup for the request destination but first
            // determine whether to force the msg to be sent via tcp
            OsSocket::IpProtocolSocketType msgSizeProtocol = getPreferredProtocol();
            mpDnsDestinations = SipSrvLookup::servers(mSendToAddress.data(),
                                                      "sip", // TODO - scheme should be from the request URI
                                                      mSendToProtocol,
                                                      mSendToPort,
                                                      msgSizeProtocol);

            // HACK:
            // Add a via to this request so when we set a timer it is
            // identified (by branchId) which transaction it is related to
            if(mpRequest)
            {
                // This via should never see the light of day
                // (or rather the bits of the network).
                mpRequest->addVia("127.0.0.1",
                                  9999,
                                  "UNKNOWN",
                                  mpBranchId->data());
            }

            // Set the transaction expires timeout for the DNS parent
            int expireSeconds = mExpires;

            // Non-INVITE transactions time out sooner
            int maxExpires = (  (this->mRequestMethod.compareTo(SIP_INVITE_METHOD) != 0)
                              ? (userAgent.getSipStateTransactionTimeout())/1000
                              : userAgent.getDefaultExpiresSeconds()
                              );

            if(expireSeconds <= 0)
            {
               expireSeconds = (  (   mpParentTransaction
                                   && mpParentTransaction->isChildSerial()
                                   )
                                ? userAgent.getDefaultSerialExpiresSeconds()
                                : maxExpires
                                );
            }

            // Make sure the expiration is not longer than
            // the maximum length of time we keep a transaction around
            if(expireSeconds > maxExpires)
            {
                expireSeconds = maxExpires;
            }

            // Save the transaction in the timeout message to make
            // it easier to find the transaction when the timer fires.
            mpRequest->setTransaction(this);

            // Keep separate copy for the timer
            SipMessage* pRequestMessage = NULL;
            pRequestMessage = new SipMessage(*mpRequest);

            SipMessageEvent* expiresEvent =
                new SipMessageEvent(pRequestMessage,
                                    SipMessageEvent::TRANSACTION_EXPIRATION);

            OsMsgQ* incomingQ = userAgent.getMessageQueue();
            OsTimer* expiresTimer = new OsTimer(incomingQ, expiresEvent);
            mTimers.append(expiresTimer);
#ifdef TEST_PRINT
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::recurseDnsSrvChildren added timer %p to timer list.",
                          expiresTimer);
#endif

            OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::recurseDnsSrvChildren"
                          " transaction %p setting timeout %d secs.",
                          this, expireSeconds
               );

            OsTime expiresTime(expireSeconds, 0);
            expiresTimer->oneshotAfter(expiresTime);

            if(mpDnsDestinations[0].isValidServerT())   // leave redundant check at least for now
            {
                int numSrvRecords = 0;
                int maxSrvRecords = userAgent.getMaxSrvRecords();

                // Create child transactions for each SRV record
                // up to the maximum
                while(numSrvRecords < maxSrvRecords &&
                    mpDnsDestinations[numSrvRecords].isValidServerT())
                {
                    SipTransaction* childTransaction =
                        new SipTransaction(mpRequest,
                                           TRUE, // outgoing
                                           mIsUaTransaction,
                                           (  mpParentTransaction
                                            ? mpParentTransaction->mpBranchId
                                            : NULL )
                                           ); // same as parent

                    mpDnsDestinations[numSrvRecords].
                       getIpAddressFromServerT(childTransaction->mSendToAddress);

                    childTransaction->mSendToPort =
                        mpDnsDestinations[numSrvRecords].getPortFromServerT();

                    childTransaction->mSendToProtocol =
                        mpDnsDestinations[numSrvRecords].getProtocolFromServerT();

#                   ifdef ROUTE_DEBUG
                         {
                            UtlString protoString;
                            SipMessage::convertProtocolEnumToString(childTransaction->mSendToProtocol,
                                                                    protoString);
                            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                          "SipTransaction::recurseDnsSrvChildren "
                                          "new DNS SRV child %s:%d via '%s'",
                                          childTransaction->mSendToAddress.data(),
                                          childTransaction->mSendToPort,
                                          protoString.data());
                         }
#                   endif

                    // Do not create child for unsupported protocol types
                    if(childTransaction->mSendToProtocol == OsSocket::UNKNOWN)
                    {
                        maxSrvRecords++;
                        delete childTransaction;
                        childTransaction = NULL;
                    }
                    else
                    {
                        // Set the q values of the child based upon the parent
                        // As DNS SRV is recursed serially the Q values are decremented
                        // by a factor of the record index
                        childTransaction->mQvalue = mQvalue - numSrvRecords * 0.0001;

                        // Inherit the expiration from the parent
                        childTransaction->mExpires = mExpires;

                        // Mark this as a DNS SRV child
                        childTransaction->mIsDnsSrvChild = TRUE;

                        childTransaction->mIsBusy = mIsBusy;

                        // Add it to the list
                        transactionList.addTransaction(childTransaction);

                        // Link it in to this parent
                        this->linkChild(*childTransaction);
                    }

                    numSrvRecords++;
                }
            }
            // We got no useful DNS records back
            else
            {
                UtlString protoString;
                SipMessage::convertProtocolEnumToString(mSendToProtocol, protoString);

                OsSysLog::add(FAC_SIP, PRI_WARNING, "SipTransaction::recurseDnsSrvChildren "
                              "no valid DNS records found for sendTo sip:'%s':%d proto = '%s'",
                              mSendToAddress.data(), mSendToPort, protoString.data());
            }
        }
    }

    UtlBoolean childRecursed = FALSE;
    UtlBoolean childRecursing = FALSE;
    if(!mIsServerTransaction &&
        !mIsDnsSrvChild &&
        mpDnsDestinations &&                            // means sendto address was not NULL
        mpDnsDestinations[0].isValidServerT() &&        // means DNS search returned at least
                                                        // one destination address
        mpRequest)
    {
        UtlSListIterator iterator(mChildTransactions);
        SipTransaction* childTransaction = NULL;

        while((childTransaction = (SipTransaction*) iterator()) &&
             !childRecursed &&
             !childRecursing)
        {
            if(childTransaction->mTransactionState == TRANSACTION_LOCALLY_INIITATED)
            {
                // Make a local copy to modify and send
                SipMessage recursedRequest(*mpRequest);

                // Clear the address and port of the previous send
                // of the parent request.
                recursedRequest.removeTopVia(); // the fake via for identifying this TX
                recursedRequest.resetTransport();
                recursedRequest.clearDNSField();

#               ifdef LOG_FORKING
                OsSysLog::add(FAC_SIP, PRI_DEBUG,
                              "SipTransaction::recurseDnsSrvChildren "
                              "%p sending child transaction request %s:%d protocol: %d",
                              this,
                              childTransaction->mSendToAddress.data(),
                              childTransaction->mSendToPort,
                              childTransaction->mSendToProtocol);
#               endif
                // Start the transaction by sending its request
                if(childTransaction->handleOutgoing(recursedRequest,
                                                 userAgent,
                                                 transactionList,
                                                 MESSAGE_REQUEST))
                {
                    childRecursed = TRUE;
                }

            }

            // If there is a child transaction that is currently
            // being pursued, do not start any new searches
            else if((childTransaction->mTransactionState == TRANSACTION_CALLING ||
                childTransaction->mTransactionState == TRANSACTION_PROCEEDING) &&
                !childTransaction->mIsCanceled)
            {
                childRecursing = TRUE;
#               ifdef LOG_FORKING
                OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::recurseDnsSrvChildren "
                              "%p still pursing", this);
#               endif
            }

            // This parent is not canceled (implicit) and we found a non-canceled
            // DNS SRV child with any sort of response, so there is no need to
            // recurse.  We have a DNS SRV child that has succeeded (at least in
            // as much as getting a response).
            else if(!childTransaction->mIsCanceled &&
                (childTransaction->mpLastProvisionalResponse ||
                childTransaction->mpLastFinalResponse))
            {
                break;
            }

            else
            {
#               ifdef LOG_FORKING
                OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::recurseDnsSrvChildren "
                              "%p transaction not recursed state: %s", this,
                              stateString(childTransaction->mTransactionState));
#               endif
            }
        }   // end while
    }

    if(childRecursed) mIsRecursing = TRUE;
    return(childRecursed);
}

UtlBoolean SipTransaction::recurseChildren(SipUserAgent& userAgent,
                                   SipTransactionList& transactionList)
{
    UtlBoolean childRecursed = FALSE;

#   ifdef LOG_FORKING
#   ifdef TEST_PRINT
    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                  "SipTransaction::recurseChildren %p", this);
#   endif

    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::recurseChildren %p", this);
#   endif

    if(mpRequest == NULL)
    {
        UtlString transactionString;
        toString(transactionString, TRUE);
        OsSysLog::add(FAC_SIP, PRI_ERR, "SipTransaction::recurseChildren "
                      "NULL mpResponse\n======>\n%s\n======>",
                      transactionString.data());
    }

    if(mpLastFinalResponse && mpRequest)
    {
        SipTransaction* childTransaction = NULL;
        int responseCode = mpLastFinalResponse->getResponseStatusCode();

#       ifdef TEST_PRINT
        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                      "SipTransaction: lastfinalsresponse & request forking enabled: %d ce: %d",
                      userAgent.isForkingEnabled(),
                      mChildTransactions.isEmpty()
           );
#       endif
        // If this is a client transaction for which we received
        // a 3XX response on which recursion makes sense,
        // decide whether or not to pursue the contacts
        if(   userAgent.isForkingEnabled()
           && responseCode >= SIP_3XX_CLASS_CODE
           && responseCode < SIP_4XX_CLASS_CODE
           && mChildTransactions.isEmpty()
           )
        {
            // collect all the fork urls and calculate the loop detection hash
            UtlSList children;
            UtlString contactString;
            for ( int contactIndex = 0;
                  mpLastFinalResponse->getContactEntry(contactIndex, &contactString);
                  contactIndex++
                  )
            {
                Url contactUrl(contactString);
                mpBranchId->addFork(contactUrl);

                children.insert(new UtlString(contactString));
            }

            /*
             * Before actually creating the child transactions, check for loops.
             * This look for a match between the loop detection key we just built
             * and one in an earlier via of the request; if they match, the request
             * has had exactly this set of contacts before, so reject it as a loop.
             */
            unsigned int loopHop = mpBranchId->loopDetected(*mpRequest);
            if (!loopHop) // no loop was detected
            {
               // no loop - go over the list of child addresses and create the new transactions
               UtlSListIterator nextChild(children);
               UtlString* contact;
               while ((contact = dynamic_cast<UtlString*>(nextChild())))
               {
                  Url contactUrl(*contact);

                  // Make sure we do not add the contact twice and
                  // we have not already pursued this contact
                  if(!isUriRecursed(contactUrl))
                  {

                     childTransaction = new SipTransaction(mpRequest,
                                                           TRUE, // outgoing
                                                           FALSE,
                                                           mpBranchId
                                                           ); // proxy transaction

#                   ifdef LOG_FORKING
                     OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                   "SipTransaction::recurseChildren "
                                   "%p adding child %p for contact: '%s'",
                                   this, childTransaction, contact->data());
#                   endif
                     // Add it to the list of all transactions
                     transactionList.addTransaction(childTransaction);

                     // Set the URI of the copy to that from the contact
                     contactUrl.getUri(childTransaction->mRequestUri);

                     // Set the Q value and Expires value
                     UtlString qString;
                     UtlString expiresString;
                     double qValue = 1.0;
                     if(contactUrl.getFieldParameter("q", qString))
                        qValue = atof(qString.data());
                     int expiresSeconds = -1;
                     if((mRequestMethod.compareTo(SIP_INVITE_METHOD) == 0) &&
                        contactUrl.getHeaderParameter("expires", expiresString))
                     {
                        if(Url::isDigitString(expiresString.data()))
                        {
                           // All digits, the format is relative seconds
                           expiresSeconds = atoi(expiresString.data());
                        }
                        else // Alphanumeric, it is an HTTP absolute date
                        {
                           // This format is allowed in RFC 2543, though not in RFC 3261.
                           expiresSeconds =
                              OsDateTime::convertHttpDateToEpoch(expiresString.data());
                           OsTime time;
                           OsDateTime::getCurTimeSinceBoot(time);
                           expiresSeconds -= time.seconds();
                        }
                     }

                     // Set the values of the child
                     childTransaction->mQvalue = qValue;
                     childTransaction->mExpires = expiresSeconds;

                     // Link it in to this parent
                     this->linkChild(*childTransaction);
                  }
                  else // We have already recursed this contact
                  {
                     OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::recurseChildren "
                                   "%p already recursed: %s",
                                   this, contactString.data());
                  }
               } // end for each contact
            }
            else
            {
               // detected a loop, so change the redirection response into a loop detected.
               OsSysLog::add(FAC_SIP, PRI_WARNING, "SipTransaction::recurseChildren "
                             "loop detected on call '%s' "
                             "%d hops ago had same contacts",
                             mCallId.data(), loopHop
                             );

               UtlString myAddress;
               int myPort;
               userAgent.getViaInfo(OsSocket::TCP, myAddress, myPort);
               Url mySipAddress;
               mySipAddress.setHostAddress(myAddress.data());
               mySipAddress.setHostPort(myPort);

               UtlString identityAddress;
               mySipAddress.getUri(identityAddress);

               char loopDetectedText[128];
               sprintf(loopDetectedText, SIP_LOOP_DETECTED_TEXT " with %d hops ago", loopHop);

               SipMessage* loopDetectedResponse = new SipMessage;
               loopDetectedResponse->setDiagnosticSipFragResponse(*mpRequest,
                                                                  SIP_LOOP_DETECTED_CODE,
                                                                  loopDetectedText,
                                                                  SIP_WARN_MISC_CODE,
                                                                  loopDetectedText,
                                                                  identityAddress
                                                                 );
               userAgent.setSelfHeader(*loopDetectedResponse);

               loopDetectedResponse->resetTransport();
               loopDetectedResponse->clearDNSField();

               // Change the redirect response we have into the loop detected response
               addResponse(loopDetectedResponse, FALSE /* incoming */, MESSAGE_FINAL );

               childRecursed = FALSE;
            }

            children.destroyAll(); // release the urls for the forks
        }

        double nextQvalue = -1.0;
        int numRecursed = 0;
        UtlSListIterator iterator(mChildTransactions);
        while ((childTransaction = (SipTransaction*) iterator()))
        {
            // Until a request is successfully sent, reset the
            // current Q value at which transactions of equal
            // value are searched in parallel
            if(numRecursed == 0)
            {
               nextQvalue = -1.0;
            }

            if(childTransaction->mTransactionState == TRANSACTION_LOCALLY_INIITATED)
            {
                double qDelta = nextQvalue - childTransaction->mQvalue;
                double qDeltaSquare = qDelta * qDelta;

#               ifdef LOG_FORKING
                OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::recurseChildren"
                              " %p qDelta: %f qDeltaSquare: %f mQvalue: %f",
                              this, qDelta, qDeltaSquare, childTransaction->mQvalue);
#               endif

                if(nextQvalue <= 0.0 ||
                   qDeltaSquare < MIN_Q_DELTA_SQUARE)
                {
                    nextQvalue = childTransaction->mQvalue;

#                   ifdef LOG_FORKING
                    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::recurseChildren"
                                  " %p should recurse child: %p q: %f",
                                  this, childTransaction, childTransaction->mQvalue);
#                   endif

                    // Make a local copy to modify and send
                    SipMessage recursedRequest(*mpRequest);

                    // Clear the address and port of the previous send
                    // of the parent request.
                    recursedRequest.removeTopVia();
                    recursedRequest.resetTransport();
                    recursedRequest.clearDNSField();

                    // If there was a loose route pop it off
                    // The assumption is that this was previously routed
                    // to the redirect server.  Perhaps we can get the
                    // parent's parent's request to use here instead.  I just
                    // cannot work it out in my head right now
                    UtlString routeUri;
                    recursedRequest.getRouteUri(0, &routeUri);
                    Url routeUrlParser(routeUri);
                    UtlString dummyValue;
                    UtlBoolean nextHopLooseRoutes =
                       routeUrlParser.getUrlParameter("lr", dummyValue, 0);
                    if(nextHopLooseRoutes)
                    {
                        recursedRequest.removeRouteUri(0, &routeUri);
                    }

                    // Correct the URI of the request to be the recursed URI
                    recursedRequest.setSipRequestFirstHeaderLine(mRequestMethod,
                        childTransaction->mRequestUri, SIP_PROTOCOL_VERSION);

                    // Decrement max-forwards
                    int maxForwards;
                    if(!recursedRequest.getMaxForwards(maxForwards))
                    {
                        recursedRequest.setMaxForwards(userAgent.getMaxForwards() - 1);
                    }
                    else
                    {
                        recursedRequest.setMaxForwards(maxForwards - 1);
                    }

#                   ifdef LOG_FORKING
                    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::recurseChildren"
                                  " %p sending child transaction request", this);
#                   endif
                    // Start the transaction by sending its request
                    if(childTransaction->handleOutgoing(recursedRequest,
                                                     userAgent,
                                                     transactionList,
                                                     MESSAGE_REQUEST))
                    {

                        numRecursed++;
                        // Recursing is TRUE
                        childRecursed = TRUE; // Recursion disabled
                    }
                }

                else
                {
#                   ifdef LOG_FORKING
                    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::recurseChildren"
                                  " %p nextQvalue: %f qDeltaSquare: %f", this,
                                  nextQvalue, qDeltaSquare);
#                   endif
                }
            }

            // If there is a child transaction that is currently
            // being pursued, do not start any new searches
            else if((childTransaction->mTransactionState == TRANSACTION_CALLING ||
                childTransaction->mTransactionState == TRANSACTION_PROCEEDING) &&
                !childTransaction->mIsCanceled)
            {
                nextQvalue = childTransaction->mQvalue;
#               ifdef LOG_FORKING
                OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::recurseChildren"
                              " %p still pursing", this);
#               endif
            }

            else
            {
#               ifdef LOG_FORKING
                OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::recurseChildren"
                              " %p transaction not recursed state: %s",
                              this, stateString(childTransaction->mTransactionState));
#               endif
            }

            // Optionally we only look at the first contact
            // for 300 response (not 300 class e.g. 302)
            if(userAgent.recurseOnlyOne300Contact() &&
                responseCode == SIP_MULTI_CHOICE_CODE) break;
        }
    }

    if(childRecursed) mIsRecursing = TRUE;
    return(childRecursed);
}

void SipTransaction::getChallengeRealms(const SipMessage& response, UtlSList& realmList)
{
   // Get any the proxy authentication challenges from the new challenge
   UtlString authField;
   for (unsigned authIndex = 0;
        response.getAuthenticationField(authIndex, HttpMessage::PROXY, authField);
        authIndex++
        )
   {
      UtlString challengeRealm;
      if (   HttpMessage::parseAuthenticationData(authField,
                                                  NULL, // scheme
                                                  &challengeRealm,
                                                  NULL, // nonce
                                                  NULL, // opaque
                                                  NULL, // algorithm
                                                  NULL, // qop
                                                  NULL  // domain
                                                  )
          && !realmList.contains(&challengeRealm)
          )
      {
         realmList.insert(new UtlString(challengeRealm));
      }
   }
}

UtlBoolean SipTransaction::findBestResponse(SipMessage& bestResponse)
{
#   ifdef TEST_PRINT
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::findBestResponse %p", this);
#   endif
    int responseFoundCount = 0;
    UtlBoolean retVal = FALSE;

    retVal = findBestChildResponse(bestResponse, responseFoundCount);

    return retVal;
}

enum SipTransaction::ResponsePriority SipTransaction::findRespPriority(int responseCode)
{
    enum SipTransaction::ResponsePriority respPri;

#ifdef RESPONSE_DEBUG
    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                  "SipTransaction::findRespPriority"
                  " %p response code %d",
                  this, responseCode);
#endif
    switch (responseCode)
    {
    case HTTP_UNAUTHORIZED_CODE:        // 401       
    case HTTP_PROXY_UNAUTHORIZED_CODE:  // 407
        // 401 & 407 are better than any other 4xx, 5xx or 6xx
        respPri = RESP_PRI_CHALLENGE;
        break;

    case SIP_REQUEST_TERMINATED_CODE:   // 487
        // 487 is better than any other 4xx, 5xx, or 6xx if the
        // transaction has been canceled.
        respPri = RESP_PRI_CANCEL;
        break;

    case SIP_3XX_CLASS_CODE:            // 300
    case SIP_TEMPORARY_MOVE_CODE:       // 302
    case SIP_PERMANENT_MOVE_CODE:       // 301
    case SIP_USE_PROXY_CODE:            // 305
        // 3xx is better than 4xx
        respPri = RESP_PRI_3XX;
        break;

    case SIP_BAD_REQUEST_CODE:              // 400
    case SIP_FORBIDDEN_CODE:                // 403
    case SIP_DECLINE_CODE:                  // 603 - not a typo
    case SIP_BAD_METHOD_CODE:               // 405
    case SIP_REQUEST_TIMEOUT_CODE:          // 408
    case SIP_CONDITIONAL_REQUEST_FAILED_CODE:    // 412
    case SIP_BAD_MEDIA_CODE:                // 415
    case SIP_UNSUPPORTED_URI_SCHEME_CODE:   // 416
    case SIP_BAD_EXTENSION_CODE:            // 420
    case SIP_EXTENSION_REQUIRED_CODE:       // 421
    case SIP_TOO_BRIEF_CODE:                // 423
    case SIP_TEMPORARILY_UNAVAILABLE_CODE:   // 480
    case SIP_BAD_TRANSACTION_CODE:          // 481
    case SIP_LOOP_DETECTED_CODE:            // 482
    case SIP_TOO_MANY_HOPS_CODE:            // 483
    case SIP_BAD_ADDRESS_CODE:              // 484
    case SIP_BUSY_CODE:                     // 486
    case SIP_REQUEST_NOT_ACCEPTABLE_HERE_CODE:   // 488
    case SIP_BAD_EVENT_CODE:                // 489
        respPri = RESP_PRI_4XX;
        break;

    case SIP_NOT_FOUND_CODE:                // 404
        respPri = RESP_PRI_404;
        break;

    case SIP_SERVER_INTERNAL_ERROR_CODE:    // 500
    case SIP_UNIMPLEMENTED_METHOD_CODE:     // 501
    case SIP_SERVICE_UNAVAILABLE_CODE:      // 503
    case SIP_BAD_VERSION_CODE:              // 505
        respPri = RESP_PRI_5XX;
        break;

    case SIP_6XX_CLASS_CODE:                // 600
        respPri = RESP_PRI_6XX;
        break;

    default:
        // not a specific code we defined, select by category
        if (responseCode >= SIP_6XX_CLASS_CODE)    // must be 6xx
        {
            respPri = RESP_PRI_6XX;
        }
        else if (responseCode >= SIP_5XX_CLASS_CODE)   // must be 5xx
        {
            respPri = RESP_PRI_5XX;
        }
        else if (responseCode >= SIP_4XX_CLASS_CODE)   // must be 4xx
        {
            respPri = RESP_PRI_4XX;
        }
        else if (responseCode >= SIP_3XX_CLASS_CODE)   // must be 3xx
        {
            respPri = RESP_PRI_3XX;
        }
        else    // best response must still be empty
        {
            respPri = RESP_PRI_NOMATCH;
        }
        break;
    }
#ifdef RESPONSE_DEBUG
    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                  "SipTransaction::findRespPriority"
                  " %p response code %d returns %d",
                  this, responseCode, respPri);
#endif
    return respPri;
}

UtlBoolean SipTransaction::findBestChildResponse(SipMessage& bestResponse, int responseFoundCount)
{
#   ifdef TEST_PRINT
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::findBestChildResponse start %p", this);
#   endif

    UtlSListIterator iterator(mChildTransactions);
    SipTransaction* childTransaction = NULL;
    UtlBoolean responseFound = FALSE;
    SipMessage* childResponse = NULL;
    int bestResponseCode = -1;
    int childResponseCode = -1;
    UtlBoolean foundChild = FALSE;
    UtlBoolean useThisResp = FALSE;
    enum ResponsePriority respPri = RESP_PRI_NOMATCH, bestPri = RESP_PRI_NOMATCH;
    int pathNum = 0;

    UtlSList proxyRealmsSeen;
    while ((childTransaction = (SipTransaction*) iterator()))
    {
        // Check the child's decendents first
        // Note: we need to check the child's children even if this child
        // has no response.
        foundChild = childTransaction->findBestChildResponse(bestResponse, responseFoundCount);
        if(foundChild)
        {
           responseFound = TRUE;
        }

        childResponse = childTransaction->mpLastFinalResponse;
#ifdef RESPONSE_DEBUG
        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                      "SipTransaction::findBestChildResponse"
                      " %p child %p returns %d with lastResp %p (bestResp %p)",
                      this, childTransaction, foundChild, childResponse, &bestResponse);
#endif
        if(childResponse)
        {
            bestResponseCode = bestResponse.getResponseStatusCode();
            childResponseCode = childResponse->getResponseStatusCode();

            respPri = findRespPriority(childResponseCode);
            bestPri = findRespPriority(bestResponseCode);

#ifdef RESPONSE_DEBUG
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::findBestChildResponse"
                          " %p child %p status/pri %d/%d (bestResp %d/%d)",
                          this, childTransaction, childResponseCode, respPri, bestResponseCode, bestPri);
#endif
            if (bestPri == RESP_PRI_NOMATCH)
            {
                // this is the first response we have found
                useThisResp = TRUE;
                pathNum = 1;
            }
            else
            {
                pathNum = 2;

                switch (respPri)
                {
                case RESP_PRI_CHALLENGE:
                    pathNum = 3;
                    if (bestPri == RESP_PRI_CHALLENGE)
                    {
                        pathNum = 4;
                        /*
                         * Some implementations get confused if there is more than one
                         * Proxy challenge for the same realm, so filter out any extra
                         * ones.  Since ours are all generated with the same shared secret,
                         * a challenge from any sipXproxy is acceptable at any sipXproxy;
                         * it is possible that this will cause problems with multiple
                         * proxy authentication requests being forwarded from some
                         * downstream forking proxy that does not have that quality, but
                         * it's better to get something back to the UA that won't break it.
                         * We can't filter and do this only to our own realm, because at this
                         * level in the stack we have no idea what our own realm is :-(
                         */
                        getChallengeRealms(bestResponse, proxyRealmsSeen);
    
                        // Get the proxy authenticate challenges
                        UtlString authField;
                        unsigned  authIndex;
                        for (authIndex = 0;
                             childResponse->getAuthenticationField(authIndex, HttpMessage::PROXY,
                                                                   authField);
                             authIndex++
                             )
                        {
                           UtlString challengeRealm;
                           if (HttpMessage::parseAuthenticationData(authField,
                                                                    NULL, // scheme
                                                                    &challengeRealm,
                                                                    NULL, // nonce
                                                                    NULL, // opaque
                                                                    NULL, // algorithm
                                                                    NULL, // qop
                                                                    NULL  // domain
                                                                    )
                               )
                           {
                              if (!proxyRealmsSeen.contains(&challengeRealm))
                              {
                                 proxyRealmsSeen.insert(new UtlString(challengeRealm));
                                 bestResponse.addAuthenticationField(authField, HttpMessage::PROXY);
                              }
                              else
                              {
                                 OsSysLog::add(FAC_SIP, PRI_INFO,
                                               "SipTransaction::findBestChildResponse"
                                               " removing redundant proxy challenge:\n   %s",
                                               authField.data());
                              }
                           }
                           else
                           {
                              OsSysLog::add(FAC_SIP, PRI_WARNING,
                                            "SipTransaction::findBestChildResponse"
                                            " removing unparsable proxy challenge:\n   %s",
                                            authField.data());
                           }
                        }
    
                        // Get the UA server authenticate challenges
                        for (authIndex = 0;
                             childResponse->getAuthenticationField(authIndex,
                                                                   HttpMessage::SERVER, authField);
                             authIndex++
                             )
                         {
                             bestResponse.addAuthenticationField(authField, HttpMessage::SERVER);
                         }
                    }   // end child and best both want to challenge
                    else if (bestPri >= RESP_PRI_4XX)  // bestResp == cancel will top this
                    {
                        pathNum = 5;
                        // 401 & 407 are better than any other 4xx, 5xx or 6xx
                        useThisResp = TRUE;
                    }
                    else
                    {
                        pathNum = 6;
                    }
                    break;  // end child wants to challenge
                case RESP_PRI_CANCEL:
                    // 487 is better than any other 4xx, 5xx, or 6xx if the
                    // transaction has been canceled.
                    // This improves the odds that we send a 487 response to
                    // canceled transactions, which is not required, but tends
                    // to make UAs behave better.
                    pathNum = 7;
                    if ( childTransaction->mIsCanceled
                         && bestPri >= RESP_PRI_4XX)   // bestResp == 401, 407, 487 will top this
                    {
                        // An unforked 3xx response
                        useThisResp = TRUE;
                        pathNum = 8;
                    }
                    break;  // end 487 cancel
                case RESP_PRI_3XX:
                    pathNum = 9;
                    // 3xx is better than 4xx
                    if ( bestPri >= RESP_PRI_4XX   // bestResp == 401, 407, 487 will top this
                         && childTransaction->mChildTransactions.isEmpty())
                    {
                        useThisResp = TRUE;
                        pathNum = 10;
                    }
                    break;
                case RESP_PRI_4XX:
                    pathNum = 11;
                    if ( bestPri > RESP_PRI_4XX )
                    {
                        pathNum = 12;
                        useThisResp = TRUE;
                    }
                    break;
                case RESP_PRI_5XX:
                    pathNum = 13;
                    if ( bestPri > RESP_PRI_5XX )
                    {
                        pathNum = 14;
                        useThisResp = TRUE;
                    }
                    break;
                case RESP_PRI_6XX:
                    pathNum = 15;
                    if ( bestPri > RESP_PRI_6XX )
                    {
                        pathNum = 16;
                        useThisResp = TRUE;
                    }
                    break;
                default:
                    pathNum = 17;
                    useThisResp = TRUE;
                    break;
                }   // end respPri switch
            }   // end bestresp is valid

            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::findBestChildResponse"
                          " %p child %p status/pri %d/%d (bestResp %d/%d) usethis=%d pathNum %d",
                          this, childTransaction, childResponseCode, respPri, bestResponseCode, bestPri, useThisResp, pathNum);

            if (useThisResp)
            {
                pathNum = 18;
                bestResponse = *(childResponse);

                // Not supposed to return 503 unless we know that
                // there is absolutely no way to reach the end point
                if(childResponseCode == SIP_SERVICE_UNAVAILABLE_CODE)
                {
                    pathNum = 19;
                    bestResponse.setResponseFirstHeaderLine(SIP_PROTOCOL_VERSION,
                        SIP_SERVER_INTERNAL_ERROR_CODE,
                        SIP_SERVER_INTERNAL_ERROR_TEXT);
                }

                bestResponse.removeTopVia();
                bestResponse.resetTransport();
                bestResponse.clearDNSField();
                responseFound = TRUE;
            }
        }
    }
    proxyRealmsSeen.destroyAll();

    // We have made it to the top and there are no responses
    if (!responseFound && mpParentTransaction == NULL)
    {
        if (mpRequest)
        {
            bestResponse.setResponseData(mpRequest,
                                         SIP_REQUEST_TIMEOUT_CODE,
                                         SIP_REQUEST_TIMEOUT_TEXT);
            responseFound = TRUE;
        }
        else
        {
            OsSysLog::add(FAC_SIP, PRI_ERR, "SipTransaction::findBestChildResponse no request");
        }
    }

    if(responseFound)
    {
        const char* firstHeaderLine = bestResponse.getFirstHeaderLine();

        if(firstHeaderLine == NULL ||
            *firstHeaderLine == '\0')
        {
            if (OsSysLog::willLog(FAC_SIP, PRI_WARNING))
            {
                UtlString msgString;
                ssize_t msgLen;
                bestResponse.getBytes(&msgString, &msgLen);

                // We got a bad response
                OsSysLog::add(FAC_SIP, PRI_ERR,
                              "SipTransaction::findBestChildResponse invalid response:\n%s",
                              msgString.data());
            }
        }
    }
#ifdef RESPONSE_DEBUG
    OsSysLog::add(FAC_SIP, PRI_INFO,
                  "SipTransaction::findBestChildResponse"
                  " end %p child %p status/pri %d/%d (bestResp %d/%d) usethis=%d pathNum %d",
                  this, childTransaction, childResponseCode, respPri, bestResponseCode, bestPri, useThisResp, pathNum);
#endif
    return(responseFound);
}

UtlBoolean SipTransaction::doResend(SipMessage& resendMessage,
                                   SipUserAgent& userAgent,
                                   int& nextTimeout)
{
    // The timeout to set from this resend to the next resend. (msec)
    nextTimeout = 0;
    // Get how many times we have sent this message before.
    int numTries = resendMessage.getTimesSent();
    // Get the sending protocol.
    OsSocket::IpProtocolSocketType protocol = resendMessage.getSendProtocol();
    int lastTimeout = resendMessage.getResendDuration();
    // Get the address/port to which to send the message.
    UtlString sendAddress;
    int sendPort;
    resendMessage.getSendAddress(&sendAddress, &sendPort);
    UtlBoolean sentOk = FALSE;

    // TCP only gets one try
    // UDP gets SIP_UDP_RESEND_TIMES tries

    if(protocol == OsSocket::UDP)
    {
        if(numTries < SIP_UDP_RESEND_TIMES)
        {
            // Try UDP again
            if(userAgent.sendUdp(&resendMessage, sendAddress.data(), sendPort))
            {
                // Do this after the send so that the log message is correct
                resendMessage.incrementTimesSent();

                // Schedule the timeout for the next resend.
                if(lastTimeout < userAgent.getFirstResendTimeout())
                {
                    nextTimeout = userAgent.getFirstResendTimeout();
                }
                else if(lastTimeout < userAgent.getLastResendTimeout())
                {
                    nextTimeout = lastTimeout * 2;
                }
                else
                {
                    nextTimeout = userAgent.getLastResendTimeout();
                }
                resendMessage.setResendDuration(nextTimeout);

                sentOk = TRUE;
            }
        }
    }
    else if(   protocol == OsSocket::TCP
#ifdef SIP_TLS
            || protocol == OsSocket::SSL_SOCKET
#endif
           )
    {
       if(numTries >= 1)
       {
            // we are done send back a transport error

            // Dispatch is called from above this method

            // Dispatch needs it own copy of the message
            //SipMessage* dispatchMessage = new SipMessage(resendMessage);

            // The TCP send failed, pass back an error
            //userAgent.dispatch(dispatchMessage,
            //                   SipMessageEvent::TRANSPORT_ERROR);
       }
       else
       {
           // Try TCP once
           UtlBoolean sendOk;
           if(protocol == OsSocket::TCP)
           {
              sendOk = userAgent.sendTcp(&resendMessage,
                                         sendAddress.data(),
                                         sendPort);
           }
#ifdef SIP_TLS
           else if(protocol == OsSocket::SSL_SOCKET)
           {
              sendOk = userAgent.sendTls(&resendMessage,
                                         sendAddress.data(),
                                         sendPort);
           }
#endif
           else
           {
              sendOk = FALSE;
           }

           if(sendOk)
           {
               // Schedule a timeout
               nextTimeout = userAgent.getReliableTransportTimeout();

               resendMessage.incrementTimesSent();
               resendMessage.setResendDuration(nextTimeout);
               resendMessage.setSendProtocol(protocol);

#ifdef TEST_PRINT
                if(resendMessage.getSipTransaction() == NULL)
                {
                    UtlString msgString;
                    ssize_t msgLen;
                    resendMessage.getBytes(&msgString, &msgLen);
                    OsSysLog::add(FAC_SIP, PRI_WARNING,
                                  "SipTransaction::doResend reschedule of request resend with NULL transaction, message = '%s'",
                                  msgString.data());
                }
#endif

               // Schedule a timeout for requests which do not
               // receive a response
               SipMessageEvent* resendEvent =
                    new SipMessageEvent(new SipMessage(resendMessage),
                                    SipMessageEvent::TRANSACTION_RESEND);

               OsMsgQ* incomingQ = userAgent.getMessageQueue();
               OsTimer* timer = new OsTimer(incomingQ, resendEvent);
               mTimers.append(timer);
#ifdef TEST_PRINT
               OsSysLog::add(FAC_SIP, PRI_DEBUG,
                             "SipTransaction::doResend added timer %p to timer list.",
                             timer);
#endif

               // Convert from mSeconds to uSeconds
               OsTime lapseTime(0, nextTimeout * 1000);
               timer->oneshotAfter(lapseTime);

               sentOk = TRUE;
           }
           else
           {
               // Send failed send back the transport error

               // Dispatch is done from above this method

               // Dispatch needs it own copy of the message
               // SipMessage* dispatchMessage = new SipMessage(resendMessage);

                // The TCP send failed, pass back an error
                //userAgent.dispatch(dispatchMessage,
                //                   SipMessageEvent::TRANSPORT_ERROR);
           }
       }
    } // TCP

    return(sentOk);
} // end doResend

UtlBoolean SipTransaction::handleIncoming(SipMessage& incomingMessage,
                                         SipUserAgent& userAgent,
                                         enum messageRelationship relationship,
                                         SipTransactionList& transactionList,
                                         SipMessage*& delayedDispatchedMessage)
{
   OsSysLog::add(FAC_SIP, PRI_DEBUG,
                 "SipTransaction::handleIncoming %p relationship %s",
                 this, relationshipString(relationship)
                 );

    if(delayedDispatchedMessage)
    {
        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                      "SipTransaction::handleIncoming delayedDispatchedMessage not NULL");
        delayedDispatchedMessage = NULL;
    }

    UtlBoolean shouldDispatch = FALSE;

    if(relationship == MESSAGE_UNKNOWN)
    {
        relationship = whatRelation(incomingMessage,
                                    TRUE);
    }

    // This is a message was already recieved once
    if(relationship == MESSAGE_DUPLICATE)
    {
        // Update the time stamp so that this message
        // does not get garbage collected right away.
        // This is so that rogue UAs that keep sending
        // the same message for a long (i.e. longer than
        // transaction timeout) period of time are ignored.
        // Otherwise this message looks like a new message
        // after a transaction timeout and the original
        // copy gets garbage collected.  We explicitly
        // do NOT touch the outgoing (i.e. sentMessages)
        // as we do not want to keep responding after
        // the transaction timeout.
        //previousMessage->touchTransportTime();

        // If it is a request resend the response if it exists
        if(!(incomingMessage.isResponse()))
        {
            SipMessage* response = NULL; //sentMessages.getResponseFor(message);
            UtlString method;
            incomingMessage.getRequestMethod(&method);
            if(method.compareTo(SIP_ACK_METHOD) == 0)
            {
                // Do nothing, we already have the ACK
            }
            else if(method.compareTo(SIP_CANCEL_METHOD) == 0)
            {
                // Resend the CANCEL response
                response = mpCancelResponse;
            }
            else
            {
                // Resend the final response if there is one
                // Otherwise resend the provisional response
                response = mpLastFinalResponse ?
                    mpLastFinalResponse : mpLastProvisionalResponse;
            }


#ifdef TEST_PRINT
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::handleIncoming duplicate REQUEST response");

            if(response)
            {
               OsSysLog::add(FAC_SIP, PRI_DEBUG,
                             "response protocol: %d",
                             response->getSendProtocol());
            }
#endif
            if(response)
            {
                int sendProtocol = response->getSendProtocol();
                UtlString sendAddress;
                int sendPort;
                response->getSendAddress(&sendAddress, &sendPort);

                switch (sendProtocol)
                {
                case OsSocket::UDP:
                    userAgent.sendUdp(response, sendAddress.data(), sendPort);
                    break;

                case OsSocket::TCP:
                    userAgent.sendTcp(response, sendAddress.data(), sendPort);
                    break;

#               ifdef SIP_TLS
                case OsSocket::SSL_SOCKET:
                    userAgent.sendTls(response, sendAddress.data(), sendPort);
                    break;
#               endif

                default:
                   OsSysLog::add(FAC_SIP, PRI_CRIT, "SipTransaction::handleIncoming"
                                 " invalid response send protocol %d", sendProtocol);
                }

#ifdef TEST_PRINT
                OsSysLog::add(FAC_SIP, PRI_DEBUG,
                              "SipTransaction::handleIncoming resending response");
#endif
            }
        }

        // If it is an INVITE response, resend the ACK if it exists
        //
        else
        {
            int cSeq;
            UtlString seqMethod;
            incomingMessage.getCSeqField(&cSeq, &seqMethod);

            // We assume ACK will only exist if this was an INVITE
            // transaction.  We resend only if this is a
            // UA transaction.
            if (   !seqMethod.compareTo(SIP_CANCEL_METHOD)
                && mpAck
                && mIsUaTransaction
                )
            {
                OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::handleIncoming resending ACK");

                int sendProtocol = mpAck->getSendProtocol();
                UtlString sendAddress;
                int sendPort;
                mpAck->getSendAddress(&sendAddress, &sendPort);

                switch (sendProtocol)
                {
                case OsSocket::UDP:
                    userAgent.sendUdp(mpAck, sendAddress.data(), sendPort);
                    break;
                case OsSocket::TCP:
                    userAgent.sendTcp(mpAck, sendAddress.data(), sendPort);
                    break;
#               ifdef SIP_TLS
                case OsSocket::SSL_SOCKET:
                   userAgent.sendTls(mpAck, sendAddress.data(), sendPort);
                   break;
#               endif
                default:
                   OsSysLog::add(FAC_SIP, PRI_CRIT, "SipTransaction::handleIncoming"
                                 " invalid ACK send protocol %d", sendProtocol);
                }

#ifdef TEST_PRINT
                OsSysLog::add(FAC_SIP, PRI_DEBUG,
                              "SipTransaction::handleIncoming resent ACK");
#endif
            }
        }
    }

    // The first time we received this message
    else if(relationship == MESSAGE_FINAL)
    {
        if(mpAck)
        {
            int cSeq;
            UtlString seqMethod;
            incomingMessage.getCSeqField(&cSeq, &seqMethod);
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::handleIncoming resending ACK for final response %s",
                          seqMethod.data()
                          );

            int sendProtocol = mpAck->getSendProtocol();
            UtlString sendAddress;
            int sendPort;
            mpAck->getSendAddress(&sendAddress, &sendPort);

            switch (sendProtocol)
            {
            case OsSocket::UDP:
               userAgent.sendUdp(mpAck, sendAddress.data(), sendPort);
               break;
            case OsSocket::TCP:
               userAgent.sendTcp(mpAck, sendAddress.data(), sendPort);
               break;
#           ifdef SIP_TLS
            case OsSocket::SSL_SOCKET:
               userAgent.sendTls(mpAck, sendAddress.data(), sendPort);
               break;
#           endif
            default:
               OsSysLog::add(FAC_SIP, PRI_CRIT, "SipTransaction::handleIncoming"
                             " invalid ACK send protocol %d", sendProtocol);
            }
            mpAck->incrementTimesSent();
            mpAck->touchTransportTime();

            shouldDispatch = TRUE;

            OsSysLog::add(FAC_SIP, PRI_WARNING,
                          "SipTransaction::handleIncoming received final response,"
                          "sending existing ACK"
                          );
        }
        else
        {
            // If this is an error final response to an INVITE
            // We can automatically construct the ACK here:
            if(   mpRequest
               && (mRequestMethod.compareTo(SIP_INVITE_METHOD) == 0)
               && (incomingMessage.getResponseStatusCode() >= SIP_3XX_CLASS_CODE)
               )
            {
                SipMessage ack;
                ack.setAckData(&incomingMessage,
                    mpRequest);

               // SDUA
               // is err code > 300 , set the DNS data files in the response
               // We may not need this any more as the ACK for error
               // responses is now done here
               UtlString protocol;
               UtlString address;
               UtlString port;
               if (mpRequest->getDNSField( &protocol , &address , &port))
               {
                   ack.setDNSField(protocol,
                                   address,
                                   port);
               }
#ifdef TEST_PRINT
               OsSysLog::add(FAC_SIP, PRI_DEBUG,
                             "SipTransaction::handleIncoming %p sending ACK for error response",
                             this);
#endif
               handleOutgoing(ack,
                              userAgent,
                              transactionList,
                              MESSAGE_ACK);

               shouldDispatch = TRUE;
            }   // end INVITE failure response

            // Non INVITE response or 2XX INVITE responses for which
            // there is no ACK yet get dispatched.  The app layer must
            // generate the ACK for 2XX responses as it may contain
            // SDP
            else
            {
                shouldDispatch = TRUE;
            }
        }

        SipMessage* responseCopy = new SipMessage(incomingMessage);
        addResponse(responseCopy,
                    FALSE, // Incoming
                    relationship);

    } // End if new final response

    // Requests, provisional responses, CANCEL response
    else
    {
        SipMessage* responseCopy = new SipMessage(incomingMessage);
        addResponse(responseCopy,
                    FALSE, // Incoming
                    relationship);

        if(relationship == MESSAGE_REQUEST &&
           mIsServerTransaction)
        {
            if(mpLastProvisionalResponse)
            {
               OsSysLog::add(FAC_SIP, PRI_WARNING, "SipTransaction::handleIncoming"
                             " new request with an existing provisional response");
            }
            else if(mpLastFinalResponse)
            {
                OsSysLog::add(FAC_SIP, PRI_WARNING, "SipTransaction::handleIncoming"
                              " new request with an existing final response");
            }
            // INVITE transactions we can send trying to stop the resends
            else if(mRequestMethod.compareTo(SIP_INVITE_METHOD) == 0)
            {
                // Create and send a 100 Trying
                SipMessage trying;
                trying.setTryingResponseData(&incomingMessage);

#               ifdef TEST_PRINT
              	OsSysLog::add(FAC_SIP, PRI_DEBUG,
                	      "SipTransaction::handleIncoming sending trying response");
#               endif
                handleOutgoing(trying,
                               userAgent,
                               transactionList,
                               MESSAGE_PROVISIONAL);
            }
        }   // end is server request transaction

        // If this transaction was marked as canceled
        // but we could not send the CANCEL until we
        // got a provisional response, we can now send
        // the CANCEL (we only send CANCEL for INVITEs).
        if(relationship == MESSAGE_PROVISIONAL &&
            !mIsServerTransaction &&
            mIsCanceled &&
            mpRequest &&
            mRequestMethod.compareTo(SIP_INVITE_METHOD) == 0)
        {
            SipMessage cancel;

            cancel.setCancelData(mpRequest);
#ifdef TEST_PRINT
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::handleIncoming sending cancel after receiving first provisional response");
#endif
            handleOutgoing(cancel,
                           userAgent,
                           transactionList,
                           MESSAGE_CANCEL);
        }     // end send delayed cancel now that we got provo response

        // Incoming CANCEL, respond and cancel children
        else if(mIsServerTransaction &&
            relationship == MESSAGE_CANCEL)
        {
            if(mpRequest)
            {
                UtlString reqMethod;
                mpRequest->getRequestMethod(&reqMethod);
                if(reqMethod.compareTo(SIP_INVITE_METHOD) == 0 &&
                   (mTransactionState == TRANSACTION_PROCEEDING ||
                   mTransactionState == TRANSACTION_CALLING))
                {
                    // Proxy transaction
                    if(!mIsUaTransaction)
                    {
                        cancelChildren(userAgent,
                                       transactionList);
                        shouldDispatch = FALSE;
                    }

                    // UA transaction
                    else
                    {
                        shouldDispatch = TRUE;
                    }

                    if(mpLastFinalResponse == NULL)
                    {
                        // I think this is wrong only the app. layer of a
                        // UAS should response with 487
                        // Respond to the server transaction INVITE
                        //SipMessage inviteResponse;
                        //inviteResponse.setResponseData(mpRequest,
                        //                    SIP_REQUEST_TERMINATED_CODE,
                        //                    SIP_REQUEST_TERMINATED_TEXT);
                        //handleOutgoing(inviteResponse, userAgent,
                        //    MESSAGE_FINAL);
                    }

                }

                // Too late to cancel, non-INVITE request and
                // successfully canceled INVITEs all get a
                // 200 response to the CANCEL
                SipMessage cancelResponse;
                cancelResponse.setResponseData(&incomingMessage,
                                    SIP_OK_CODE,
                                    SIP_OK_TEXT);
#ifdef TEST_PRINT
                OsSysLog::add(FAC_SIP, PRI_DEBUG,
                              "SipTransaction::handleIncoming sending CANCEL response");
#endif
                handleOutgoing(cancelResponse,
                               userAgent,
                               transactionList,
                               MESSAGE_CANCEL_RESPONSE);
            }

            // No request for to cancel
            else
            {
                // Send a transaction not found error
                SipMessage cancelError;
                cancelError.setBadTransactionData(&incomingMessage);
#ifdef TEST_PRINT
                OsSysLog::add(FAC_SIP, PRI_DEBUG,
                              "SipTransaction::handleIncoming transaction not found, sending CANCEL response");
#endif
                handleOutgoing(cancelError,
                               userAgent,
                               transactionList,
                               MESSAGE_CANCEL_RESPONSE);
            }

        } // End cancel request

        else
        {
            // ACK received for server transaction
            if(mIsServerTransaction &&
               relationship == MESSAGE_ACK)
            {
                int responseCode = -1;
                if(mpLastFinalResponse)
                {
                    responseCode =
                        mpLastFinalResponse->getResponseStatusCode();
                }

                // If this INVITE transaction ended in an error final response
                // we do not forward the message via the clients transactions
                // the client generates its own ACK
                if(responseCode >= SIP_3XX_CLASS_CODE)
                {
                    shouldDispatch = FALSE;
                }

                // Else if this was a successful INVITE the ACK should:
                //     only come to this proxy if the INVITE was record-routed
                //  or the previous hop that send this ACK incorrectly sent the
                //     ACK to the same URI as the INVITE
                else
                {
                    shouldDispatch = TRUE;
                }
            }

            else    // covers 2xx ACKs
            {
                shouldDispatch = TRUE;
            }

        }       // end "else" that seems to take care of ACKs
    } // End: Requests, provisional responses, Cancel response

#   ifdef TEST_PRINT
    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                  "SipTransaction::handleIncoming %p asking parent shouldDispatch=%d delayed=%p",
                  this, shouldDispatch, delayedDispatchedMessage );
#   endif

#   ifdef DISPATCH_DEBUG
    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                  "SipTransaction::handleIncoming %p "
                  "before handleChildIncoming shouldDispatch=%d delayed=%p",
                  this, shouldDispatch, delayedDispatchedMessage );
#   endif

    shouldDispatch =
        handleChildIncoming(incomingMessage,
                             userAgent,
                             relationship,
                             transactionList,
                             shouldDispatch,
                             delayedDispatchedMessage);

#   ifdef DISPATCH_DEBUG
    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                  "SipTransaction::handleIncoming %p "
                  "after handleChildIncoming shouldDispatch=%d delayed=%p",
                  this, shouldDispatch, delayedDispatchedMessage);
#   endif
#   ifdef TEST_PRINT
    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                  "SipTransaction::handleIncoming %p "
                  "parent says shouldDispatch=%d delayed=%p",
                  this, shouldDispatch, delayedDispatchedMessage);
#   endif
    touch();

    return(shouldDispatch);
} // end handleIncoming

void SipTransaction::removeTimer(OsTimer* timer)
{
   mTimers.removeReference(timer);
}

void SipTransaction::deleteTimers()
{
    OsTimer* timer = NULL;

    while ((timer = dynamic_cast<OsTimer*>(mTimers.get() /* pop one timer */)))
    {
#       ifdef TEST_PRINT
        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                      "SipTransaction::deleteTimers deleting timer %p",
                      timer);
#       endif

        // If the timer has not fired, we must delete the dependent
        // SipMessageEvent.
        // If the timer has fired, the consequent OsEventMsg is on the
        // queue of the SipUserAgent, and SipUserAgent::handleMessage will
        // delete the dependent SipMessageEvent.
        if (timer->stop(FALSE /* do not block */) == OS_SUCCESS)
        {
            SipMessageEvent* pMsgEvent = (SipMessageEvent*) timer->getUserData();
            delete pMsgEvent;
        }

        // We always delete the timer.
        delete timer;
    }
}

void SipTransaction::stopTimers()
{
    UtlSListIterator iterator(mTimers);
    OsTimer* timer = NULL;

    while ((timer = (OsTimer*)iterator()))
    {
        timer->stop();
    }
}


void SipTransaction::cancel(SipUserAgent& userAgent,
                            SipTransactionList& transactionList)
{
    if(mIsServerTransaction)
    {
        // Should not get here this is only for kids (i.e. child client transactions).
        OsSysLog::add(FAC_SIP, PRI_ERR, "SipTransaction::cancel called on server transaction");
    }

    else if(!mIsCanceled)
    {
        mIsCanceled = TRUE;

        if(mpRequest)
        {
            if(mpCancel)
            {
                OsSysLog::add(FAC_SIP, PRI_ERR, "SipTransaction::cancel"
                              " cancel request already exists");
            }
            // Do not send CANCELs for non-INVITE transactions
            // (all the other state stuff should be done)
            else if(mTransactionState == TRANSACTION_PROCEEDING &&
                    mIsDnsSrvChild &&
                    mRequestMethod.compareTo(SIP_INVITE_METHOD) == 0)
            {
                // We can only send a CANCEL if we have heard
                // back a provisional response.  If we have a
                // final response it is too late
                SipMessage cancel;
                cancel.setCancelData(mpRequest);
#ifdef TEST_PRINT
                OsSysLog::add(FAC_SIP, PRI_DEBUG,
                              "SipTransaction::cancel sending CANCEL");
#endif
                handleOutgoing(cancel,
                               userAgent,
                               transactionList,
                               MESSAGE_CANCEL);
            }

            //if(mIsRecursing)
            {
                cancelChildren(userAgent,
                               transactionList);
            }
        }

        // If this transaction has been initiated (i.e. request
        // has been created and sent)
        else if(mTransactionState != TRANSACTION_LOCALLY_INIITATED)
        {
            OsSysLog::add(FAC_SIP, PRI_ERR, "SipTransaction::cancel no request");
        }

    }
    else
    {
        OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::cancel already canceled");
    }
}

void SipTransaction::cancelChildren(SipUserAgent& userAgent,
                                    SipTransactionList& transactionList)
{
    // Cancel all the child transactions
    UtlSListIterator iterator(mChildTransactions);
    SipTransaction* childTransaction = NULL;
    while ((childTransaction = (SipTransaction*) iterator()))
    {
        childTransaction->cancel(userAgent,
                                 transactionList);
    }
}

void SipTransaction::linkChild(SipTransaction& newChild)
{
    if(newChild.mpParentTransaction)
    {
        OsSysLog::add(FAC_SIP, PRI_WARNING, "SipTransaction::linkChild child.parent is not NULL");
    }
    newChild.mpParentTransaction = this;
    newChild.mIsBusy = mIsBusy;

    if(mChildTransactions.containsReference(&newChild))
    {
        OsSysLog::add(FAC_SIP, PRI_WARNING, "SipTransaction::linkChild child already a child");
    }
    else
    {
        // The children are supposed to be sorted by Q value,
        // largest first
        UtlSListIterator iterator(mChildTransactions);
        SipTransaction* childTransaction = NULL;
        UtlBoolean childInserted = FALSE;
        int sortIndex = 0;

        while (!childInserted && (childTransaction = (SipTransaction*) iterator()))
        {
            if(childTransaction->mQvalue < newChild.mQvalue)
            {
                mChildTransactions.insertAt(sortIndex, &newChild);
                childInserted = TRUE;
            }
            else
            {
               sortIndex++;
            }
        }

        // It goes last
        if(!childInserted)
        {
           mChildTransactions.append(&newChild);
        }
    }

    if(mIsServerTransaction &&
        mIsUaTransaction)
    {
        mIsUaTransaction = FALSE;
        OsSysLog::add(FAC_SIP, PRI_WARNING, "SipTransaction::linkChild"
                      " converting server UA transaction to server proxy transaction");
    }
#ifdef DUMP_TRANSACTIONS
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::linkChild dumps whole TransactionTree");
    justDumpTransactionTree();
#endif
}

void SipTransaction::dumpTransactionTree(UtlString& dumpstring,
                                         UtlBoolean dumpMessagesAlso)
{
    SipTransaction* parent = getTopMostParent();
    if(parent == NULL) parent = this;

    if(parent)
    {
        parent->toString(dumpstring, dumpMessagesAlso);
        parent->dumpChildren(dumpstring, dumpMessagesAlso);
    }
}


void SipTransaction::justDumpTransactionTree(void)
{
    UtlString transTree;
    SipTransaction* parent = getTopMostParent();

    if (!parent)
    {
        parent = this;
    }
    parent->dumpTransactionTree(transTree, FALSE);
    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                  "SipTransactionList::justDumpTransactionTree"
                  " transaction: %p "
                  " top-most transaction tree: %s",
                  this,
                  transTree.data()
                  );
}

void SipTransaction::dumpChildren(UtlString& dumpstring,
                                  UtlBoolean dumpMessagesAlso)
{
    UtlSListIterator iterator(mChildTransactions);
    SipTransaction* childTransaction = NULL;
    UtlString childString;

    while ((childTransaction = (SipTransaction*) iterator()))
    {
        // Dump Child
        childString.remove(0);
        childTransaction->toString(childString, dumpMessagesAlso);
        dumpstring.append(childString);

        // Dump childred recursively
        childString.remove(0);
        childTransaction->dumpChildren(childString, dumpMessagesAlso);
        dumpstring.append(childString);
    }
}

void SipTransaction::toString(UtlString& dumpString,
                              UtlBoolean dumpMessagesAlso)
{
    char numBuffer[64];

    dumpString.append("  SipTransaction dump:\n\tthis: ");
    sprintf(numBuffer, "%p", this);
    dumpString.append(numBuffer);

    dumpString.append("\n\thash: ");
    dumpString.append(this->data());
    dumpString.append("\n\tmCallId: ");
    dumpString.append(mCallId);
    dumpString.append("\n\tmpBranchId->data(): ");
    dumpString.append(mpBranchId->data());
    dumpString.append("\n\tmRequestUri: ");
    dumpString.append(mRequestUri);
    dumpString.append("\n\tmSendToAddress: ");
    dumpString.append(mSendToAddress);
    dumpString.append("\n\tmSendToPort: ");
    sprintf(numBuffer, "%d", mSendToPort);
    dumpString.append(numBuffer);
    dumpString.append("\n\tmSendToProtocol: ");
    UtlString protocolString;
    SipMessage::convertProtocolEnumToString(mSendToProtocol,
        protocolString);
    dumpString.append(protocolString);
    //sprintf(numBuffer, "%d", mSendToProtocol);
    //dumpString.append(numBuffer);

    if(mpDnsDestinations)
    {
        dumpString.append("\n\tmpDnsSrvRecords:\n\t\tPref\tWt\tType\tName(ip):Port");
        UtlString srvName;
        UtlString srvIp;
        char srvRecordNums[128];
        for (int i=0; mpDnsDestinations[i].isValidServerT(); i++)
        {
           mpDnsDestinations[i].getHostNameFromServerT(srvName);
           mpDnsDestinations[i].getIpAddressFromServerT(srvIp);
           sprintf(srvRecordNums, "\n\t\t%d\t%d\t%d\t",
                   mpDnsDestinations[i].getPriorityFromServerT(),
                   mpDnsDestinations[i].getWeightFromServerT(),
                   mpDnsDestinations[i].getProtocolFromServerT());
           dumpString.append(srvRecordNums);
           dumpString.append(srvName);
           dumpString.append("(");
           dumpString.append(srvIp);
           sprintf(srvRecordNums, "):%d",
                   mpDnsDestinations[i].getPortFromServerT());
           dumpString.append(srvRecordNums);
        }
    }
    else
    {
        dumpString.append("\n\tmpDnsSrvRecords: NULL");
    }

    dumpString.append("\n\tmFromField: ");
    dumpString.append(mFromField.toString());
    dumpString.append("\n\tmToField: ");
    dumpString.append(mToField.toString());
    dumpString.append("\n\tmRequestMethod: ");
    dumpString.append(mRequestMethod);
    dumpString.append("\n\tmCseq: ");
    sprintf(numBuffer, "%d", mCseq);
    dumpString.append(numBuffer);
    dumpString.append("\n\tmIsServerTransaction: ");
    dumpString.append(mIsServerTransaction ? "TRUE" : " FALSE");
    dumpString.append("\n\tmIsUaTransaction: ");
    dumpString.append(mIsUaTransaction ? "TRUE" : " FALSE");

    UtlString msgString;
    ssize_t len;

    dumpString.append("\n\tmpRequest: ");
    if(mpRequest && dumpMessagesAlso)
    {
        mpRequest->getBytes(&msgString, &len);
        dumpString.append("\n==========>\n");
        dumpString.append(msgString);
        dumpString.append("\n==========>\n");
    }
    else
    {
        sprintf(numBuffer, "%p", mpRequest);
        dumpString.append(numBuffer);
    }

    dumpString.append("\n\tmpLastProvisionalResponse: ");
    if(mpLastProvisionalResponse && dumpMessagesAlso)
    {
        mpLastProvisionalResponse->getBytes(&msgString, &len);
        dumpString.append("\n==========>\n");
        dumpString.append(msgString);
        dumpString.append("\n==========>\n");
    }
    else
    {
        if (mpLastProvisionalResponse)
        {
            sprintf(numBuffer, "%d ", mpLastProvisionalResponse->getResponseStatusCode());
            dumpString.append(numBuffer);
        }
        sprintf(numBuffer, "%p", mpLastProvisionalResponse);
        dumpString.append(numBuffer);
    }

    dumpString.append("\n\tmpLastFinalResponse: ");
    if(mpLastFinalResponse && dumpMessagesAlso)
    {
        mpLastFinalResponse->getBytes(&msgString, &len);
        dumpString.append("\n==========>\n");
        dumpString.append(msgString);
        dumpString.append("\n==========>\n");
    }
    else
    {
        if (mpLastFinalResponse)
        {
            sprintf(numBuffer, "%d ", mpLastFinalResponse->getResponseStatusCode());
            dumpString.append(numBuffer);
        }
        sprintf(numBuffer, "%p", mpLastFinalResponse);
        dumpString.append(numBuffer);
    }

    dumpString.append("\n\tmpAck: ");
    if(mpAck && dumpMessagesAlso)
    {
        mpAck->getBytes(&msgString, &len);
        dumpString.append("\n==========>\n");
        dumpString.append(msgString);
        dumpString.append("\n==========>\n");
    }
    else
    {
        sprintf(numBuffer, "%p", mpAck);
        dumpString.append(numBuffer);
    }

    dumpString.append("\n\tmpCancel: ");
    if(mpCancel && dumpMessagesAlso)
    {
        mpCancel->getBytes(&msgString, &len);
        dumpString.append("\n==========>\n");
        dumpString.append(msgString);
        dumpString.append("\n==========>\n");
    }
    else
    {
        sprintf(numBuffer, "%p", mpCancel);
        dumpString.append(numBuffer);
    }

    dumpString.append("\n\tmpCancelResponse: ");
    if(mpCancelResponse && dumpMessagesAlso)
    {
        mpCancelResponse->getBytes(&msgString, &len);
        dumpString.append("\n==========>\n");
        dumpString.append(msgString);
        dumpString.append("\n==========>\n");
    }
    else
    {
        if (mpCancelResponse)
        {
            sprintf(numBuffer, "%d", mpCancelResponse->getResponseStatusCode());
            dumpString.append(numBuffer);
        }
        sprintf(numBuffer, "%p", mpCancelResponse);
        dumpString.append(numBuffer);
    }

    dumpString.append("\n\tmpParentTransaction: ");
    sprintf(numBuffer, "%p", mpParentTransaction);
    dumpString.append(numBuffer);

    UtlSListIterator iterator(mChildTransactions);
    SipTransaction* childTransaction = NULL;
    int childCount = 0;
    while ((childTransaction = (SipTransaction*) iterator()))
    {
        dumpString.append("\n\tmChildTransactions[");
        sprintf(numBuffer, "%d]: %p", childCount, childTransaction);
        dumpString.append(numBuffer);
        childCount++;
    }
    if(childCount == 0)
    {
        dumpString.append("\n\tmChildTransactions: none");
    }

    dumpString.append("\n\tmTransactionCreateTime: ");
    sprintf(numBuffer, "%ld", mTransactionCreateTime);
    dumpString.append(numBuffer);

    dumpString.append("\n\tmTransactionStartTime: ");
    sprintf(numBuffer, "%ld", mTransactionStartTime);
    dumpString.append(numBuffer);

    dumpString.append("\n\tmTimeStamp: ");
    sprintf(numBuffer, "%ld", mTimeStamp);
    dumpString.append(numBuffer);

    dumpString.append("\n\tmTransactionState: ");
    dumpString.append(stateString(mTransactionState));

    dumpString.append("\n\tmIsCanceled: ");
    dumpString.append(mIsCanceled ? "TRUE" : " FALSE");

    dumpString.append("\n\tmIsRecursing: ");
    dumpString.append(mIsRecursing ? "TRUE" : " FALSE");

    dumpString.append("\n\tmIsDnsSrvChild: ");
    dumpString.append(mIsDnsSrvChild ? "TRUE" : " FALSE");

    dumpString.append("\n\tmProvisionalSdp: ");
    dumpString.append(mProvisionalSdp ? "TRUE" : " FALSE");

    dumpString.append("\n\tmQvalue: ");
    sprintf(numBuffer, "%lf", mQvalue);
    dumpString.append(numBuffer);

    dumpString.append("\n\tmExpires: ");
    sprintf(numBuffer, "%d", mExpires);
    dumpString.append(numBuffer);

    dumpString.append("\n\tmIsBusy: ");
    sprintf(numBuffer, "%d", mIsBusy);
    dumpString.append(numBuffer);

    dumpString.append("\n\tmBusyTaskName: ");
    dumpString.append(mBusyTaskName);

    dumpString.append("\n\tmWaitingList: ");
    sprintf(numBuffer, "%p ", mWaitingList);
    dumpString.append(numBuffer);
    if(mWaitingList)
    {
       sprintf(numBuffer, "(%ld)", (long)mWaitingList->entries());
        dumpString.append(numBuffer);
    }

    dumpString.append("\n");
}


void SipTransaction::notifyWhenAvailable(OsEvent* availableEvent)
{
    SipTransaction* parent = getTopMostParent();
    if(parent == NULL) parent = this;

    if(parent && availableEvent)
    {
        if(parent->mWaitingList == NULL)
        {
            parent->mWaitingList = new UtlSList();
        }

        UtlSList* list = parent->mWaitingList;

        UtlVoidPtr* eventNode = new UtlVoidPtr(availableEvent);

        list->append(eventNode);
    }
    else
    {
        OsSysLog::add(FAC_SIP, PRI_ERR, "SipTransaction::notifyWhenAvailable"
                      " parent: %p avialableEvent: %p",
                      parent, availableEvent);
    }
}

void SipTransaction::signalNextAvailable()
{
    SipTransaction* parent = getTopMostParent();
    if(parent == NULL) parent = this;

    if(parent && parent->mWaitingList)
    {
        // Remove the first event that is waiting for this transaction
        UtlVoidPtr* eventNode = (UtlVoidPtr*) parent->mWaitingList->get();

        if(eventNode)
        {
            OsEvent* waitingEvent = (OsEvent*) eventNode->getValue();

            OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::signalNextAvailable"
                          " %p signaling: %p",
                          parent, waitingEvent);

            if(waitingEvent)
            {
                // If the event is already signaled, the other side
                // gave up waiting, so this side needs to free up
                // the event.
                if(waitingEvent->signal(1) == OS_ALREADY_SIGNALED)
                {
                    delete waitingEvent;
                    waitingEvent = NULL;
                }
            }
            delete eventNode;
            eventNode = NULL;
        }
    }
}

void SipTransaction::signalAllAvailable()
{
    SipTransaction* parent = getTopMostParent();
    if(parent == NULL) parent = this;

    if(parent && parent->mWaitingList)
    {
        UtlSList* list = parent->mWaitingList;
        // Remove the first event that is waiting for this transaction
        UtlVoidPtr* eventNode = NULL;
        while ((eventNode = (UtlVoidPtr*) list->get()))
        {
            if(eventNode)
            {
                OsEvent* waitingEvent = (OsEvent*) eventNode->getValue();

                if(waitingEvent)
                {
                    // If the event is already signaled, the other side
                    // gave up waiting, so this side needs to free up
                    // the event.
                    if(waitingEvent->signal(1) == OS_ALREADY_SIGNALED)
                    {
                        delete waitingEvent;
                        waitingEvent = NULL;
                    }
                }
                delete eventNode;
                eventNode = NULL;
            }
        }
    }
}


/* ============================ ACCESSORS ================================= */

const char* SipTransaction::stateString(enum transactionStates state)
{
   const char* stateStrings[NUM_TRANSACTION_STATES+1] =
      {
         "UNKNOWN",
         "LOCALLY_INITIATED",
         "CALLING",
         "PROCEEDING",
         "COMPLETE",
         "CONFIRMED",
         "TERMINATED",
         "UNDEFINED"
      };
   return (state >= TRANSACTION_UNKNOWN && state < NUM_TRANSACTION_STATES)
      ? stateStrings[state]
      : stateStrings[NUM_TRANSACTION_STATES] /* UNDEFINED */;
}

const char* SipTransaction::relationshipString(enum messageRelationship relationship)
{
   const char* relationshipStrings[NUM_RELATIONSHIPS+1] =
      {
         "UNKNOWN",
         "UNRELATED",
         "SAME_SESSION",
         "DIFFERENT_BRANCH",
         "REQUEST",
         "PROVISIONAL",
         "FINAL",
         "NEW_FINAL",
         "CANCEL",
         "CANCEL_RESPONSE",
         "ACK",
         "2XX_ACK",
         "MESSAGE_2XX_ACK_PROXY",
         "DUPLICATE",
         "UNDEFINED"
      };
   return (relationship >= MESSAGE_UNKNOWN && relationship < NUM_RELATIONSHIPS)
      ? relationshipStrings[relationship]
      : relationshipStrings[NUM_RELATIONSHIPS] /* UNDEFINED */;
}

void SipTransaction::buildHash(const SipMessage& message,
                              UtlBoolean isOutgoing,
                              UtlString& hash)
{
    UtlBoolean isServerTransaction =
        message.isServerTransaction(isOutgoing);

    message.getCallIdField(&hash);
    hash.append(isServerTransaction ? 's' : 'c');

    int cSeq;
    //UtlString method;
    message.getCSeqField(&cSeq, NULL /*&method*/);
    char cSeqString[20];
    sprintf(cSeqString, "%d", cSeq);
    hash.append(cSeqString);
}

SipTransaction* SipTransaction::getTopMostParent() const
{
    SipTransaction* topParent = NULL;
    if(mpParentTransaction)
    {
        topParent = mpParentTransaction->getTopMostParent();

        if(topParent == NULL)
        {
            topParent = mpParentTransaction;
        }
    }

    return(topParent);
}

void SipTransaction::getCallId(UtlString& callId) const
{
    callId = mCallId;
}

enum SipTransaction::transactionStates SipTransaction::getState() const
{
    return(mTransactionState);
}

/*long SipTransaction::getStartTime() const
{
    return(mTransactionStartTime);
}*/

long SipTransaction::getTimeStamp() const
{
    return(mTimeStamp);
}

void SipTransaction::touch()
{
    // We touch the whole parent-child tree so that
    // none of transactions get garbage collected
    // until they are all stale.  This saves checking
    // up and down the tree during garbage collection
    // to see if there are any still active transactions.

    SipTransaction* topParent = getTopMostParent();

    // We end up setting the date twice on this
    // transaction if it is not the top most parent
    // but so what.  The alternative is using a local
    // variable to hold the date.  There is no net
    // savings.
    OsTime time;
    OsDateTime::getCurTimeSinceBoot(time);
    mTimeStamp = time.seconds();
    //osPrintf("SipTransaction::touch seconds: %ld usecs: %ld\n",
    //    time.seconds(), time.usecs());
    //mTimeStamp = OsDateTime::getSecsSinceEpoch();

    if(topParent)
    {
        topParent->touchBelow(mTimeStamp);
    }
    else
    {
        touchBelow(mTimeStamp);
    }
}

void SipTransaction::touchBelow(int newDate)
{
    mTimeStamp = newDate;

#ifdef TEST_PRINT
    UtlString serialized;
    toString(serialized, FALSE);
    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                  "SipTransaction::touchBelow '%s'", serialized.data());
#endif // TEST_PRINT

    SipTransaction* child = NULL;
    UtlSListIterator iterator(mChildTransactions);
    while((child = (SipTransaction*) iterator()))
    {
        child->touchBelow(newDate);
    }
}


SipMessage* SipTransaction::getRequest()
{
    return(mpRequest);
}

SipMessage* SipTransaction::getLastProvisionalResponse()
{
    return(mpLastProvisionalResponse);
}

SipMessage* SipTransaction::getLastFinalResponse()
{
    return(mpLastFinalResponse);
}

void SipTransaction::markBusy()
{
#   ifdef LOG_TRANSLOCK
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::markBusy %p", this);
#   endif

    if(mpParentTransaction) mpParentTransaction->markBusy();
    else
    {
        OsTime time;
        OsDateTime::getCurTimeSinceBoot(time);
        int busyTime = time.seconds();
        // Make sure it is not equal to zero
        if(!busyTime) busyTime++;
        doMarkBusy(busyTime);

        OsTask* busyTask = OsTask::getCurrentTask();
        if(busyTask) mBusyTaskName = busyTask->getName();
        else mBusyTaskName = "";
    }
}

void SipTransaction::doMarkBusy(int markData)
{
#   ifdef LOG_TRANSLOCK
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::doMarkBusy(%d) %p", markData, this);
#   endif

    mIsBusy = markData;

    // Recurse through the children and mark them busy
    UtlSListIterator iterator(mChildTransactions);
    SipTransaction* childTransaction = NULL;
    while ((childTransaction = (SipTransaction*) iterator()))
    {
        childTransaction->doMarkBusy(markData);
    }
}

void SipTransaction::markAvailable()
{
#   ifdef LOG_TRANSLOCK
    OsSysLog::add(FAC_SIP, PRI_DEBUG, "SipTransaction::markAvailable %p", this);
#   endif

    // Recurse to the parent
    if(mpParentTransaction) mpParentTransaction->markAvailable();

    // This is the top most parent
    else
    {
        touch(); //set the last used time for parent and all children
        doMarkBusy(0);
        signalNextAvailable();
    }
}

/*void SipTransaction::doMarkAvailable()
{
    mIsBusy = FALSE;

    // Recurse through the children and mark them available
    UtlSListIterator iterator(mChildTransactions);
    SipTransaction* childTransaction = NULL;
    while(childTransaction = (SipTransaction*) iterator())
    {
        childTransaction->doMarkAvailable();
    }
}
*/

/* ============================ INQUIRY =================================== */

UtlBoolean SipTransaction::isServerTransaction() const
{
    return(mIsServerTransaction);
}

UtlBoolean SipTransaction::isDnsSrvChild() const
{
    return(mIsDnsSrvChild);
}

UtlBoolean SipTransaction::isUaTransaction() const
{
    return(mIsUaTransaction);
}

UtlBoolean SipTransaction::isChildSerial()
{
    // The child transactions are supposed to be sorted by
    // Q value.  So if we look at the first and last and they
    // are different then there are serially searched children

    UtlBoolean isSerial = FALSE;
    SipTransaction* child = (SipTransaction*)mChildTransactions.first();
    if(child)
    {
        double q1 = child->mQvalue;

        child = (SipTransaction*)mChildTransactions.last();
        if(child)
        {
            double q2 = child->mQvalue;
            if((q1-q2)*(q1-q2) > MIN_Q_DELTA_SQUARE)
            {
                isSerial = TRUE;
            }
        }

    }

    return(isSerial);
}

UtlBoolean SipTransaction::isEarlyDialogWithMedia()
{
    UtlBoolean earlyDialogWithMedia = FALSE;

    if(mProvisionalSdp &&
       mTransactionState > TRANSACTION_LOCALLY_INIITATED &&
       mTransactionState < TRANSACTION_COMPLETE)
    {
        earlyDialogWithMedia = TRUE;

        // This should not occur, the state should be ?TERMINATED?
        if(mIsCanceled)
        {
            OsSysLog::add(FAC_SIP, PRI_ERR, "SipTransaction::isEarlyDialogWithMedia"
                          " transaction state: %s incorrect for canceled transaction",
                          stateString(mTransactionState));
        }

        // This should not occur, the state should be COMPETED or CONFIRMED
        if(mIsRecursing)
        {
           OsSysLog::add(FAC_SIP, PRI_ERR, "SipTransaction::isEarlyDialogWithMedia"
                         " transaction state: %s incorrect for recursing transaction",
                          stateString(mTransactionState));
        }
    }

    return(earlyDialogWithMedia);
}

UtlBoolean SipTransaction::isChildEarlyDialogWithMedia()
{
    UtlBoolean earlyDialogWithMedia = FALSE;
    UtlSListIterator iterator(mChildTransactions);
    SipTransaction* childTransaction = NULL;

    while ((childTransaction = (SipTransaction*) iterator()))
    {
        // If the state is initiated, no request has been sent
        // for this transaction and this an all other children after
        // this one in the list should be in the same state
        if(childTransaction->mTransactionState ==
            TRANSACTION_LOCALLY_INIITATED)
        {
            break;
        }

        earlyDialogWithMedia = childTransaction->isEarlyDialogWithMedia();
    }

    return(earlyDialogWithMedia);
}

UtlBoolean SipTransaction::isMethod(const char* methodToMatch) const
{
    return(strcmp(mRequestMethod.data(), methodToMatch) == 0);
}

enum SipTransaction::messageRelationship
SipTransaction::whatRelation(const SipMessage& message,
                             UtlBoolean isOutgoing) const
{
    enum messageRelationship relationship;
#   ifdef LOG_FORKING
    int matchClause;
#   define SET_RELATIONSHIP(r) \
    {                          \
       relationship = r;       \
       matchClause = __LINE__; \
    }
#   else
#   define SET_RELATIONSHIP(r) \
    {                          \
       relationship = r;       \
    }
#   endif
    SET_RELATIONSHIP(MESSAGE_UNKNOWN);

    UtlString msgCallId;
    message.getCallIdField(&msgCallId);

    // Note: this is nested to bail out as soon as possible
    // for efficiency reasons

#   ifdef LOG_FORKING
    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                  "SipTransaction::whatRelation %s\n"
                  "\tmsg call: '%s'\n"
                  "\tcmp call: '%s'",
                  isOutgoing ? "out" : "in",
                  msgCallId.data(), mCallId.data());
#   endif

    // CallId matches
    if(mCallId.compareTo(msgCallId) == 0)
    {
        int msgCseq;
        UtlString msgMethod;
        message.getCSeqField(&msgCseq, &msgMethod);
        UtlBoolean isResponse = message.isResponse();
        int lastFinalResponseCode = mpLastFinalResponse ?
            mpLastFinalResponse->getResponseStatusCode() : -1;

        UtlString viaField;
        UtlString msgBranch;
        UtlBoolean msgHasVia = message.getViaFieldSubField(&viaField, 0);
        SipMessage::getViaTag(viaField.data(), "branch", msgBranch);

#       ifdef LOG_FORKING
        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                      "SipTransaction::whatRelation call id matched\n"
                      "\tvia: '%s'\n\tbranch: '%s'",
                      viaField.data(), msgBranch.data());
#       endif

        bool branchPrefixSet = BranchId::isRFC3261(msgBranch);

        UtlBoolean toTagMatches = FALSE;
        int toTagFinalRespIsSet = -1;
        UtlBoolean fromTagMatches;
        UtlBoolean branchIdMatches = mpBranchId->equals(msgBranch);
        UtlBoolean mustCheckTags;

        // If the branch prefix is set, we can assume that
        // all we need to look at is the branch parameter to
        // determine if this is the same transaction or not.
        // ACK to INVITEs with 200 response are really a different
        // transaction.  For convenience we consider them to be
        // the same.  CANCEL is also a different transaction
        // that we store in the same SipTransaction object.
        if(!branchPrefixSet ||                                  // no RFC3261 branch id
           (!isResponse &&                                      
               (msgMethod.compareTo(SIP_CANCEL_METHOD) == 0 ||  // or it's aCANCEL request
               (msgMethod.compareTo(SIP_ACK_METHOD) == 0 &&     // or it's an ACK request and THIS
                lastFinalResponseCode < SIP_3XX_CLASS_CODE &&   // transaction never got error response
                lastFinalResponseCode >= SIP_2XX_CLASS_CODE) ||
                (!mIsServerTransaction &&                       // or ???
                mTransactionState == TRANSACTION_LOCALLY_INIITATED))))
        {
            // Must do expensive tag matching in these cases
            // parse the To and From fields into a Url object.
            mustCheckTags = TRUE;

            Url msgFrom;
            UtlString msgFromTag;
            UtlString fromTag;
            message.getFromUrl(msgFrom);
            msgFrom.getFieldParameter("tag", msgFromTag);

            Url *pFromUrl = (Url *)&mFromField;
            pFromUrl->getFieldParameter("tag", fromTag);
            fromTagMatches = msgFromTag.compareTo(fromTag) == 0;
        }
        else
        {
            mustCheckTags = FALSE;
            toTagMatches  = branchIdMatches;
            fromTagMatches = branchIdMatches;
        }

#       ifdef LOG_FORKING
        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                      "SipTransaction::whatRelation  %s %s %s %s",
                      branchIdMatches ? "BranchMatch" : "NoBranchMatch",
                      mustCheckTags ? "CheckTags" : "NoCheckTags",
                      fromTagMatches ? "FromTagMatch" : "NoFromTagMatch",
                      toTagMatches ? "ToTagMatch" : "NoToTagMatch"
                      );
#       endif

        // From field tag matches
        if(fromTagMatches)
        {
            UtlString msgToTag;
            // Do not check if the to tag matches untill we know
            // the from tag matches.  Also do not look at the to
            // tag if we can determine a match from the branch.
            // The parsing the To field into a Url is expensive
            // so we avoid it if we can.
            if(mustCheckTags)
            {
                Url msgTo;

                UtlString toTag;
                message.getToUrl(msgTo);
                msgTo.getFieldParameter("tag", msgToTag);

                Url *pToUrl = (Url *)&mToField;
                pToUrl->getFieldParameter("tag", toTag);

                toTagMatches = (toTag.isNull() ||
                    toTag.compareTo(msgToTag) == 0);
            }

            // To field tag matches
            if(toTagMatches)
            {
                if(mCseq == msgCseq)
                {
                    UtlBoolean isMsgServerTransaction =
                        message.isServerTransaction(isOutgoing);
                    // The message and this transaction are either both
                    // part of a server or client transaction
                    if(isMsgServerTransaction == mIsServerTransaction)
                    {
                        UtlString finalResponseToTag;
                        // Note getting and parsing a URL is expensive so
                        // we avoid it
                        if(mpLastFinalResponse &&
                           mustCheckTags)
                        {
                            Url responseTo;
                            mpLastFinalResponse->getToUrl(responseTo);
                            toTagFinalRespIsSet = responseTo.getFieldParameter("tag", finalResponseToTag);
                        }

                        UtlString msgUri;
                        UtlBoolean parentBranchIdMatches = FALSE;
                        if(!isResponse && // request
                           !mIsServerTransaction && // client transaction
                            mTransactionState == TRANSACTION_LOCALLY_INIITATED)
                        {
                            SipTransaction* parent = getTopMostParent();
                            // Should this matter whether it is a server or client TX?
                            if(parent &&
                               parent->mIsServerTransaction &&
                               parent->mpBranchId->equals(msgBranch))
                            {
                                // We know that the request originated from
                                // this client transaction's parent server
                                // transaction.
                                parentBranchIdMatches = TRUE;
                                message.getRequestUri(&msgUri);
                            }
                            else if ((parent == NULL) && !msgHasVia)
                            {
                                // This is a client transaction with no parent and
                                // it originated from this UA because there are no
                                // vias
                                parentBranchIdMatches = TRUE;
                                message.getRequestUri(&msgUri);
                            }
                        }
#if 0
                        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                      "SipTransaction::whatRelation complicated %d%d%d %d%d%d%d %d '%s' '%s' '%s'",
                                      branchIdMatches, parentBranchIdMatches, msgUri.compareTo(mRequestUri),
                                      mIsUaTransaction, mIsServerTransaction, isResponse, msgHasVia, toTagFinalRespIsSet, 
                                      finalResponseToTag.data(), msgToTag.data(), msgMethod.data());
#endif
                        // The branch of this message matches the
                        // transaction or
                        // This is the request for this client transaction
                        // and the Via had not been added yet for this
                        // transaction.  So the branch is not there yet.
                        if(branchIdMatches 
                           || (parentBranchIdMatches &&
                               msgUri.compareTo(mRequestUri) == 0) 
                           || (mIsUaTransaction && // UA client ACK transaction for 2xx
                               !mIsServerTransaction &&
                               !isResponse &&
                               !msgHasVia &&
                               msgMethod.compareTo(SIP_ACK_METHOD) == 0 &&
                               lastFinalResponseCode < SIP_3XX_CLASS_CODE &&
                               lastFinalResponseCode >= SIP_2XX_CLASS_CODE) 
                           || (mIsUaTransaction && // UA server ACK transaction for 2xx
                               mIsServerTransaction &&
                               !isResponse &&
                               msgMethod.compareTo(SIP_ACK_METHOD) == 0 &&
                               lastFinalResponseCode < SIP_3XX_CLASS_CODE &&
                               lastFinalResponseCode >= SIP_2XX_CLASS_CODE &&
                               finalResponseToTag.compareTo(msgToTag) == 0 ) 
                           || (!mIsUaTransaction && // proxy forwards ACK transaction for 2xx
                               //mIsServerTransaction &&
                               !isResponse &&
                               msgMethod.compareTo(SIP_ACK_METHOD) == 0 &&
                               lastFinalResponseCode < SIP_3XX_CLASS_CODE &&
                               lastFinalResponseCode >= SIP_2XX_CLASS_CODE ) 
                           || (mIsUaTransaction && // UA client CANCEL transaction
                               !mIsServerTransaction &&
                               !isResponse &&
                               !msgHasVia &&
                               msgMethod.compareTo(SIP_CANCEL_METHOD) == 0))
                        {
                            if(isResponse)
                            {
                                int msgResponseCode =
                                    message.getResponseStatusCode();

                                // Provisional responses
                                if(msgResponseCode < SIP_2XX_CLASS_CODE)
                                {
                                    SET_RELATIONSHIP(MESSAGE_PROVISIONAL);
                                }

                                // Final responses
                                else
                                {
                                    if(msgMethod.compareTo(SIP_ACK_METHOD) == 0)
                                    {
                                        OsSysLog::add(FAC_SIP, PRI_ERR,
                                                      "SipTransaction::messageRelationship"
                                                      " ACK response");
                                    }

                                    else if(msgMethod.compareTo(SIP_CANCEL_METHOD) == 0)
                                    {
                                        SET_RELATIONSHIP(MESSAGE_CANCEL_RESPONSE);
                                    }

                                    else if(mpLastFinalResponse)
                                    {
                                        int finalResponseCode =
                                            mpLastFinalResponse->getResponseStatusCode();
                                        if(finalResponseCode == msgResponseCode)
                                        {
                                            if (msgMethod.compareTo(SIP_INVITE_METHOD) == 0)
                                            {
                                                if(!mustCheckTags)
                                                {
                                                    Url msgTo;

                                                    message.getToUrl(msgTo);
                                                    msgTo.getFieldParameter("tag", msgToTag);

                                                    UtlString toTag;
                                                    Url toUrl;
                                                    mpLastFinalResponse->getToUrl(toUrl);
                                                    toUrl.getFieldParameter("tag", toTag);

                                                    toTagMatches = (toTag.isNull() ||
                                                        toTag.compareTo(msgToTag) == 0);
                                                }

                                                if (toTagMatches)
                                                {
#ifdef TEST_PRINT
                                                    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                                                  "DUPLICATED msg finalResponseCode - %d toTagMatches %d",
                                                                  finalResponseCode, toTagMatches);
#endif
                                                    relationship = MESSAGE_DUPLICATE;
                                                }
                                                else
                                                {
#ifdef TEST_PRINT
                                                    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                                                  "MESSAGE_NEW_FINAL - finalResponseCode - %d toTagMatches %d",
                                                                  finalResponseCode, toTagMatches);
#endif
                                                    relationship = MESSAGE_NEW_FINAL;
                                                }

                                            }
                                            else
                                            {
#ifdef TEST_PRINT
                                               OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                                             "DUPLICATED msg finalResponseCode - %d",
                                                             finalResponseCode);
#endif
                                               relationship = MESSAGE_DUPLICATE;
                                            }
                                        }
                                        else
                                        {
                                            SET_RELATIONSHIP(MESSAGE_NEW_FINAL);
                                        }
                                    }
                                    else
                                    {
                                        SET_RELATIONSHIP(MESSAGE_FINAL);
                                    }
                                }
                            }

                            // Requests
                            else
                            {
                                if(mpRequest)
                                {
                                    UtlString previousRequestMethod;
                                    mpRequest->getRequestMethod(&previousRequestMethod);

                                    if(previousRequestMethod.compareTo(msgMethod) == 0)
                                    {
#ifdef TEST_PRINT
                                        OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                                      "DUPLICATED msg previousRequestMethod - %s",
                                                      previousRequestMethod.data());
#endif
                                        SET_RELATIONSHIP(MESSAGE_DUPLICATE);
                                    }
                                    else if(msgMethod.compareTo(SIP_ACK_METHOD) == 0)
                                    {
                                        if(mpLastFinalResponse)
                                        {
                                            int finalResponseCode =
                                                mpLastFinalResponse->getResponseStatusCode();
                                            if(finalResponseCode >= SIP_3XX_CLASS_CODE)
                                            {
                                                SET_RELATIONSHIP(MESSAGE_ACK);
                                            }
                                            else    // 2xx response
                                            {
                                                if(!mIsUaTransaction)
                                                {
                                                   // changed this to DEBUG from WARNING, 
                                                   // it should be ok to get here in the new ACK version
                                                   OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                                                 "SipTransaction::messageRelationship"
                                                                 " ACK matches transaction"
                                                                 " with 2XX class response");
                                                }
                                                if (mIsServerTransaction)
                                                {
                                                    SET_RELATIONSHIP(MESSAGE_2XX_ACK);
                                                }
                                                else 
                                                {
#ifdef TEST_PRINT
                                                    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                                                                  "SipTransaction::whatRelation ACK PROXY is REQUEST");
#endif
                                                    SET_RELATIONSHIP(MESSAGE_2XX_ACK_PROXY);
                                                }
                                            }       // end ACK with 2xx final response
                                        }       // end ACK with valid lastFinalResponse 
                                        else
                                        {
                                            OsSysLog::add(FAC_SIP, PRI_WARNING,
                                                          "SipTransaction::whatRelation"
                                                          " ACK matches transaction "
                                                          "with NO final response");
                                            SET_RELATIONSHIP(MESSAGE_ACK);
                                        }
                                    }       // end ACK with mpRequest 
                                    else if(msgMethod.compareTo(SIP_CANCEL_METHOD) == 0)
                                    {
                                        SET_RELATIONSHIP(MESSAGE_CANCEL);
                                    }
                                    else
                                    {
                                        SET_RELATIONSHIP(MESSAGE_DUPLICATE);
                                        OsSysLog::add(FAC_SIP, PRI_WARNING,
                                                      "SipTransaction::messageRelationship"
                                                      " found %s request for transaction with %s",
                                                      msgMethod.data(),
                                                      previousRequestMethod.data());
                                    }
                                }       // no entry in mpRequest
                                else
                                {
                                    if(msgMethod.compareTo(SIP_CANCEL_METHOD) == 0)
                                    {
                                        SET_RELATIONSHIP(MESSAGE_CANCEL);
                                    }
                                    else if(msgMethod.compareTo(SIP_ACK_METHOD) == 0)
                                    {
                                        SET_RELATIONSHIP(MESSAGE_ACK);
                                    }
                                    else
                                    {
                                        SET_RELATIONSHIP(MESSAGE_REQUEST);
                                    }
                                }       // end ACK/CANCEL/request choice
                            }   // end requests that got past complex if
                        }  else { SET_RELATIONSHIP(MESSAGE_DIFFERENT_BRANCH); }   // complex test failed
                    } else { SET_RELATIONSHIP(MESSAGE_DIFFERENT_BRANCH); }     // mIsServerTransaction values don't match
                } else { SET_RELATIONSHIP(MESSAGE_SAME_SESSION); }         // no Cseq match
            } else { SET_RELATIONSHIP(MESSAGE_UNRELATED); }            // no to tag (or branch-id) match
        } else { SET_RELATIONSHIP(MESSAGE_UNRELATED); }            // no from tag (or branch-id) match
    } else { SET_RELATIONSHIP(MESSAGE_UNRELATED); }            // no callid match

#   ifdef LOG_FORKING
    OsSysLog::add(FAC_SIP, PRI_DEBUG,
                  "SipTransaction::whatRelation returning %s set on %d"
                  ,relationshipString(relationship)
                  ,matchClause
                  );
#   endif

    return(relationship);
}

UtlBoolean SipTransaction::isBusy()
{
    return(mIsBusy);
}

#if 0 // TODO redundant?
UtlBoolean SipTransaction::isUriChild(Url& uri)
{
    UtlSListIterator iterator(mChildTransactions);
    SipTransaction* childTransaction = NULL;
    UtlBoolean childHasSameUri = FALSE;
    UtlString uriString;
    uri.getUri(uriString);

    while ((childTransaction = (SipTransaction*) iterator()))
    {
        if(uriString.compareTo(childTransaction->mRequestUri) == 0)
        {
            childHasSameUri = TRUE;
            break;
        }
    }

    return(childHasSameUri);
}
#endif

UtlBoolean SipTransaction::isUriRecursed(Url& uri)
{
    SipTransaction* parent = getTopMostParent();
    if(parent == NULL) parent = this;
    UtlString uriString;
    uri.getUri(uriString);

    return(isUriRecursedChildren(uriString));
}

UtlBoolean SipTransaction::isUriRecursedChildren(UtlString& uriString)
{
    UtlBoolean childHasSameUri = FALSE;

    UtlSListIterator iterator(mChildTransactions);
    SipTransaction* childTransaction;

    while (!childHasSameUri && (childTransaction = (SipTransaction*) iterator()))
    {
       childHasSameUri = (   childTransaction->mTransactionState > TRANSACTION_LOCALLY_INIITATED
                          && (   0 == uriString.compareTo(childTransaction->mRequestUri)
                              || isUriRecursedChildren(uriString)
                              )
                          );
    }

    return(childHasSameUri);
}

//: Determine best choice for protocol, based on message size
//  Default is UDP, returns TCP only for large messages
OsSocket::IpProtocolSocketType SipTransaction::getPreferredProtocol()
{
    ssize_t msgLength;
    UtlString msgBytes;
    OsSocket::IpProtocolSocketType retProto = OsSocket::UDP;

    if (1)
    {
        mpRequest->getBytes(&msgBytes, &msgLength);
        if (msgLength > UDP_LARGE_MSG_LIMIT)
        {
            retProto = OsSocket::TCP;
            OsSysLog::add(FAC_SIP, PRI_DEBUG,
                          "SipTransaction::getLargeMsgProtocol change %d to %d for size %zd"
                          ,mSendToProtocol
                          ,retProto
                          ,msgLength
                          );
        }
    }
    return retProto;
}


/* //////////////////////////// PROTECTED ///////////////////////////////// */

/* //////////////////////////// PRIVATE /////////////////////////////////// */

/* ============================ FUNCTIONS ================================= */
