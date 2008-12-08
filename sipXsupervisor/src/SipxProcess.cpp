// 
// Copyright (C) 2008 Pingtel Corp., certain elements licensed under a Contributor Agreement.  
// Contributors retain copyright to elements licensed under a Contributor Agreement.
// Licensed to the User under the LGPL license.
// 
//////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES

// APPLICATION INCLUDES
#include "alarm/Alarm.h"
#include "os/OsFS.h"
#include "utl/UtlSListIterator.h"
#include "utl/UtlTokenizer.h"
#include "os/OsSysLog.h"
#include "xmlparser/tinyxml.h"
#include "xmlparser/XmlErrorMsg.h"
#include "xmlparser/ExtractContent.h"
#include "sipXecsService/SipXecsService.h"

#include "SipxProcessCmd.h"
#include "SipxResource.h"
#include "FileResource.h"
#include "SipxProcessResource.h"
#include "SipxProcessResourceManager.h"
#include "SipxProcessManager.h"
#include "SipxProcess.h"
#include "SipxCommand.h"

// DEFINES
#define MAX_RETRY_ATTEMPTS 3 // raise alarm after this many retries

// CONSTANTS

const UtlContainableType SipxProcess::TYPE = "SipxProcess";

const char* SipXecsProcessRootElement = "sipXecs-process";
const char* SipXecsProcessNamespace =
   "http://www.sipfoundry.org/sipX/schema/xml/sipXecs-process-01-00";

const char* SipxProcessStateDir = "process-state";
const char* SipxProcessConfigVersionDir = "process-cfgver";

// The following tags are used by sipXconfig to display status messages nicely
// and should not be changed without consulting sipXconfig designers.
const char* resourceMissingTag = "resource.missing";
const char* versionMismatchTag = "version.mismatch";

const size_t MAX_STATUS_MSGS = 100;  /// maximum number of status msgs to save

// TYPEDEFS
struct SipxProcessFsmStateStruct
{
   Disabled              disabled;
   ConfigurationMismatch configurationMismatch;
   ResourceRequired      resourceRequired;   
   Testing               testing;
   StoppingConfigtestToRestart  stoppingConfigtestToRestart;
   ConfigTestFailed      configTestFailed;
   Starting              starting;
   Running               running;
   Stopping              stopping;
   Failed                failed;
   ShuttingDown          shuttingDown;
   ShutDown              shutDown;
   Undefined             undefined;
};

// STATICS INITIALIZATION
Disabled*                SipxProcess::pDisabled = 0;
ConfigurationMismatch*   SipxProcess::pConfigurationMismatch = 0;
ResourceRequired*        SipxProcess::pResourceRequired = 0;
Testing*                 SipxProcess::pTesting = 0;
StoppingConfigtestToRestart*  SipxProcess::pStoppingConfigtestToRestart = 0;
ConfigTestFailed*        SipxProcess::pConfigTestFailed = 0;
Starting*                SipxProcess::pStarting = 0;
Running*                 SipxProcess::pRunning = 0;
Stopping*                SipxProcess::pStopping = 0;
Failed*                  SipxProcess::pFailed = 0;
ShuttingDown*            SipxProcess::pShuttingDown = 0;
ShutDown*                SipxProcess::pShutDown = 0;
Undefined*               SipxProcess::pUndefined = 0;

// FORWARD DECLARATIONS

/// constructor
SipxProcess::SipxProcess(const UtlString& name,
                         const UtlString& version,
                         const OsPath& definitionFile) :
   UtlString(name),
   OsServerTask("SipxProcess-%d"),
   mLock(OsMutex::Q_FIFO),
   mVersion(version),
   mConfigtest(NULL),
   mStart(NULL),
   mStop(NULL),
   mReconfigure(NULL),
   mConfigtestStandalone(NULL),
   mDefinitionFile(definitionFile),
   mbTaskRunning(false),
   mpTimer(NULL),
   mpTimeoutCallback(NULL),
   mRetries(0),
   mNumStdoutMsgs(0),
   mNumStderrMsgs(0)
{
   // Init state pointers
   initializeStatePointers();
   mpCurrentState = pUndefined;
   mpDesiredState = pUndefined;

   /*
    * Get the SipxProcessResource for this SipxProcess; it may have already been created
    * by some other SipxProcess that declared this one as a resource.
    */
   SipxProcessResourceManager* processResourceManager = SipxProcessResourceManager::getInstance();
   if (!(mSelfResource = processResourceManager->find(name.data())))
   {
      // No other SipxProcess has declared this one as a resource yet,
      // so create the SipxProcessResource 
      mSelfResource = new SipxProcessResource(name.data());
      processResourceManager->save(mSelfResource);
   }
   else
   {
      /* the resource has already been created by some other SipxProcess
       * listing this one as a resource.
       */
      OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "SipxProcess: resource has already been created ");
   }
};

void SipxProcess::initializeStatePointers( void )
{
   static SipxProcessFsmStateStruct states;
   
   pDisabled              = &states.disabled;
   pConfigurationMismatch = &states.configurationMismatch;
   pResourceRequired      = &states.resourceRequired;
   pTesting               = &states.testing;
   pStoppingConfigtestToRestart   = &states.stoppingConfigtestToRestart;
   pConfigTestFailed      = &states.configTestFailed;
   pStarting              = &states.starting;
   pRunning               = &states.running;
   pStopping              = &states.stopping;
   pFailed                = &states.failed;
   pShuttingDown          = &states.shuttingDown;
   pShutDown              = &states.shutDown;
   pUndefined             = &states.undefined;

}


const SipxProcessFsm* SipxProcess::GetCurrentState()
{
   OsLock mutex(mLock);

   return mpCurrentState;
}  
   
void SipxProcess::SetCurrentState( const SipxProcessFsm* pState )
{
   OsLock mutex(mLock);

   mpCurrentState = pState;
}


const char* SipxProcess::name( void ) const
{
   return this->data();
}

      

