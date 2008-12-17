// TODO: handle SDPs within multipart and or S/MIME bodies   
// TODO handle UPDATE
// TODO: Ensure that LG-Nortel config plugin sets Line-based Proxy Address to 
//       SIP domain, Outbound proxy address to SIP domain and domain to <blank>.
//       This is required to allow call-park scenarios when park server sends
//       REFERs with Refer-Tos containing the contact as opposed to the AOR.
//       These settings ensure that the resulting INVITE gets sent to the proxy
//       and not directly to the contact.
// TODO: For the same reasons as above, ensure that the polycom has the followings:
//        - SIP->Outbound Proxy->Address = SIP domain
//        - SIP->Server[12] = <blank>
//        - Lines->Server1->Address = SIP domain
// 
// Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.  
// Contributors retain copyright to elements licensed under a Contributor Agreement.
// Licensed to the User under the LGPL license.
// 
//////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES

// APPLICATION INCLUDES
#include "net/SipMessage.h"
#include "os/OsReadLock.h"
#include "os/OsWriteLock.h"
#include "os/OsTimer.h"
#include "os/OsConfigDb.h"
#include "os/OsSysLog.h"
#include "utl/UtlRegex.h"
#include "utl/UtlHashMapIterator.h"
#include "sipXecsService/SipXecsService.h"
#include "sipXecsService/SharedSecret.h"
#include "SipRouter.h"
#include <net/NameValueTokenizer.h>
#include <net/Url.h>
#include "NatTraversalAgent.h"
#include "CallTracker.h"
#include "NatMaintainer.h"
#include "MediaRelay.h"

// DEFINES
#define NAT_TRAVERSAL_RULES_FILENAME         "nattraversalrules.xml"
#define NAT_TRAVERSAL_AGENT_OUTPUT_PROC_PRIO (100)
#define MAX_MEDIA_RELAY_INIT_ATTEMPTS        (3)
#define NAT_RULES_FILENAME_CONFIG_PARAM      ("NATRULES")

// CONSTANTS
// TYPEDEFS
// FORWARD DECLARATIONS

/// Factory used by PluginHooks to dynamically link the plugin instance
extern "C" AuthPlugin* getAuthPlugin(const UtlString& pluginName)
{
   return new NatTraversalAgent(pluginName);
}

/// constructor
NatTraversalAgent::NatTraversalAgent(const UtlString& pluginName ) ///< the name for this instance
   : AuthPlugin(pluginName),
     SipOutputProcessor( NAT_TRAVERSAL_AGENT_OUTPUT_PROC_PRIO ),
     mbNatTraversalFeatureEnabled( false ),
     mbOutputProcessorRegistrrationDone( false ),
     mpSipRouter( 0 ),
     mMessageProcessingMutex( OsRWMutex::Q_FIFO ),
     mpMediaRelay( 0 ),
     mpNatMaintainer( 0 ),
     mCleanupTimer( *this ),
     mpRegistrationDB( 0 ),
     mbConnectedToRegistrationDB( false ),
     mNextAvailableCallTrackerHandle( 0 )
{
   OsSysLog::add(FAC_NAT,PRI_INFO,"NatTraversalAgent plugin instantiated '%s'",
                 mInstanceName.data() );
};

