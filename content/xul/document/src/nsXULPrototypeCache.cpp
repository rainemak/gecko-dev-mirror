/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: NPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is 
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Chris Waterson <waterson@netscape.com>
 *   Brendan Eich <brendan@mozilla.org>
 *   Ben Goodger <ben@netscape.com>
 *
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the NPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the NPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/*



 */

#include "nsCOMPtr.h"
#include "nsXPIDLString.h"
#include "nsICSSStyleSheet.h"
#include "nsIXULPrototypeCache.h"
#include "nsIXULPrototypeDocument.h"
#include "nsIXULDocument.h"
#include "nsIURI.h"
#include "nsIURL.h"
#include "nsHashtable.h"
#include "nsXPIDLString.h"
#include "plstr.h"
#include "nsIDocument.h"
#include "nsIXBLDocumentInfo.h"
#include "nsIPref.h"
#include "nsIServiceManager.h"
#include "nsXULDocument.h"
#include "nsIJSRuntimeService.h"
#include "jsapi.h"

#include "nsIFastLoadService.h"
#include "nsIFastLoadFileControl.h"
#include "nsIFile.h"
#include "nsIObjectInputStream.h"
#include "nsIObjectOutputStream.h"

#include "nsNetUtil.h"
#include "nsAppDirectoryServiceDefs.h"


class nsXULPrototypeCache : public nsIXULPrototypeCache
{
public:
    // nsISupports
    NS_DECL_ISUPPORTS

    NS_IMETHOD GetPrototype(nsIURI* aURI, nsIXULPrototypeDocument** _result);
    NS_IMETHOD PutPrototype(nsIXULPrototypeDocument* aDocument);
    NS_IMETHOD FlushPrototypes();

    NS_IMETHOD GetStyleSheet(nsIURI* aURI, nsICSSStyleSheet** _result);
    NS_IMETHOD PutStyleSheet(nsICSSStyleSheet* aStyleSheet);
    NS_IMETHOD FlushStyleSheets();

    NS_IMETHOD GetScript(nsIURI* aURI, void** aScriptObject);
    NS_IMETHOD PutScript(nsIURI* aURI, void* aScriptObject);
    NS_IMETHOD FlushScripts();

    NS_IMETHOD GetXBLDocumentInfo(const nsCString& aString, nsIXBLDocumentInfo** _result);
    NS_IMETHOD PutXBLDocumentInfo(nsIXBLDocumentInfo* aDocumentInfo);
    NS_IMETHOD FlushXBLInformation();
    NS_IMETHOD FlushSkinFiles();

    NS_IMETHOD Flush();

    NS_IMETHOD GetEnabled(PRBool* aIsEnabled);

    NS_IMETHOD AbortFastLoads();
    NS_IMETHOD GetFastLoadService(nsIFastLoadService** aResult);
    NS_IMETHOD RemoveFromFastLoadSet(nsIURI* aDocumentURI);
    NS_IMETHOD WritePrototype(nsIXULPrototypeDocument* aPrototypeDocument);

protected:
    friend NS_IMETHODIMP
    NS_NewXULPrototypeCache(nsISupports* aOuter, REFNSIID aIID, void** aResult);

    nsXULPrototypeCache();
    virtual ~nsXULPrototypeCache();

    static PRBool PR_CALLBACK UnlockJSObjectCallback(nsHashKey* aKey, void* aData, void* aClosure);
    JSRuntime*  GetJSRuntime();

    nsSupportsHashtable mPrototypeTable;
    nsSupportsHashtable mStyleSheetTable;
    nsHashtable         mScriptTable;
    nsSupportsHashtable mXBLDocTable;

    JSRuntime*          mJSRuntime;

    class nsIURIKey : public nsHashKey {
    protected:
        nsCOMPtr<nsIURI> mKey;
  
    public:
        nsIURIKey(nsIURI* key) : mKey(key) {}
        ~nsIURIKey(void) {}
  
        PRUint32 HashCode(void) const {
            nsCAutoString spec;
            mKey->GetSpec(spec);
            return (PRUint32) PL_HashString(spec.get());
        }

        PRBool Equals(const nsHashKey *aKey) const {
            PRBool eq;
            mKey->Equals( ((nsIURIKey*) aKey)->mKey, &eq );
            return eq;
        }

        nsHashKey *Clone(void) const {
            return new nsIURIKey(mKey);
        }
    };


    ///////////////////////////////////////////////////////////////////////////
    // FastLoad
    nsSupportsHashtable mFastLoadURITable;