/// Read a process definition and return a process if definition is valid.
SipxProcess* SipxProcess::createFromDefinition(const OsPath& definitionFile)
{
   SipxProcess* process = NULL;
   SipxProcessManager* processManager = SipxProcessManager::getInstance();
   
   UtlString errorMsg;

   TiXmlDocument processDefinitionDoc(definitionFile);
   if (processDefinitionDoc.LoadFile())
   {
      // definition is well formed xml, at least
      TiXmlElement* processDefElem;
      if ((processDefElem = processDefinitionDoc.RootElement()))
      {
         const char* rootElementName = processDefElem->Value();
         const char* definitionNamespace = processDefElem->Attribute("xmlns");
         if (   rootElementName && definitionNamespace
             && 0 == strcmp(rootElementName, SipXecsProcessRootElement)
             && 0 == strcmp(definitionNamespace, SipXecsProcessNamespace)
             )
         {
            // step through the top level elements

            bool definitionValid = true;
            
            TiXmlElement* nameElement = NULL;
            UtlString name;
            TiXmlElement* versionElement = NULL;
            UtlString version;
            TiXmlElement* commandsElement = NULL;
            TiXmlElement* statusElement = NULL;
            TiXmlElement* resourcesElement = NULL;

            // Get the 'name' element
            nameElement = processDefElem->FirstChildElement();
            if (nameElement && (0 == strcmp("name",nameElement->Value())))
            {
            
               if (   ! textContent(name, nameElement)
                   || name.isNull()
                   )
               {
                  definitionValid = false;
                  XmlErrorMsg(processDefinitionDoc,errorMsg);
                  OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess::createFromDefinition "
                                "'name' element content is invalid %s",
                                errorMsg.data()
                                );
               }
               else if ((process = processManager->findProcess(name)))
               {
                  definitionValid = false;
                  XmlErrorMsg(processDefinitionDoc,errorMsg);
                  OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess::createFromDefinition "
                                "duplicate process name '%s'\n"
                                "  %s\n"
                                "  previously defined in '%s'",
                                name.data(), errorMsg.data(), process->mDefinitionFile.data()
                                );
                  process = NULL; // so that we don't return or delete it
               }
            }
            else
            {
               definitionValid = false;
               XmlErrorMsg(processDefinitionDoc,errorMsg);
               OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess::createFromDefinition "
                             "required 'name' element is missing %s",
                             errorMsg.data()
                             );
            }
   
            // Get the 'version' element
            if ( definitionValid )
            {
               if (   (versionElement = nameElement->NextSiblingElement())
                   && (0 == strcmp("version",versionElement->Value())))
               {
                  if (   ! textContent(version, versionElement)
                      || version.isNull()
                      )
                  {
                     definitionValid = false;
                     XmlErrorMsg(processDefinitionDoc,errorMsg);
                     OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess::createFromDefinition "
                                   "'version' element content is invalid %s",
                                   errorMsg.data()
                                   );
                  }
               }
               else
               {
                  definitionValid = false;
                  XmlErrorMsg(processDefinitionDoc,errorMsg);
                  OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess::createFromDefinition "
                                "required 'version' element is missing %s",
                                errorMsg.data()
                                );
               }
            }

            // Get the 'commands' element
            if ( definitionValid )
            {
               if (   (commandsElement = versionElement->NextSiblingElement())
                   && (0 == strcmp("commands",commandsElement->Value())))
               {
                  // defer parsing commands until SipxProcess object is created below
               }
               else
               {
                  definitionValid = false;
                  XmlErrorMsg(processDefinitionDoc,errorMsg);
                  OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess::createFromDefinition "
                                "required 'commands' element is missing %s",
                                errorMsg.data()
                                );
               }
            }

            // Get the 'status' element
            if ( definitionValid )
            {
               if (   (statusElement = commandsElement->NextSiblingElement())
                   && (0 == strcmp("status",statusElement->Value())))
               {
                  // defer parsing status until SipxProcess object is created below
               }
               else
               {
                  definitionValid = false;
                  XmlErrorMsg(processDefinitionDoc,errorMsg);
                  OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess::createFromDefinition "
                                "required 'status' element is missing %s",
                                errorMsg.data()
                                );
               }
            }

            // Get the 'resources' element
            if ( definitionValid )
            {
               if ((resourcesElement = statusElement->NextSiblingElement()))
               {
                  const char* elementName = resourcesElement->Value();
                  if (0 != strcmp("resources",elementName))
                  {
                     definitionValid = false;
                     XmlErrorMsg(processDefinitionDoc,errorMsg);
                     OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess::createFromDefinition "
                                   "invalid '%s' element: expected 'resources'  %s",
                                   elementName, errorMsg.data()
                                   );
                  }
               }
            }

            /* All the required top level elements have been found, so create
             * the SipxProcess object and invoke the parsers for the components. */
            if (definitionValid)
            {
               if ((process = new SipxProcess(name, version, definitionFile)))
               {
                  // Parse the 'commands' elements
                  TiXmlElement* configtestCmdElement;
                  TiXmlElement* startCmdElement;
                  TiXmlElement* stopCmdElement;
                  TiXmlElement* reconfigureCmdElement;

                  // mConfigtest <= sipXecs-process/commands/configtest
                  configtestCmdElement = commandsElement->FirstChildElement();
                  if (   configtestCmdElement
                      && (0 == strcmp("configtest",configtestCmdElement->Value())))
                  {
                     if (! (process->mConfigtest =
                            SipxProcessCmd::parseCommandDefinition(processDefinitionDoc,
                                                               configtestCmdElement)))
                     {
                        definitionValid = false;
                        XmlErrorMsg(processDefinitionDoc,errorMsg);
                        OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess::createFromDefinition "
                                      "'configtest' content is invalid %s",
                                      errorMsg.data()
                                      );
                     }
                     else
                     {
                        // create a standalone command for configtest, which can be
                        // run independent of the FSM
                        UtlString name = process->data();
                        name.append("-configtest");
                        process->mConfigtestStandalone =
                           SipxCommand::createFromDefinition(name,
                                                             processDefinitionDoc,
                                                             configtestCmdElement);
                     }
                  }
                  else
                  {
                     definitionValid = false;
                     XmlErrorMsg(processDefinitionDoc,errorMsg);
                     OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess::createFromDefinition "
                                   "required 'configtest' element is missing %s",
                                   errorMsg.data()
                                   );
                  }

                  // mStart <= sipXecs-process/commands/start
                  if (definitionValid)
                  {
                     startCmdElement = configtestCmdElement->NextSiblingElement();
                     if (   startCmdElement
                         && (0 == strcmp("start",startCmdElement->Value())))
                     {
                        if (! (process->mStart =
                               SipxProcessCmd::parseCommandDefinition(processDefinitionDoc,
                                                                  startCmdElement)))
                        {
                           definitionValid = false;
                           XmlErrorMsg(processDefinitionDoc,errorMsg);
                           OsSysLog::add(FAC_SUPERVISOR, PRI_ERR,
                                         "SipxProcess::createFromDefinition "
                                         "'start' content is invalid %s",
                                         errorMsg.data()
                                         );
                        }
                     }
                     else
                     {
                        definitionValid = false;
                        XmlErrorMsg(processDefinitionDoc,errorMsg);
                        OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess::createFromDefinition "
                                      "required 'start' element is missing %s",
                                      errorMsg.data()
                                      );
                     }
                  }

                  // mStop <= sipXecs-process/commands/stop
                  if (definitionValid)
                  {
                     stopCmdElement = startCmdElement->NextSiblingElement();
                     if (   stopCmdElement
                         && (0 == strcmp("stop",stopCmdElement->Value())))
                     {
                        if (! (process->mStop =
                               SipxProcessCmd::parseCommandDefinition(processDefinitionDoc,
                                                                  stopCmdElement)))
                        {
                           definitionValid = false;
                           XmlErrorMsg(processDefinitionDoc,errorMsg);
                           OsSysLog::add(FAC_SUPERVISOR, PRI_ERR,
                                         "SipxProcess::createFromDefinition "
                                         "'stop' content is invalid %s",
                                         errorMsg.data()
                                         );
                        }
                     }
                     else
                     {
                        definitionValid = false;
                        XmlErrorMsg(processDefinitionDoc,errorMsg);
                        OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess::createFromDefinition "
                                      "required 'stop' element is missing %s",
                                      errorMsg.data()
                                      );
                     }
                  }

                  // mReconfigure <= sipXecs-process/commands/reconfigure
                  if (definitionValid)
                  {
                     reconfigureCmdElement = stopCmdElement->NextSiblingElement();
                     if (   reconfigureCmdElement
                         && (0 == strcmp("reconfigure",reconfigureCmdElement->Value())))
                     {
                        if (!(process->mReconfigure =
                              SipxProcessCmd::parseCommandDefinition(processDefinitionDoc,
                                                                 reconfigureCmdElement)))
                        {
                           definitionValid = false;
                           XmlErrorMsg(processDefinitionDoc,errorMsg);
                           OsSysLog::add(FAC_SUPERVISOR, PRI_ERR,
                                         "SipxProcess::createFromDefinition "
                                         "'reconfigure' content is invalid %s",
                                         errorMsg.data()
                                         );
                        }
                     }
                     else
                     {
                        // reconfigure is optional, so this is ok.
                     }
                  }

                  if (definitionValid)
                  {
                     // Parse the 'status' elements
                     TiXmlElement* statusElement;

                     statusElement = commandsElement->NextSiblingElement();
                     if (   statusElement
                         && (0 == strcmp("status",statusElement->Value())))
                     {
                        TiXmlElement* statusChildElement;
                     
                        // mPidFile <= sipXecs-process/status/pid
                        statusChildElement = statusElement->FirstChildElement();
                        if (statusChildElement)
                        {
                           if (0 == strcmp("pid",statusChildElement->Value()))
                           {
                              // the optional pid element is present
                              if (   textContent(process->mPidFile, statusChildElement)
                                  && !process->mPidFile.isNull())
                              {
                                 // advance the statusChildElement to the first log element, if any
                                 statusChildElement = statusChildElement->NextSiblingElement();
                              }
                              else
                              {
                                 definitionValid = false;
                                 XmlErrorMsg(processDefinitionDoc,errorMsg);
                                 OsSysLog::add(FAC_SUPERVISOR, PRI_ERR,
                                               "SipxProcess::createFromDefinition "
                                               "'pid' element is empty"
                                               " - if present, it must be a pathname %s",
                                               errorMsg.data()
                                               );

                              }
                           }
                           /*
                            * There is no pid element, which is allowed.
                            * If statusChildElement is non-NULL, it should point to a 'log' element.
                            * Leave it for the 'log' loop below.
                            */
                        }

                        // mLogFiles <- sipXecs-process/status/log
                        while (definitionValid && statusChildElement)
                        {
                           if (0 == strcmp("log",statusChildElement->Value()))
                           {
                              UtlString logPath;
                              if (textContent(logPath, statusChildElement) && !logPath.isNull())
                              {
                                 FileResource* logFileResource =
                                    FileResource::logFileResource(logPath, process);
                                 
                                 process->mLogFiles.append(logFileResource);
                              
                                 // advance the statusChildElement to the first log element, if any
                                 statusChildElement = statusChildElement->NextSiblingElement();
                              }
                              else
                              {
                                 definitionValid = false;
                                 XmlErrorMsg(processDefinitionDoc,errorMsg);
                                 OsSysLog::add(FAC_SUPERVISOR, PRI_ERR,
                                               "SipxProcess::createFromDefinition "
                                               "'log' element is empty"
                                               " - if present, it must be a pathname %s",
                                               errorMsg.data()
                                               );
                              }
                           }
                           else
                           {
                              definitionValid = false;
                              XmlErrorMsg(processDefinitionDoc,errorMsg);
                              OsSysLog::add(FAC_SUPERVISOR, PRI_ERR,
                                            "SipxProcess::createFromDefinition "
                                            "'%s' element is invalid here: expected 'log'",
                                            statusChildElement->Value()                
                                            );
                           }
                        }
                     }
                     else
                     {
                        definitionValid = false;
                     
                        XmlErrorMsg(processDefinitionDoc,errorMsg);
                        OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess::createFromDefinition "
                                      "required 'status' element is missing %s",
                                      errorMsg.data()
                                      );
                     }
                  }
                  
                  if (definitionValid)
                  {
                     // Parse the 'resources' elements
                     TiXmlElement* resourcesElement;

                     resourcesElement = statusElement->NextSiblingElement();
                     if (resourcesElement)
                     {
                        if (0 == strcmp("resources",resourcesElement->Value()))
                        {
                           TiXmlElement* resourceElement;
                           for (resourceElement = resourcesElement->FirstChildElement();
                                definitionValid && resourceElement;
                                resourceElement = resourceElement->NextSiblingElement()
                                )
                           {
                              /*
                               * We do not validate the element name here, because
                               * we don't know all valid resource element types.  That
                               * is done in the factory we call to parse the element.
                               *
                               * If this resource is required for starting or stopping,
                               * then the requireResourceToStart and/or checkResourceBeforeStop
                               * methods on the new process is called to add the new
                               * resource to the appropriate list.
                               */
                              definitionValid =
                                 SipxResource::parse(processDefinitionDoc,resourceElement,process);
                           }
                        }
                        else
                        {
                           definitionValid = false;
                           XmlErrorMsg(processDefinitionDoc,errorMsg);
                           OsSysLog::add(FAC_SUPERVISOR, PRI_ERR,
                                         "SipxProcess::createFromDefinition "
                                         "'%s' element is invalid here: expected 'resources'",
                                         resourcesElement->Value()                
                                         );
                        }
                     }
                     else
                     {
                        definitionValid = false;
                        XmlErrorMsg(processDefinitionDoc,errorMsg);
                        OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess::createFromDefinition "
                                      "required 'resources' element is missing %s",
                                      errorMsg.data()
                                      );
                     }
                  }
                  
                  if (definitionValid)
                  {
                     SipxProcessManager::getInstance()->save(process);

                     process->readPersistentState();

                     {
                        OsLock mutex(process->mLock);
                        /*
                         * Now that the SipxProcessManager could return this, locking is
                         * important.  Both persistDesiredState and readConfigurationVersion
                         * require that the lock be held.
                         */
                        
                        process->mpTimeoutCallback = new OsCallback((void*)process, timeoutCallback);
                        process->mpTimer = new OsTimer(*process->mpTimeoutCallback);

                        // start the task which will listen for messages and launch programs in the background
                        process->mbTaskRunning = process->start();

                        if (pUndefined == process->mpDesiredState)
                        {
                           process->mpDesiredState = pDisabled; // default is disabled.
                           process->persistDesiredState();
                        }
                        process->readConfigurationVersion();
                        
                     } // end lock

                     // kickstart the state machine
                     process->startStateMachine();
                     yield();
                  }
                  else
                  {
                     // something is wrong, so get rid of the invalid SipxProcess object
                     delete process;
                     process = NULL;
                  }
               }
               else
               {
                  OsSysLog::add(FAC_SUPERVISOR, PRI_CRIT, "SipxProcess::createFromDefinition "
                                "failed to create SipxProcess object for '%s'", name.data());
               }
            }
         }
         else
         {
            XmlErrorMsg(processDefinitionDoc,errorMsg);
            OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess::createFromDefinition "
                          "invalid root element '%s' in namespace '%s'\n"
                          "should be '%s' in namespace '%s' %s",
                          rootElementName, definitionNamespace,
                          SipXecsProcessRootElement, SipXecsProcessNamespace,
                          errorMsg.data()
                          );
         }

      }
      else
      {
         XmlErrorMsg(processDefinitionDoc,errorMsg);
         OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess::createFromDefinition "
                       "root element not found in '%s': %s",
                       definitionFile.data(), errorMsg.data()
                       );
      }
   }
   else
   {
      UtlString errorMsg;
      XmlErrorMsg(processDefinitionDoc,errorMsg);
      OsSysLog::add(FAC_SUPERVISOR, PRI_ERR,
                    "SipxProcess::createFromDefinition failed to load '%s': %s",
                    definitionFile.data(), errorMsg.data()
                    );
   }

   return process;
}

