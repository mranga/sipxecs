//
// Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.  
// Contributors retain copyright to elements licensed under a Contributor Agreement.
// Licensed to the User under the LGPL license.
// 
//
// $$
////////////////////////////////////////////////////////////////////////
//////


#ifndef _SipTransaction_h_
#define _SipTransaction_h_

// SYSTEM INCLUDES
//#include <...>

// APPLICATION INCLUDES


#include <os/OsDefs.h>
#include <os/OsSocket.h>
#include <os/OsMsgQ.h>
#include <net/BranchId.h>
#include <net/Url.h>
#include <net/SipSrvLookup.h>

// DEFINES
// MACROS
// EXTERNAL FUNCTIONS
// EXTERNAL VARIABLES
// CONSTANTS
// STRUCTS
// TYPEDEFS
// FORWARD DECLARATIONS
class SipMessage;
class SipUserAgent;
class SipTransactionList;
class OsEvent;
class OsTimer;

/** SipTransaction correlates requests and responses.
 * 
 * CallId  + 's' or 'c' (for server or client) is used as
 * the key for the hash (i.e. stored as the string/data in
 * the parent UtlString.
 */
class SipTransaction : public UtlString {
/* //////////////////////////// PUBLIC //////////////////////////////////// */
public:

    /*** States of a transaction.
     * @note
     *   See RFC 3261 for a definition and description
     *   of these transaction states and when the transitions occur.
     * @endnote
     */
    enum transactionStates {
        TRANSACTION_UNKNOWN,           ///< not yet set
        TRANSACTION_LOCALLY_INIITATED, ///< No messages sent (usually client)
        TRANSACTION_CALLING,           ///< Request sent
        TRANSACTION_PROCEEDING,        ///< Provisional response received
        TRANSACTION_COMPLETE,          ///< Final response received
        TRANSACTION_CONFIRMED,         ///< ACK recieved for 300-699 response classes
        TRANSACTION_TERMINATED,        ///< Ready to be garbage collected
        NUM_TRANSACTION_STATES         /**< used for array allocation and limit checks.
                                        * New values must be added before this one, and must also
                                        * be added to the string constants in the stateString
                                        * method. */
    };

    /// The relationship of a message to a transaction
    enum messageRelationship {
        MESSAGE_UNKNOWN,          ///< Relationship not yet determined, or error
        MESSAGE_UNRELATED,        ///< A with different Call-Id, To or From
        MESSAGE_SAME_SESSION,     ///< But not part of this TX or related branches
        MESSAGE_DIFFERENT_BRANCH, ///< Same Call-Id, to, from, cseq but different TX
        MESSAGE_REQUEST,          ///< The request to this TX
        MESSAGE_PROVISIONAL,      ///< A provision response to this TX
        MESSAGE_FINAL,            ///< The first final response to this TX
        MESSAGE_NEW_FINAL,        ///< A different final response for this TX
        MESSAGE_CANCEL,           ///< A cancel for this TX
        MESSAGE_CANCEL_RESPONSE,
        MESSAGE_ACK,              ///< An ACK for this non-2xx TX
        MESSAGE_2XX_ACK,          ///< An ACK assocated with this TX (but considered a different TX)
        MESSAGE_2XX_ACK_PROXY,    ///< An ACK assocated with this TX (but considered a different TX), to be sent to next hop
        MESSAGE_DUPLICATE,        ///< A duplicate message for this TX
        NUM_RELATIONSHIPS         /**< used for array allocation and limit checks.
                                   * New values must be added before this one, and must also
                                   * be added to the string constants in the relationshipString
                                   * method. */
    };