    static nsIFastLoadService*    gFastLoadService;
    static nsIFile*               gFastLoadFile;

    // Bootstrap FastLoad Service
    nsresult StartFastLoad(nsIURI* aDocumentURI);
    nsresult StartFastLoadingURI(nsIURI* aURI, PRInt32 aDirectionFlags);
};

static PRBool gDisableXULCache = PR_FALSE; // enabled by default
static const char kDisableXULCachePref[] = "nglayout.debug.disable_xul_cache";

//----------------------------------------------------------------------

PR_STATIC_CALLBACK(int)
DisableXULCacheChangedCallback(const char* aPref, void* aClosure)
{
    nsresult rv;

    nsCOMPtr<nsIPref> prefs = do_GetService(NS_PREF_CONTRACTID, &rv);
    if (prefs)
        prefs->GetBoolPref(kDisableXULCachePref, &gDisableXULCache);

    // Flush the cache, regardless
    static NS_DEFINE_CID(kXULPrototypeCacheCID, NS_XULPROTOTYPECACHE_CID);
    nsCOMPtr<nsIXULPrototypeCache> cache =
        do_GetService(kXULPrototypeCacheCID, &rv);

    if (cache)
        cache->Flush();

    return 0;
}

//----------------------------------------------------------------------


nsIFastLoadService*   nsXULPrototypeCache::gFastLoadService = nsnull;
nsIFile*              nsXULPrototypeCache::gFastLoadFile = nsnull;

nsXULPrototypeCache::nsXULPrototypeCache()
    : mJSRuntime(nsnull)
{
    NS_INIT_REFCNT();
}


nsXULPrototypeCache::~nsXULPrototypeCache()
{
    FlushScripts();

    NS_IF_RELEASE(gFastLoadService); // don't need ReleaseService nowadays!
    NS_IF_RELEASE(gFastLoadFile);
}


NS_IMPL_THREADSAFE_ISUPPORTS1(nsXULPrototypeCache, nsIXULPrototypeCache);


NS_IMETHODIMP
NS_NewXULPrototypeCache(nsISupports* aOuter, REFNSIID aIID, void** aResult)
{
    NS_PRECONDITION(! aOuter, "no aggregation");
    if (aOuter)
        return NS_ERROR_NO_AGGREGATION;

    nsXULPrototypeCache* result = new nsXULPrototypeCache();
    if (! result)
        return NS_ERROR_OUT_OF_MEMORY;

    nsresult rv;

    nsCOMPtr<nsIPref> prefs(do_GetService(NS_PREF_CONTRACTID, &rv));
    if (NS_SUCCEEDED(rv)) {
        // XXX Ignore return values.
        prefs->GetBoolPref(kDisableXULCachePref, &gDisableXULCache);
        prefs->RegisterCallback(kDisableXULCachePref, DisableXULCacheChangedCallback, nsnull);
    }

    NS_ADDREF(result);
    rv = result->QueryInterface(aIID, aResult);
    NS_RELEASE(result);

    return rv;
}


//----------------------------------------------------------------------


NS_IMETHODIMP
nsXULPrototypeCache::GetPrototype(nsIURI* aURI, nsIXULPrototypeDocument** _result)
{
    nsresult rv = NS_OK;

    nsIURIKey key(aURI);
    *_result = NS_STATIC_CAST(nsIXULPrototypeDocument*, mPrototypeTable.Get(&key));

    if (! *_result) {
        // No prototype in XUL memory cache. Spin up FastLoad Service and
        // look in FastLoad file.
        rv = StartFastLoad(aURI);
        if (NS_SUCCEEDED(rv)) {
            nsCOMPtr<nsIObjectInputStream> objectInput;
            gFastLoadService->GetInputStream(getter_AddRefs(objectInput));
        
            rv = StartFastLoadingURI(aURI, nsIFastLoadService::NS_FASTLOAD_READ);
            if (NS_SUCCEEDED (rv)) {
                nsCOMPtr<nsIURI> oldURI;
                gFastLoadService->SelectMuxedDocument(aURI, getter_AddRefs(oldURI));

                // Create a new prototype document.
                nsCOMPtr<nsIXULPrototypeDocument> protoDoc;
                rv = NS_NewXULPrototypeDocument(nsnull,
                                                NS_GET_IID(nsIXULPrototypeDocument),
                                                getter_AddRefs(protoDoc));
                if (NS_FAILED(rv)) return rv;

                rv = protoDoc->Read(objectInput);
                if (NS_SUCCEEDED(rv)) {
                    NS_ADDREF(*_result = protoDoc);
                    PutPrototype(protoDoc);
                }

                gFastLoadService->EndMuxedDocument(aURI);

                RemoveFromFastLoadSet(aURI);

            }
        }
    }

    return rv;
}