/// Read (or re-read) the authorization rules.
void
NatTraversalAgent::readConfig( OsConfigDb& configDb /**< a subhash of the individual configuration
                                                      * parameters for this instance of this plugin. */
                             )
{
   OsSysLog::add(FAC_NAT, PRI_DEBUG, "NatTraversalAgent[%s]::readConfig",
                 mInstanceName.data() );
 
   UtlString fileName;
   if ( configDb.get( NAT_RULES_FILENAME_CONFIG_PARAM, fileName ) != OS_SUCCESS )
   {
      OsPath defaultPath =
         SipXecsService::Path(SipXecsService::ConfigurationDirType, NAT_TRAVERSAL_RULES_FILENAME );

      fileName = defaultPath;
      
      OsSysLog::add(FAC_NAT, PRI_DEBUG, "NatTraversalAgent[%s]::readConfig "
                    " no rules file configured; trying '%s'",
                    mInstanceName.data(), fileName.data()
                    );
   }
   else
   {
      OsSysLog::add(FAC_NAT, PRI_DEBUG, "NatTraversalAgent[%s]::readConfig "
                    " did rules file configured; trying '%s'",
                    mInstanceName.data(), fileName.data()
                    );
   }
   mNatTraversalRules.loadRules( fileName );
   
   if( mNatTraversalRules.isEnabled() )
   {
      OsSysLog::add(FAC_NAT, PRI_INFO, "NatTraversalAgent[%s]::readConfig: "
                    " NAT Traversal feature is ENABLED", mInstanceName.data() );
      
      if( !mpMediaRelay )
      {
         mpMediaRelay = new MediaRelay();
      }
      
      size_t attemptCounter;
      for( attemptCounter = 0; attemptCounter < MAX_MEDIA_RELAY_INIT_ATTEMPTS; attemptCounter++ )
      {
         if( mpMediaRelay->initialize( mNatTraversalRules.getMediaRelayPublicAddress(),
                                       mNatTraversalRules.getMediaRelayNativeAddress(),
                                       mNatTraversalRules.isXmlRpcSecured(),
                                       mNatTraversalRules.isPartOfLocalTopology( mNatTraversalRules.getMediaRelayNativeAddress() ),
                                       mNatTraversalRules.getMediaRelayXmlRpcPort(),
                                       mNatTraversalRules.getMaxMediaRelaySessions() ) == true )
         {
            break;
         }
         else
         {
            sleep( 5 );
         }
      }
      
      if( attemptCounter >= MAX_MEDIA_RELAY_INIT_ATTEMPTS )
      {
         mbNatTraversalFeatureEnabled = false;
         OsSysLog::add(FAC_NAT, PRI_CRIT, "NatTraversalAgent[%s]::readConfig failed to initialize media relay - NAT traversal feature will be disabled",
                       mInstanceName.data() );
         
      }
      else
      {
         // we managed to initialize the media relay.  Nothing prevents us now from
         // enabling the NAT traversal feature.  Set the mbNatTraversalFeatureEnabled
         // complete the initialization required by the feature
         mbNatTraversalFeatureEnabled = true;
         
         // connect to regDB as we may need to consult it from time to time.
         mpRegistrationDB = RegistrationDB::getInstance();
         mbConnectedToRegistrationDB = true;

         // start the timer that will be the heartbeat to delete stale session context objects
         OsTime cleanUpTimerPeriod( CLEAN_UP_TIMER_IN_SECS, 0 );
         mCleanupTimer.periodicEvery( cleanUpTimerPeriod, cleanUpTimerPeriod );
         OsSysLog::add(FAC_NAT, PRI_INFO, "NatTraversalAgent[%s]::readConfig successfully initialized media relay - NAT traversal feature will is enabled",
                       mInstanceName.data() );

      }
   }
   else
   {
      OsSysLog::add(FAC_NAT, PRI_INFO, "NatTraversalAgent[%s]::readConfig: "
                    " NAT Traversal feature is DISABLED", mInstanceName.data() );
  }
}