    /// The relative priority of a particular response
    enum ResponsePriority {
        RESP_PRI_CHALLENGE,     ///< Highest priority may need to be challenged
        RESP_PRI_CANCEL,        ///< next lower priority
        RESP_PRI_3XX,           ///< next lower priority
        RESP_PRI_4XX,           ///< next lower priority
        RESP_PRI_404,           ///< next lower priority
        RESP_PRI_5XX,           ///< next lower priority
        RESP_PRI_6XX,           ///< next lower priority
        RESP_PRI_LOWEST,        ///< lowest priority
        RESP_PRI_NOMATCH,       ///< Should only happen if no response exists
        NUM_PRIORITIES          /**< used for array allocation and limit checks.
                                   * New values must be added before this one. */
    };

/* ============================ CREATORS ================================== */

    /// Create a new transaction
    SipTransaction(SipMessage* initialMsg = NULL,     ///< message whose state this tracks
                   UtlBoolean isOutgoing = TRUE,          ///< direction 
                   UtlBoolean userAgentTransaction = TRUE,///< local initiated 
                   BranchId*  parentBranch = NULL         ///< for use in loop detection
                   );
    /**<
     * When this is an out going request, this is a client
     * transaction.  The via header field MUST be added before
     * constructing this transaction as this sets the branch ID.
     */

    virtual
    ~SipTransaction();
    //:Destructor

/* ============================ MANIPULATORS ============================== */

    UtlBoolean handleOutgoing(SipMessage& outgoingMessage,
                             SipUserAgent& userAgent,
                             SipTransactionList& transactionList,
                             enum messageRelationship relationship);

    void handleResendEvent(const SipMessage& outgoingMessage,
                           SipUserAgent& userAgent,
                           enum messageRelationship relationship,
                           SipTransactionList& transactionList,
                           // Time until the next resend should happen (msec) (output)
                           int& nextTimeout,
                           SipMessage*& delayedDispatchedMessage);

    void handleExpiresEvent(const SipMessage& outgoingMessage,
                            SipUserAgent& userAgent,
                            enum messageRelationship relationship,
                            SipTransactionList& transactionList,
                            // Time until the next resend should happen (msec) (output)
                            int& nextTimeout,
                            SipMessage*& delayedDispatchedMessage);

    UtlBoolean handleIncoming(SipMessage& incomingMessage,
                             SipUserAgent& userAgent,
                             enum messageRelationship relationship,
                             SipTransactionList& transactionList,
                             SipMessage*& delayedDispatchedMessage);

    void removeTimer(OsTimer* timer);

    void stopTimers();
    void deleteTimers();

/* ============================ Deprecated ============================== */

    void linkChild(SipTransaction& child);

    void toString(UtlString& dumpString,
                  UtlBoolean dumpMessagesAlso);
    //: Serialize the contents of this

    void justDumpTransactionTree(void);

    void dumpTransactionTree(UtlString& dumpstring, 
                             UtlBoolean dumpMessagesAlso);
    //: Serialize the contents of all the transactions in this tree
    // The parent is found first and then all children are serialized
    // recursively

    void dumpChildren(UtlString& dumpstring, 
                      UtlBoolean dumpMessagesAlso);
    //: Serialize the contents of all the child transactions to this transaction
    // All children are serialized recursively


/* ============================ ACCESSORS ================================= */

    static const char* stateString(enum transactionStates state);

    static const char* relationshipString(enum messageRelationship relationship);
    
    static void buildHash(const SipMessage& message, 
                          UtlBoolean isOutgoing,
                          UtlString& hash);

    SipTransaction* getTopMostParent() const;

    void getCallId(UtlString& callId) const;

    enum transactionStates getState() const;

    long getStartTime() const;

    long getTimeStamp() const;

    void touch();
    void touchBelow(int newDate);

    SipMessage* getRequest();

    SipMessage* getLastProvisionalResponse();

    SipMessage* getLastFinalResponse();

    void cancel(SipUserAgent& userAgent,
                SipTransactionList& transactionList);
    //: cancel any outstanding client transactions (recursively on children)

    void markBusy();

    void markAvailable();

    void notifyWhenAvailable(OsEvent* availableEvent);
    //: The given event is signaled when this transaction is not busy

    void signalNextAvailable();

    void signalAllAvailable();

/* ============================ INQUIRY =================================== */

