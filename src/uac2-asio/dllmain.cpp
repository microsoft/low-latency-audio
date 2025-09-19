// Copyright (c) Yamaha Corporation.
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================
// ASIO is a trademark and software of Steinberg Media Technologies GmbH

/*++

Module Name:

    dllmain.cpp

Abstract:

    TBD

Environment:

    TBD

--*/

// dllmain.cpp : Defines the entry point for the DLL application.
#include <windows.h>
#include <strmif.h>
#include "wxdebug.h"
#define AM_NOVTABLE __declspec(novtable)
#include <combase.h>
#undef AM_NOVTABLE

extern CFactoryTemplate g_Templates[];
extern int              g_NumOfTemplates;
extern HRESULT          RegisterAsioDriver();
extern HRESULT          UnregisterAsioDriver();

class CClassFactory : public IClassFactory
{
  public:
    CClassFactory(const CFactoryTemplate * factoryTemplate);
    virtual ~CClassFactory();

    // IUnknown
    STDMETHODIMP QueryInterface(
        REFIID  riid,
        void ** ppv
    );

    STDMETHODIMP_(ULONG)
    AddRef();
    STDMETHODIMP_(ULONG)
    Release();

    // IClassFactory
    STDMETHODIMP CreateInstance(
        _In_ IUnknown * unknown,
        _In_ REFIID     riid,
        _Out_ void **   object
    );

    STDMETHODIMP LockServer(
        _In_ BOOL fLock
    );

    static BOOL IsLocked();

  private:
    static LONG s_Locked;

    const CFactoryTemplate * m_Template{nullptr};
    ULONG                    m_Ref;
};

LONG CClassFactory::s_Locked = 0;

CClassFactory::CClassFactory(const CFactoryTemplate * factoryTemplate)
    : m_Template(factoryTemplate), m_Ref(0)
{
}

CClassFactory::~CClassFactory()
{
}

STDMETHODIMP CClassFactory::QueryInterface(
    REFIID  riid,
    void ** ppv
)
{
    // CheckPointer(ppv, E_POINTER);
    // ValidateReadWritePtr(ppv, sizeof(PVOID));
    *ppv = nullptr;

    if ((riid == IID_IUnknown) || (riid == IID_IClassFactory))
    {
        *ppv = (LPVOID)this;
        ((LPUNKNOWN)*ppv)->AddRef();
        return NOERROR;
    }

    return ResultFromScode(E_NOINTERFACE);
}

STDMETHODIMP_(ULONG)
CClassFactory::AddRef()
{
    return ++m_Ref;
}

ULONG CClassFactory::Release()
{
    LONG result = 0;

    if (--m_Ref == 0)
    {
        delete this;
        result = 0;
    }
    else
    {
        result = m_Ref;
    }

    return result;
}

STDMETHODIMP CClassFactory::CreateInstance(
    _In_ IUnknown * unknown,
    _In_ REFIID     riid,
    _Out_ void **   object
)
{
    HRESULT result = NOERROR;

    CheckPointer(object, E_POINTER);
    ValidateReadWritePtr(object, sizeof(void *));

    if (unknown != nullptr)
    {
        if (IsEqualIID(riid, IID_IUnknown) == FALSE)
        {
            return ResultFromScode(E_NOINTERFACE);
        }
    }

    CUnknown * newObject = m_Template->CreateInstance(unknown, &result);

    if (newObject == nullptr)
    {
        return E_OUTOFMEMORY;
    }

    if (FAILED(result))
    {
        delete newObject;
    }
    else
    {
        newObject->NonDelegatingAddRef();
        result = newObject->NonDelegatingQueryInterface(riid, object);
        newObject->NonDelegatingRelease();
        // if (SUCCEEDED(result)) {
        // 	ASSERT(*object);
        // }
    }
    return result;
}

STDMETHODIMP CClassFactory::LockServer(
    _In_ BOOL fLock
)
{
    if (fLock)
    {
        InterlockedIncrement(&s_Locked);
    }
    else
    {
        InterlockedDecrement(&s_Locked);
    }
    return NOERROR;
}

BOOL CClassFactory::IsLocked()
{
    return (s_Locked > 0);
}

BOOL APIENTRY DllMain(HMODULE, DWORD ul_reason_for_call, LPVOID)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

_Use_decl_annotations_
STDAPI DllGetClassObject(
    REFCLSID rclsid,
    REFIID   riid,
    LPVOID * ppv
)
{
    if ((riid != IID_IUnknown) && (riid != IID_IClassFactory))
    {
        return E_NOINTERFACE;
    }

    for (int index = 0; index < g_NumOfTemplates; index++)
    {
        const CFactoryTemplate * factoryTemplate = &g_Templates[index];
        if (factoryTemplate->IsClassID(rclsid))
        {
            *ppv = (LPVOID)(LPUNKNOWN) new CClassFactory(factoryTemplate);
            if (*ppv == nullptr)
            {
                return E_OUTOFMEMORY;
            }
            ((LPUNKNOWN)*ppv)->AddRef();
            return NOERROR;
        }
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

_Use_decl_annotations_
STDAPI DllCanUnloadNow()
{
    if (CClassFactory::IsLocked() || CBaseObject::ObjectsActive())
    {
        return S_FALSE;
    }
    else
    {
        return S_OK;
    }
}

STDAPI DllRegisterServer()
{
    return RegisterAsioDriver();
}

STDAPI DllUnregisterServer()
{
    return UnregisterAsioDriver();
}