NS_IMETHODIMP
nsXULPrototypeCache::PutPrototype(nsIXULPrototypeDocument* aDocument)
{
    nsresult rv;
    nsCOMPtr<nsIURI> uri;
    rv = aDocument->GetURI(getter_AddRefs(uri));

    nsIURIKey key(uri);

    // Put() w/o  a third parameter with a destination for the
    // replaced value releases it 
    mPrototypeTable.Put(&key, aDocument);

    return NS_OK;
}

JSRuntime*
nsXULPrototypeCache::GetJSRuntime()
{
    if (!mJSRuntime) {
        nsCOMPtr<nsIJSRuntimeService> rtsvc = do_GetService("@mozilla.org/js/xpc/RuntimeService;1");
        if (rtsvc)
            rtsvc->GetRuntime(&mJSRuntime);
    }

    return mJSRuntime;
}

NS_IMETHODIMP
nsXULPrototypeCache::FlushPrototypes()
{
    mPrototypeTable.Reset();

    // Clear the script cache, as it refers to prototype-owned mJSObjects.
    FlushScripts();
    return NS_OK;
}


NS_IMETHODIMP
nsXULPrototypeCache::GetStyleSheet(nsIURI* aURI, nsICSSStyleSheet** _result)
{
    nsIURIKey key(aURI);
    *_result = NS_STATIC_CAST(nsICSSStyleSheet*, mStyleSheetTable.Get(&key));
    return NS_OK;
}


NS_IMETHODIMP
nsXULPrototypeCache::PutStyleSheet(nsICSSStyleSheet* aStyleSheet)
{
    nsresult rv;
    nsCOMPtr<nsIURI> uri;
    rv = aStyleSheet->GetURL(*getter_AddRefs(uri));

    nsIURIKey key(uri);
    mStyleSheetTable.Put(&key, aStyleSheet);
    
    return NS_OK;
}


NS_IMETHODIMP
nsXULPrototypeCache::FlushStyleSheets()
{
    mStyleSheetTable.Reset();
    return NS_OK;
}


NS_IMETHODIMP
nsXULPrototypeCache::GetScript(nsIURI* aURI, void** aScriptObject)
{
    nsIURIKey key(aURI);
    *aScriptObject = mScriptTable.Get(&key);
    return NS_OK;
}


NS_IMETHODIMP
nsXULPrototypeCache::PutScript(nsIURI* aURI, void* aScriptObject)
{
    nsIURIKey key(aURI);
    mScriptTable.Put(&key, aScriptObject);

    // Lock the object from being gc'd until it is removed from the cache
    JS_LockGCThingRT(GetJSRuntime(), aScriptObject);
    return NS_OK;
}

/* static */
PRBool
nsXULPrototypeCache::UnlockJSObjectCallback(nsHashKey *aKey, void *aData, void* aClosure)
{
    JS_UnlockGCThingRT((JSRuntime*) aClosure, aData);
    return PR_TRUE;
}

NS_IMETHODIMP
nsXULPrototypeCache::FlushScripts()
{
    // This callback will unlock each object so it can once again be gc'd.
    mScriptTable.Reset(UnlockJSObjectCallback, (void*) GetJSRuntime());
    return NS_OK;
}


NS_IMETHODIMP
nsXULPrototypeCache::GetXBLDocumentInfo(const nsCString& aURL, nsIXBLDocumentInfo** aResult)
{
    nsCStringKey key(aURL);
    *aResult = NS_STATIC_CAST(nsIXBLDocumentInfo*, mXBLDocTable.Get(&key)); // Addref happens here.
    return NS_OK;
}


NS_IMETHODIMP
nsXULPrototypeCache::PutXBLDocumentInfo(nsIXBLDocumentInfo* aDocumentInfo)
{
    nsCOMPtr<nsIDocument> doc;
    aDocumentInfo->GetDocument(getter_AddRefs(doc));

    nsCOMPtr<nsIURI> uri;
    doc->GetDocumentURL(getter_AddRefs(uri));

    nsCAutoString str;
    uri->GetSpec(str);
    
    nsCStringKey key(str.get());
    nsCOMPtr<nsIXBLDocumentInfo> info = getter_AddRefs(NS_STATIC_CAST(nsIXBLDocumentInfo*, mXBLDocTable.Get(&key)));
    if (!info)
        mXBLDocTable.Put(&key, aDocumentInfo);

    return NS_OK;
}


