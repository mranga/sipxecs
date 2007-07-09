// 
// 
// Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.  
// Contributors retain copyright to elements licensed under a Contributor Agreement.
// Licensed to the User under the LGPL license.
// 
//////////////////////////////////////////////////////////////////////////////

#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/TestCase.h>
#include <sipxunit/TestUtilities.h>
#include <utl/UtlHashMap.h>

#include <os/OsDefs.h>
#include <os/OsDateTime.h>
#include <net/SipDialog.h>
#include <net/SipMessage.h>
#include <net/SipDialogMgr.h>
#include <net/SipSubscriptionMgr.h>
#include <net/SipSubscribeServerEventHandler.h>
#include <net/SipUserAgent.h>
#include <net/SipSubscribeServer.h>
#include <net/SipPublishContentMgr.h>

#define UNIT_TEST_SIP_PORT 44444

/**
 * Unittest for SipSubscriptionMgr
 */
class SipSubscribeServerTest : public CppUnit::TestCase
{
      CPPUNIT_TEST_SUITE(SipSubscribeServerTest);
      CPPUNIT_TEST(subscriptionTest);
      CPPUNIT_TEST_SUITE_END();

      public:

   // Service routine to listen for messages.
   void runListener(OsMsgQ& msgQueue, //< OsMsgQ to listen on
                    SipUserAgent& userAgent, //< SipUserAgent to send responses
                    OsTime timeout, //< Length of time before timing out.
                    const SipMessage*& request, //< Pointer to any request that was received
                    const SipMessage*& response, //< Pointer to any response that was received
                    int responseCode //< Response code to give to the request
      )
   {
      // Initialize the request and response pointers.
      request = NULL;
      response = NULL;

      // Because the SUBSCRIBE response and NOTIFY request can come in either order,
      // we have to read messages until no more arrive.
      OsMsg* message;
      while (msgQueue.receive(message, timeout) == OS_SUCCESS)
      {
         int msgType = message->getMsgType();
         int msgSubType = message->getMsgSubType();
         CPPUNIT_ASSERT(msgType == OsMsg::PHONE_APP);
         CPPUNIT_ASSERT(msgSubType == SipMessage::NET_SIP_MESSAGE);
         const SipMessage* sipMessage = ((SipMessageEvent*) message)->getMessage();
         CPPUNIT_ASSERT(sipMessage);
         int messageType = ((SipMessageEvent*) message)->getMessageStatus();
         CPPUNIT_ASSERT(messageType == SipMessageEvent::APPLICATION);

         if (sipMessage->isResponse())
         {
            // Check that we get only one response.
            CPPUNIT_ASSERT(response == NULL);
            response = sipMessage;
         }
         else
         {
            // Check that we get only one request.
            CPPUNIT_ASSERT(request == NULL);
            request = sipMessage;
            // Immediately generate a response to the request
            SipMessage requestResponse;
            requestResponse.setResponseData(request,
                                            responseCode,
                                            // Provide dummy response text
                                            "dummy");
            CPPUNIT_ASSERT(userAgent.send(requestResponse));
         }
      }
   }