    UtlBoolean isServerTransaction() const;
    //: Inquire if this transaction is a server as opposed to a client transaction

    //! Inquiry as to whether this transaction is a recursed DNS SRV child
    UtlBoolean isDnsSrvChild() const;

    UtlBoolean isUaTransaction() const;
    //: Inquire if transaction is UA based or proxy
    // Note this is different than server vs client transaction

    UtlBoolean isChildSerial();
    //: Inquire as to whether child transaction will be serial or all parallel searched
    // If all immediate child transactions have the same 
    // Q value FALSE is returned

    UtlBoolean isEarlyDialogWithMedia();
    //: Tests to see if this is an existing early dialog with early media
    // If transaction has not yet been completed and there was early media
    // (determined by the presence of SDP in a provisional response

    UtlBoolean isChildEarlyDialogWithMedia();
    //: Are any of the children in an early dialog with media

    UtlBoolean isMethod(const char* methodToMatch) const;
    //: see if this tranaction is of the given method type

    enum messageRelationship whatRelation(const SipMessage& message,
                                          UtlBoolean isOutgoing) const;
    //: Check if the given message is part of this transaction

    UtlBoolean isBusy();
    //: is this transaction being used (e.g. locked)

    //UtlBoolean isDuplicateMessage(SipMessage& message,
    //                             UtlBoolean checkIfTransactionMatches = TRUE);
    //: Check to see if this request or response has already been received by this transaction

    UtlBoolean isUriChild(Url& uri);
    // Does this URI already exist as an immediate child to this transaction
    // Search through each of the children and see if the child
    // transaction's URI matches.

    UtlBoolean isUriRecursed(Url& uri);
    // Has this URI been recursed anywhere in this transaction tree already
    // Start looking at the parent

    UtlBoolean isUriRecursedChildren(UtlString& uriString);
    // Has this URI been recursed anywhere at or below in this transaction tree already
    // Look at or below the current transaction in the transaction tree

/* //////////////////////////// PROTECTED ///////////////////////////////// */
protected:
    void handleChildTimeoutEvent(SipTransaction& child,
                                 const SipMessage& outgoingMessage,
                                 SipUserAgent& userAgent,
                                 enum messageRelationship relationship,
                                 SipTransactionList& transactionList,
                                 // Time until the next resend should happen (msec) (output)
                                 int& nextTimeout,
                                 SipMessage*& delayedDispatchedMessage);
    //: tells the parent transaction the result of the timeout event

    UtlBoolean handleChildIncoming(//SipTransaction& child,
                                  SipMessage& incomingMessage,
                                  SipUserAgent& userAgent,
                                  enum messageRelationship relationship,
                                  SipTransactionList& transactionList,
                                  UtlBoolean childSaysShouldDispatch,
                                  SipMessage*& delayedDispatchedMessage);
    //: Tells the parent transaction the result of the incoming message
    //! returns: TRUE/FALSE as to whether the message should be dispatched to applications

    UtlBoolean startSequentialSearch(SipUserAgent& userAgent,
                                    SipTransactionList& transactionList);
    //: Checks to see if a final response can be sent or if sequential search should be started

    UtlBoolean recurseChildren(SipUserAgent& userAgent,
                              SipTransactionList& transactionList);
    //: Starts search on any immediate children of the highest unpursued Q value

    UtlBoolean recurseDnsSrvChildren(SipUserAgent& userAgent,
                              SipTransactionList& transactionList);
    //: Starts search on any immediate DNS SRV children of the highest unpursued Q value

    /// Copy unique realms from any proxy challenges in the response into realmList
    void getChallengeRealms(const SipMessage& response, UtlSList& realmList);
    ///< This is for use only within findBestResponse, to filter duplicate realms

    UtlBoolean findBestResponse(SipMessage& bestResponse);
    // Finds the best final response to return the the server transaction

    UtlBoolean findBestChildResponse(SipMessage& bestResponse, int responseFoundCount);

    enum ResponsePriority findRespPriority(int responseCode);