AuthPlugin::AuthResult
NatTraversalAgent::authorizeAndModify(const UtlString& id, /**< The authenticated identity of the
                                                            *   request originator, if any (the null
                                                            *   string if not).
                                                            *   This is in the form of a SIP uri
                                                            *   identity value as used in the
                                                            *   credentials database (user@domain)
                                                            *   without the scheme or any parameters.
                                                            */
                                      const Url&  requestUri, ///< parsed target Uri
                                      RouteState& routeState, ///< the state for this request.  
                                      const UtlString& method,///< the request method
                                      AuthResult  priorResult,///< results from earlier plugins.
                                      SipMessage& request,    ///< see AuthPlugin wrt modifying
                                      bool bSpiralingRequest, ///< request spiraling indication 
                                      UtlString&  reason      ///< rejection reason
                                      )
{
   OsWriteLock lock( mMessageProcessingMutex );
   
   AuthResult result = CONTINUE;
   
   if( mbNatTraversalFeatureEnabled )
   {
      // do not look at request if we are still spiraling.  We are only interested in requests
      // that are about to be sent to the request target.
      if( priorResult != DENY && bSpiralingRequest == false )
      {
         // clean up any of the proprietary headers we may have added
         request.removeHeader( SIP_SIPX_SESSION_CONTEXT_ID_HEADER, 0 );
         request.removeHeader( SIP_SIPX_FROM_CALLER_DIRECTION_HEADER, 0 );  
         
         UtlString msgBytes;
         ssize_t msgLen;
         request.getBytes(&msgBytes, &msgLen);
         OsSysLog::add(FAC_NAT, PRI_DEBUG, "NTAP considering %s", msgBytes.data() );         

         UtlString callId;
         UtlString method;
         const EndpointDescriptor* pCaller;
         const EndpointDescriptor* pCallee;
         CallTracker* pCallTracker = 0;
         
         request.getCallIdField( &callId );
         request.getRequestMethod(&method);
         pCallTracker = getCallTrackerFromCallId( callId );
         
         // If we are dealing with a dialog-forming INVITE, we may be in the presence
         // of a new call that we are not already tracking or a new call leg of an
         // already tracked call.  Figure out which case we are in by trying to find
         // the CallId in our CallTrackers map
         if( method.compareTo( SIP_INVITE_METHOD ) == 0 && routeState.isMutable() )
         {
            if( pCallTracker == 0 )
            {
               // We are not tracking this call, this means that it is 
               // a new call that we haven't seen yet.  Instantiate
               // a new tracker to follow its media session negotiations
               // and intervene as necessary to facilitate NAT traversal.
               pCallTracker = createCallTrackerAndAddToMap( callId, mNextAvailableCallTrackerHandle );   
               OsSysLog::add(FAC_NAT, PRI_DEBUG, "NatTraversalAgent[%s]::authorizeAndModify create new call tracker[%zu] to track call id %s"
                                                  , mInstanceName.data(), mNextAvailableCallTrackerHandle, callId.data() );
               mNextAvailableCallTrackerHandle++;
            }
            
            if( pCallTracker )
            {
               // We have a dialog-forming INVITE.  Inform the call tracker
               // so that it can start tracking the media session that will ensue.
               if( pCallTracker->notifyIncomingDialogFormingInvite( request, routeState, pCaller, pCallee ) )
               {
                  // We are initiating the tracking of a new fork.  Look at the locations of the 
                  // the returned caller and callee EndpointDescriptors to learn more about
                  // the location of the caller and callee endpoints. If either of these endpoints
                  // are located behind a remote NAT then special processing is required to ensure
                  // that the requests will be sent to the endpoint's public IP address and not its
                  // private IP address as will be the case if nothing is done about it.  The approach
                  // is as follows.  The caller and callee locations are inspected.  If the caller
                  // is REMOTE_NATED then a 'CrT' (CallerTransport) parameter containing the caller's
                  // public transport information will be added to the RouteState. If the callee
                  // is REMOTE_NATED then a 'CeT' (CalleeTransport) parameter containing the callee's
                  // public transport information will be added to the RouteState.
                  // Every time requests are processed by this routine, if the request travels in the
                  // caller->callee direction and no top route exists, a special X-SipX-Nat-Route:
                  // header containing the callee's public IP address is added.  A symmetric operation
                  // is performed when requests travel in the callee->caller direction. The sipXtackLib is built  
                  // to understand that header, strip it and route the request to the hostport it contains,
                  // The net result of that operation is that the request will be sent to the endpoint's public
                  // IP address and the request as seen on the wire will not contain X-SipX-Nat-Route: as it is
                  // removed by the stack before sending the message out.
                  UtlString urlString;
                  if( pCaller->getLocationCode() == REMOTE_NATED )
                  {
                     pCaller->getPublicTransportAddress().toUrlString( urlString );
                     routeState.setParameter( mInstanceName.data(), CALLER_PUBLIC_TRANSPORT_PARAM, urlString );
                  }         
                  if( pCallee->getLocationCode() == REMOTE_NATED )
                  {
                     pCallee->getPublicTransportAddress().toUrlString( urlString );
                     routeState.setParameter( mInstanceName.data(), CALLEE_PUBLIC_TRANSPORT_PARAM, urlString );
                  }  
               }
            }
         }

         bool directionIsCallerToCalled = false;
         if( routeState.isFound() || routeState.isMutable() )
         {
            directionIsCallerToCalled = routeState.directionIsCallerToCalled( mInstanceName.data() );
         }
         
         UtlString topRoute;
         if( !request.getRouteUri( 0, &topRoute ) )
         {
            // If the request target is behind a NAT and the request has an empty Route set (i.e. no 
            // Route: headers), unless we intervene here, the request will simply be sent to the endpoint's
            // private address which will fail to reach the endpoint.  To prevent that, when we are in the 
            // presence of a request that has a routeState, we extract the public IP address transport
            // in CrT (for caller) or CeT (for callee) from the RouteState and use that information in a 
            // SipxNatRoute that will cause the sipXtack to route the message to the proper routable address.
            //
            // If no route state is present and the request is not an INVITE, then send the request to 
            // the IP address contained in the x-sipX-pubcontact URL parameter of the R-URI if present.
            //
            // NOTE: For INVITE requests, we strictly rely on the CrT and CeT parameters of the Route state
            // and we ignore any x-sipX-pubcontact URL parameter that may be found in the R-URI.  The rationale
            // behind that behavior is as follows: 
            //  => INVITE requests are guaranteed to carry a Route state with CrT and CeT params when endpoint is REMOTE_NATED
            //  => CrT and CeT params cannot be forged by a malicious endpoint whereas x-sipX-pubcontact URL parameter in R-URI can.
            //     If we relied on x-sipX-pubcontact URL parameter to route INVITEs, someone could exploit that to 
            //     reach destinations that it would otherwise not be allowed to reach (PSTN gateways for example).
            UtlString sipxNatRouteString;         

            const char* pParameterName = ( directionIsCallerToCalled ? CALLEE_PUBLIC_TRANSPORT_PARAM : CALLER_PUBLIC_TRANSPORT_PARAM );
            routeState.getParameter( mInstanceName.data(), pParameterName, sipxNatRouteString );

            // If the logic above did not yield a SipXNatRoute then look for 'x-sipX-pubcontact'
            // in R-URI if the request in not tracked by a CallTracker and is not an INVITE
            if( sipxNatRouteString.isNull() && !pCallTracker && method.compareTo( SIP_INVITE_METHOD ) != 0 )
            {
               UtlString ruri;
               request.getRequestUri( &ruri );
               Url requestUri( ruri, TRUE );
               requestUri.getUrlParameter( SIPX_PUBLIC_CONTACT_URI_PARAM, sipxNatRouteString, 0 );
            }
            
            // If we managed to collect a sipXNatRoute value then set it in the message
            if( !sipxNatRouteString.isNull() )
            {
               request.setSipXNatRoute( sipxNatRouteString );
            }
         }
         
         // OPTIMIZATION CODE
         // The following lines of code implement an optimization that relieves the 
         // NatTraversalAgent::handleOutputMessage() method of this class from having
         // to decode a request's routeState in order to implement its logic.  The
         // logic in question requires two pieces of information that are encoded 
         // in the RouteState, namely, the Session-Id ("id") and directioanlity of 
         // the request. Given that the operation of decoding the RouteState 
         // from a message is somewhat expensive and given that we already have it 
         // here in decoded form, we simply  write these two information elements
         // in plain-text in proprietary headers that we add to the message so that
         // NatTraversalAgent::handleOutputMessage() can simply  extract the
         // information it needs from the headers without having to decode the RouteState.
         // The optimization introduces some coupling between classes but it was
         // decided that the benefits outweigthed the negatives.
         if( pCallTracker )
         {
            // add proprietaty param to convey session context for the benefit of 
            // NatTraversalAgent::handleOutputMessage().
            UtlString sessionId;            
            pCallTracker->getSessionContextHandle( routeState, sessionId );

            request.setHeaderValue( SIP_SIPX_SESSION_CONTEXT_ID_HEADER, sessionId.data(), 0 );

            // add proprietary param that will tell our output processor the direction of the message
            if( directionIsCallerToCalled == true )
            {
               request.setHeaderValue( SIP_SIPX_FROM_CALLER_DIRECTION_HEADER, "true", 0 );  
            }      
         }
      }
   }
   return result;
}

