// 
// 
// Copyright (C) 2007 Pingtel Corp., certain elements licensed under a Contributor Agreement.  
// Contributors retain copyright to elements licensed under a Contributor Agreement.
// Licensed to the User under the LGPL license.
// 
// $$
//////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES
//#include <...>

// APPLICATION INCLUDES
#include "os/OsLock.h"
#include "os/OsDateTime.h"
#include "os/OsFS.h"
#include "os/OsSysLog.h"
#include "net/Url.h"

#include "xmlparser/tinyxml.h"
#include "fastdb/fastdb.h"
#include "sipXecsService/SipXecsService.h"
#include "sipdb/SIPDBManager.h"
#include "sipdb/ResultSet.h"
#include "sipdb/ExtensionRow.h"
#include "sipdb/ExtensionDB.h"

REGISTER ( ExtensionRow );

// Static Initializers
ExtensionDB* ExtensionDB::spInstance = NULL;
OsMutex  ExtensionDB::sLockMutex (OsMutex::Q_FIFO);
UtlString ExtensionDB::gUriKey("uri");
UtlString ExtensionDB::gExtensionKey("extension");

// The 'type' attribute of the top-level 'items' element.
const UtlString ExtensionDB::sType("extension");

// The XML namespace of the top-level 'items' element.
const UtlString ExtensionDB::sXmlNamespace("http://www.sipfoundry.org/sipX/schema/xml/extension-00-00");

/* ============================ CREATORS ================================== */

ExtensionDB::ExtensionDB( const UtlString& name ) : 
    mDatabaseName( name )
{
    // Access the shared table databse
    SIPDBManager* pSIPDBManager = SIPDBManager::getInstance();
    m_pFastDB = pSIPDBManager->getDatabase(name);

    // If we are the first process to attach
    // then we need to load the DB
    int users = pSIPDBManager->getNumDatabaseProcesses(name);
    if ( users == 1 || ( users > 1 && mTableLoaded == false ) )
    {
        mTableLoaded = false;
        // Load the file implicitly
        if (this->load() == OS_SUCCESS)
        {
           mTableLoaded = true;
        }
    }
}

ExtensionDB::~ExtensionDB()
{
    OsSysLog::add(FAC_DB, PRI_DEBUG, "<><>## ExtensionDB:: DESTRUCTOR");
}

/* ============================ MANIPULATORS ============================== */

void
ExtensionDB::releaseInstance()
{
    OsSysLog::add(FAC_DB, PRI_DEBUG, "<><>## ExtensionDB:: releaseInstance() spInstance=%p", spInstance);

    // Critical Section here
    OsLock lock( sLockMutex );

    // if it exists, delete the object and NULL out the pointer
    if (spInstance != NULL) {

        // unregister this table/process from the IMDB
        SIPDBManager::getInstance()->removeDatabase ( spInstance->mDatabaseName );

        // NULL out the fastDB pointer also
        spInstance->m_pFastDB = NULL;

        delete spInstance;
        spInstance = NULL;
    }
}

OsStatus
ExtensionDB::load()
{
    // Critical Section here
    OsLock lock( sLockMutex );
    OsStatus result = OS_SUCCESS;

    if ( m_pFastDB != NULL ) 
    {
        // Clean out the existing DB rows before loading
        // a new set from persistent storage
        removeAllRows ();

        UtlString fileName = OsPath::separator + mDatabaseName + ".xml";
        UtlString pathName = SipXecsService::Path(SipXecsService::DatabaseDirType,
                                                  fileName.data());

        OsSysLog::add(FAC_DB, PRI_DEBUG, "ExtensionDB::load loading \"%s\"",
                    pathName.data());

        TiXmlDocument doc ( pathName );

        // Verify that we can load the file (i.e it must exist)
        if( doc.LoadFile() )
        {
            // the checksum is used to determine if the db changed between reloads
            int loadChecksum = 0;
            TiXmlNode * rootNode = doc.FirstChild ("items");
            if (rootNode != NULL)
            {
                // the folder node contains at least the name/displayname/
                // and autodelete elements, it may contain others
                for( TiXmlNode *itemNode = rootNode->FirstChild( "item" );
                     itemNode; 
                     itemNode = itemNode->NextSibling( "item" ) )
                {
                    // Create a hash dictionary for element attributes
                    UtlHashMap nvPairs;

                    for( TiXmlNode *elementNode = itemNode->FirstChild();
                         elementNode; 
                         elementNode = elementNode->NextSibling() )
                    {
                        // Bypass comments and other element types only interested
                        // in parsing element attributes
                        if ( elementNode->Type() == TiXmlNode::ELEMENT )
                        {
                            UtlString elementName = elementNode->Value();
                            UtlString elementValue;

                            result = SIPDBManager::getAttributeValue (
                                *itemNode, elementName, elementValue);

                            // update the load checksum
                            loadChecksum += ( elementName.hash() + elementValue.hash() );
                            if (result == OS_SUCCESS)
                            {
                                UtlString* collectableKey = 
                                    new UtlString( elementName ); 
                                UtlString* collectableValue = 
                                    new UtlString( elementValue ); 
                                nvPairs.insertKeyAndValue ( 
                                    collectableKey, collectableValue );
                            } else if ( elementNode->FirstChild() == NULL )
                            {
                                // NULL Element value create a special 
                                // char string we have key and value so insert
                                UtlString* collectableKey = 
                                    new UtlString( elementName ); 
                                UtlString* collectableValue = 
                                    new UtlString( SPECIAL_IMDB_NULL_VALUE ); 
                                nvPairs.insertKeyAndValue ( 
                                    collectableKey, collectableValue );
                            }
                        }
                    }
                    // Insert the item row into the IMDB
                    insertRow ( nvPairs );
                }
            }
        } else 
        {
            OsSysLog::add(FAC_SIP, PRI_WARNING, "ExtensionDB::load failed to load \"%s\"",
                    pathName.data());
            result = OS_FAILED;
        }
    } else
    {
        result = OS_FAILED;
    }
    return result;
}

