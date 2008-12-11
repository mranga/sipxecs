//
// Copyright (C) 2008 Pingtel Corp., certain elements licensed under a Contributor Agreement.  
// Contributors retain copyright to elements licensed under a Contributor Agreement.
// Licensed to the User under the LGPL license.
//
//////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES
// APPLICATION INCLUDES
// 
#include <unistd.h>
#include <sys/stat.h>
#include "utl/UtlBool.h"
#include "utl/UtlHashBag.h"
#include "utl/UtlHashBagIterator.h"
#include "utl/UtlHashMapIterator.h" 
#include "utl/UtlSListIterator.h" 
#include "utl/UtlTokenizer.h"
#include "os/OsSysLog.h"
#include "os/OsFileSystem.h"
#include "os/OsProcess.h"
#include "os/OsProcessIterator.h"
#include "net/XmlRpcDispatch.h"
#include "net/XmlRpcMethod.h"
#include "net/XmlRpcRequest.h"
#include "net/NetBase64Codec.h"

#include "sipXecsService/SipXecsService.h"
#include "SipxRpc.h"
#include "SwAdminRpc.h"

// DEFINES
// EXTERNAL FUNCTIONS
// EXTERNAL VARIABLES
// CONSTANTS
//
//
const char* SwAdminExec = "sipx-swadmin.py";
const char* SwAdminCheckUpdate_cmd = "check-update";
const char* SwAdminUpdate_cmd = "update";
const char* SwAdminVersion_cmd = "version";
const char* SwAdminStdOut_filetype = ".output";
const char* SwAdminStdErr_filetype = ".err";

// STATIC VARIABLE INITIALIZATIONS

/*************************************************************************
 **** SwAdminRpcMethod contains common code for SwAdminRpc methods
 ************************************************************************/
const char* SwAdminRpcMethod::METHOD_NAME = "SwAdminRpc.BASE";
const char* SwAdminRpcMethod::PARAM_NAME_CALLING_HOST = "callingHostname";
const char* SwAdminRpcMethod::PARAM_NAME_COMMAND = "swCommand";


XmlRpcMethod* SwAdminRpcMethod::get()
{
   assert(false);  // this should have been overridden in the subclass

   return NULL;
}

void SwAdminRpcMethod::registerSelf(SipxRpc & sipxRpcImpl)
{
   assert(false);  // this should have been overridden in the subclass
}

SwAdminRpcMethod::SwAdminRpcMethod()
{
}

void SwAdminRpcMethod::registerMethod(const char*       methodName,
                                   XmlRpcMethod::Get getMethod,
                                   SipxRpc &         sipxRpcImpl
                                   )
{
   sipxRpcImpl.getXmlRpcDispatch()->addMethod(methodName, getMethod, &sipxRpcImpl );
}

bool SwAdminRpcMethod::execute(const HttpRequestContext& requestContext,
                            UtlSList&                 params,
                            void*                     userData,
                            XmlRpcResponse&           response,
                            ExecutionStatus&          status
                            )
{
   assert(false); // this should have been overridden in the subclass

   return false;
}

bool SwAdminRpcMethod::validCaller(const HttpRequestContext& requestContext,
                                const UtlString&          peerName,
                                XmlRpcResponse&           response,
                                const SipxRpc&            sipxRpcImpl,
                                const char*               callingMethod
                                )
{
   bool result = false;

   if (!peerName.isNull() && requestContext.isTrustedPeer(peerName))
   {
      // ssl says the connection is from the named host
      if (sipxRpcImpl.isAllowedPeer(peerName))
      {
         // sipXsupervisor says it is one of the allowed peers.
         result = true;
         OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG,
                       "SwAdminRpcMethod::validCaller '%s' peer authenticated for %s",
                       peerName.data(), callingMethod
                       );
      }
      else
      {
         // this peer is authenticated, but not configured, so provide a good error response
         UtlString faultMsg;
         faultMsg.append("Unconfigured calling host '");
         faultMsg.append(peerName);
         faultMsg.append("'");
         response.setFault(SwAdminRpcMethod::UnconfiguredPeer, faultMsg.data());
            
         OsSysLog::add(FAC_SUPERVISOR, PRI_ERR,
                       "%s failed - '%s' not a configured peer",
                       callingMethod, peerName.data()
                       );
      }
   }
   else
   {
      // ssl says not authenticated - provide only a generic error
      response.setFault(XmlRpcResponse::AuthenticationRequired, "TLS Peer Authentication Failure");
            
      OsSysLog::add(FAC_SUPERVISOR, PRI_ERR,
                    "%s failed: '%s' failed SSL authentication",
                    callingMethod, peerName.data()
                    );
   }

   return result;
}

