// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License
// ============================================================================
// This is part of the Microsoft Low-Latency Audio driver project.
// Further information: https://aka.ms/asio
// ============================================================================
//
// BAFunctions extension for the WiX Standard Bootstrapper Application. The bundle loads it when
// its user interface starts, and it stops a second copy of the setup from running at the same
// time as the first.

#include <windows.h>
#include <unknwn.h>
#include <msi.h>

#include <BootstrapperApplicationTypes.h>
#include <BAFunctions.h>

namespace
{
	// Mutex name used to prevent multiple setup instances.
	constexpr wchar_t SingleInstanceMutexName[] = L"Local\\LowLatencyAudioSetup-{2f6e4a00-5669-446b-bec1-a2db6c849187}";

	constexpr wchar_t AlreadyRunningTitle[] = L"Low-Latency Audio Setup";
	constexpr wchar_t AlreadyRunningMessage[] = L"Setup is already running. Finish or close the other setup window, then try again.";

	HANDLE g_singleInstanceMutex = nullptr;

	HRESULT WINAPI BAFunctionsProc(BA_FUNCTIONS_MESSAGE, const LPVOID, LPVOID, LPVOID)
	{
		// Leave every message to the standard bootstrapper application.
		return E_NOTIMPL;
	}

	bool IsTopLevelLaunch(const BOOTSTRAPPER_COMMAND* command)
	{
		// A related bundle (an upgrade removing the previous version, for example) is started by
		// the setup that already holds the mutex, so only a launch by the user is checked.
		return command == nullptr || (command->relationType == BOOTSTRAPPER_RELATION_NONE && !command->fPassthrough);
	}

	bool ShowsUserInterface(const BOOTSTRAPPER_COMMAND* command)
	{
		return command == nullptr || command->display == BOOTSTRAPPER_DISPLAY_FULL || command->display == BOOTSTRAPPER_DISPLAY_PASSIVE;
	}
}

extern "C" HRESULT WINAPI BAFunctionsCreate(const BA_FUNCTIONS_CREATE_ARGS* args, BA_FUNCTIONS_CREATE_RESULTS* results)
{
	if (args == nullptr || results == nullptr)
	{
		return E_INVALIDARG;
	}

	const BOOTSTRAPPER_COMMAND* command = args->pCommand;

	if (IsTopLevelLaunch(command) && g_singleInstanceMutex == nullptr)
	{
		HANDLE mutex = ::CreateMutexW(nullptr, FALSE, SingleInstanceMutexName);
		DWORD error = ::GetLastError();

		if (mutex == nullptr && error != ERROR_ACCESS_DENIED)
		{
			return HRESULT_FROM_WIN32(error);
		}

		// ERROR_ACCESS_DENIED means another user's setup created the mutex with a security
		// descriptor this process cannot open. That is still another running setup.
		if (error == ERROR_ALREADY_EXISTS || error == ERROR_ACCESS_DENIED)
		{
			if (mutex != nullptr)
			{
				::CloseHandle(mutex);
			}

			if (ShowsUserInterface(command))
			{
				::MessageBoxW(nullptr, AlreadyRunningMessage, AlreadyRunningTitle, MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
			}

			return HRESULT_FROM_WIN32(ERROR_INSTALL_ALREADY_RUNNING);
		}

		g_singleInstanceMutex = mutex;
	}

	results->pfnBAFunctionsProc = BAFunctionsProc;
	results->pvBAFunctionsProcContext = nullptr;

	return S_OK;
}

extern "C" void WINAPI BAFunctionsDestroy(const BA_FUNCTIONS_DESTROY_ARGS* args, BA_FUNCTIONS_DESTROY_RESULTS*)
{
	// On a reload the standard bootstrapper application calls BAFunctionsCreate again in this
	// process, so the mutex stays with it.
	if ((args == nullptr || !args->fReload) && g_singleInstanceMutex != nullptr)
	{
		::CloseHandle(g_singleInstanceMutex);
		g_singleInstanceMutex = nullptr;
	}
}