/// Return the SipxProcessResource for this SipxProcess.
SipxProcessResource* SipxProcess::resource()
{
   OsLock mutex(mLock);
   
   return mSelfResource;
}

/// Return whether or not the service for this process should be Running.
// Caller must hold the lock
bool SipxProcess::isEnabled()
{
   OsLock mutex(mLock);
   
   OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "SipxProcess[%s]::isEnabled %d",
                 data(), (pRunning == mpDesiredState) );

   return (pRunning == mpDesiredState);
}
/**< @returns true if the desired state of this service is Running;
 *            this does not indicate anything about the current state of
 *            the service: for that see getState
 */

/// Return whether or not the service for this process is Running.
bool SipxProcess::isRunning()
{
   OsLock mutex(mLock);
   
   OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "SipxProcess[%s]::isRunning %d",
                 data(), (pRunning == mpCurrentState) );

   return (pRunning == mpCurrentState);
}


bool SipxProcess::isCompletelyStopped()
{
   // We are only stopped when both the "start" command and the "stop" command are done.
   return (!mStart->isRunning() && !mStop->isRunning());
}


void SipxProcess::startStateMachineInTask()
{
   OsLock mutex(mLock);

   const SipxProcessFsm* pState = pDisabled;
   StateAlg::StartStateMachine( *this, pState );
}

