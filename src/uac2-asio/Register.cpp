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

    Register.cpp

Abstract:

    TBD

Environment:

    TBD

--*/

#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <oleauto.h>

#define CLSID_STRING_LEN      100

#define REGSTR_DESCRIPTION    _T("Description")
#define REGSTR_CLSID          _T("CLSID")
#define REGSTR_INPROCSERVER32 _T("InprocServer32")
#define REGSTR_THREADINGMODEL _T("ThreadingModel")
#define REGSTR_SOFTWAREASIO   _T("SOFTWARE\\ASIO")

typedef struct _REGISTRY_ELEMENT
{
    HKEY   RootKey;
    PCTSTR KeyName;
    PCTSTR ValueName;
    PCTSTR Data;
} REGISTRY_ELEMENT;

static bool FindRegPath(
    _In_ HKEY    key,
    _In_ LPCTSTR regPath
)
{
    bool find = false;

    if (regPath != nullptr)
    {
        HKEY    subKey = 0;
        LSTATUS status = RegOpenKeyEx(key, regPath, 0, KEY_ALL_ACCESS, &subKey);
        if (status == ERROR_SUCCESS)
        {
            RegCloseKey(subKey);
            find = true;
        }
    }

    return find;
}

static bool GetRegString(HKEY mainKey, LPCTSTR regPath, LPCTSTR valueName, LPVOID data, DWORD dataSize)
{
    HKEY  key;
    DWORD valueSize, valueType;
    bool  result = false;

    if (regPath)
    {
        if (RegOpenKeyEx(mainKey, regPath, 0, KEY_ALL_ACCESS, &key) == ERROR_SUCCESS)
        {
            if (RegQueryValueEx(key, valueName, 0, &valueType, 0, &valueSize) == ERROR_SUCCESS)
            {
                if (valueSize <= dataSize)
                {
                    RegQueryValueEx(key, valueName, 0, &valueType, (LPBYTE)data, &valueSize);
                    result = true;
                }
            }
            RegCloseKey(key);
        }
    }
    return result;
}

static HRESULT CreateRegistryKeyAndSetValue(
    _In_ HKEY       rootKey,
    _In_ PCTSTR     keyName,
    _In_opt_ PCTSTR valueName,
    _In_opt_ PCTSTR data
)
{
    HRESULT result = S_OK;
    LONG    ret;
    HKEY    key;

    ret = RegCreateKeyEx(rootKey, keyName, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &key, nullptr);
    if (ret != ERROR_SUCCESS)
    {
        result = HRESULT_FROM_WIN32(ret);
    }
    else
    {
        if (data != nullptr)
        {
            ret = RegSetValueEx(key, valueName, 0, REG_SZ, (LPBYTE)data, (DWORD)((_tcslen(data) + 1) * sizeof(TCHAR)));
            result = HRESULT_FROM_WIN32(ret);
        }
        RegCloseKey(key);
    }
    return result;
}

HRESULT UnregisterAsioDriver(
    _In_ CLSID   clsId,
    _In_         LPCTSTR /* dllName */,
    _In_ LPCTSTR regName
)
{
    HRESULT  result = S_OK;
    TCHAR    regpath[_MAX_PATH] = {0};
    TCHAR    clsid[CLSID_STRING_LEN] = {0};
    LPOLESTR wszCLSID = nullptr;

    result = StringFromCLSID(clsId, &wszCLSID);
    if (!SUCCEEDED(result))
    {
        return result;
    }
#ifdef _UNICODE
    wcsncpy_s(clsid, CLSID_STRING_LEN - 1, wszCLSID, _TRUNCATE);
#else
    if (WideCharToMultiByte(CP_ACP, 0, (LPWSTR)wszCLSID, -1, clsid, CLSID_STRING_LEN, 0, 0) == 0)
    {
        CoTaskMemFree(wszCLSID);
        return HRESULT_FROM_WIN32(GetLastError());
    }
#endif
    CoTaskMemFree(wszCLSID);

    _stprintf_s(regpath, _MAX_PATH, REGSTR_CLSID _T("\\%s"), clsid);
    if (FindRegPath(HKEY_CLASSES_ROOT, regpath))
    {
        result = HRESULT_FROM_WIN32(RegDeleteTree(HKEY_CLASSES_ROOT, regpath));
        // ASSERT(SUCCEEDED(result));
    }

    if (FindRegPath(HKEY_LOCAL_MACHINE, REGSTR_SOFTWAREASIO))
    {
        _stprintf_s(regpath, _MAX_PATH, REGSTR_SOFTWAREASIO _T("\\%s"), regName);
        if (FindRegPath(HKEY_LOCAL_MACHINE, regpath))
        {
            result = HRESULT_FROM_WIN32(RegDeleteTree(HKEY_LOCAL_MACHINE, regpath));
            // ASSERT(SUCCEEDED(result));
        }
    }

    return result;
}