NS_IMETHODIMP
nsXULPrototypeCache::FlushXBLInformation()
{
    mXBLDocTable.Reset();
    return NS_OK;
}

struct nsHashKeyEntry {
  nsHashKey* mKey;
  nsHashKeyEntry* mNext;

  nsHashKeyEntry(nsHashKey* aKey, nsHashKeyEntry* aNext = nsnull) {
    mKey = aKey;
    mNext = aNext;
  }

  ~nsHashKeyEntry() {
    delete mNext;
  }
};

struct nsHashKeys {
  nsHashKeyEntry* mFirst;

  nsHashKeys() { mFirst = nsnull; };
  ~nsHashKeys() { Clear(); };

  void AppendKey(nsHashKey* aKey) {
    mFirst = new nsHashKeyEntry(aKey, mFirst);
  }

  void Clear() {
    delete mFirst;
    mFirst = nsnull;
  }
};

PRBool PR_CALLBACK FlushSkinXBL(nsHashKey* aKey, void* aData, void* aClosure)
{
  nsIXBLDocumentInfo* docInfo = (nsIXBLDocumentInfo*)aData;
  nsCOMPtr<nsIDocument> doc;
  docInfo->GetDocument(getter_AddRefs(doc));
  nsCOMPtr<nsIURI> uri;
  doc->GetDocumentURL(getter_AddRefs(uri));
  nsCAutoString str;
  uri->GetPath(str);
  if (!strncmp(str.get(), "/skin", 5)) {
    // This is a skin binding. Add the key to the list.
    nsHashKeys* list = (nsHashKeys*)aClosure;
    list->AppendKey(aKey);
  }
  return PR_TRUE;
}

PRBool PR_CALLBACK FlushSkinSheets(nsHashKey* aKey, void* aData, void* aClosure)
{
  nsICSSStyleSheet* sheet = (nsICSSStyleSheet*)aData;
  nsCOMPtr<nsIURI> uri;
  sheet->GetURL(*getter_AddRefs(uri));
  nsCAutoString str;
  uri->GetPath(str);
  if (!strncmp(str.get(), "/skin", 5)) {
    // This is a skin binding. Add the key to the list.
    nsHashKeys* list = (nsHashKeys*)aClosure;
    list->AppendKey(aKey);
  }
  return PR_TRUE;
}

PRBool PR_CALLBACK FlushScopedSkinStylesheets(nsHashKey* aKey, void* aData, void* aClosure)
{
  nsIXBLDocumentInfo* docInfo = (nsIXBLDocumentInfo*)aData;
  docInfo->FlushSkinStylesheets();
  return PR_TRUE;
}

NS_IMETHODIMP
nsXULPrototypeCache::FlushSkinFiles()
{
  // Flush out skin XBL files from the cache.
  nsHashKeys keysToRemove;
  nsHashKeyEntry* curr;
  mXBLDocTable.Enumerate(FlushSkinXBL, &keysToRemove);
  for (curr = keysToRemove.mFirst; curr; curr = curr->mNext)
    mXBLDocTable.Remove(curr->mKey);

  // Now flush out our skin stylesheets from the cache.
  keysToRemove.Clear();
  mStyleSheetTable.Enumerate(FlushSkinSheets, &keysToRemove);
  for (curr = keysToRemove.mFirst; curr; curr = curr->mNext)
    mStyleSheetTable.Remove(curr->mKey);

  // Iterate over all the remaining XBL and make sure cached 
  // scoped skin stylesheets are flushed and refetched by the
  // prototype bindings.
  mXBLDocTable.Enumerate(FlushScopedSkinStylesheets);
  return NS_OK;
}


NS_IMETHODIMP
nsXULPrototypeCache::Flush()
{
    FlushPrototypes();  // flushes the script table as well
    FlushStyleSheets();
    FlushXBLInformation();
    return NS_OK;
}


NS_IMETHODIMP
nsXULPrototypeCache::GetEnabled(PRBool* aIsEnabled)
{
    *aIsEnabled = !gDisableXULCache;
    return NS_OK;
}


NS_IMETHODIMP
nsXULPrototypeCache::GetFastLoadService(nsIFastLoadService** aResult)
{
    NS_IF_ADDREF(*aResult = gFastLoadService);
    return NS_OK;
}


