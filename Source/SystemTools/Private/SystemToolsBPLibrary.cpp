// Copyright 2025 Andrew Bindraw. All Rights Reserved.

#include "SystemToolsBPLibrary.h"
#include "SystemTools.h"

void* USystemToolsBPLibrary::BrowserWindowHandle = nullptr;
TAtomic <uint32> USystemToolsBPLibrary::ProcessId = 0;
TAtomic <uint32> USystemToolsBPLibrary::ProcessIdCached = 0;
TAtomic <bool> USystemToolsBPLibrary::IsBrowserChildProcess = false;

DEFINE_LOG_CATEGORY(LogSystemTools)

USystemToolsBPLibrary::USystemToolsBPLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{

}

void USystemToolsBPLibrary::LaunchURLEx(const FString& URL)
{
	UE_LOG(LogSystemTools, Log, TEXT("----------------> LaunchURLEx: %s"), *URL);

	if (URL.IsEmpty())
	{
		return;
	}

#if PLATFORM_WINDOWS

	/* 
	* LaunchURLEx(String: URL) it fix for UKismetSystemLibrary::LaunchURL(String: URL), only for Windows.
	* Because function from engine UKismetSystemLibrary::LaunchURL(String: URL): 
	* It can't move the browser window on top when the game window is in fullscreen, launches the browser behind the game window. 
	* It can't close the browser window after closing the game, (if the browser was opened from the game) 
	* and the Steam client thinks the game is still running.
	* Tested on Windows 10, 11 for browsers: Chrome, Opera, Firefox, Edge.
	*/

// === START COPY CODE from [UE 5.4.4] Engine\Source\Runtime\Engine\Private\KismetSystemLibrary.cpp
	UE::Core::FURLRequestFilter Filter(TEXT("SystemLibrary.LaunchURLFilter"), GEngineIni);
	const bool bAllowedByFilter = Filter.IsRequestAllowed(URL);
	if (!bAllowedByFilter)
	{
		return;
	}

	FString BrowserOpenCommand;

	// First lookup the program Id for the default browser.
	FString ProgId;
	if (FWindowsPlatformMisc::QueryRegKey(HKEY_CURRENT_USER, TEXT("Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\http\\UserChoice"), TEXT("Progid"), ProgId))
	{
		// If we found it, then lookup it's open shell command in the classes registry.
		FString BrowserRegPath = ProgId + TEXT("\\shell\\open\\command");
		FWindowsPlatformMisc::QueryRegKey(HKEY_CLASSES_ROOT, *BrowserRegPath, nullptr, BrowserOpenCommand);
	}

	// If we failed to find a default browser using the newer location, revert to using shell open command for the HTTP file association.
	if (BrowserOpenCommand.IsEmpty())
	{
		FWindowsPlatformMisc::QueryRegKey(HKEY_CLASSES_ROOT, TEXT("http\\shell\\open\\command"), nullptr, BrowserOpenCommand);
	}

	// If we have successfully looked up the correct shell command, then we can create a new process using that command
	// we do this instead of shell execute due to security concerns.  By starting the browser directly we avoid most issues.
	if (!BrowserOpenCommand.IsEmpty())
	{
		FString ExePath, ExeArgs;

		// If everything has gone to plan, the shell command should be something like this:
		// "C:\Program Files (x86)\Mozilla Firefox\firefox.exe" -osint -url "%1"
		// We need to extract out the executable portion, and the arguments portion and expand any %1's with the URL,
		// then start the browser process.

		// Extract the exe and any arguments to the executable.
		const int32 FirstQuote = BrowserOpenCommand.Find(TEXT("\""));
		if (FirstQuote != INDEX_NONE)
		{
			const int32 SecondQuote = BrowserOpenCommand.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, FirstQuote + 1);
			if (SecondQuote != INDEX_NONE)
			{
				ExePath = BrowserOpenCommand.Mid(FirstQuote + 1, (SecondQuote - 1) - FirstQuote);
				ExeArgs = BrowserOpenCommand.Mid(SecondQuote + 1);
			}
		}

		// If anything failed to parse right, don't continue down this path, just use shell execute.
		if (!ExePath.IsEmpty())
		{
			if (ExeArgs.ReplaceInline(TEXT("%1"), *URL) == 0)
			{
				// If we fail to detect the placement token we append the URL to the arguments.
				// This is for robustness, and to fix a known error case when using Internet Explorer 8. 
				ExeArgs.Append(TEXT(" \"") + URL + TEXT("\""));
			}
// === END COPY CODE from [UE 5.4.4] Engine\Source\Runtime\Engine\Private\KismetSystemLibrary.cpp

			UE_LOG(LogSystemTools, Log, TEXT("-> ThreadId: [%d]"), ::GetCurrentThreadId());

			bool bLaunchDetached = true;
			ProcessId = 0;
			BrowserWindowHandle = nullptr;
			AsyncTask(ENamedThreads::AnyHiPriThreadNormalTask, [ExePath, ExeArgs, bLaunchDetached]
			{
				UE_LOG(LogSystemTools, Log, TEXT("ExePath: %s, ExeArgs: %s, bLaunchDetached: %s"),
					*ExePath, *ExeArgs, bLaunchDetached ? TEXT("true") : TEXT("false"));

				UE_LOG(LogSystemTools, Log, TEXT("-> ThreadId: [%d]"), ::GetCurrentThreadId());
				/*
				* FindWindow(TEXT("Chrome_WidgetWin_1"), nullptr);
				* It's not good solution, because in the system can installed software with same class name for window: Chrome_WidgetWin_1
				* In my case: Samsung Magician (Version 8.2.0)
				* Also browsers: Google Chrome, Opera and Edge has same class name for window, because based on Chromium (open source project).
				*/

				/*
				* Check is browser already launched
				*
				* This function ::HWND GetTopWindowForProcess(const ::DWORD InProcessID) from
				* Engine\Plugins\Developer\VisualStudioSourceCodeAccess\Source\VisualStudioSourceCodeAccess\Private\VisualStudioSourceCodeAccessor.cpp
				* with some modifications
				*/

				UE_LOG(LogSystemTools, Log, TEXT("-> Trying to find process id and hwnd for %s"), *ExePath);
				struct EnumWindowsData
				{
					::DWORD OutProcessID;
					::HWND OutHwnd;
					WIDECHAR* InExePath;

					static ::BOOL CALLBACK EnumWindowsProc(::HWND Hwnd, ::LPARAM lParam)
					{
						EnumWindowsData* const Data = (EnumWindowsData*)lParam;

						::DWORD HwndProcessId = 0;
						::GetWindowThreadProcessId(Hwnd, &HwndProcessId);

						FProcHandle OpenProcessHandle = FPlatformProcess::OpenProcess(HwndProcessId);
						WIDECHAR FilePath[MAX_PATH];
						::DWORD CountFilePath = ::GetModuleFileNameEx(OpenProcessHandle.Get(), 0, FilePath, MAX_PATH);
						const FString FilePathStr = FString(CountFilePath, FilePath);
						const FString InExePathStr = FString(Data->InExePath);

						if (FilePathStr.Equals(InExePathStr, ESearchCase::IgnoreCase))
						{
							UE_LOG(LogSystemTools, Log, TEXT("-> Process: [%d] %s"), HwndProcessId, *FilePathStr);
							if (::IsWindow(Hwnd) && ::IsWindowVisible(Hwnd))
							{
								Data->OutHwnd = Hwnd;
								Data->OutProcessID = HwndProcessId;
								UE_LOG(LogSystemTools, Log, TEXT("-> Main process: [%d] %s"), HwndProcessId, *FilePathStr);
								FPlatformProcess::CloseProc(OpenProcessHandle);
								return 0;
							}
						}

						FPlatformProcess::CloseProc(OpenProcessHandle);
						return 1;
					}
				};

				EnumWindowsData Data = { 0 };
				Data.OutProcessID = 0;
				Data.OutHwnd = nullptr;
				Data.InExePath = TCHAR_TO_WCHAR(*ExePath);
				::EnumWindows(&EnumWindowsData::EnumWindowsProc, (LPARAM)&Data);

				BrowserWindowHandle = Data.OutHwnd;
				ProcessId = Data.OutProcessID;

				bool IsSetWindowPos = false;
				bool IsSetForegroundWindow = false;
				::DWORD ProcessIdCreated = 0;

				// Create new process or open url in current process
				FProcHandle CreateProcessHandle = FPlatformProcess::CreateProc(*ExePath, *ExeArgs, bLaunchDetached, false, false, nullptr, 0, nullptr, nullptr, nullptr);

				if (CreateProcessHandle.IsValid())
				{
					ProcessIdCreated = ::GetProcessId(CreateProcessHandle.Get());
					UE_LOG(LogSystemTools, Log, TEXT("-> ProcessIdCreated: [%d]"), ProcessIdCreated);
				}

				if (BrowserWindowHandle == nullptr || ProcessId == 0)
				{
					float TimeWait = 5000.0f; // ms
					float StartTime = FPlatformTime::ToMilliseconds(FPlatformTime::Cycles());
					if (!FPlatformProcess::IsProcRunning(CreateProcessHandle))
					{
						UE_LOG(LogSystemTools, Log, TEXT("-> Start time utc: %s"), *FDateTime::UtcNow().ToString());
					}

					while (!FPlatformProcess::IsProcRunning(CreateProcessHandle))
					{
						UE_LOG(LogSystemTools, Log, TEXT("-> Launching the process: %s..."), *ExePath);
						FPlatformProcess::Sleep(1.0f);

						float CurrentTime = FPlatformTime::ToMilliseconds(FPlatformTime::Cycles());

						if (CurrentTime >= StartTime + TimeWait)
						{
							UE_LOG(LogSystemTools, Log, TEXT("-> Current time utc: %s, after %.0f ms"), *FDateTime::UtcNow().ToString(), TimeWait);
							UE_LOG(LogSystemTools, Log, TEXT("-> Exit, can't launch process: %s"), *ExePath);
							return;
						}
					}

					ProcessId = ::GetProcessId(CreateProcessHandle.Get());
					UE_LOG(LogSystemTools, Log, TEXT("-> ProcessId: [%d] %s"), ProcessId.Load(), *ExePath);



					TimeWait = 2000.0f; // ms
					StartTime = FPlatformTime::ToMilliseconds(FPlatformTime::Cycles());
					UE_LOG(LogSystemTools, Log, TEXT("-> Start time utc: %s"), *FDateTime::UtcNow().ToString());
					while (!BrowserWindowHandle)
					{
						UE_LOG(LogSystemTools, Log, TEXT("-> Try get BrowserWindowHandle ..."));
						BrowserWindowHandle = FPlatformMisc::GetTopLevelWindowHandle(ProcessId);
						FPlatformProcess::Sleep(0.4f);

						float CurrentTime = FPlatformTime::ToMilliseconds(FPlatformTime::Cycles());

						if (CurrentTime >= StartTime + TimeWait)
						{
							UE_LOG(LogSystemTools, Log, TEXT("-> Current time utc: %s, after %.0f ms"), *FDateTime::UtcNow().ToString(), TimeWait);
							UE_LOG(LogSystemTools, Log, TEXT("-> Trying to find process id and hwnd for %s"), *ExePath);

							EnumWindowsData DataNew = { 0 };
							DataNew.OutProcessID = 0;
							DataNew.OutHwnd = nullptr;
							DataNew.InExePath = TCHAR_TO_WCHAR(*ExePath);
							::EnumWindows(&EnumWindowsData::EnumWindowsProc, (LPARAM)&DataNew);

							BrowserWindowHandle = DataNew.OutHwnd;
							ProcessId = DataNew.OutProcessID;
							break;
						}
					}
				}

				if (BrowserWindowHandle && ProcessId != 0)
				{
					// Check is browser process child from current process and set flag IsBrowserChildProcess.
					// If browser opened before launched game, than don't close after game app closed.
					// If browser process child of game process and lauched by user from game, than close browser after game closed.

					UE_LOG(LogSystemTools, Log, TEXT("-> ProcessIdCreated: [%d], ProcessId: [%d]"), ProcessIdCreated, ProcessId.Load());

					::HANDLE hSnapshotProcess = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
					if (hSnapshotProcess != INVALID_HANDLE_VALUE)
					{
						PROCESSENTRY32 pe32;
						pe32.dwSize = sizeof(PROCESSENTRY32);

						if (::Process32First(hSnapshotProcess, &pe32))
						{
							do
							{
								::HANDLE hOpenProcess = ::OpenProcess(PROCESS_QUERY_INFORMATION, false, pe32.th32ProcessID);
								if (hOpenProcess)
								{
									if (pe32.th32ProcessID == ProcessId)
									{
										UE_LOG(LogSystemTools, Log, TEXT("-> Process: [%d], Process parent: [%d], MyProcessId: [%d]"), pe32.th32ProcessID, pe32.th32ParentProcessID, ::GetCurrentProcessId());

										// Fix for firefox, because firefox process always detached!
										if (ProcessIdCreated == pe32.th32ParentProcessID)
										{
											ProcessIdCached = ProcessIdCreated;
										}

										if (pe32.th32ParentProcessID == ::GetCurrentProcessId() ||
											ProcessIdCached == pe32.th32ParentProcessID)
										{
											IsBrowserChildProcess = true;
										}
										else
										{
											IsBrowserChildProcess = false;
										}
										UE_LOG(LogSystemTools, Log, TEXT("-> Set flag IsBrowserChildProcess: %s"), IsBrowserChildProcess ? TEXT("true") : TEXT("false"));

										::CloseHandle(hOpenProcess);
										break;
									}
									::CloseHandle(hOpenProcess);
								}
							} while (Process32Next(hSnapshotProcess, &pe32));
						}

						::CloseHandle(hSnapshotProcess);
					}

					// Bring the browser window on top
					IsSetWindowPos = ::SetWindowPos((HWND)BrowserWindowHandle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE); // HWND_TOP for debug, HWND_TOPMOST for release
					IsSetForegroundWindow = ::SetForegroundWindow((HWND)BrowserWindowHandle);
					UE_LOG(LogSystemTools, Log, TEXT("-> Move window on top, process: [%d], IsSetWindowPos: %s, IsSetForegroundWindow: %s"),
						ProcessId.Load(),
						IsSetWindowPos ? TEXT("true") : TEXT("false"),
						IsSetForegroundWindow ? TEXT("true") : TEXT("false"));
				}

				FPlatformProcess::CloseProc(CreateProcessHandle);
			});
		}
	}