void SwAdminRpcMethod::handleMissingExecuteParam(const char* methodName,
                                              const char* paramName,
                                              XmlRpcResponse& response,
                                              ExecutionStatus& status)
{
   UtlString faultMsg;
   faultMsg += methodName;
   faultMsg += " '";
   faultMsg += paramName;
   faultMsg += "' parameter is missing or invalid type";
   status = XmlRpcMethod::FAILED;
   response.setFault(SwAdminRpcMethod::InvalidParameter, faultMsg);
   OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, faultMsg);
}

void SwAdminRpcMethod::handleExtraExecuteParam(const char* methodName,
                                            XmlRpcResponse& response,
                                            ExecutionStatus& status)
{
   UtlString faultMsg;
   faultMsg += methodName;
   faultMsg += " has incorrect number of parameters";
   status = XmlRpcMethod::FAILED;
   response.setFault(SwAdminRpcMethod::InvalidParameter, faultMsg);
   OsSysLog::add(FAC_SUPERVISOR, PRI_ERR, faultMsg);
}

bool SwAdminRpcMethod::duplicateProcess(const char*     command,
                                        XmlRpcResponse& response,
                                        ExecutionStatus& status
                                       )
{
   bool result = false;
   OsProcessIterator ProcessIterator;
   OsProcess runningProcess;
   OsProcessInfo procInfo;
   OsStatus procStatus;

   OsStatus iter_status = ProcessIterator.findFirst( runningProcess );
   while ( (iter_status == OS_SUCCESS) && (!result) ) 
   {
      procStatus = runningProcess.getInfo(procInfo);
      if (procStatus == OS_SUCCESS)
      {
         procInfo.name.data(), procInfo.commandline.data();
         if (procInfo.commandline.contains(command))
         {
            // process already found running.  set found to true.
            UtlString faultMsg;

            faultMsg.append("Duplicate process '");
            faultMsg.append(command);
            faultMsg.append("' found");
            response.setFault(SwAdminRpcMethod::DuplicateInstance, faultMsg.data());
            status = XmlRpcMethod::FAILED;
            result = true;
         }
      }
      iter_status = ProcessIterator.findNext(runningProcess);
   }

   return result;
}

bool SwAdminRpcMethod::buildOutputFiles(const UtlString&     command,
                         UtlString&      stdoutfn,
                         UtlString&      stderrfn
                        )
{
   bool result = true;
   stdoutfn.remove(0);
   stderrfn.remove(0);
   if ( command.compareTo(SwAdminVersion_cmd, UtlString::ignoreCase) == 0) 
   {
       stdoutfn.append(SwAdminVersion_cmd);
       stderrfn.append(SwAdminVersion_cmd);
   } 
   else
      if ( command.compareTo(SwAdminCheckUpdate_cmd, UtlString::ignoreCase) == 0) 
      {
         stdoutfn.append(SwAdminCheckUpdate_cmd);
         stderrfn.append(SwAdminCheckUpdate_cmd);
      }      
      else
         if ( command.compareTo(SwAdminUpdate_cmd, UtlString::ignoreCase) == 0) 
         {
            stdoutfn.append(SwAdminUpdate_cmd);
            stderrfn.append(SwAdminUpdate_cmd);
         }   
         else
         {
            return false;
         }
  
   stderrfn.append(SwAdminStdErr_filetype);
   stdoutfn.append(SwAdminStdOut_filetype);

   return result;
}

/*****************************************************************
 **** SwAdminRpcExec
 *****************************************************************/

const char* SwAdminRpcExec::METHOD_NAME = "SwAdminRpc.exec";

const char* SwAdminRpcExec::name()
{
   return METHOD_NAME;
}

SwAdminRpcExec::SwAdminRpcExec()
{
}

XmlRpcMethod* SwAdminRpcExec::get()
{
   return new SwAdminRpcExec();
}

void SwAdminRpcExec::registerSelf(SipxRpc & sipxRpcImpl)
{
   registerMethod(METHOD_NAME, SwAdminRpcExec::get, sipxRpcImpl);
}

