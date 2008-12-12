// 
// 
// Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.  
// Contributors retain copyright to elements licensed under a Contributor Agreement.
// Licensed to the User under the LGPL license.
// 
// $$
////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES
#include <signal.h>

// APPLICATION INCLUDES
#include <os/OsConfigDb.h>
#include <os/OsSysLog.h>
#include <os/OsFS.h>
#include <os/OsSocket.h>
#include <xmlparser/tinystr.h>
#include "xmlparser/ExtractContent.h"
#include <utl/UtlString.h>
#include <utl/XmlContent.h>
#include <net/NameValueTokenizer.h>
#include <net/SipDialogEvent.h>
#include <os/OsSysLog.h>
#include "ResourceListServer.h"
#include "main.h"
#include <net/SipLine.h>
#include <net/SipLineMgr.h>
#include <sipdb/CredentialDB.h>
#include <net/HttpMessage.h>

// DEFINES

#ifndef SIPX_VERSION
#  include "sipxpbx-buildstamp.h"
#  define SIPXCHANGE_VERSION          SipXpbxVersion
#  define SIPXCHANGE_VERSION_COMMENT  SipXpbxBuildStamp
#else
#  define SIPXCHANGE_VERSION          SIPX_VERSION
#  define SIPXCHANGE_VERSION_COMMENT  ""
#endif

// SIPXRLS User ID
#define RLSSERVER_ID_TOKEN            "~~id~sipXrls"
#define CONFIG_SETTINGS_FILE          "sipxrls-config"
#define CONFIG_ETC_DIR                SIPX_CONFDIR

#define CONFIG_LOG_FILE               "sipxrls.log"
#define CONFIG_LOG_DIR                SIPX_LOGDIR

#define CONFIG_SETTING_LOG_DIR        "SIP_RLS_LOG_DIR"
#define CONFIG_SETTING_LOG_LEVEL      "SIP_RLS_LOG_LEVEL"
#define CONFIG_SETTING_LOG_CONSOLE    "SIP_RLS_LOG_CONSOLE"
#define CONFIG_SETTING_UDP_PORT       "SIP_RLS_UDP_PORT"
#define CONFIG_SETTING_TCP_PORT       "SIP_RLS_TCP_PORT"
#define CONFIG_SETTING_BIND_IP        "SIP_RLS_BIND_IP"
#define CONFIG_SETTING_RLS_FILE       "SIP_RLS_FILE_NAME"
#define CONFIG_SETTING_DOMAIN_NAME    "SIP_RLS_DOMAIN_NAME"
#define CONFIG_SETTING_AUTHENTICATE_REALM "SIP_RLS_AUTHENTICATE_REALM"
#define CONFIG_SETTING_STARTUP_WAIT   "SIP_RLS_STARTUP_WAIT"
#define CONFIG_SETTING_REFRESH_INTERVAL "SIP_RLS_REFRESH_INTERVAL"
#define CONFIG_SETTING_RESUBSCRIBE_INTERVAL "SIP_RLS_RESUBSCRIBE_INTERVAL"
#define CONFIG_SETTING_MIN_RESUBSCRIBE_INTERVAL "SIP_RLS_MIN_RESUBSCRIBE_INTERVAL"

#define LOG_FACILITY                  FAC_RLS

#define RLS_DEFAULT_UDP_PORT          5130       // Default UDP port
#define RLS_DEFAULT_TCP_PORT          5130       // Default TCP port
#define RLS_DEFAULT_BIND_IP           "0.0.0.0"  // Default bind ip; all interfaces
#define RLS_DEFAULT_STARTUP_WAIT      (2 * 60)   // Default wait upon startup (in sec.)
#define RLS_DEFAULT_REFRESH_INTERVAL  (24 * 60 * 60) // Default subscription refresh interval.
#define RLS_DEFAULT_RESUBSCRIBE_INTERVAL (60 * 60) // Default subscription resubscribe interval.
#define RLS_DEFAULT_MIN_RESUBSCRIBE_INTERVAL (40 * 60) // Default minimum subscription resubscribe interval.