void NatTraversalAgent::handleOutputMessage( SipMessage& message,
                                             const char* address,
                                             int port )
{
   OsWriteLock lock( mMessageProcessingMutex );
   CallTracker* pCallTracker = 0;
   
   if( mbNatTraversalFeatureEnabled )
   {
      UtlString msgBytes;
      ssize_t msgLen;
      message.getBytes(&msgBytes, &msgLen);
      OsSysLog::add(FAC_NAT, PRI_DEBUG, "handleOutputMessage considering %s", msgBytes.data() );         

      // Check if the sipXecs is located behind a NAT.  If it is and the message 
      // is going to a destination that is not on our local private subnet then
      // we need to alter the our via: to advertize our public IP address.  A similar
      // alteration must be done to requests carrying our Record-Route.
      adjustViaForNatTraversal( message, address, port );
      adjustRecordRouteForNatTraversal( message, address, port );      

      pCallTracker = getCallTrackerForMessage( message );
      if( pCallTracker )
      {
         if( !message.isResponse() ) // handling a request
         {
            pCallTracker->handleRequest ( message, address, port );
         }
         else // handling a response
         {
            pCallTracker->handleResponse( message, address, port );
         }   
      }
      else
      {
         OsSysLog::add(FAC_NAT, PRI_DEBUG, "NatTraversalAgent[%s]::handleOutputMessage failed to retrieve CallTracker to handle request"
                                            , mInstanceName.data() );
      }
   }
}