bool SwAdminRpcExec::execute(const HttpRequestContext& requestContext,
                                 UtlSList&                 params,
                                 void*                     userData,
                                 XmlRpcResponse&           response,
                                 ExecutionStatus&          status)
{

   bool result = false;
   status = XmlRpcMethod::FAILED;

   if (2 != params.entries())
   {
      handleExtraExecuteParam(name(), response, status);
   }
   else
   {
      if (!params.at(0) || !params.at(0)->isInstanceOf(UtlString::TYPE))
      {
         handleMissingExecuteParam(name(), PARAM_NAME_CALLING_HOST, response, status);
      }
      else
      {
         UtlString* pCallingHostname = dynamic_cast<UtlString*>(params.at(0));
         SipxRpc* pSipxRpcImpl = ((SipxRpc *)userData);

         if (!params.at(1) || !params.at(1)->isInstanceOf(UtlString::TYPE))
         {
            handleMissingExecuteParam(name(), PARAM_NAME_COMMAND, response, status);
         }
         else
         {        
            if(validCaller(requestContext, *pCallingHostname, response, *pSipxRpcImpl, name()))
            {
               UtlBool   method_result(true);
               UtlString arguments[3];
               OsPath    mWorkingDirectory = ".";
               OsPath    mExec = "sudo";
               UtlString mStdOutFile;
               UtlString mStdErrFile;

               UtlString* pSubCommand = dynamic_cast<UtlString*>(params.at(1));

               if ( !buildOutputFiles(*pSubCommand, mStdOutFile, mStdErrFile))
               {
                    // Invalid request. Set a Fault.
                    response.setFault(SwAdminRpcMethod::FailureToLaunch, "Invalid command");
                    status = XmlRpcMethod::FAILED;
                    return result;
               }

               OsPath    mStdOutPath = SipXecsService::Path(SipXecsService::TmpDirType, mStdOutFile.data());
               OsPath    mStdErrPath = SipXecsService::Path(SipXecsService::TmpDirType, mStdErrFile.data());

               arguments[0] = SipXecsService::Path(SipXecsService::BinDirType, SwAdminExec).data();
               arguments[1] = pSubCommand->data();
               arguments[2] = NULL;

               // Make sure that there is no other instance running.
               if (! duplicateProcess(arguments[0], response, status))
               {
                  // execute the command and return whether or not the launch was successful.
                  OsProcess* swCheck = new OsProcess();

                  // Setup the Standard Output and Standard Error files.
                  OsPath mStdInFile;   // Blank
                  int rc;
                  rc = swCheck->setIORedirect(mStdInFile, mStdOutPath, mStdErrPath); 
            
                  // Launch the process but tell the parent to ignore the child's signals (especially on shutdown).  
                  // It will let the system handle it to avoid a defunct process.
                  if ( (rc=swCheck->launch(mExec, &arguments[0], mWorkingDirectory,
                                   swCheck->NormalPriorityClass, FALSE,
                                   TRUE)) // Parent to ignore child signals.
                           == OS_SUCCESS )
                  {
                      // Construct and set the response.
                      response.setResponse(&method_result);
                      status = XmlRpcMethod::OK;
                      result = true;
                  }   // launch
                  else
                  {
                     // Failed to launch the command, send a fault.
                     response.setFault(SwAdminRpcMethod::FailureToLaunch, "Failure to launch command");
                     status = XmlRpcMethod::FAILED;
                  }

                  delete swCheck;
               }  // duplicateProcess
            }  // validcaller
            else
            {
               status = XmlRpcMethod::FAILED;
            }
         }  // param 1 okay
      }  // param 0 okay
   } //number of parms check

   return result;
}

 

/*****************************************************************
 **** SwAdminRpcGetResult
 *****************************************************************/

const char* SwAdminRpcGetResult::METHOD_NAME = "SwAdminRpc.getResult";

const char* SwAdminRpcGetResult::name()
{
   return METHOD_NAME;
}

SwAdminRpcGetResult::SwAdminRpcGetResult()
{
}

XmlRpcMethod* SwAdminRpcGetResult::get()
{
   return new SwAdminRpcGetResult();
}

void SwAdminRpcGetResult::registerSelf(SipxRpc & sipxRpcImpl)
{
   registerMethod(METHOD_NAME, SwAdminRpcGetResult::get, sipxRpcImpl);
}