// MACROS
// EXTERNAL FUNCTIONS
// EXTERNAL VARIABLES
// CONSTANTS
// STRUCTS
// TYPEDEFS
// FUNCTIONS
// FORWARD DECLARATIONS
// EXTERNAL VARIABLES
// CONSTANTS
// STATIC VARIABLE INITIALIZATIONS
// GLOBAL VARIABLE INITIALIZATIONS

UtlBoolean    gShutdownFlag = FALSE;


/* ============================ FUNCTIONS ================================= */

class SignalTask : public OsTask
{
public:
   SignalTask() : OsTask("SignalTask") {}

   int
   run(void *pArg)
   {
       int sig_num ;
       OsStatus res ;

       // Wait for a signal.  This will unblock signals
       // for THIS thread only, so this will be the only thread
       // to catch an async signal directed to the process 
       // from the outside.
       res = awaitSignal(sig_num);
       if (res == OS_SUCCESS)
       {
          if (SIGTERM == sig_num)
          {
             OsSysLog::add( LOG_FACILITY, PRI_INFO, "SignalTask: terminate signal received.");
          }
          else
          {
            OsSysLog::add( LOG_FACILITY, PRI_CRIT, "SignalTask: caught signal: %d", sig_num );
          }
       }
       else
       {
            OsSysLog::add( LOG_FACILITY, PRI_CRIT, "SignalTask: awaitSignal() failed");
       }
       // set the global shutdown flag
       gShutdownFlag = TRUE ;
       return 0 ;
   }
} ;