HRESULT RegisterAsioDriver(
    _In_ CLSID   clsId,
    _In_ LPCTSTR dllName,
    _In_ LPCTSTR regName,
    _In_ LPCTSTR asioDescriptor,
    _In_ LPCTSTR threadModel
)
{
    HRESULT  result = S_OK;
    HMODULE  module = nullptr;
    bool     newregentry = false;
    TCHAR    dllPath[_MAX_PATH] = {0};
    TCHAR    moduleName[_MAX_PATH] = {0};
    TCHAR    classRegistryPath[_MAX_PATH] = {0};
    TCHAR    InprocServer32RegistryPath[_MAX_PATH] = {0};
    TCHAR    asioRegistryPath[_MAX_PATH] = {0};
    TCHAR    clsid[CLSID_STRING_LEN] = {0};
    LPOLESTR wszCLSID = nullptr;

    module = GetModuleHandle(dllName);
    if (module == nullptr)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    GetModuleFileName(module, moduleName, _MAX_PATH);
    if (moduleName[0] == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    CharLower(moduleName);

    result = StringFromCLSID(clsId, &wszCLSID);
    if (!SUCCEEDED(result))
    {
        return result;
    }
#ifdef _UNICODE
    wcsncpy_s(clsid, CLSID_STRING_LEN - 1, wszCLSID, _TRUNCATE);
#else
    if (WideCharToMultiByte(CP_ACP, 0, (LPWSTR)wszCLSID, -1, clsid, CLSID_STRING_LEN, 0, 0) == 0)
    {
        CoTaskMemFree(wszCLSID);
        return HRESULT_FROM_WIN32(GetLastError());
    }
#endif
    CoTaskMemFree(wszCLSID);

    _stprintf_s(classRegistryPath, _countof(classRegistryPath), REGSTR_CLSID _T("\\%s"), clsid);
    _stprintf_s(InprocServer32RegistryPath, _countof(InprocServer32RegistryPath), REGSTR_CLSID _T("\\%s\\") REGSTR_INPROCSERVER32, clsid);
    _stprintf_s(asioRegistryPath, _countof(asioRegistryPath), REGSTR_SOFTWAREASIO _T("\\%s"), regName);

    // If class registry already exists and another dll has been registered, delete it.
    // If class registry already exists and another dll is registered, reuse it.
    if (FindRegPath(HKEY_CLASSES_ROOT, classRegistryPath))
    {
        if (GetRegString(HKEY_CLASSES_ROOT, InprocServer32RegistryPath, 0, (LPVOID)dllPath, _MAX_PATH))
        {
            CharLower((LPTSTR)dllPath);
            if (_tcscmp(dllPath, moduleName))
            {
                RegDeleteTree(HKEY_LOCAL_MACHINE, classRegistryPath);
                // ASSERT(SUCCEEDED(result));
                newregentry = true;
            }
        }
    }
    else
    {
        newregentry = true;
    }

    if (newregentry && SUCCEEDED(result))
    {
        REGISTRY_ELEMENT registryElements[] = {
            {HKEY_CLASSES_ROOT, classRegistryPath, nullptr, asioDescriptor},
            {HKEY_CLASSES_ROOT, InprocServer32RegistryPath, nullptr, moduleName},
            {HKEY_CLASSES_ROOT, InprocServer32RegistryPath, REGSTR_THREADINGMODEL, threadModel}
        };

        for (int index = 0; (index < _countof(registryElements)) && SUCCEEDED(result); index++)
        {
            result = CreateRegistryKeyAndSetValue(
                registryElements[index].RootKey,
                registryElements[index].KeyName,
                registryElements[index].ValueName,
                registryElements[index].Data
            );
        }
    }
    if (SUCCEEDED(result))
    {
        // HKEY_LOCAL_MACHINE\SOFTWARE\ASIO
        if (FindRegPath(HKEY_LOCAL_MACHINE, REGSTR_SOFTWAREASIO))
        {
            if (FindRegPath(HKEY_LOCAL_MACHINE, asioRegistryPath))
            {
                result = HRESULT_FROM_WIN32(RegDeleteTree(HKEY_LOCAL_MACHINE, asioRegistryPath));
                // ASSERT(SUCCEEDED(result));
            }
        }
        else
        {
            // HKEY_LOCAL_MACHINE\SOFTWARE\ASIO
            result = CreateRegistryKeyAndSetValue(HKEY_LOCAL_MACHINE, REGSTR_SOFTWAREASIO, nullptr, nullptr);
        }
    }

    if (SUCCEEDED(result))
    {
        REGISTRY_ELEMENT registryElements[] = {
            {HKEY_LOCAL_MACHINE, asioRegistryPath, nullptr, nullptr},
            {HKEY_LOCAL_MACHINE, asioRegistryPath, REGSTR_CLSID, clsid},
            {HKEY_LOCAL_MACHINE, asioRegistryPath, REGSTR_DESCRIPTION, asioDescriptor}
        };

        for (int index = 0; (index < _countof(registryElements)) && SUCCEEDED(result); index++)
        {
            result = CreateRegistryKeyAndSetValue(
                registryElements[index].RootKey,
                registryElements[index].KeyName,
                registryElements[index].ValueName,
                registryElements[index].Data
            );
        }
    }

    if (!SUCCEEDED(result))
    {
        UnregisterAsioDriver(clsId, dllName, regName);
    }

    return result;
}