OsStatus
ExtensionDB::store()
{
    // Critical Section here
    OsLock lock( sLockMutex );
    OsStatus result = OS_SUCCESS;

    if ( m_pFastDB != NULL)
    {
        UtlString fileName = OsPath::separator + mDatabaseName + ".xml";
        UtlString pathName = SipXecsService::Path(SipXecsService::DatabaseDirType,
                                                  fileName.data());

        // Create an empty document
        TiXmlDocument document;

        // Create a hard coded standalone declaration section
        document.Parse ("<?xml version=\"1.0\" standalone=\"yes\"?>");

        // Create the root node container
        TiXmlElement itemsElement ( "items" );
        itemsElement.SetAttribute( "type", sType.data() );
        itemsElement.SetAttribute( "xmlns", sXmlNamespace.data() );

        // Thread Local Storage
        m_pFastDB->attach();

        // Search our memory for rows
        dbCursor< ExtensionRow > cursor;

        // Select everything in the IMDB and add as item elements if present
        if ( cursor.select() > 0 )
        {
            // metadata contains column names
            dbTableDescriptor* pTableMetaData = &ExtensionRow::dbDescriptor;

            do {
                // Create an item container
                TiXmlElement itemElement ("item");

                byte* base = (byte*)cursor.get();

                // Add the column name value pairs
                for ( dbFieldDescriptor* fd = pTableMetaData->getFirstField();
                      fd != NULL; fd = fd->nextField ) 
                {
                    // if the column name does not contain the 
                    // np_prefix we must_presist it
                    if ( strstr( fd->name, "np_" ) == NULL )
                    {
                        // Create the a column element named after the IMDB column name
                        TiXmlElement element (fd->name );

                        // See if the IMDB has the predefined SPECIAL_NULL_VALUE
                        UtlString textValue;
                        SIPDBManager::getFieldValue(base, fd, textValue);

                        // If the value is not null append a text child element
                        if ( textValue != SPECIAL_IMDB_NULL_VALUE ) 
                        {
                            // Text type assumed here... @todo change this
                            TiXmlText value ( textValue.data() );
                            // Store the column value in the element making this
                            // <colname>coltextvalue</colname>
                            element.InsertEndChild  ( value );
                        }

                        // Store this in the item tag as follows
                        // <item>
                        // .. <col1name>col1textvalue</col1name>
                        // .. <col2name>col2textvalue</col2name>
                        // .... etc
                        itemElement.InsertEndChild  ( element );
                    }
                }
                // add the line to the element
                itemsElement.InsertEndChild ( itemElement );
            } while ( cursor.next() );
        }  
        // Attach the root node to the document
        document.InsertEndChild ( itemsElement );
        document.SaveFile ( pathName );
        // Commit rows to memory - multiprocess workaround
        m_pFastDB->detach(0);
        mTableLoaded = true;
    } else
    {
        result = OS_FAILED;
    }
    return result;
}

UtlBoolean
ExtensionDB::insertRow (const UtlHashMap& nvPairs) 
{
    // Note we do not need the identity object here
    // as it is inferred from the uri
    return insertRow (
        Url( *((UtlString*)nvPairs.findValue(&gUriKey)) ),
        *((UtlString*)nvPairs.findValue(&gExtensionKey)) );
}

UtlBoolean
ExtensionDB::insertRow (
    const Url& uri,
    const UtlString& extension )
{
    UtlBoolean result = FALSE;

    UtlString identity;
    uri.getIdentity(identity);
    if ( !identity.isNull() && (m_pFastDB != NULL) )
    {
        // Thread Local Storage
        m_pFastDB->attach();

        // Search for a matching row before deciding to update or insert
        dbCursor< ExtensionRow > cursor(dbCursorForUpdate);
        ExtensionRow row;

        // see if this in an insert or update
        dbQuery query;

        query="np_identity=",identity;

        if ( cursor.select( query ) > 0 )
        {
            // already in DB so update the extension
            // the uri should remain the same as the identity
            // part has'nt changed
            do {
                cursor->extension = extension;
                cursor.update();
            } while ( cursor.next() );
        } else // Insert as the row does not exist
        {
            UtlString uriStr;
            uri.toString(uriStr);
            // Fill out the row columns
            row.np_identity = identity;
            row.uri = uriStr;
            row.extension = extension;
            insert (row);
        }
        // Commit rows to memory - multiprocess workaround
        m_pFastDB->detach(0);

        // Table Data changed
        SIPDBManager::getInstance()->
            setDatabaseChangedFlag(mDatabaseName, TRUE);
        result = TRUE;
    }
    return result;
}

