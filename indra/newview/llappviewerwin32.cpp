/**
 * @file llappviewerwin32.cpp
 * @brief The LLAppViewerWin32 class definitions
 *
 * $LicenseInfo:firstyear=2007&license=viewergpl$
 * 
 * Copyright (c) 2007-2009, Linden Research, Inc.
 * 
 * Second Life Viewer Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab.  Terms of
 * the GPL can be found in doc/GPL-license.txt in this distribution, or
 * online at http://secondlifegrid.net/programs/open_source/licensing/gplv2
 * 
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution, or
 * online at
 * http://secondlifegrid.net/programs/open_source/licensing/flossexception
 * 
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 * 
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 * $/LicenseInfo$
 */ 

#include "llviewerprecompiledheaders.h"

#if defined(_DEBUG)
# if _MSC_VER >= 1400 // Visual C++ 2005 or later
#	define WINDOWS_CRT_MEM_CHECKS 1
# endif
#endif

#include "llwindowwin32.h" // *FIX: for setting gIconResource.

#include "llappviewerwin32.h"

#include "llgl.h"
#include "res/resource.h" // *FIX: for setting gIconResource.

#include <fcntl.h>		//_O_APPEND
#include <io.h>			//_open_osfhandle()
#include <errorrep.h>	// for AddERExcludedApplicationA()
#include <process.h>	// _spawnl()
#include <tchar.h>		// For TCHAR support
#include <Werapi.h>

#include "llviewercontrol.h"
#include "lldxhardware.h"

#ifdef USE_NVAPI
#include "nvapi/nvapi.h"
#include "nvapi/NvApiDriverSettings.h"
#endif

#include <stdlib.h>

#include "llweb.h"
#include "llsecondlifeurls.h"

#include "llwindebug.h"

#include "llviewernetwork.h"
#include "llmd5.h"
#include "llfindlocale.h"

#include "llcommandlineparser.h"
#include "lltrans.h"

// *FIX:Mani - This hack is to fix a linker issue with libndofdev.lib
// The lib was compiled under VS2005 - in VS2003 we need to remap assert
#ifdef LL_DEBUG
#ifdef LL_MSVC7 
extern "C" {
    void _wassert(const wchar_t * _Message, const wchar_t *_File, unsigned _Line)
    {
        LL_ERRS() << _Message << LL_ENDL;
    }
}
#endif
#endif

const std::string LLAppViewerWin32::sWindowClass = "Second Life";

// Create app mutex creates a unique global windows object. 
// If the object can be created it returns true, otherwise
// it returns false. The false result can be used to determine 
// if another instance of a second life app (this vers. or later)
// is running.
// *NOTE: Do not use this method to run a single instance of the app.
// This is intended to help debug problems with the cross-platform 
// locked file method used for that purpose.
bool create_app_mutex()
{
	bool result = true;
	LPCWSTR unique_mutex_name = L"SecondLifeAppMutex";
	HANDLE hMutex;
	hMutex = CreateMutex(NULL, TRUE, unique_mutex_name); 
	if(GetLastError() == ERROR_ALREADY_EXISTS) 
	{     
		result = false;
	}
	return result;
}

#ifdef USE_NVAPI
#define NVAPI_APPNAME L"Second Life"

/*
This function is used to print to the command line a text message
describing the nvapi error and quits
*/
void nvapi_error(NvAPI_Status status)
{
	NvAPI_ShortString szDesc = { 0 };
	NvAPI_GetErrorMessage(status, szDesc);
	LL_WARNS() << szDesc << LL_ENDL;

	//should always trigger when asserts are enabled
	//llassert(status == NVAPI_OK);
}