OsStatus NatTraversalAgent::signal( intptr_t eventData )
{
   OsWriteLock lock( mMessageProcessingMutex );
   // send a timer event to all instantiated CallTracker objects
   UtlHashMapIterator CallTrackerIterator( mCallTrackersMap );
   
   while( CallTrackerIterator() )
   {
      CallTracker *pCallTracker;
      pCallTracker = dynamic_cast<CallTracker*>( CallTrackerIterator.value() );
      
      if( pCallTracker->handleCleanUpTimerTick() )
      {
         mCallTrackersMap.destroy( CallTrackerIterator.key() );
      }
   }      
   return OS_SUCCESS;
}

void NatTraversalAgent::announceAssociatedSipRouter( SipRouter* sipRouter )
{
   mpSipRouter = sipRouter;
   if( mbNatTraversalFeatureEnabled == true )
   {
      if( mbOutputProcessorRegistrrationDone == false )
      {
         mpSipRouter->addSipOutputProcessor( this );
         mbOutputProcessorRegistrrationDone = true;
      }

      if( mNatTraversalRules.isBehindNat() )
      {
         UtlString publicTransportAlias;
         char portNumericForm[24];
         sprintf( portNumericForm, "%d", mNatTraversalRules.getPublicTransportInfo().getPort() );

         publicTransportAlias = mNatTraversalRules.getPublicTransportInfo().getAddress();
         publicTransportAlias += ":";
         publicTransportAlias += portNumericForm;
         mpSipRouter->addHostAlias( publicTransportAlias );
      }
      
      // launch thread that will maintain NAT keep alives
      mpNatMaintainer = new NatMaintainer( sipRouter );
      mpNatMaintainer->start();
   }
}

// If SipXecs is behind a NAT and we are about to send a request to an
// endpoint that is not inside our local private network, this means
// that the sipXecs' native IP address is not reachable from that endpoint.
// Replace the sent-by field of the topmost via to contain our public address.
// This is requires to ensure that all responses to that request can be routed back to us.
void NatTraversalAgent::adjustViaForNatTraversal( SipMessage& message, const char* address, int port )
{
   if( mNatTraversalRules.isBehindNat() )
   {
      // check if we are dealing with a request and the destination outside our local private network
      if( !message.isResponse() && !mNatTraversalRules.isPartOfLocalTopology( address, true, true ) )
      {
         UtlString topmostVia;
         if( message.getViaFieldSubField(&topmostVia, 0 ) )
         {
            UtlString viaSentProtocol;
            NameValueTokenizer::getSubField( topmostVia, 0, SIP_SUBFIELD_SEPARATORS, &viaSentProtocol );
            UtlString sentByAndViaParams;
            NameValueTokenizer::getSubField( topmostVia, 1, SIP_SUBFIELD_SEPARATORS, &sentByAndViaParams );
            UtlString viaParams;
            NameValueTokenizer::getSubField( sentByAndViaParams, 1, ";", &viaParams );
            
            message.removeTopVia();  
            
            UtlString newVia;
            char portNumericForm[24];
            sprintf( portNumericForm, "%d", mNatTraversalRules.getPublicTransportInfo().getPort() );
            newVia += viaSentProtocol;
            newVia += SIP_SUBFIELD_SEPARATOR;
            newVia += mNatTraversalRules.getPublicTransportInfo().getAddress();
            newVia += ":";
            newVia += portNumericForm;
            newVia += ";";
            newVia += viaParams;
            
            message.addViaField( newVia, TRUE );
         }
      }
   }
}