/// Set the persistent desired state of the SipxProcess to Running.
void SipxProcess::enableInTask()
{
   OsLock mutex(mLock);

   OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "SipxProcess[%s]::enable",
                 data());

   mRetries=0;
   mpDesiredState = pRunning;
   persistDesiredState();

   mpCurrentState->evStartProcess(*this);
}

/// Set the persistent desired state of the SipxProcess to Disabled.
void SipxProcess::disableInTask()
{
   OsLock mutex(mLock);
   
   OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "SipxProcess[%s]::disable",
                 data());

   mpDesiredState = pDisabled;
   persistDesiredState();

   mpCurrentState->evStopProcess(*this);
}

/// Stop the process without Disabling the persistent desired state
void SipxProcess::restartInTask()
{
   OsLock mutex(mLock);
   
   OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "SipxProcess[%s]::restart",
                 data());

   mRetries=0;

   mpCurrentState->evRestartProcess(*this);
}

/// Shutting down sipXsupervisor, so shut down the service.
void SipxProcess::shutdownInTask()
{
   OsLock mutex(mLock);
   
   //This does not affect the persistent state of the service.
   OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "SipxProcess[%s]::shutdown in state %s",
                 data(), mpCurrentState->name());

   mpCurrentState->evShutdown(*this);
}