void ll_nvapi_init(NvDRSSessionHandle hSession)
{
	// (2) load all the system settings into the session
	NvAPI_Status status = NvAPI_DRS_LoadSettings(hSession);
	if (status != NVAPI_OK) 
	{
		nvapi_error(status);
		return;
	}

	NvAPI_UnicodeString wsz = { 0 };
	memcpy_s(wsz, sizeof(wsz), NVAPI_APPNAME, sizeof(NVAPI_APPNAME));
	status = NvAPI_DRS_SetCurrentGlobalProfile(hSession, wsz);
	if (status != NVAPI_OK)
	{
		nvapi_error(status);
		return;
	}

	// (3) Obtain the current profile. 
	NvDRSProfileHandle hProfile = 0;
	status = NvAPI_DRS_GetCurrentGlobalProfile(hSession, &hProfile);
	if (status != NVAPI_OK) 
	{
		nvapi_error(status);
		return;
	}

	// load settings for querying 
	status = NvAPI_DRS_LoadSettings(hSession);
	if (status != NVAPI_OK)
	{
		nvapi_error(status);
		return;
	}

	//get the preferred power management mode for Second Life
	NVDRS_SETTING drsSetting = {0};
	drsSetting.version = NVDRS_SETTING_VER;
	status = NvAPI_DRS_GetSetting(hSession, hProfile, PREFERRED_PSTATE_ID, &drsSetting);
	if (status == NVAPI_SETTING_NOT_FOUND)
	{ //only override if the user hasn't specifically set this setting
		// (4) Specify that we want the VSYNC disabled setting
		// first we fill the NVDRS_SETTING struct, then we call the function
		drsSetting.version = NVDRS_SETTING_VER;
		drsSetting.settingId = PREFERRED_PSTATE_ID;
		drsSetting.settingType = NVDRS_DWORD_TYPE;
		drsSetting.u32CurrentValue = PREFERRED_PSTATE_PREFER_MAX;
		status = NvAPI_DRS_SetSetting(hSession, hProfile, &drsSetting);
		if (status != NVAPI_OK) 
		{
			nvapi_error(status);
			return;
		}

        // (5) Now we apply (or save) our changes to the system
        status = NvAPI_DRS_SaveSettings(hSession);
        if (status != NVAPI_OK) 
        {
            nvapi_error(status);
            return;
        }
	}
	else if (status != NVAPI_OK)
	{
		nvapi_error(status);
		return;
	}
}
#endif

//#define DEBUGGING_SEH_FILTER 1
#if DEBUGGING_SEH_FILTER
#	define WINMAIN DebuggingWinMain
#else
#	define WINMAIN WinMain
#endif

int APIENTRY WINMAIN(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR     lpCmdLine,
                     int       nCmdShow)
{
	const S32 MAX_HEAPS = 255;
	DWORD heap_enable_lfh_error[MAX_HEAPS];
	S32 num_heaps = 0;
	
#if WINDOWS_CRT_MEM_CHECKS && !INCLUDE_VLD
	_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF ); // dump memory leaks on exit
#elif 1
	// Experimental - enable the low fragmentation heap
	// This results in a 2-3x improvement in opening a new Inventory window (which uses a large numebr of allocations)
	// Note: This won't work when running from the debugger unless the _NO_DEBUG_HEAP environment variable is set to 1

	_CrtSetDbgFlag(0); // default, just making explicit
	
	ULONG ulEnableLFH = 2;
	HANDLE* hHeaps = new HANDLE[MAX_HEAPS];
	num_heaps = GetProcessHeaps(MAX_HEAPS, hHeaps);
	for(S32 i = 0; i < num_heaps; i++)
	{
		bool success = HeapSetInformation(hHeaps[i], HeapCompatibilityInformation, &ulEnableLFH, sizeof(ulEnableLFH));
		if (success)
			heap_enable_lfh_error[i] = 0;
		else
			heap_enable_lfh_error[i] = GetLastError();
	}
#endif
	
	// *FIX: global
	gIconResource = MAKEINTRESOURCE(IDI_LL_ICON);

	LLAppViewerWin32* viewer_app_ptr = new LLAppViewerWin32(lpCmdLine);
	
	viewer_app_ptr->setErrorHandler(LLAppViewer::handleViewerCrash);

	// Set a debug info flag to indicate if multiple instances are running.
	bool found_other_instance = !create_app_mutex();
	gDebugInfo["FoundOtherInstanceAtStartup"] = LLSD::Boolean(found_other_instance);

	bool ok = viewer_app_ptr->init();
	if(!ok)
	{
		LL_WARNS() << "Application init failed." << LL_ENDL;
		return -1;
	}
	
#ifdef USE_NVAPI
	NvAPI_Status status;
    
	// Initialize NVAPI
	status = NvAPI_Initialize();
	NvDRSSessionHandle hSession = 0;

    if (status == NVAPI_OK) 
	{
		// Create the session handle to access driver settings
		status = NvAPI_DRS_CreateSession(&hSession);
		if (status != NVAPI_OK) 
		{
			nvapi_error(status);
		}
		else
		{
			//override driver setting as needed
			ll_nvapi_init(hSession);
		}
	}