// * If the message is a request and the destination is outside of our local private network then
//      ensure that the hostport field of the top Record-Route contains our public IP address to ensure
//      that subsequent in-dialog requests sent by that endpoint do get routed back to us.
// * If the message is a request and the destination is inside of our local private network then
//      ensure that the hostport field of the top Record-Route contains our private IP address to ensure
//      that subsequent in-dialog requests sent by that endpoint do get routed back to us.
// * If the message is a response, the behavior depends on the location of the destination:
//   * If the destination is part of our local private network then scan the Record-Route set
//     in search of the first unhandled entry matching or public or private IP address.  If one is found, change its
//     hostport to be our private IP address to ensure that subsequent in-dialog requests sent by that 
//     endpoint do get routed back to us and mark the entry as handled..
//   * If the destination is outside of our local private network then scan the Record-Route set
//     in search of the first unhandled entry matching our private or public IP address.  If one is found, change its
//     hostport to be our public IP address to ensure that subsequent in-dialog requests sent by that 
//     endpoint do get routed back to us and mark the entry as handled.
void NatTraversalAgent::adjustRecordRouteForNatTraversal( SipMessage& message, const char* address, int port )
{
   bool bDestInsideLocalPrivateNetwork = mNatTraversalRules.isPartOfLocalTopology( address, true, true );
   UtlString tmpRecordRoute;
   
   UtlString replacementAddress;
   int       replacementPort = 0;

   if( bDestInsideLocalPrivateNetwork )
   {
      replacementAddress = mNatTraversalRules.getProxyTransportInfo().getAddress();
      replacementPort    = mNatTraversalRules.getProxyTransportInfo().getPort();
   }
   else
   {
      replacementAddress = mNatTraversalRules.getPublicTransportInfo().getAddress();
      replacementPort    = mNatTraversalRules.getPublicTransportInfo().getPort();
   }
   
   if( !message.isResponse() )
   {
      if( message.getRecordRouteUri( 0, &tmpRecordRoute ) )
      {
         Url tmpRecordRouteUrl( tmpRecordRoute );
         UtlString tmpRecordRouteHost;
         
         tmpRecordRouteUrl.getHostAddress( tmpRecordRouteHost );
         int tmpRecordRoutePort = tmpRecordRouteUrl.getHostPort();            

         if( ( tmpRecordRouteHost == mNatTraversalRules.getProxyTransportInfo().getAddress()  && 
             tmpRecordRoutePort == mNatTraversalRules.getProxyTransportInfo().getPort() ) 
           ||
           ( tmpRecordRouteHost == mNatTraversalRules.getPublicTransportInfo().getAddress() && 
             tmpRecordRoutePort == mNatTraversalRules.getPublicTransportInfo().getPort() )  )
         {
               // the top route points to us, change it for the replacment IP:Port
               tmpRecordRouteUrl.setHostAddress( replacementAddress );
               tmpRecordRouteUrl.setHostPort( replacementPort );

               UtlString newtmpRecordRouteValue;
               tmpRecordRouteUrl.toString( newtmpRecordRouteValue );                        
               message.setRecordRouteField(newtmpRecordRouteValue.data(), 0 );
         }
      }
   }
   else  // we are dealing with a response
   {
      // Check if we are the intended destination of the response.  This can happen when
      // a request has spiraled through the system before being sent to the request target.
      // There is no point in analyzing the RecordRoute on every pass inside the spiral - 
      // only do it if the destination is not us.
      
      if( address != mNatTraversalRules.getProxyTransportInfo().getAddress() ||
          port    != mNatTraversalRules.getProxyTransportInfo().getPort() )
      {
         // SipMessage's interface allows us to get a specific RecordRoute entry within a recordRoute field
         // but does not provide any methods to allow for its modification.  The logic here
         // reads all the RecordRoute entries and re-adds them as single RecordRoute fields.
         // During this re-add process, the logic modifies the first Record-Route to pointing to us
         // that has not yet been modified (as determined by the presence of an 'x-sipx-done' URL param) to allow
         // for NAT traversal.  Once this is done, the old RecordRoute fields are deleted.
         //
         // As an example, let's say that a given message contains:
         // RecordRoute: <sip:IP1:port1>,<sip:IP2:port2>,<sip:IP3:port3>
         // RecordRoute: <sip:IPtoModify:portToModify>,<sip:IP4:port4>
         // RecordRoute: <sip:IP5:port5>
         // the resulting message after processing will be of the form:
         // RecordRoute: <sip:IP1:port1>
         // RecordRoute: <sip:IP2:port2><
         // RecordRoute: <sip:IP3:port3>
         // RecordRoute: <sip:ModifiedIP:ModifiedPort>
         // RecordRoute: <sip:IP4:port4>
         // RecordRoute: <sip:IP5:port5>
      
         int readRecordRouteIndex = 0;
         int writeRecordRouteIndex = 0;
         bool bRecordRouteAdjusted = false;
         
         while( message.getRecordRouteUri( readRecordRouteIndex, &tmpRecordRoute ) )
         {
            Url tmpRecordRouteUrl( tmpRecordRoute );
            UtlString tmpRecordRouteHost;
            tmpRecordRouteUrl.getHostAddress( tmpRecordRouteHost );
            int tmpRecordRoutePort = tmpRecordRouteUrl.getHostPort();

            if( !bRecordRouteAdjusted &&
                ( ( tmpRecordRouteHost == mNatTraversalRules.getProxyTransportInfo().getAddress() && 
                    tmpRecordRoutePort == mNatTraversalRules.getProxyTransportInfo().getPort() ) 
                  ||
                  ( tmpRecordRouteHost == mNatTraversalRules.getPublicTransportInfo().getAddress() && 
                    tmpRecordRoutePort == mNatTraversalRules.getPublicTransportInfo().getPort() ) 
                ) 
              )
            {
               // we found a Record-Route to us - check if it has already been adjusted and if not,
               // adjust it.
               UtlString dummyValue;
               if( !tmpRecordRouteUrl.getUrlParameter( SIPX_DONE_URI_PARAM, dummyValue ) )
               {
                  tmpRecordRouteUrl.setHostAddress( replacementAddress );
                  tmpRecordRouteUrl.setHostPort( replacementPort );
                  tmpRecordRouteUrl.setUrlParameter( SIPX_DONE_URI_PARAM, NULL );
   
                  tmpRecordRouteUrl.toString( tmpRecordRoute );                        
                  bRecordRouteAdjusted = true;
               }
            }
            message.insertHeaderField( SIP_RECORD_ROUTE_FIELD, tmpRecordRoute.data(), writeRecordRouteIndex );
            writeRecordRouteIndex++;
            readRecordRouteIndex += 2; //+1 to advance index and +1 to skip over the RecordRoute field we just added
         }
         // remove all the old record routes
         while( message.removeHeader( SIP_RECORD_ROUTE_FIELD, writeRecordRouteIndex ) )
         {
         }
      }
   }
}