#else
	// This is temporary solution for other platforms exclude Windows
	UKismetSystemLibrary::LaunchURL(URL);
#endif

}

void USystemToolsBPLibrary::BeginDestroy()
{
	Super::BeginDestroy();

#if PLATFORM_WINDOWS
	// Close browser without terminate process, it's correct release resources and close tabs without restore process after launch again.
	//HWND WindowHandle = FPlatformMisc::GetTopLevelWindowHandle(ProcessId);
	if (IsBrowserChildProcess && BrowserWindowHandle)
	{
		bool IsPostMsg = ::PostMessageW((HWND)BrowserWindowHandle, WM_CLOSE, 0, 0);
		UE_LOG(LogSystemTools, Log, TEXT("-> Close window, process: [%d], IsPostMsg: %s"), ProcessId.Load(), IsPostMsg ? TEXT("true") : TEXT("false"));
	}

	//HANDLE ProcTerminateHandle = OpenProcess(PROCESS_TERMINATE, false, ProcessId);
	//FProcHandle ProcessHandle = FProcHandle(ProcTerminateHandle);
	//
	//ProcessHandle = FPlatformProcess::OpenProcess(ProcessId);
	//FPlatformProcess::TerminateProc(ProcessHandle, true);
#endif
}

void USystemToolsBPLibrary::ExecCommand(const FString& Command)
{
	FPlatformMisc::OsExecute(TEXT("open"), *Command);
}