#endif

	// Have to wait until after logging is initialized to display LFH info
	if (num_heaps > 0)
	{
		LL_INFOS() << "Attempted to enable LFH for " << num_heaps << " heaps." << LL_ENDL;
		for(S32 i = 0; i < num_heaps; i++)
		{
			if (heap_enable_lfh_error[i])
			{
				LL_INFOS() << "  Failed to enable LFH for heap: " << i << " Error: " << heap_enable_lfh_error[i] << LL_ENDL;
			}
		}
	}
	
	// Run the application main loop
	if(!LLApp::isQuitting()) 
	{
		viewer_app_ptr->mainLoop();
	}

	if (!LLApp::isError())
	{
		//
		// We don't want to do cleanup here if the error handler got called -
		// the assumption is that the error handler is responsible for doing
		// app cleanup if there was a problem.
		//
#if WINDOWS_CRT_MEM_CHECKS
    LL_INFOS() << "CRT Checking memory:" << LL_ENDL;
	if (!_CrtCheckMemory())
	{
		LL_WARNS() << "_CrtCheckMemory() failed at prior to cleanup!" << LL_ENDL;
	}
	else
	{
		LL_INFOS() << " No corruption detected." << LL_ENDL;
	}
#endif
	
	gGLActive = TRUE;

	viewer_app_ptr->cleanup();
	
#if WINDOWS_CRT_MEM_CHECKS
    LL_INFOS() << "CRT Checking memory:" << LL_ENDL;
	if (!_CrtCheckMemory())
	{
		LL_WARNS() << "_CrtCheckMemory() failed after cleanup!" << LL_ENDL;
	}
	else
	{
		LL_INFOS() << " No corruption detected." << LL_ENDL;
	}
#endif
	 
	}
	delete viewer_app_ptr;
	viewer_app_ptr = NULL;

	//start updater
	if(LLAppViewer::sUpdaterInfo)
	{
		_spawnl(_P_NOWAIT, LLAppViewer::sUpdaterInfo->mUpdateExePath.c_str(), LLAppViewer::sUpdaterInfo->mUpdateExePath.c_str(), LLAppViewer::sUpdaterInfo->mParams.str().c_str(), NULL);

		delete LLAppViewer::sUpdaterInfo ;
		LLAppViewer::sUpdaterInfo = NULL ;
	}

#ifdef USE_NVAPI
	// (NVAPI) (6) We clean up. This is analogous to doing a free()
	if (hSession)
	{
		NvAPI_DRS_DestroySession(hSession);
		hSession = 0;
	}
#endif
	
	return 0;
}

#if DEBUGGING_SEH_FILTER
// The compiler doesn't like it when you use __try/__except blocks
// in a method that uses object destructors. Go figure.
// This winmain just calls the real winmain inside __try.
// The __except calls our exception filter function. For debugging purposes.
int APIENTRY WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR     lpCmdLine,
                     int       nCmdShow)
{
    __try
    {
        WINMAIN(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
    }
    __except( viewer_windows_exception_handler( GetExceptionInformation() ) )
    {
        _tprintf( _T("Exception handled.\n") );
    }
}
#endif

void LLAppViewerWin32::disableWinErrorReporting()
{
	const char win_xp_string[] = "Microsoft Windows XP";
	BOOL is_win_xp = ( getOSInfo().getOSString().substr(0, strlen(win_xp_string) ) == win_xp_string );		/* Flawfinder: ignore*/
	if( is_win_xp )
	{
		// Note: we need to use run-time dynamic linking, because load-time dynamic linking will fail
		// on systems that don't have the library installed (all non-Windows XP systems)
		HINSTANCE fault_rep_dll_handle = LoadLibrary(L"faultrep.dll");		/* Flawfinder: ignore */
		if( fault_rep_dll_handle )
		{
			pfn_ADDEREXCLUDEDAPPLICATIONA pAddERExcludedApplicationA  = (pfn_ADDEREXCLUDEDAPPLICATIONA) GetProcAddress(fault_rep_dll_handle, "AddERExcludedApplicationA");
			if( pAddERExcludedApplicationA )
			{

				// Strip the path off the name
				const char* executable_name = gDirUtilp->getExecutableFilename().c_str();

				if( 0 == pAddERExcludedApplicationA( executable_name ) )
				{
					U32 error_code = GetLastError();
					LL_INFOS() << "AddERExcludedApplication() failed with error code " << error_code << LL_ENDL;
				}
				else
				{
					LL_INFOS() << "AddERExcludedApplication() success for " << executable_name << LL_ENDL;
				}
			}
			FreeLibrary( fault_rep_dll_handle );
		}
	}
}

const S32 MAX_CONSOLE_LINES = 500;

void create_console()
{

	CONSOLE_SCREEN_BUFFER_INFO coninfo;

	// allocate a console for this app
	AllocConsole();

	// set the screen buffer to be big enough to let us scroll text
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &coninfo);
	coninfo.dwSize.Y = MAX_CONSOLE_LINES;
	SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), coninfo.dwSize);

	// Redirect the CRT standard input, output, and error handles to the console
	freopen("CONIN$", "r", stdin);
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);

	setvbuf( stdin, NULL, _IONBF, 0 );
	setvbuf( stdout, NULL, _IONBF, 0 );
	setvbuf( stderr, NULL, _IONBF, 0 );


}