// Initialize the OsSysLog
void initSysLog(OsConfigDb* pConfig)
{
   UtlString logLevel;               // Controls Log Verbosity
   UtlString consoleLogging;         // Enable console logging by default?
   UtlString fileTarget;             // Path to store log file.
   UtlBoolean bSpecifiedDirError ;   // Set if the specified log dir does not
                                    // exist
   struct tagPrioriotyLookupTable
   {
      const char*      pIdentity;
      OsSysLogPriority ePriority;
   };

   struct tagPrioriotyLookupTable lkupTable[] =
   {
      { "DEBUG",   PRI_DEBUG},
      { "INFO",    PRI_INFO},
      { "NOTICE",  PRI_NOTICE},
      { "WARNING", PRI_WARNING},
      { "ERR",     PRI_ERR},
      { "CRIT",    PRI_CRIT},
      { "ALERT",   PRI_ALERT},
      { "EMERG",   PRI_EMERG},
   };

   OsSysLog::initialize(0, "sipxrls");


   //
   // Get/Apply Log Filename
   //
   fileTarget.remove(0);
   if ((pConfig->get(CONFIG_SETTING_LOG_DIR, fileTarget) != OS_SUCCESS) ||
      fileTarget.isNull() || !OsFileSystem::exists(fileTarget))
   {
      bSpecifiedDirError = !fileTarget.isNull();

      // If the log file directory exists use that, otherwise place the log
      // in the current directory
      OsPath workingDirectory;
      if (OsFileSystem::exists(CONFIG_LOG_DIR))
      {
         fileTarget = CONFIG_LOG_DIR;
         OsPath path(fileTarget);
         path.getNativePath(workingDirectory);

         osPrintf("%s : %s\n", CONFIG_SETTING_LOG_DIR, workingDirectory.data());
         OsSysLog::add(LOG_FACILITY, PRI_INFO, "%s : %s",
                       CONFIG_SETTING_LOG_DIR, workingDirectory.data());
      }
      else
      {
         OsPath path;
         OsFileSystem::getWorkingDirectory(path);
         path.getNativePath(workingDirectory);

         osPrintf("%s : %s\n", CONFIG_SETTING_LOG_DIR, workingDirectory.data());
         OsSysLog::add(LOG_FACILITY, PRI_INFO, "%s : %s",
                       CONFIG_SETTING_LOG_DIR, workingDirectory.data());
      }

      fileTarget = workingDirectory +
         OsPathBase::separator +
         CONFIG_LOG_FILE;
   }
   else
   {
      bSpecifiedDirError = false;
      osPrintf("%s : %s\n", CONFIG_SETTING_LOG_DIR, fileTarget.data());
      OsSysLog::add(LOG_FACILITY, PRI_INFO, "%s : %s",
                    CONFIG_SETTING_LOG_DIR, fileTarget.data());

      fileTarget = fileTarget +
         OsPathBase::separator +
         CONFIG_LOG_FILE;
   }
   OsSysLog::setOutputFile(0, fileTarget);


   //
   // Get/Apply Log Level
   //
   if ((pConfig->get(CONFIG_SETTING_LOG_LEVEL, logLevel) != OS_SUCCESS) ||
         logLevel.isNull())
   {
      logLevel = "ERR";
   }
   logLevel.toUpper();
   OsSysLogPriority priority = PRI_ERR;
   int iEntries = sizeof(lkupTable) / sizeof(struct tagPrioriotyLookupTable);
   for (int i = 0; i < iEntries; i++)
   {
      if (logLevel == lkupTable[i].pIdentity)
      {
         priority = lkupTable[i].ePriority;
         osPrintf("%s : %s\n", CONFIG_SETTING_LOG_LEVEL,
                  lkupTable[i].pIdentity);
         OsSysLog::add(LOG_FACILITY, PRI_INFO, "%s : %s",
                       CONFIG_SETTING_LOG_LEVEL, lkupTable[i].pIdentity);
         break;
      }
   }
   OsSysLog::setLoggingPriority(priority);
   OsSysLog::setLoggingPriorityForFacility(FAC_SIP_INCOMING_PARSED, PRI_ERR);

   //
   // Get/Apply console logging
   //
   UtlBoolean bConsoleLoggingEnabled = false;
   if ((pConfig->get(CONFIG_SETTING_LOG_CONSOLE, consoleLogging) == OS_SUCCESS))
   {
      consoleLogging.toUpper();
      if (consoleLogging == "ENABLE")
      {
         OsSysLog::enableConsoleOutput(true);
         bConsoleLoggingEnabled = true;
      }
   }

   osPrintf("%s : %s\n", CONFIG_SETTING_LOG_CONSOLE,
            bConsoleLoggingEnabled ? "ENABLE" : "DISABLE") ;
   OsSysLog::add(LOG_FACILITY, PRI_INFO, "%s : %s", CONFIG_SETTING_LOG_CONSOLE,
                 bConsoleLoggingEnabled ? "ENABLE" : "DISABLE") ;

   if (bSpecifiedDirError)
   {
      OsSysLog::add(LOG_FACILITY, PRI_CRIT,
                    "Cannot access %s directory; please check configuration.",
                    CONFIG_SETTING_LOG_DIR);
   }
}