   void subscriptionTest()
   {
      UtlString hostIp = "127.0.0.1";
      // OsSocket::getHostIp(&hostIp);

      // The resource address to use.
      UtlString aor;
      {
         char buffer[100];
         sprintf(buffer, "sip:111@%s:%d", hostIp.data(), UNIT_TEST_SIP_PORT);
         aor = buffer;
      }

      // Test MWI messages
      const char* mwiSubscribe =
         "SUBSCRIBE sip:111@localhost SIP/2.0\r\n"
         "From: \"Dan Petrie\"<sip:111@example.com>;tag=1612c1612\r\n"
         "To: \"Dan Petrie\"<sip:111@example.com>\r\n"
         "Call-Id: e2aab34a72a0eb18300fbec445d5d665\r\n"
         "Cseq: 1 SUBSCRIBE\r\n"
         "Event: message-summary\r\n"
         "Accept: application/simple-message-summary\r\n"
         "Expires: 3600\r\n"
         "Date: Tue, 26 Apr 2005 14:59:30 GMT\r\n"
         "Max-Forwards: 20\r\n"
         "User-Agent: Pingtel/2.2.0 (VxWorks)\r\n"
         "Accept-Language: en\r\n"
         "Supported: sip-cc, sip-cc-01, timer, replaces\r\n"
         "Content-Length: 0\r\n"
         "\r\n";

      const char* mwiStateString =
         "Messages-Waiting: no\r\n"
         "Voice-Message: 0/0 (0/0)\r\n";

      UtlString eventName("message-summary");
      UtlString mwiMimeType("application/simple-message-summary");
      SipUserAgent userAgent(UNIT_TEST_SIP_PORT, UNIT_TEST_SIP_PORT, PORT_NONE,
                             NULL, NULL, hostIp);
      userAgent.start();

      SipSubscribeServer* subServer = 
         SipSubscribeServer::buildBasicServer(userAgent, 
                                              eventName);
      subServer->start();

      // Get pointers to the Subscription Manager and Dialog Manager.
      SipSubscriptionMgr* subMgr = subServer->getSubscriptionMgr(eventName);
      CPPUNIT_ASSERT(subMgr);
      SipDialogMgr* dialogMgr = subMgr->getDialogMgr();
      CPPUNIT_ASSERT(dialogMgr);

      // Create a crude Subscription client
      OsMsgQ incomingClientMsgQueue;
      // Register an interest in SUBSCRIBE responses and NOTIFY requests
      // for this event type
      userAgent.addMessageObserver(incomingClientMsgQueue,
                                   SIP_SUBSCRIBE_METHOD,
                                   FALSE, // no requests
                                   TRUE, // reponses
                                   TRUE, // incoming
                                   FALSE, // no outgoing
                                   eventName,
                                   NULL,
                                   NULL);
      userAgent.addMessageObserver(incomingClientMsgQueue,
                                   SIP_NOTIFY_METHOD,
                                   TRUE, // requests
                                   FALSE, // not reponses
                                   TRUE, // incoming
                                   FALSE, // no outgoing
                                   eventName,
                                   NULL,
                                   NULL);

      // Validate that authentication and authorization are
      // disabled by default.
      {
         SipSubscribeServerEventHandler* eventHandler = 
            subServer->getEventHandler(eventName);
         CPPUNIT_ASSERT(eventHandler);

         SipMessage bogusSubscribeRequest;
         SipMessage bogusSubscribeResponse;
         CPPUNIT_ASSERT(eventHandler->isAuthenticated(bogusSubscribeRequest,
                                                      "foo@bar.com",
                                                      eventName,
                                                      bogusSubscribeResponse));
         CPPUNIT_ASSERT(eventHandler->isAuthorized(bogusSubscribeRequest,
                                                   "foo@bar.com",
                                                   eventName,
                                                   bogusSubscribeResponse));
      }

      // Send a SUBSCRIBE to ourselves
      SipMessage mwiSubscribeRequest(mwiSubscribe);
      mwiSubscribeRequest.setSipRequestFirstHeaderLine(SIP_SUBSCRIBE_METHOD, 
                                                       aor, 
                                                       SIP_PROTOCOL_VERSION);
      mwiSubscribeRequest.setContactField(aor);

      CPPUNIT_ASSERT(userAgent.send(mwiSubscribeRequest));

      // We should get a 202 response and a NOTIFY request in the queue
      OsTime messageTimeout(1, 0);  // 1 second
      const SipMessage* subscribeResponse;
      const SipMessage* notifyRequest;
      runListener(incomingClientMsgQueue, userAgent, messageTimeout,
                  notifyRequest, subscribeResponse, SIP_OK_CODE);

      // We should have received a SUBSCRIBE response and a NOTIFY request.
      CPPUNIT_ASSERT(subscribeResponse);
      CPPUNIT_ASSERT(notifyRequest);

      // Check that the CSeq method in the subscribe response is OK.
      {
         UtlString subscribeMethod;
         subscribeResponse->getCSeqField(NULL, &subscribeMethod);
         ASSERT_STR_EQUAL(SIP_SUBSCRIBE_METHOD, subscribeMethod.data());
      }

      // Check that the method in the notify request is OK.
      {
         UtlString notifyMethod;
         notifyRequest->getRequestMethod(&notifyMethod);
         ASSERT_STR_EQUAL(SIP_NOTIFY_METHOD, notifyMethod.data());
      }

      // Check that the Event header in the NOTIFY is the same as the
      // one in the SUBSCRIBE.
      {
         UtlString notifyEventHeader;
         UtlString subscribeEventHeader;
         notifyRequest->getEventField(notifyEventHeader);
         mwiSubscribeRequest.getEventField(subscribeEventHeader);
         ASSERT_STR_EQUAL(subscribeEventHeader, notifyEventHeader);
      }

      // The NOTIFY should have no body because none has been published yet.
      {
         const HttpBody* bodyPtr = notifyRequest->getBody();
         CPPUNIT_ASSERT(bodyPtr == NULL);
      }

      // Publish some content (mwiStateString) for this resourceID
      {
         HttpBody* newMwiBodyPtr = new HttpBody(mwiStateString, 
                                                strlen(mwiStateString), 
                                                mwiMimeType);
         const int version = 0;
         SipPublishContentMgr* publishMgr = subServer->getPublishMgr(eventName);
         CPPUNIT_ASSERT(publishMgr);
         publishMgr->publish(aor, 
                             eventName, 
                             eventName, 
                             1, 
                             &newMwiBodyPtr,
                             &version);
      }

      // Should get a NOTIFY queued up
      const SipMessage* secondNotify;
      runListener(incomingClientMsgQueue, userAgent, messageTimeout,
                  secondNotify, subscribeResponse, SIP_OK_CODE);
      CPPUNIT_ASSERT(secondNotify);
      CPPUNIT_ASSERT(subscribeResponse == NULL);

      // Check that the body of the NOTIFY is what we expect (mwiStateString).
      const HttpBody* secondNotifyBody = secondNotify->getBody();
      CPPUNIT_ASSERT(secondNotifyBody);
      int notifyBodySize;
      const char* notifyBodyBytes;
      secondNotifyBody->getBytes(&notifyBodyBytes, &notifyBodySize);
      CPPUNIT_ASSERT(notifyBodyBytes);
      ASSERT_STR_EQUAL(mwiStateString, notifyBodyBytes);

      // Check that the Dialog Manager reports that the dialog handle is OK.
      UtlString secondNotifyDialogHandle;
      secondNotify->getDialogHandle(secondNotifyDialogHandle);
      CPPUNIT_ASSERT(!secondNotifyDialogHandle.isNull());
      CPPUNIT_ASSERT(dialogMgr->dialogExists(secondNotifyDialogHandle));
      CPPUNIT_ASSERT(dialogMgr->countDialogs() == 1);

      // Create a new one-time SUBSCRIBE
      SipMessage oneTimeMwiSubscribeRequest(mwiSubscribe);
      oneTimeMwiSubscribeRequest.setCallIdField("1234567890");
      oneTimeMwiSubscribeRequest.setExpiresField(0);
      oneTimeMwiSubscribeRequest.setSipRequestFirstHeaderLine(SIP_SUBSCRIBE_METHOD, 
                                                              aor, 
                                                              SIP_PROTOCOL_VERSION);
      oneTimeMwiSubscribeRequest.setContactField(aor);

      CPPUNIT_ASSERT(userAgent.send(oneTimeMwiSubscribeRequest));

      const SipMessage* oneTimeNotifyRequest;
      const SipMessage* oneTimeSubscribeResponse;
      runListener(incomingClientMsgQueue, userAgent, messageTimeout,
                  oneTimeNotifyRequest, oneTimeSubscribeResponse, SIP_OK_CODE);

#if 0

      // Get the subscribe response or notify request
      incomingClientMsgQueue.receive(osMessage, messageTimeout);
      msgType = osMessage->getMsgType();
      msgSubType = osMessage->getMsgSubType();
      CPPUNIT_ASSERT(msgType == OsMsg::PHONE_APP);
      CPPUNIT_ASSERT(msgSubType == SipMessage::NET_SIP_MESSAGE);
      const SipMessage* sipMessage = ((SipMessageEvent*)osMessage)->getMessage();
      messageType = ((SipMessageEvent*)osMessage)->getMessageStatus();
      CPPUNIT_ASSERT(sipMessage);
      CPPUNIT_ASSERT(messageType == SipMessageEvent::APPLICATION);
      // SUBSCRIBE response and NOTIFY request can come in either order
      if(sipMessage->isResponse())
      {
         oneTimeSubscribeResponse = sipMessage;
      }
      else
      {
         oneTimeNotifyRequest = sipMessage;
         // Immediately generate a NOTIFY response
         SipMessage notifyResponse;
         notifyResponse.setResponseData(oneTimeNotifyRequest, 
                                        SIP_OK_CODE,
                                        SIP_OK_TEXT);
         userAgent.send(notifyResponse);
      }

      // Get the subscribe response or notify request
      incomingClientMsgQueue.receive(osMessage, messageTimeout);
      msgType = osMessage->getMsgType();
      msgSubType = osMessage->getMsgSubType();
      CPPUNIT_ASSERT(msgType == OsMsg::PHONE_APP);
      CPPUNIT_ASSERT(msgSubType == SipMessage::NET_SIP_MESSAGE);
      sipMessage = ((SipMessageEvent*)osMessage)->getMessage();
      messageType = ((SipMessageEvent*)osMessage)->getMessageStatus();
      CPPUNIT_ASSERT(sipMessage);
      CPPUNIT_ASSERT(messageType == SipMessageEvent::APPLICATION);
      // SUBSCRIBE response and NOTIFY request can come in either order
      if(sipMessage->isResponse())
      {
         oneTimeSubscribeResponse = sipMessage;
      }
      else
      {
         oneTimeNotifyRequest = sipMessage;
         // Immediately generate a NOTIFY response
         SipMessage notifyResponse;
         notifyResponse.setResponseData(oneTimeNotifyRequest, 
                                        SIP_OK_CODE,
                                        SIP_OK_TEXT);
         userAgent.send(notifyResponse);
      }

      // Validate the one time subscribe response and notify request
      CPPUNIT_ASSERT(oneTimeSubscribeResponse);
      CPPUNIT_ASSERT(oneTimeNotifyRequest);

      UtlString oneTimeSubscribeMethod;
      UtlString oneTimeNotifyMethod;
      oneTimeNotifyRequest->getRequestMethod(&oneTimeNotifyMethod);
      oneTimeSubscribeResponse->getCSeqField(NULL, &oneTimeSubscribeMethod);
      ASSERT_STR_EQUAL(SIP_SUBSCRIBE_METHOD, oneTimeSubscribeMethod.data());
      ASSERT_STR_EQUAL(SIP_NOTIFY_METHOD, oneTimeNotifyMethod.data());
      UtlString oneTimeNotifyEventHeader;
      UtlString oneTimeSubscribeEventHeader;
      oneTimeNotifyRequest->getEventField(oneTimeNotifyEventHeader);
      oneTimeSubscribeResponse->getEventField(oneTimeSubscribeEventHeader);
      ASSERT_STR_EQUAL(oneTimeSubscribeEventHeader, oneTimeNotifyEventHeader);

      const HttpBody* oneTimeBodyPtr = NULL;
      oneTimeBodyPtr = oneTimeNotifyRequest->getBody();
      CPPUNIT_ASSERT(oneTimeBodyPtr != NULL);
      const char* oneTimeBodyString = NULL;
      int oneTimeNotifyBodySize;
      oneTimeBodyPtr->getBytes(&oneTimeBodyString, &oneTimeNotifyBodySize);
      ASSERT_STR_EQUAL(mwiStateString, oneTimeBodyString);
      UtlString oneTimeNotifyDialogHandle;
      secondNotify->getDialogHandle(oneTimeNotifyDialogHandle);
      CPPUNIT_ASSERT(!oneTimeNotifyDialogHandle.isNull());
      CPPUNIT_ASSERT(dialogMgr->dialogExists(oneTimeNotifyDialogHandle));
      CPPUNIT_ASSERT(dialogMgr->countDialogs() == 2);
      long now = OsDateTime::getSecsSinceEpoch();
      UtlString dialogDump;
      dialogMgr->toString(dialogDump);
      //ASSERT_STR_EQUAL(dialogDump.isNull() ? "" : dialogDump.data(), "");
      subMgr->removeOldSubscriptions(now + 1);
      // The one time subscription should get garbage collected
      // leaving only the persistant 3600 second subscription
      CPPUNIT_ASSERT(dialogMgr->countDialogs() == 1);

      // Cleanup to prevent use of the queue after it goes out of
      // scope
      userAgent.removeMessageObserver(incomingClientMsgQueue);
      userAgent.removeMessageObserver(incomingClientMsgQueue);

      userAgent.shutdown(TRUE);
       
      delete subServer;
      subServer = NULL;
#endif // 0
   }

};

CPPUNIT_TEST_SUITE_REGISTRATION(SipSubscribeServerTest);