LLAppViewerWin32::LLAppViewerWin32(const char* cmd_line) :
    mCmdLine(cmd_line)
{
}

LLAppViewerWin32::~LLAppViewerWin32()
{
}

bool LLAppViewerWin32::init()
{
	// Platform specific initialization.
	
	// Turn off Windows XP Error Reporting
	// (Don't send our data to Microsoft--at least until we are Logo approved and have a way
	// of getting the data back from them.)
	//
	// LL_INFOS() << "Turning off Windows error reporting." << LL_ENDL;
	disableWinErrorReporting();

#ifndef LL_RELEASE_FOR_DOWNLOAD
	LLWinDebug::instance().init();
#endif

#if LL_WINDOWS
#if LL_SEND_CRASH_REPORTS

	LLAppViewer* pApp = LLAppViewer::instance();
	pApp->initCrashReporting();

#endif
#endif

	bool success = LLAppViewer::init();

    return success;
}

bool LLAppViewerWin32::cleanup()
{
	bool result = LLAppViewer::cleanup();

	gDXHardware.cleanup();

	return result;
}

bool LLAppViewerWin32::initLogging()
{
	return LLAppViewer::initLogging();
}

void LLAppViewerWin32::initConsole()
{
	// pop up debug console
	create_console();
	return LLAppViewer::initConsole();
}

void write_debug_dx(const char* str)
{
	std::string value = gDebugInfo["DXInfo"].asString();
	value += str;
	gDebugInfo["DXInfo"] = value;
}

void write_debug_dx(const std::string& str)
{
	write_debug_dx(str.c_str());
}

bool LLAppViewerWin32::initHardwareTest()
{
	//
	// Do driver verification and initialization based on DirectX
	// hardware polling and driver versions
	//
	if (FALSE == gSavedSettings.getBOOL("NoHardwareProbe"))
	{
		BOOL vram_only = !gSavedSettings.getBOOL("ProbeHardwareOnStartup");

		// per DEV-11631 - disable hardware probing for everything
		// but vram.
		vram_only = TRUE;

		LLSplashScreen::update(LLTrans::getString("StartupDetectingHardware"));

		LL_DEBUGS("AppInit") << "Attempting to poll DirectX for hardware info" << LL_ENDL;
		gDXHardware.setWriteDebugFunc(write_debug_dx);
		BOOL probe_ok = gDXHardware.getInfo(vram_only);

		if (!probe_ok
			&& gSavedSettings.getWarning("AboutDirectX9"))
		{
			LL_WARNS("AppInit") << "DirectX probe failed, alerting user." << LL_ENDL;

			// Warn them that runnin without DirectX 9 will
			// not allow us to tell them about driver issues
			std::ostringstream msg;
			msg << LLTrans::getString ("MBNoDirectX");
			S32 button = OSMessageBox(
				msg.str(),
				LLTrans::getString("MBWarning"),
				OSMB_YESNO);
			if (OSBTN_NO== button)
			{
				LL_INFOS("AppInit") << "User quitting after failed DirectX 9 detection" << LL_ENDL;
				LLWeb::loadURLExternal(DIRECTX_9_URL, false);
				return false;
			}
			gSavedSettings.setWarning("AboutDirectX9", FALSE);
		}
		LL_DEBUGS("AppInit") << "Done polling DirectX for hardware info" << LL_ENDL;

		// Only probe once after installation
		gSavedSettings.setBOOL("ProbeHardwareOnStartup", FALSE);

		// Disable so debugger can work
		std::string splash_msg;
		LLStringUtil::format_map_t args;
		args["[APP_NAME]"] = LLAppViewer::instance()->getSecondLifeTitle();
		splash_msg = LLTrans::getString("StartupLoading", args);

		LLSplashScreen::update(splash_msg);
	}

	if (!restoreErrorTrap())
	{
		LL_WARNS("AppInit") << " Someone took over my exception handler (post hardware probe)!" << LL_ENDL;
	}

	if (gGLManager.mVRAM == 0)
	{
		gGLManager.mVRAM = gDXHardware.getVRAM();
	}

	LL_INFOS("AppInit") << "Detected VRAM: " << gGLManager.mVRAM << LL_ENDL;

	return true;
}