// Get and add the credentials for sipXrls
SipLineMgr* addCredentials (UtlString domain, UtlString realm)
{
   SipLine* line = NULL;
   SipLineMgr* lineMgr = NULL;
   UtlString user;

   CredentialDB* credentialDb;
   if ((credentialDb = CredentialDB::getInstance()))
   {
      Url identity;

      identity.setUserId(RLSSERVER_ID_TOKEN);
      identity.setHostAddress(domain);
      UtlString ha1_authenticator;
      UtlString authtype;
      bool bSuccess = false;
      
      if (credentialDb->getCredential(identity, realm, user, ha1_authenticator, authtype))
      {
         if ((line = new SipLine( identity // user entered url
                                 ,identity // identity url
                                 ,user     // user
                                 ,TRUE     // visible
                                 ,SipLine::LINE_STATE_PROVISIONED
                                 ,TRUE     // auto enable
                                 ,FALSE    // use call handling
                                 )))
         {
            if ((lineMgr = new SipLineMgr()))
            {
               if (lineMgr->addLine(*line))
               {
                  if (lineMgr->addCredentialForLine( identity, realm, user, ha1_authenticator
                                                    ,HTTP_DIGEST_AUTHENTICATION
                                                    )
                      )
                  {
                     lineMgr->setDefaultOutboundLine(identity);
                     bSuccess = true;

                     OsSysLog::add(LOG_FACILITY, PRI_INFO,
                                   "Added identity '%s': user='%s' realm='%s'"
                                   ,identity.toString().data(), user.data(), realm.data()
                                   );
                  }
                  else
                  {
                     OsSysLog::add(LOG_FACILITY, PRI_CRIT,
                                   "Error adding identity '%s': user='%s' realm='%s'\n",
                                   identity.toString().data(), user.data(), realm.data()
                                   );
                  }
               }
               else
               {
                  OsSysLog::add(LOG_FACILITY, PRI_CRIT, "addLine failed" );
               }
            }
            else
            {
               OsSysLog::add(LOG_FACILITY, PRI_CRIT,
                             "Constructing SipLineMgr failed" );
            }
         }
         else
         {
            OsSysLog::add(LOG_FACILITY, PRI_CRIT,
                          "Constructing SipLine failed" );
         }
      }
      else
      {
         OsSysLog::add(LOG_FACILITY, PRI_CRIT,
                       "No credential found for '%s' in realm '%s'"
                       ,identity.toString().data(), domain.data(), realm.data()
                       );
      }
      
      if( !bSuccess )
      {
         delete line;
         line = NULL;
         
         delete lineMgr;
         lineMgr = NULL;         
      }
   }

   credentialDb->releaseInstance();

   return lineMgr;
}