static PRBool gDisableXULFastLoad = PR_FALSE;           // enabled by default
static PRBool gChecksumXULFastLoadFile = PR_TRUE;       // XXXbe too paranoid

NS_IMETHODIMP
nsXULPrototypeCache::AbortFastLoads()
{
#ifdef DEBUG_brendan
    NS_BREAK();
#endif

    // Save a strong ref to the FastLoad file, so we can remove it after we
    // close open streams to it.
    nsCOMPtr<nsIFile> file = gFastLoadFile;

    // Now rename or remove the file.
    if (file) {
#ifdef DEBUG
        file->MoveTo(nsnull, NS_LITERAL_CSTRING("Aborted.mfasl"));
#else
        file->Remove(PR_FALSE);
#endif
    }

    // Flush the XUL cache for good measure, in case we cached a bogus/downrev
    // script, somehow.
    Flush();

    // Clear the FastLoad set
    mFastLoadURITable.Reset();

    if (! gFastLoadService) 
        return NS_OK;

    // Fetch the current input (if FastLoad file existed) or output (if we're
    // creating the FastLoad file during this app startup) stream.
    nsCOMPtr<nsIObjectInputStream> objectInput;
    nsCOMPtr<nsIObjectOutputStream> objectOutput;
    gFastLoadService->GetInputStream(getter_AddRefs(objectInput));
    gFastLoadService->GetOutputStream(getter_AddRefs(objectOutput));

    if (objectOutput) {
        gFastLoadService->SetOutputStream(nsnull);
        
        if (NS_SUCCEEDED(objectOutput->Close()) && gChecksumXULFastLoadFile)
            gFastLoadService->CacheChecksum(gFastLoadFile,
                                            objectOutput);
    }

    if (objectInput) {
        // If this is the last of one or more XUL master documents loaded
        // together at app startup, close the FastLoad service's singleton
        // input stream now.
        gFastLoadService->SetInputStream(nsnull);
        objectInput->Close();
    }

    // If the list is empty now, the FastLoad process is done.
    NS_RELEASE(gFastLoadService);
    NS_RELEASE(gFastLoadFile);

    return NS_OK;
}


NS_IMETHODIMP
nsXULPrototypeCache::RemoveFromFastLoadSet(nsIURI* aURI) 
{
    nsIURIKey key(aURI);
    mFastLoadURITable.Remove(&key);

    return NS_OK;
}

static const char kDisableXULFastLoadPref[] = "nglayout.debug.disable_xul_fastload";
static const char kChecksumXULFastLoadFilePref[] = "nglayout.debug.checksum_xul_fastload_file";

NS_IMETHODIMP
nsXULPrototypeCache::WritePrototype(nsIXULPrototypeDocument* aPrototypeDocument)
{
    nsresult rv = NS_OK, rv2 = NS_OK;

    // We're here before the FastLoad service has been initialized, probably because 
    // of the profile manager. Bail quietly, don't worry, we'll be back later.
    if (! gFastLoadService) 
        return NS_OK;

    // Fetch the current input (if FastLoad file existed) or output (if we're
    // creating the FastLoad file during this app startup) stream.
    nsCOMPtr<nsIObjectInputStream> objectInput;
    nsCOMPtr<nsIObjectOutputStream> objectOutput;
    gFastLoadService->GetInputStream(getter_AddRefs(objectInput));
    gFastLoadService->GetOutputStream(getter_AddRefs(objectOutput));

    nsCOMPtr<nsIURI> protoURI;
    aPrototypeDocument->GetURI(getter_AddRefs(protoURI));

    // Remove this document from the FastLoad table. We use the table's
    // emptiness instead of a counter to decide when the FastLoad process
    // has completed. When complete, we can write footer details to the
    // FastLoad file. 
    RemoveFromFastLoadSet(protoURI);

    PRInt32 count = mFastLoadURITable.Count();

    if (objectOutput) {
        rv = StartFastLoadingURI(protoURI, nsIFastLoadService::NS_FASTLOAD_WRITE);
        if (NS_SUCCEEDED (rv)) {
            // Re-select the URL of the current prototype, as out-of-line script loads
            // may have changed
            nsCOMPtr<nsIURI> oldURI;
            gFastLoadService->SelectMuxedDocument(protoURI, getter_AddRefs(oldURI));

            aPrototypeDocument->Write(objectOutput);

            gFastLoadService->EndMuxedDocument(protoURI);
        }

        // If this is the last of one or more XUL master documents loaded
        // together at app startup, close the FastLoad service's singleton
        // output stream now.
        //
        // NB: we must close input after output, in case the output stream
        // implementation needs to read from the input stream, to compute a
        // FastLoad file checksum.  In that case, the implementation used
        // nsIFastLoadFileIO to get the corresponding input stream for this
        // output stream.
        if (count == 0) {
            gFastLoadService->SetOutputStream(nsnull);
            rv = objectOutput->Close();

            if (NS_SUCCEEDED(rv) && gChecksumXULFastLoadFile) {
                rv = gFastLoadService->CacheChecksum(gFastLoadFile,
                                                     objectOutput);
            }
        }
    }

    if (objectInput) {
        // If this is the last of one or more XUL master documents loaded
        // together at app startup, close the FastLoad service's singleton
        // input stream now.
        if (count == 0) {
            gFastLoadService->SetInputStream(nsnull);
            rv2 = objectInput->Close();
        }
    }

    // If the list is empty now, the FastLoad process is done.
    if (count == 0) {
        NS_RELEASE(gFastLoadService);
        NS_RELEASE(gFastLoadFile);
    }

    return NS_FAILED(rv) ? rv : rv2;
}