UtlBoolean
ExtensionDB::removeRow ( const Url& uri )
{
    UtlBoolean removed = FALSE;
    UtlString identity;
    uri.getIdentity(identity);

    if ( !identity.isNull() && (m_pFastDB != NULL) )
    {
        // Thread Local Storage
        m_pFastDB->attach();

        dbCursor< ExtensionRow > cursor(dbCursorForUpdate);

        dbQuery query;
        query="np_identity=",identity;
        if ( cursor.select( query ) > 0 )
        {
            cursor.removeAllSelected();
            removed = TRUE;
        }
        // Commit rows to memory - multiprocess workaround
        m_pFastDB->detach(0);

        // Table Data changed
        SIPDBManager::getInstance()->
            setDatabaseChangedFlag(mDatabaseName, TRUE);
    }
    return removed;
}

void
ExtensionDB::removeAllRows ()
{
    if ( m_pFastDB != NULL )
    {
        // Thread Local Storage
        m_pFastDB->attach();

        dbCursor< ExtensionRow > cursor(dbCursorForUpdate);

        if (cursor.select() > 0)
        {
            cursor.removeAllSelected();
        }
        // Commit rows to memory - multiprocess workaround
        m_pFastDB->detach(0);

        // Table Data changed
        SIPDBManager::getInstance()->
            setDatabaseChangedFlag(mDatabaseName, TRUE);
    }
}

void
ExtensionDB::getAllRows(ResultSet& rResultSet) const
{
    // Clear the out any previous records
    rResultSet.destroyAll();

    if ( m_pFastDB != NULL )
    {
        // must do this first to ensure process/tls integrity
        m_pFastDB->attach();

        dbCursor< ExtensionRow > cursor;
        if ( cursor.select() > 0 )
        {
            do {
                UtlHashMap record;
                UtlString* uriValue = 
                    new UtlString ( cursor->uri );
                UtlString* extensionValue = 
                    new UtlString ( cursor->extension );

                // Memory Leak fixes, make shallow copies of static keys
                UtlString* uriKey = new UtlString( gUriKey );
                UtlString* extensionKey = new UtlString( gExtensionKey );

                record.insertKeyAndValue ( 
                    uriKey, uriValue );
                record.insertKeyAndValue ( 
                    extensionKey, extensionValue );

                rResultSet.addValue(record);
            } while (cursor.next());
        }
        // commit rows and also ensure process/tls integrity
        m_pFastDB->detach(0);
    }
}

UtlBoolean
ExtensionDB::getExtension (
    const Url& uri,
    UtlString& rExtesnion ) const
{
    UtlBoolean found = FALSE;

    UtlString identity;
    uri.getIdentity(identity);

    if ( !identity.isNull() && (m_pFastDB != NULL) )
    {
        // Thread Local Storage
        m_pFastDB->attach();

        dbQuery query;

        // Primary Key is the uriExtension's identity
        query="np_identity=",identity;

        // Search to see if we have a Credential Row
        dbCursor< ExtensionRow > cursor;

        if ( cursor.select(query) > 0 )
        {
            // should only be row
            do {
                rExtesnion = cursor->extension;
            } while ( cursor.next() );
            found = TRUE;
        }
        // Commit the rows to memory - multiprocess workaround
        m_pFastDB->detach(0);
    }
    return found;
}

UtlBoolean 
ExtensionDB::getUri (
    const UtlString& extension,
    Url& rUri ) const
{
    UtlBoolean found = FALSE;
    if ( !extension.isNull() && (m_pFastDB != NULL) )
    {
        // Thread Local Storage
        m_pFastDB->attach();

        dbQuery query;

        // Primary Key is the uriExtension's identity
        query="extension=",extension;

        // Search to see if we have a Credential Row
        dbCursor< ExtensionRow > cursor;

        // new style extensions have a domain specifier
        // we can only return true here if we have one unique
        // row, if there are > 1 rows then we have the same extension
        // used in multiple domains
        if ( cursor.select(query) == 1 )
        {
            // should only be row
            do {
                // The serialized vrsion of the uri
                // is compatible with the Uri constructor
                rUri = cursor->uri;
            } while ( cursor.next() );
            found = TRUE;
        }
        // Commit the rows to memory - multiprocess workaround
        m_pFastDB->detach(0);
    }
    return found;
}

bool
ExtensionDB::isLoaded()
{
   return mTableLoaded;
}

ExtensionDB*
ExtensionDB::getInstance( const UtlString& name )
{
    // Critical Section here
    OsLock lock( sLockMutex );

    // See if this is the first time through for this process
    // Note that this being null => pgDatabase is also null
    if ( spInstance == NULL )
    {
        // Create the singleton class for clients to use
        spInstance = new ExtensionDB( name );
    }
    return spInstance;
}