// prototype of a fix to address fact that park server sends REFERs with Refer-To: carrying contact.
// Does not work but keeping it around jus tin case
void NatTraversalAgent::adjustReferToHeaderForNatTraversal( SipMessage& message, const char* address, int port )
{
   return;
   UtlString method;
   message.getRequestMethod(&method);      
   
   if( method.compareTo( SIP_REFER_METHOD ) == 0 )
   {
      UtlString referToStr;
      if (message.getReferToField( referToStr ) )
      {
         
         // Check if we are the intended destination of the REFER.  This can happen when
         // a request is spiraling through the system before being sent to the request target.
         // There is no point in analyzing the REFER on every pass inside the spiral - 
         // only do it if the destination is not us.
         if( address != mNatTraversalRules.getProxyTransportInfo().getAddress() ||
             port    != mNatTraversalRules.getProxyTransportInfo().getPort() )
         {
            Url referToUrl( referToStr );
            UtlString dummyValue;

            // if the Refer-To already has a Route: header parameter, do not 
            // interfere and leave things the way they are.  The assumption here
            // is that whoever added that Route header parameter will take care 
            // NAT traversal.
            if( !referToUrl.getHeaderParameter( SIP_ROUTE_FIELD, dummyValue ) )
            {
               // check if the host address of the Refer-To url is in IP address format
               // and it is, check to see it is ours.
               UtlString referToHost;
               
               referToUrl.getHostAddress( referToHost );
               RegEx ipV4("^[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}$");
               if( ipV4.Search( referToHost.data() ) )
               {
                  // endpoint processing the REFER will try to send the INVITE resulting
                  // from the REFER processing directly to the IP address specified in the 
                  // Refer-To: header which can cause 2 problems:
                  // #1- IP address may not be routable
                  // #2- even if #1 is not a problem, this will break the symmetric 
                  //     signaling which can lead to some NATs dropping the resulting INVITE
                  // To remedy to that, add ourself in the signaling loop so that we
                  // become the recipient of the INVITE by adding a ROUTE header parameter
                  // pointing to us in the Refer-To header.  That INVITE will be processed
                  // by the NAT traversal feature which will likely compensate for any 
                  // NAT in the path.
                  Url routeHeaderParamUrl;
                  if( mNatTraversalRules.isPartOfLocalTopology( address, true, true ) )
                  {
                     routeHeaderParamUrl.setHostAddress( mNatTraversalRules.getProxyTransportInfo().getAddress().data() );                     
                     routeHeaderParamUrl.setHostPort( mNatTraversalRules.getProxyTransportInfo().getPort() );                     
                  }
                  else
                  {
                     routeHeaderParamUrl.setHostAddress( mNatTraversalRules.getPublicTransportInfo().getAddress().data() );                     
                     routeHeaderParamUrl.setHostPort( mNatTraversalRules.getPublicTransportInfo().getPort() );                     
                  }
                  routeHeaderParamUrl.setUrlParameter("lr", NULL );
                  UtlString routeHeaderString;
                  routeHeaderParamUrl.toString( routeHeaderString );
//                  referToUrl.setHeaderParameter("X-dummy", routeHeaderString );
                  referToUrl.setHeaderParameter(SIP_ROUTE_FIELD, routeHeaderString );
                  UtlString newReferToField;
                  referToUrl.toString( newReferToField );
                  
                  UtlString toMatch("%3A");
                  ssize_t matchIndex;
                  char replacementString[] = ":";
                  while( (matchIndex = newReferToField.index( toMatch ) ) != UTL_NOT_FOUND )
                  {
                     newReferToField.replace(matchIndex, 3, replacementString );
                  }
                  message.setReferToField( newReferToField.data() );
               }
            }
         }
      }
   }
}