nsresult 
nsXULPrototypeCache::StartFastLoadingURI(nsIURI* aURI, PRInt32 aDirectionFlags)
{
    nsresult rv;

    nsCAutoString urlspec;
    rv = aURI->GetAsciiSpec(urlspec);
    if (NS_FAILED(rv)) return rv;

    // If StartMuxedDocument returns NS_ERROR_NOT_AVAILABLE, then
    // we must be reading the file, and urlspec was not associated
    // with any multiplexed stream in it.  The FastLoad service
    // will therefore arrange to update the file, writing new data
    // at the end while old (available) data continues to be read
    // from the pre-existing part of the file.
    return gFastLoadService->StartMuxedDocument(aURI, urlspec.get(), aDirectionFlags);
}
 
PR_STATIC_CALLBACK(int)
FastLoadPrefChangedCallback(const char* aPref, void* aClosure)
{
    nsCOMPtr<nsIPref> prefs = do_GetService(NS_PREF_CONTRACTID);
    if (prefs) {
        PRBool wasEnabled = !gDisableXULFastLoad;
        prefs->GetBoolPref(kDisableXULFastLoadPref, &gDisableXULFastLoad);

        if (wasEnabled && gDisableXULFastLoad) {
            static NS_DEFINE_CID(kXULPrototypeCacheCID, NS_XULPROTOTYPECACHE_CID);
            nsCOMPtr<nsIXULPrototypeCache> cache(do_GetService(kXULPrototypeCacheCID));
            if (cache)
                cache->AbortFastLoads();
        }

        prefs->GetBoolPref(kChecksumXULFastLoadFilePref, &gChecksumXULFastLoadFile);
    }
    return 0;
}


class nsXULFastLoadFileIO : public nsIFastLoadFileIO
{
  public:
    nsXULFastLoadFileIO(nsIFile* aFile)
      : mFile(aFile) {
        NS_INIT_REFCNT();
        MOZ_COUNT_CTOR(nsXULFastLoadFileIO);
    }

    virtual ~nsXULFastLoadFileIO() {
        MOZ_COUNT_DTOR(nsXULFastLoadFileIO);
    }

    NS_DECL_ISUPPORTS
    NS_DECL_NSIFASTLOADFILEIO

    nsCOMPtr<nsIFile>         mFile;
    nsCOMPtr<nsIInputStream>  mInputStream;
    nsCOMPtr<nsIOutputStream> mOutputStream;
};


NS_IMPL_THREADSAFE_ISUPPORTS1(nsXULFastLoadFileIO, nsIFastLoadFileIO)
MOZ_DECL_CTOR_COUNTER(nsXULFastLoadFileIO)


NS_IMETHODIMP
nsXULFastLoadFileIO::GetInputStream(nsIInputStream** aResult)
{
    if (! mInputStream) {
        nsresult rv;
        nsCOMPtr<nsIInputStream> fileInput;
        rv = NS_NewLocalFileInputStream(getter_AddRefs(fileInput), mFile);
        if (NS_FAILED(rv)) return rv;

        rv = NS_NewBufferedInputStream(getter_AddRefs(mInputStream),
                                       fileInput,
                                       XUL_DESERIALIZATION_BUFFER_SIZE);
        if (NS_FAILED(rv)) return rv;
    }

    NS_ADDREF(*aResult = mInputStream);
    return NS_OK;
}