// the following functions post messages to the FSM task
void SipxProcess::startStateMachine()
{
   SipxProcessMsg message(SipxProcessMsg::START_FSM );
   postMessage( message );
}

void SipxProcess::enable()
{
   SipxProcessMsg message(SipxProcessMsg::ENABLE );
   postMessage( message );
}

void SipxProcess::disable()
{
   SipxProcessMsg message(SipxProcessMsg::DISABLE );
   postMessage( message );
}

void SipxProcess::restart()
{
   SipxProcessMsg message(SipxProcessMsg::RESTART );
   postMessage( message );
}

void SipxProcess::shutdown()
{
   SipxProcessMsg message(SipxProcessMsg::SHUTDOWN );
   postMessage( message );
}

// This should be called from the FSM task when it is ready to be shut down
void SipxProcess::done()
{

}

bool SipxProcess::runConfigtest()
{
   return mConfigtestStandalone->execute();
}

/// Return any messages accumulated during most recent run of configtest
/// The caller is responsible for freeing the memory used for the strings.
void SipxProcess::getConfigtestMessages(UtlSList& statusMessages)
{
   mConfigtestStandalone->getCommandMessages(statusMessages);
}

void SipxProcess::evCommandStarted(const SipxProcessCmd* command)
{
   SipxProcessMsg message(SipxProcessMsg::STARTED, command );
   postMessage( message );
}

void SipxProcess::evCommandStopped(const SipxProcessCmd* command, int rc)
{
   SipxProcessMsg message(SipxProcessMsg::STOPPED, command, rc );
   postMessage( message );
}

void SipxProcess::evCommandOutput(const SipxProcessCmd* command, 
                                  OsSysLogPriority pri, 
                                  UtlString output)
{
   SipxProcessMsg message(SipxProcessMsg::OUTPUT, command, pri, output );
   postMessage( message );
}

void SipxProcess::evRetryTimeout()
{
   SipxProcessMsg message(SipxProcessMsg::RETRY_TIMEOUT );
   postMessage( message );
}

void SipxProcess::timeoutCallback(void* userData, const intptr_t eventData)
{
   SipxProcess* process = (SipxProcess*) userData;
   process->evRetryTimeout();
}

UtlBoolean SipxProcess::handleMessage( OsMsg& rMsg )
{
   UtlBoolean handled = FALSE;
   SipxProcessMsg* pMsg = dynamic_cast <SipxProcessMsg*> ( &rMsg );

   switch ( rMsg.getMsgType() )
   {
   case OsMsg::OS_SHUTDOWN:
      requestShutdown();
      handled = TRUE;
      break;
      
   case OsMsg::OS_EVENT:
   {
      switch ( pMsg->getMsgSubType() )
      {
      case SipxProcessMsg::START_FSM:
         startStateMachineInTask();
         handled = TRUE;
         break;
         
      case SipxProcessMsg::ENABLE:
         enableInTask();
         handled = TRUE;
         break;
      case SipxProcessMsg::DISABLE:
         disableInTask();
         handled = TRUE;
         break;
      case SipxProcessMsg::RESTART:
         restartInTask();
         handled = TRUE;
         break;
      case SipxProcessMsg::SHUTDOWN:
         shutdownInTask();
         handled = TRUE;
         break;
      case SipxProcessMsg::RETRY_TIMEOUT:
         evRetryTimeoutInTask();
         handled = TRUE;
         break;

      case SipxProcessMsg::STARTED:
         evCommandStartedInTask(pMsg->getCmd());
         handled = TRUE;
         break;
      case SipxProcessMsg::STOPPED:
         evCommandStoppedInTask(pMsg->getCmd(), pMsg->getIntData());
         handled = TRUE;
         break;
      case SipxProcessMsg::OUTPUT:
         evCommandOutputInTask(pMsg->getCmd(), (OsSysLogPriority)pMsg->getIntData(), pMsg->getMessage());
         handled = TRUE;
         break;
         
      default:
         OsSysLog::add(FAC_SUPERVISOR, PRI_CRIT,
                       "SipxProcess::handleMessage: '%s' unhandled message subtype %d.%d",
                       mName.data(), rMsg.getMsgType(), rMsg.getMsgSubType());
         break;
      }
      break;
   }

   default:
      OsSysLog::add(FAC_SUPERVISOR, PRI_CRIT,
                    "SipxProcess::handleMessage: '%s' unhandled message type %d.%d",
                    mName.data(), rMsg.getMsgType(), rMsg.getMsgSubType());
      break;
   }

   return handled;
}


void SipxProcess::evRetryTimeoutInTask()
{
   OsLock mutex(mLock);

   OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "SipxProcess[%s]::evRetryTimeoutInTask",
                 data());

   mpCurrentState->evRetryTimeout(*this);
}

