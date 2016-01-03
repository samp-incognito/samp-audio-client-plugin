/*
 * Copyright (C) 2012 Incognito
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "loader.h"

#include <BASS/bass.h>

#include <boost/thread.hpp>

#include <windows.h>

void loadPlugin()
{
	boost::this_thread::sleep(boost::posix_time::seconds(5));
	if (!GetModuleHandleW(L"samp.dll"))
	{
		return;
	}
	SetDllDirectoryW(L"libraries");
	HMODULE hModule = LoadLibraryW(L"audio.dll");
	if (!hModule)
	{
		return;
	}
	startPlugin_t startPlugin = (startPlugin_t)GetProcAddress(hModule, "startPlugin"); 
	if (!startPlugin)
	{
		return;
	}
	if (GetModuleHandleW(L"bass.dll"))
	{
		while (BASS_GetDevice() == -1)
		{
			boost::this_thread::sleep(boost::posix_time::seconds(1));
		}
	}
	startPlugin();
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	switch (fdwReason)
	{
		case DLL_PROCESS_ATTACH:
		{
			DisableThreadLibraryCalls(hinstDLL);
			boost::thread thread(loadPlugin);
			break;
		}
	}
	return TRUE;
}