//
// The main entry point to sipXrls.
//
int main(int argc, char* argv[])
{

   // Block all signals in this the main thread.
   // Any threads created from now on will have all signals masked.
   OsTask::blockSignals();

   // Create a new task to wait for signals.  Only that task
   // will ever see a signal from the outside.
   SignalTask* signalTask = new SignalTask();
   signalTask->start();

   // Configuration Database (used for OsSysLog)
   OsConfigDb configDb;

   UtlString argString;
   for (int argIndex = 1; argIndex < argc; argIndex++)
   {
      osPrintf("arg[%d]: %s\n", argIndex, argv[argIndex]);
      argString = argv[argIndex];
      NameValueTokenizer::frontBackTrim(&argString, "\t ");
      if (argString.compareTo("-v") == 0)
      {
         osPrintf("Version: %s (%s)\n", SIPXCHANGE_VERSION,
                  SIPXCHANGE_VERSION_COMMENT);
         return 1;
      }
      else
      {
         osPrintf("usage: %s [-v]\nwhere:\n -v provides the software version\n",
                  argv[0]);
         return 1;
      }
   }

   // Load configuration file.
   OsPath workingDirectory;
   if (OsFileSystem::exists(CONFIG_ETC_DIR))
   {
      workingDirectory = CONFIG_ETC_DIR;
      OsPath path(workingDirectory);
      path.getNativePath(workingDirectory);
   }
   else
   {
      OsPath path;
      OsFileSystem::getWorkingDirectory(path);
      path.getNativePath(workingDirectory);
   }

   UtlString fileName =  workingDirectory +
      OsPathBase::separator +
      CONFIG_SETTINGS_FILE;

   if (configDb.loadFromFile(fileName) != OS_SUCCESS)
   {
      exit(1);
   }

   // Initialize log file
   initSysLog(&configDb);

   // Read the user agent parameters from the config file.
   int udpPort;
   if (configDb.get(CONFIG_SETTING_UDP_PORT, udpPort) != OS_SUCCESS)
   {
      udpPort = RLS_DEFAULT_UDP_PORT;
   }

   int tcpPort;
   if (configDb.get(CONFIG_SETTING_TCP_PORT, tcpPort) != OS_SUCCESS)
   {
      tcpPort = RLS_DEFAULT_TCP_PORT;
   }

    UtlString bindIp;
    if (configDb.get(CONFIG_SETTING_BIND_IP, bindIp) != OS_SUCCESS ||
            !OsSocket::isIp4Address(bindIp))
        bindIp = RLS_DEFAULT_BIND_IP;

   UtlString resourceListFile;
   if ((configDb.get(CONFIG_SETTING_RLS_FILE, resourceListFile) !=
        OS_SUCCESS) ||
       resourceListFile.isNull())
   {
      OsSysLog::add(LOG_FACILITY, PRI_CRIT,
                    "Resource list file name is not configured");
      return 1;
   }

   UtlString domainName;
   if ((configDb.get(CONFIG_SETTING_DOMAIN_NAME, domainName) !=
        OS_SUCCESS) ||
       domainName.isNull())
   {
      OsSysLog::add(LOG_FACILITY, PRI_CRIT,
                    "Resource domain name is not configured");
      return 1;
   }
   
   UtlString realm;
   if ((configDb.get(CONFIG_SETTING_AUTHENTICATE_REALM, realm) !=
        OS_SUCCESS) ||
       realm.isNull())
   {
      OsSysLog::add(LOG_FACILITY, PRI_CRIT,
                    "Resource realm is not configured");
      return 1;
   }

   int startupWait;
   if (configDb.get(CONFIG_SETTING_STARTUP_WAIT, startupWait) != OS_SUCCESS)
   {
      startupWait = RLS_DEFAULT_STARTUP_WAIT;
   }

   int refreshInterval;
   if (configDb.get(CONFIG_SETTING_REFRESH_INTERVAL, refreshInterval) != OS_SUCCESS)
   {
      refreshInterval = RLS_DEFAULT_REFRESH_INTERVAL;
   }

   int resubscribeInterval;
   if (configDb.get(CONFIG_SETTING_RESUBSCRIBE_INTERVAL, resubscribeInterval) != OS_SUCCESS)
   {
      resubscribeInterval = RLS_DEFAULT_RESUBSCRIBE_INTERVAL;
   }
   
   int minResubscribeInterval;
   if (configDb.get(CONFIG_SETTING_MIN_RESUBSCRIBE_INTERVAL, minResubscribeInterval) != OS_SUCCESS)
   {
       minResubscribeInterval = RLS_DEFAULT_MIN_RESUBSCRIBE_INTERVAL;
   }

   // Wait to allow our targets time to come up.
   // (Wait is determined by CONFIG_SETTING_STARTUP_WAIT, default 2 minutes.)
   OsSysLog::add(LOG_FACILITY, PRI_INFO,
                 "Waiting %d sec. before starting operation.",
                 startupWait);

   // If we get the shutdown signal during this delay, the signal will
   // terminate the wait.
   for(int i=0; i<startupWait && !gShutdownFlag; i++)
   {
      OsTask::delay(1000);
   }

   // add the ~~sipXrls credentials so that sipXrls can respond to challenges
   SipLineMgr* lineMgr = addCredentials(domainName, realm);
   if(NULL == lineMgr)
   {
      return 1;
   }
   
   if (!gShutdownFlag)
   {
      // Initialize the ResourceListServer.
      // (Use tcpPort as the TLS port, too.)
      ResourceListServer rls(domainName, realm, lineMgr,
                             DIALOG_EVENT_TYPE, DIALOG_EVENT_CONTENT_TYPE,
                             tcpPort, udpPort, tcpPort, bindIp,
                             &resourceListFile,
                             refreshInterval, resubscribeInterval, minResubscribeInterval,
                             250, 20, 20, 20, 20);

      // Loop forever until signaled to shut down
      while (!gShutdownFlag)
      {
         OsTask::delay(2000);
         // See if the list configuration file has changed.
         rls.getResourceListFileReader().refresh();
      }

      // Shut down the server.
      rls.shutdown();
   }

   // Flush the log file
   OsSysLog::flush();

   // Delete the LineMgr Object
   delete lineMgr;

   // Say goodnight Gracie...
   return 0;
}

// Stub to avoid pulling in ps library
int JNI_LightButton(long)
{
   return 0;
}