void SipxProcess::evCommandStartedInTask(const SipxProcessCmd* command)
{
   OsLock mutex(mLock);

   if (command == mStart)
   {
      mpCurrentState->evProcessStarted(*this);
   }
   else if (command == mStop)
   {
      // this just means we've started the stop command.  Wait for actual stop
   }
   else if (command == mConfigtest)
   {
      // this just means we've started the configtest command.  Wait for actual stop
   }
}

void SipxProcess::evCommandStoppedInTask(const SipxProcessCmd* command, int rc)
{
   OsLock mutex(mLock);

   OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "SipxProcess[%s]::evCommandStopped %s",
                 data(), command->data());
   if (command == mStart)
   {
      mpCurrentState->evProcessStopped(*this);

   }
   else if (command == mStop)
   {
      // stop stopped, but we are really interested in the start
      mpCurrentState->evStopCompleted(*this);
   }
   else if (command == mConfigtest)
   {
      if ( rc == 0 )
      {
         mpCurrentState->evConfigTestPassed(*this);
      }
      else
      {
         mpCurrentState->evConfigTestFailed(*this);
      }
   }

}


void SipxProcess::evCommandOutputInTask(const SipxProcessCmd* command, 
                                        OsSysLogPriority pri, 
                                        UtlString output)
{
   // The output is sure to contain newlines, and may contain several lines.
   // Clean it up before dispatching.
   UtlTokenizer tokenizer(output);
   UtlString    msg;
   while ( tokenizer.next(msg, "\r\n") )
   {
      logCommandOutput(pri, msg);
   }
}

/// Notify the SipxProcess that some configuration change has occurred.
void SipxProcess::configurationChange(const SipxResource& changedResource)
{
   UtlString changedResourceDescription;
   changedResource.appendDescription(changedResourceDescription);
   
   OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "SipxProcess[%s]::configurationChange(%s)",
                 data(), changedResourceDescription.data());
   
   OsLock mutex(mLock);
   mpCurrentState->evConfigurationChanged(*this);
}
   
/// Notify the SipxProcess that some configuration change has occurred.
void SipxProcess::configurationVersionChange()
{
   OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "SipxProcess[%s]::configurationVersionChange(%s)",
                 data(), mConfigVersion.data());
   
   OsLock mutex(mLock);
   mpCurrentState->evConfigurationChanged(*this);
}

// caller must hold the lock
void SipxProcess::startConfigTest()
{
   mConfigtest->execute(this);
}

void SipxProcess::killConfigTest()
{
   mConfigtest->kill();
}

void SipxProcess::startProcess()
{
   mStart->execute(this);
}

void SipxProcess::stopProcess()
{
   mStop->execute(this);
}

void SipxProcess::processFailed()
{
   mRetries++;
   //@TODO: clear mRetries if the last failure was a long time ago (>1 minute?)
   if ( mRetries == MAX_RETRY_ATTEMPTS )
   {
      Alarm::raiseAlarm("PROCESS_FAILED", data());
   }
}

void SipxProcess::startRetryTimer()
{
   if (mpTimer)
   {
      mpTimer->oneshotAfter((mRetries+1)*2000);
   }
}

void SipxProcess::cancelRetryTimer()
{
   if (mpTimer)
   {
      mpTimer->stop();
   }
}

void SipxProcess::readConfigurationVersion()
{
   // caller must be holding mLock

   OsPath persistentConfigVersionPath(SipXecsService::Path(SipXecsService::VarDirType,
                                                           SipxProcessConfigVersionDir)
                                      + OsPath::separator + data());
   OsFile persistentConfigVersionFile(persistentConfigVersionPath);

   OsStatus rc;
   if (OS_SUCCESS == (rc=persistentConfigVersionFile.open(OsFile::READ_ONLY)))
   {
      if (OS_SUCCESS == (rc=persistentConfigVersionFile.readLine(mConfigVersion)))
      {
         OsSysLog::add(FAC_SUPERVISOR, PRI_INFO,
                       "SipxProcess[%s]::readConfigurationVersion mConfigVersion='%s'",
                       data(), mConfigVersion.data());
      }
      else
      {
         // apparently, open read-only can return success when the file is not there.
         OsSysLog::add(FAC_SUPERVISOR, PRI_ERR,
                       "SipxProcess[%s]::readConfigurationVersion read of '%s' failed, rc=%d"
                       " (ok if process has never been configured)",
                       data(), persistentConfigVersionPath.data(), rc);
      }

      persistentConfigVersionFile.close();
   }
   else
   {
      OsSysLog::add(FAC_SUPERVISOR, PRI_WARNING,
                    "SipxProcess[%s]::readConfigurationVersion open of '%s' failed, rc=%d"
                    " (ok if process has never been configured)",
                    data(), persistentConfigVersionPath.data(), rc);
   }
}

/// Check whether or not the configuration version matches the process version.
bool SipxProcess::configurationVersionMatches()
{
   OsLock mutex(mLock);
   bool versionMatches = (0==mConfigVersion.compareTo(mVersion, UtlString::matchCase));

   if (!versionMatches)
   {
      logVersionMismatch(mVersion, mConfigVersion);
   }
   
   return versionMatches;
}

   