bool SwAdminRpcGetResult::execute(const HttpRequestContext& requestContext,
                                 UtlSList&                 params,
                                 void*                     userData,
                                 XmlRpcResponse&           response,
                                 ExecutionStatus&          status)
{

   bool result = false;
   status = XmlRpcMethod::FAILED;

   if (2 != params.entries())
   {
      handleExtraExecuteParam(name(), response, status);
   }
   else
   {
      if (!params.at(0) || !params.at(0)->isInstanceOf(UtlString::TYPE))
      {
         handleMissingExecuteParam(name(), PARAM_NAME_CALLING_HOST, response, status);
      }
      else
      {
         UtlString* pCallingHostname = dynamic_cast<UtlString*>(params.at(0));
         SipxRpc* pSipxRpcImpl = ((SipxRpc *)userData);

         if (!params.at(1) || !params.at(1)->isInstanceOf(UtlString::TYPE))
         {
            handleMissingExecuteParam(name(), PARAM_NAME_COMMAND, response, status);
         }
         else
         {        
            if(validCaller(requestContext, *pCallingHostname, response, *pSipxRpcImpl, name()))
            {
               UtlString  mStdOutFile;
               UtlString  mStdErrFile;
               UtlString* pSubCommand = dynamic_cast<UtlString*>(params.at(1));
            
               if ( !buildOutputFiles(*pSubCommand, mStdOutFile, mStdErrFile))
               {
                  // Invalid request. Set a Fault.
                  response.setFault(SwAdminRpcMethod::FailureToLaunch, "Invalid command");
                  status = XmlRpcMethod::FAILED;
                  return result;
               }

               OsPath    mStdOutPath = SipXecsService::Path(SipXecsService::TmpDirType, mStdOutFile.data());
               OsPath    mStdErrPath = SipXecsService::Path(SipXecsService::TmpDirType, mStdErrFile.data());

               OsFile resultStdOutFile(mStdOutPath);
               OsFile resultStdErrFile(mStdErrPath);
               OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "StdOutfile = %s", mStdOutPath.data());
               OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "StdErrfile = %s", mStdErrPath.data());
               UtlSList encodedResults;
               if (!resultStdOutFile.exists() || !resultStdErrFile.exists())
               {
                  // Result files don't exist.  Return a fault
                  response.setFault(SwAdminRpcMethod::FailureToLaunch, "Results not found");
                  status = XmlRpcMethod::FAILED;
               }
               else
               {
                  // Open the stdout file, read it all into a UtlString and encode it base 64
                  UtlString fileContent;
                  UtlString encodedOutFile;
                  UtlString encodedErrFile;
                  UtlString errorMsg;
                  if (readResultFile(resultStdOutFile, fileContent))
                  {
                     OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "StdOutfile = %s", fileContent.data());
                     if (encodeOutputFile(fileContent, encodedOutFile, errorMsg))
                     {
                        encodedResults.insert(&encodedOutFile);

                        // Now do the stderr file.
                        if (readResultFile(resultStdErrFile, fileContent))
                        {
                           if (encodeOutputFile(fileContent, encodedErrFile, errorMsg))
                           {
                              encodedResults.insert(&encodedErrFile);
                              result = true;
                              response.setResponse(&encodedResults);
                              status = XmlRpcMethod::OK;
                           }
                           else
                           {
                              // failed to encode file content
                              response.setFault(SwAdminRpcMethod::FailureToEncode, errorMsg.data());
                              status = XmlRpcMethod::FAILED;
                           }
                        }
                        else
                        {
                           response.setFault(SwAdminRpcMethod::FailureToEncode, "Results not found");
                           status = XmlRpcMethod::FAILED;
                        }
                     }
                     else
                     {
                        // failed to encode file content
                        response.setFault(SwAdminRpcMethod::FailureToEncode, errorMsg.data());
                        status = XmlRpcMethod::FAILED;
                     }
                  }
                  else
                  {
                     response.setFault(SwAdminRpcMethod::FailureToEncode, "Results not found");
                     status = XmlRpcMethod::FAILED;
                  }
               }
            }
            else
            {
               // Caller is not valid.  Return a fault.
               status = XmlRpcMethod::FAILED;
            }
         }  // param 1 okay
      }  // param 0 okay
   } //number of parms check

   return result;
}

bool SwAdminRpcGetResult::encodeOutputFile(UtlString&  input_content,
                                          UtlString&  encoded_output,
                                          UtlString& errorMsg
                                       )
{
   bool   result = true;
  
   encoded_output.remove(0);
   errorMsg.remove(0);

   if ( input_content.length() == 0 ) 
   {
      input_content.append("\n");
   }

   OsSysLog::add(FAC_SUPERVISOR, PRI_DEBUG, "SwAdminRpcGetResult::encodeOutputFile %s",
                 input_content.data());
   NetBase64Codec::encode( input_content, encoded_output );

   if ( encoded_output.length() == 0 )
   {
         // Encode failed.
         result = false;
         errorMsg.append("Failed to encode output data in base64");
         OsSysLog::add(FAC_SUPERVISOR, PRI_INFO, "SwAdminRpcGetResult::encodeOutputFile %s",
                       errorMsg.data());
   }
   return result;
}

bool SwAdminRpcGetResult::readResultFile( OsFile& result_file,
                                          UtlString&  output_std_content
                                        )
{
   bool   result = true;
  
   output_std_content.remove(0);

   if (result_file.open(OsFile::READ_ONLY) != OS_SUCCESS)
   {
      result = false;
   }
   else
   {
      size_t flength;
      if (result_file.getLength(flength) == OS_SUCCESS)
      {
         // File is open for reading.  Read in the entire file.
         UtlString readbuf;
         size_t bytesRead = 0;

         readbuf.capacity(flength+50);
         if (result_file.read( (char*) readbuf.data(), flength, bytesRead) == OS_SUCCESS)
         {
            if (bytesRead)
            {

               readbuf.setLength(bytesRead);
               output_std_content.append(readbuf.data());
            }
            else
            {
               result = false;
            }
         }
         else
         {
            result = false;
         }
      }
      result_file.close();
   }
   return result;
}