NS_IMETHODIMP
nsXULFastLoadFileIO::GetOutputStream(nsIOutputStream** aResult)
{
    if (! mOutputStream) {
        PRInt32 ioFlags = PR_WRONLY;
        if (! mInputStream)
            ioFlags |= PR_CREATE_FILE | PR_TRUNCATE;

        nsresult rv;
        nsCOMPtr<nsIOutputStream> fileOutput;
        rv = NS_NewLocalFileOutputStream(getter_AddRefs(fileOutput), mFile,
                                         ioFlags, 0644);
        if (NS_FAILED(rv)) return rv;

        rv = NS_NewBufferedOutputStream(getter_AddRefs(mOutputStream),
                                        fileOutput,
                                        XUL_SERIALIZATION_BUFFER_SIZE);
        if (NS_FAILED(rv)) return rv;
    }

    NS_ADDREF(*aResult = mOutputStream);
    return NS_OK;
}

nsresult
nsXULPrototypeCache::StartFastLoad(nsIURI* aURI)
{
    nsresult rv;

    PRBool isChrome = PR_FALSE;
    nsCAutoString path;
    aURI->GetPath(path);
    PRInt32 length = path.Length();
    const nsACString& extn = Substring(path, path.Length()-4, 4);
    if (! extn.Equals(NS_LITERAL_CSTRING(".xul")))
        return NS_ERROR_NOT_AVAILABLE;

    nsIURIKey key(aURI);

    // Test gFastLoadList to decide whether this is the first nsXULDocument
    // participating in FastLoad.  If gFastLoadList is non-null, this document
    // must not be first, but it can join the FastLoad process.  Examples of
    // multiple master documents participating include hiddenWindow.xul and
    // navigator.xul on the Mac, and multiple-app-component (e.g., mailnews
    // and browser) startup due to command-line arguments.
    //
    // XXXbe we should attempt to update the FastLoad file after startup!
    //
    // XXXbe we do not yet use nsFastLoadPtrs, but once we do, we must keep
    // the FastLoad input stream open for the life of the app.
    if (gFastLoadService && gFastLoadFile) {
        mFastLoadURITable.Put(&key, aURI);

        return NS_OK;
    }

    // Use a local to refer to the service till we're sure we succeeded, then
    // commit to gFastLoadService.  Same for gFastLoadFile, which is used to
    // delete the FastLoad file on abort.
    nsCOMPtr<nsIFastLoadService> fastLoadService(do_GetFastLoadService());
    if (! fastLoadService)
        return NS_ERROR_FAILURE;

    
    nsCOMPtr<nsIPref> prefs(do_GetService(NS_PREF_CONTRACTID));
    if (prefs) {
        prefs->GetBoolPref(kDisableXULFastLoadPref, &gDisableXULFastLoad);
        prefs->GetBoolPref(kChecksumXULFastLoadFilePref, &gChecksumXULFastLoadFile);
        prefs->RegisterCallback(kDisableXULFastLoadPref,
                                FastLoadPrefChangedCallback,
                                nsnull);
        prefs->RegisterCallback(kChecksumXULFastLoadFilePref,
                                FastLoadPrefChangedCallback,
                                nsnull);
        if (gDisableXULFastLoad)
            return NS_ERROR_NOT_AVAILABLE;
    }

    // Get the chrome directory to validate against the one stored in the
    // FastLoad file, or to store there if we're generating a new file.
    nsCOMPtr<nsIFile> chromeDir;
    rv = NS_GetSpecialDirectory(NS_APP_CHROME_DIR, getter_AddRefs(chromeDir));
    if (NS_FAILED(rv))
        return rv;
    nsCAutoString chromePath;
    rv = chromeDir->GetPath(chromePath);
    if (NS_FAILED(rv))
        return rv;

    nsCOMPtr<nsIFile> file;
    rv = fastLoadService->NewFastLoadFile(XUL_FASTLOAD_FILE_BASENAME,
                                          getter_AddRefs(file));
    if (NS_FAILED(rv)) return rv;

    // Give the FastLoad service an object by which it can get or create a
    // file output stream given an input stream on the same file.
    nsXULFastLoadFileIO* xio = new nsXULFastLoadFileIO(file);
    nsCOMPtr<nsIFastLoadFileIO> io = NS_STATIC_CAST(nsIFastLoadFileIO*, xio);
    if (! io)
        return NS_ERROR_OUT_OF_MEMORY;
    fastLoadService->SetFileIO(io);

    // Try to read an existent FastLoad file.
    PRBool exists = PR_FALSE;
    if (NS_SUCCEEDED(file->Exists(&exists)) && exists) {
        nsCOMPtr<nsIInputStream> input;
        rv = io->GetInputStream(getter_AddRefs(input));
        if (NS_FAILED(rv)) return rv;

        nsCOMPtr<nsIObjectInputStream> objectInput;
        rv = fastLoadService->NewInputStream(input, getter_AddRefs(objectInput));

        if (NS_SUCCEEDED(rv)) {
            if (gChecksumXULFastLoadFile) {
                nsCOMPtr<nsIFastLoadReadControl>
                    readControl(do_QueryInterface(objectInput));
                if (readControl) {
                    // Verify checksum, using the fastLoadService's checksum
                    // cache to avoid computing more than once per session.
                    PRUint32 checksum;
                    rv = readControl->GetChecksum(&checksum);
                    if (NS_SUCCEEDED(rv)) {
                        PRUint32 verified;
                        rv = fastLoadService->ComputeChecksum(file,
                                                               readControl,
                                                               &verified);
                        if (NS_SUCCEEDED(rv) && verified != checksum) {
#ifdef DEBUG
                            printf("bad FastLoad file checksum\n");
#endif
                            rv = NS_ERROR_FAILURE;
                        }
                    }
                }
            }

            if (NS_SUCCEEDED(rv)) {
                // XXXbe get version number, scripts only for now -- bump
                // version later when rest of prototype document header is
                // serialized
                PRUint32 version;
                rv = objectInput->Read32(&version);
                if (NS_SUCCEEDED(rv)) {
                    if (version != XUL_FASTLOAD_FILE_VERSION) {
#ifdef DEBUG
                        printf("bad FastLoad file version\n");
#endif
                        rv = NS_ERROR_UNEXPECTED;
                    } else {
                        nsXPIDLCString fileChromePath;
                        rv = objectInput->ReadStringZ(
                                                 getter_Copies(fileChromePath));
                        if (NS_SUCCEEDED(rv) && 
                            !fileChromePath.Equals(chromePath)) { 
                            rv = NS_ERROR_UNEXPECTED;
                        }
                    }
                }
            }
        }

        if (NS_SUCCEEDED(rv)) {
            fastLoadService->SetInputStream(objectInput);
        } else {
            // NB: we must close before attempting to remove, for non-Unix OSes
            // that can't do open-unlink.
            if (objectInput)
                objectInput->Close();
            else
                input->Close();
            xio->mInputStream = nsnull;

#ifdef DEBUG
            file->MoveTo(nsnull, NS_LITERAL_CSTRING("Invalid.mfasl"));
#else
            file->Remove(PR_FALSE);
#endif
            exists = PR_FALSE;
        }
    }

    // FastLoad file not found, or invalid: write a new one.
    if (! exists) {
        nsCOMPtr<nsIOutputStream> output;
        rv = io->GetOutputStream(getter_AddRefs(output));
        if (NS_FAILED(rv)) return rv;

        nsCOMPtr<nsIObjectOutputStream> objectOutput;
        rv = fastLoadService->NewOutputStream(output,
                                               getter_AddRefs(objectOutput));
        if (NS_SUCCEEDED(rv)) {
            rv = objectOutput->Write32(XUL_FASTLOAD_FILE_VERSION);
            if (NS_SUCCEEDED(rv))
                rv = objectOutput->WriteStringZ(chromePath.get());
        }

        // Remove here even though some errors above will lead to a FastLoad
        // file invalidation.  Other errors (failure to note the dependency on
        // installed-chrome.txt, e.g.) will not cause invalidation, and we may
        // as well tidy up now.
        if (NS_FAILED(rv)) {
            if (objectOutput)
                objectOutput->Close();
            else
                output->Close();
            xio->mOutputStream = nsnull;

            file->Remove(PR_FALSE);
            return rv;
        }

        fastLoadService->SetOutputStream(objectOutput);
    }

    // Success!  Insert this URI into the mFastLoadURITable
    // and commit locals to globals.
    mFastLoadURITable.Put(&key, aURI);

    NS_ADDREF(gFastLoadService = fastLoadService);
    NS_ADDREF(gFastLoadFile = file);
    return NS_OK;
}