/// Set the version stamp value of the configuration.
void SipxProcess::setConfigurationVersion(const UtlString& newConfigVersion)
{
   OsLock mutex(mLock);
   
   if (0!=newConfigVersion.compareTo(mConfigVersion,UtlString::matchCase))
   {
      OsSysLog::add(FAC_SUPERVISOR, PRI_INFO, "SipxProcess[%s]::setConfigurationVersion"
                    " '%s' -> '%s'", data(), mConfigVersion.data(), newConfigVersion.data());

      mConfigVersion = newConfigVersion;
      
      OsPath persistentConfigVersionDirPath  // normally {prefix}/var/sipxdata/process-cfgver
         = SipXecsService::Path(SipXecsService::VarDirType, SipxProcessConfigVersionDir);
   
      OsDir persistentConfigVersionDir(persistentConfigVersionDirPath);
      OsPath persistentConfigVersionPath(persistentConfigVersionDirPath
                                         + OsPath::separator + data());
      OsFile persistentConfigVersionFile(persistentConfigVersionPath);

      if (!persistentConfigVersionDir.exists()) // does the directory exist?
      {
         if (OS_SUCCESS==OsFileSystem::createDir(persistentConfigVersionDirPath,
                                                 TRUE /* create parents */))
         {
            OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "SipxProcess::setConfigurationVersion "
                          "created directory '%s'",
                          persistentConfigVersionDirPath.data());
         }
         else 
         {
            OsSysLog::add(FAC_SUPERVISOR, PRI_CRIT, "SipxProcess[%s]::setConfigurationVersion "
                          "directory create failed for '%s'",
                          data(), persistentConfigVersionDirPath.data());
         }
      }

      if (OS_SUCCESS==persistentConfigVersionFile.open(OsFile::CREATE))
      {
         UtlString configVersion(mConfigVersion);
         configVersion.append("\n");
         
         size_t bytesWritten;
         if (OS_SUCCESS!=persistentConfigVersionFile.write(configVersion.data(),
                                                           configVersion.length(),
                                                           bytesWritten))
         {
            OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess[%s]::setConfigurationVersion "
                          "write to '%s' failed", data(), persistentConfigVersionPath.data());
         }
      }
      else
      {
         OsSysLog::add(FAC_SUPERVISOR, PRI_ERR,
                       "SipxProcess[%s]::setConfigurationVersion create of '%s' failed",
                       data(), persistentConfigVersionPath.data());
      }

      configurationVersionChange();
   }
   else
   {
      OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG,
                    "SipxProcess[%s]::setConfigurationVersion "
                    "new value '%s' matches existing value.",
                    data(),mConfigVersion.data());
   }
   
}



/// Get the version stamp value of the configuration.
void SipxProcess::getConfigurationVersion(UtlString& version)
{
   version.remove(0);
   OsLock mutex(mLock);
   
   if (!mConfigVersion.isNull())
   {
      OsSysLog::add(FAC_SUPERVISOR, PRI_INFO, "SipxProcess[%s]::getConfigurationVersion"
                    " '%s'", data(), mConfigVersion.data());
      version = mConfigVersion;
   }
   else
   {
      OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG,
                    "SipxProcess[%s]::getConfigurationVersion - no value set",
                    data());
   }
}

   
/// Check that all resources on the mRequiredResources list are ready so this can start.
bool SipxProcess::resourcesAreReady()
{
   OsLock mutex(mLock);
   
   UtlSListIterator resourceListIterator( mRequiredResources );
   SipxResource* pResource;
   bool bReady = true;

   while ( (pResource = dynamic_cast<SipxResource*> (resourceListIterator())) )
   {
      UtlString missingResource;
      if ( !pResource->isReadyToStart(missingResource) )
      {
         logMissingResource(missingResource);
         bReady = false;
      }
   }
   OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "SipxProcess[%s]::resourcesAreReady %d",
                 data(), bReady );

   return bReady; 
}


/// Determine whether or not the values in a containable are comparable.
UtlContainableType SipxProcess::getContainableType() const
{
   return TYPE;
}

void SipxProcess::requireResource(SipxResource* resource)
{
   OsLock mutex(mLock);

   mRequiredResources.append(resource);
}

void SipxProcess::resourceIsOptional(SipxResource* resource)
{
   OsLock mutex(mLock);

   mRequiredResources.removeReference(resource);
}


/// Save the persistent desired state.
void SipxProcess::persistDesiredState()
{
   // caller must be holding mLock.

   OsPath persistentStateDirPath  // normally {prefix}/var/sipxecs/process-state
      = SipXecsService::Path(SipXecsService::VarDirType, SipxProcessStateDir);
   
   OsDir persistentStateDir(persistentStateDirPath);

   if (!persistentStateDir.exists())
   {
      if (OS_SUCCESS == OsFileSystem::createDir(persistentStateDirPath, TRUE /* create parents */))
      {
         OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "SipxProcess::persistDesiredState "
                       "created directory '%s'",
                       persistentStateDirPath.data());
      }
      else
      {
         OsSysLog::add(FAC_SUPERVISOR, PRI_CRIT, "SipxProcess::persistDesiredState "
                       "directory create failed for '%s'",
                       persistentStateDirPath.data());
      }
   }

   OsPath persistentStatePath(persistentStateDirPath + OsPath::separator + data());
   OsFile persistentStateFile(persistentStatePath);
   
   if (OS_SUCCESS==persistentStateFile.open(OsFile::CREATE))
   {
      UtlString persistentState(mpDesiredState->name());
      persistentState.append("\n");
      
      size_t bytesWritten;
      if (OS_SUCCESS != persistentStateFile.write(persistentState.data(),
                                                  persistentState.length(),
                                                  bytesWritten))
      {
         OsSysLog::add(FAC_SUPERVISOR, PRI_CRIT, "SipxProcess::persistDesiredState "
                       "write to '%s' failed", persistentStatePath.data());
      }
   }
   else
   {
      OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, "SipxProcess::persistDesiredState open of '%s' failed",
                    persistentStatePath.data());
   }
}

/// Read the persistent desired state into mDesiredState.
void SipxProcess::readPersistentState()
{
   OsLock mutex(mLock);
   
   mpDesiredState = pDisabled;

   OsPath persistentStatePath(SipXecsService::Path(SipXecsService::VarDirType, SipxProcessStateDir)
                              + OsPath::separator + data());
   OsFile persistentStateFile(persistentStatePath);
   
   OsStatus rc;
   if (OS_SUCCESS == (rc=persistentStateFile.open(OsFile::READ_ONLY)))
   {
      UtlString persistentStateString;
      if (OS_SUCCESS != (rc=persistentStateFile.readLine(persistentStateString)))
      {
         OsSysLog::add(FAC_SUPERVISOR, PRI_ERR,
                       "SipxProcess::readPersistentState read failed, rc=%d. "
                       "persistentStatePath = '%s'",
                       rc, persistentStatePath.data());
      }

      if ( (persistentStateString.compareTo("Enabled")==0) ||
           (persistentStateString.compareTo(pRunning->name())==0) )
      {
         mpDesiredState = pRunning;
      }
   }
   else
   {
      OsSysLog::add(FAC_SUPERVISOR, PRI_WARNING,
                    "SipxProcess::readPersistentState open of '%s' failed",
                    persistentStatePath.data());
   }
}