CallTracker* NatTraversalAgent::createCallTrackerAndAddToMap( const UtlString& callId, ssize_t trackerHandle )        
{         
   // Allocate new CallTracker object and its key and insert it into our mCallTrackersMap.
   UtlString*   pMapKey      = new UtlString( callId );
   CallTracker* pCallTracker = new CallTracker( trackerHandle, &mNatTraversalRules, mpMediaRelay, mpRegistrationDB, mpNatMaintainer, mInstanceName );
   if( !pCallTracker ||  !mCallTrackersMap.insertKeyAndValue( pMapKey, pCallTracker ) )
   {
      delete pMapKey;
      delete pCallTracker;
      pCallTracker = 0;
      OsSysLog::add(FAC_NAT, PRI_ERR, "NatTraversalAgent[%s]::createCallTrackerAndAddToMap failed to insert "
                                        "new Call Tracker into map.  key : '%s'",
                                        mInstanceName.data(), callId.data() );
   }
   return pCallTracker;
}         

CallTracker* NatTraversalAgent::getCallTrackerForMessage( const SipMessage& message )
{
   UtlString callId;
   message.getCallIdField( &callId );
   return getCallTrackerFromCallId( callId );
}

CallTracker* NatTraversalAgent::getCallTrackerFromCallId( const UtlString& callId )
{
   CallTracker* pCallTracker = 0;
   pCallTracker = dynamic_cast<CallTracker*>(mCallTrackersMap.findValue( &callId ) );
   return pCallTracker;
}

/// destructor
NatTraversalAgent::~NatTraversalAgent()
{
   OsWriteLock lock( mMessageProcessingMutex );   
   
   mCleanupTimer.stop();
   delete mpNatMaintainer;
   if( mpSipRouter && mbOutputProcessorRegistrrationDone == true )
   {
      mpSipRouter->removeSipOutputProcessor( this );
      mbOutputProcessorRegistrrationDone = false;
   }
   mCallTrackersMap.destroyAll();   
   
   if( mbConnectedToRegistrationDB )
   {
      mpRegistrationDB->releaseInstance();
      mpRegistrationDB = 0;
   }
   delete mpMediaRelay;
}