bool LLAppViewerWin32::initParseCommandLine(LLCommandLineParser& clp)
{
	if (!clp.parseCommandLineString(mCmdLine))
	{
		return false;
	}

	// Find the system language.
	FL_Locale *locale = NULL;
	FL_Success success = FL_FindLocale(&locale, FL_MESSAGES);
	if (success != 0)
	{
		if (success >= 2 && locale->lang) // confident!
		{
			LL_INFOS("AppInit") << "Language: " << ll_safe_string(locale->lang) << LL_ENDL;
			LL_INFOS("AppInit") << "Location: " << ll_safe_string(locale->country) << LL_ENDL;
			LL_INFOS("AppInit") << "Variant: " << ll_safe_string(locale->variant) << LL_ENDL;
			LLControlVariable* c = gSavedSettings.getControl("SystemLanguage");
			if(c)
			{
				c->setValue(std::string(locale->lang), false);
			}
		}
	}
	FL_FreeLocale(&locale);

	return true;
}

bool LLAppViewerWin32::restoreErrorTrap()
{	
	return true;
	//return LLWinDebug::checkExceptionHandler();
}

void LLAppViewerWin32::initCrashReporting(bool reportFreeze)
{
	// Singu Note: we don't fork the crash logger on start
}

//virtual
bool LLAppViewerWin32::sendURLToOtherInstance(const std::string& url)
{
	wchar_t window_class[256]; /* Flawfinder: ignore */   // Assume max length < 255 chars.
	mbstowcs(window_class, sWindowClass.c_str(), 255);
	window_class[255] = 0;
	// Use the class instead of the window name.
	HWND other_window = FindWindow(window_class, NULL);

	if (other_window != NULL)
	{
		LL_DEBUGS() << "Found other window with the name '" << getWindowTitle() << "'" << LL_ENDL;
		COPYDATASTRUCT cds;
		const S32 SLURL_MESSAGE_TYPE = 0;
		cds.dwData = SLURL_MESSAGE_TYPE;
		cds.cbData = url.length() + 1;
		cds.lpData = (void*)url.c_str();

		LRESULT msg_result = SendMessage(other_window, WM_COPYDATA, NULL, (LPARAM)&cds);
		LL_DEBUGS() << "SendMessage(WM_COPYDATA) to other window '" 
				 << getWindowTitle() << "' returned " << msg_result << LL_ENDL;
		return true;
	}
	return false;
}


std::string LLAppViewerWin32::generateSerialNumber()
{
	char serial_md5[MD5HEX_STR_SIZE];		// Flawfinder: ignore
	serial_md5[0] = 0;

	DWORD serial = 0;
	DWORD flags = 0;
	BOOL success = GetVolumeInformation(
			L"C:\\",
			NULL,		// volume name buffer
			0,			// volume name buffer size
			&serial,	// volume serial
			NULL,		// max component length
			&flags,		// file system flags
			NULL,		// file system name buffer
			0);			// file system name buffer size
	if (success)
	{
		LLMD5 md5;
		md5.update( (unsigned char*)&serial, sizeof(DWORD));
		md5.finalize();
		md5.hex_digest(serial_md5);
	}
	else
	{
		LL_WARNS() << "GetVolumeInformation failed" << LL_ENDL;
	}
	return serial_md5;
}