/// Return any status messages accumulated during or leading up to the current state
/// The caller is responsible for freeing the memory used for the strings.
void SipxProcess::getStatusMessages(UtlSList& statusMessages)
{
   OsLock mutex(mLock);
   
   statusMessages.removeAll();
   UtlSListIterator messages(mStatusMessages);
   UtlString* message = NULL;
   while ((message = dynamic_cast<UtlString*>(messages())))
   {
      statusMessages.append(new UtlString(*message));
   }
}

/// Clear any status messages accumulated so far and reset log counters
void SipxProcess::clearStatusMessages()
{
   OsLock mutex(mLock);
   
   mStatusMessages.destroyAll();
   mNumStdoutMsgs = 0;
   mNumStderrMsgs = 0;
}

/// Custom comparison method that allows SipxProcess retrieved in Utl containers
/// using UtlStrings or any UtlString-derived object.
int SipxProcess::compareTo(UtlContainable const *other) const
{
   int compareFlag = -1;
   UtlString const * pOtherAsString = dynamic_cast<UtlString const *>( other );
   if ( pOtherAsString )
   {
       compareFlag = UtlString::compareTo( pOtherAsString->data() );
   }

   return compareFlag;
   
}

/// Save status message so it can be queried later
void SipxProcess::addStatusMessage(const char* msgTag, UtlString& msg)
{
   OsLock mutex(mLock);
   
   // only keep a limited amount of command output.
   if ( mStatusMessages.entries() > MAX_STATUS_MSGS )
   {
      delete mStatusMessages.at(0);
      mStatusMessages.removeAt(0);
   }

   char buf [1024];
   snprintf(buf, sizeof(buf), "%s: %s", msgTag, msg.data());
   mStatusMessages.append(new UtlString(buf));
}

/// Save and log a version mismatch message
void SipxProcess::logVersionMismatch(UtlString& swversion, UtlString& cfgversion)
{
   UtlString tmpMsg;
   char buf [1024];
   snprintf(buf, sizeof(buf), "software '%s' != config '%s'", swversion.data(), cfgversion.data());
   tmpMsg = buf;
   addStatusMessage(versionMismatchTag, tmpMsg);
   OsSysLog::add(FAC_SUPERVISOR, PRI_WARNING, "SipxProcess[%s]::logVersionMismatch: %s",
                 data(), tmpMsg.data());
}

/// Save and log a missing resource message
void SipxProcess::logMissingResource(UtlString& resource)
{
   addStatusMessage(resourceMissingTag, resource);
   OsSysLog::add(FAC_SUPERVISOR, PRI_WARNING, "SipxProcess[%s]::logMissingResource: %s",
                 data(), resource.data());
}

/// Save and log a command output message
void SipxProcess::logCommandOutput(OsSysLogPriority pri, UtlString& msg)
{
   OsLock mutex(mLock);
   
   UtlString msgTag;
   if ( pri == PRI_ERR )
   {
      msgTag = stderrMsgTag;
      msgTag.appendNumber(++mNumStderrMsgs);
   }
   else
   {
      msgTag = stdoutMsgTag;
      msgTag.appendNumber(++mNumStdoutMsgs);
   }
   addStatusMessage(msgTag, msg);
   OsSysLog::add(FAC_SUPERVISOR, pri, "SipxProcess[%s]::commandOutput '%s'",
                 data(), msg.data());
}

/// destructor
SipxProcess::~SipxProcess()
{
   OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG,
                 "~SipxProcess %s in state %s", data(), GetCurrentState()->name());

   if (mbTaskRunning)
   {
      if ( (GetCurrentState() != pShuttingDown) && (GetCurrentState() != pShutDown))
      {
         shutdown();
         yield(); // let task process the shutdown request
      }

      // give the task 10 seconds to shut down normally
      int secsToWait = 10;
      while ( (GetCurrentState() != pShutDown) && secsToWait--)
      {
         OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "~SipxProcess %s, waiting for shutdown", data());
         delay(1000);
      }

      waitUntilShutDown();
   }
   
   OsLock mutex(mLock);
   if (mConfigtest)
   {
      delete mConfigtest;
   }
   if (mConfigtestStandalone)
   {
      delete mConfigtestStandalone;
   }
   if (mStart)
   {
      delete mStart;
   }
   if (mStop)
   {
      delete mStop;
   }
   if (mReconfigure)
   {
      delete mReconfigure;
   }

   clearStatusMessages();
   mRequiredResources.removeAll();
   delete mpTimer;
   delete mpTimeoutCallback;
};


//////////////////////////////////////////////////////////////////////////////
SipxProcessMsg::SipxProcessMsg(EventSubType eventSubType,
                               const SipxProcessCmd* cmd,
                               int rc,
                               const UtlString& message
                               ):
   OsMsg( OS_EVENT, eventSubType ),
   mCmd( cmd ),
   mIntData( rc ),
   mMessage( message )
{
}

// deep copy of alarm and parameters
SipxProcessMsg::SipxProcessMsg( const SipxProcessMsg& rhs) :
   OsMsg( OS_EVENT, rhs.getMsgSubType() ),
   mCmd( rhs.getCmd() ),
   mIntData( rhs.getIntData() ),
   mMessage( rhs.getMessage() )
{
}

SipxProcessMsg::~SipxProcessMsg()
{
}

OsMsg* SipxProcessMsg::createCopy( void ) const
{  
   return new SipxProcessMsg( *this );
}