    enum messageRelationship addResponse(SipMessage*& response,
                                         UtlBoolean isOutGoing,
                                         enum messageRelationship relationship = MESSAGE_UNKNOWN);
    //: Adds the provisional or final response to the transaction

    void cancelChildren(SipUserAgent& userAgent,
                        SipTransactionList& transactionList);
    //: Cancels children transactions on a server transaction

    void doMarkBusy(int markValue);

    OsSocket::IpProtocolSocketType getPreferredProtocol();
    //: Determine best protocol, based on message size

/* //////////////////////////// PRIVATE /////////////////////////////////// */
private:
    SipTransaction(const SipTransaction& rSipTransaction);
    //:Copy constructor (disabled)
    SipTransaction& operator=(const SipTransaction& rhs);
    //:Assignment operator (disabled)

    // Resend a message when the resent timer has expired.  Also schedules
    // the next resend.
    UtlBoolean doResend(SipMessage& resendMessage,
                        SipUserAgent& userAgent,
                        // Time until the next resend should happen (msec) (output)
                        int& nextTimeoutMs);

    // Do the first transmission of a message, including for requests,
    // scheduling the timeout that triggers the first send.
    UtlBoolean doFirstSend(SipMessage& message,
                          enum messageRelationship relationship,
                          SipUserAgent& userAgent,
                          UtlString& toAddress,
                          int& port,
                          OsSocket::IpProtocolSocketType& toProtocol);

    void prepareRequestForSend(SipMessage& request,
                               SipUserAgent& userAgent,
                               UtlBoolean& addressRequiresDnsSrvLookup,
                               UtlString& toAddress,
                               int& port,
                               OsSocket::IpProtocolSocketType& toProtocol);

    // CallId  + 's' or 'c' (for server or client) is used as
    // the key for the hash (i.e. stored as the string/data in
    // the parent UtlString
    UtlString mCallId;
    BranchId* mpBranchId;
    UtlString mRequestUri;
    Url mFromField;
    Url mToField;
    UtlString mRequestMethod;
    int mCseq;
    UtlBoolean mIsServerTransaction; ///< TRUE = server, FALSE = client
    UtlBoolean mIsUaTransaction;     ///< UA or proxy transaction

    // Address and transport that have been established for this transaction.
    UtlString mSendToAddress;
    int mSendToPort;
    OsSocket::IpProtocolSocketType mSendToProtocol;

    server_t* mpDnsDestinations;        ///< list obtained from DNS server, can contain 0 valid destinations
    SipMessage* mpRequest;
    SipMessage* mpLastProvisionalResponse;
    SipMessage* mpLastFinalResponse;
    SipMessage* mpAck;
    SipMessage* mpCancel;
    SipMessage* mpCancelResponse;
    SipTransaction* mpParentTransaction;
    UtlSList mChildTransactions;
    long mTransactionCreateTime;         ///< When this thing was created
    long mTransactionStartTime;          /**<  When the request was sent/received
                                          * i.e. went to TRANSACTION_CALLING state */
    long mTimeStamp;                     ///< When this was last used
    enum transactionStates mTransactionState;
    UtlBoolean mDispatchedFinalResponse; ///< For UA recursion
    UtlBoolean mProvisionalSdp;          ///< early media
    UtlSList mTimers;                    /**< A list of all outstanding timers
                                          *   started by this transaction. */

    // Recursion members
    UtlBoolean mIsCanceled;
    UtlBoolean mIsRecursing;   ///< TRUE if any braches have not be pursued
    UtlBoolean mIsDnsSrvChild; ///< This CT pursues one of the SRV records of the parent CT
    double mQvalue;            ///< Recurse order.  equal values are recursed in parallel
    int mExpires;              ///< Maximum time (seconds) to wait for a final outcome
    UtlBoolean mIsBusy;
    UtlString mBusyTaskName;
    UtlSList* mWaitingList;    /**< Events waiting until this is available
                                * Note only a parent tx should have a waiting list */

};

/* ============================ INLINE METHODS ============================ */

#endif // _SipTransaction_h_
