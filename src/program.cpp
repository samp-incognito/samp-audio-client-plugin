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

#include "program.h"

#include "core.h"

#include <BASS/bass.h>
#include <BASS/basswma.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/scoped_ptr.hpp>

#include <SimpleIni/SimpleIni.h>

#include <fstream>
#include <string>
#include <vector>

#include <shlobj.h>
#include <shlwapi.h>
#include <windows.h>

Program::Program()
{
	wchar_t savePathBuffer[MAX_PATH];
	if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, savePathBuffer) >= 0)
	{
		PathAppendW(savePathBuffer, L"\\SA-MP Audio Plugin");
		if (!boost::filesystem::exists(savePathBuffer))
		{
			boost::filesystem::create_directories(savePathBuffer);
		}
		savePath = savePathBuffer;
	}
	else
	{
		savePath = L".";
	}
	const char *defaultAcceptedFileExtensions[] =
	{
		".afc",
		".aif",
		".aifc",
		".aiff",
		".it",
		".mo3",
		".mod",
		".mp1",
		".mp2",
		".mp3",
		".mtm",
		".oga",
		".ogg",
		".s3m",
		".umx",
		".wav",
		".wave",
		".xm"
	};
	for (std::size_t i = 0; i < sizeof(defaultAcceptedFileExtensions) / sizeof(const char*); ++i)
	{
		acceptedFileExtensions.insert(defaultAcceptedFileExtensions[i]);
	}
	const char *defaultIllegalCharacters[] =
	{
		"\"",
		"*",
		"..",
		"/",
		":",
		"<",
		">",
		"?",
		"\\",
		"|"
	};
	for (std::size_t i = 0; i < sizeof(defaultIllegalCharacters) / sizeof(const char*); ++i)
	{
		illegalCharacters.insert(defaultIllegalCharacters[i]);
	}
	settings.reset(new Settings);
	loadSettings();
	logText("SA-MP Audio Plugin loaded");
}

Program::Settings::Settings()
{
	allowRadioStationAdjustment = true;
	connectAttempts = 10;
	connectDelay = 10000;
	connectTimeout = 5000;
	enableLogging = true;
	networkTimeout = 20000;
	streamFiles = true;
	transferFiles = true;
}

void Program::createLogFile()
{
	std::wstring filePath = boost::str(boost::wformat(L"%1%\\audio.txt") % savePath);
	std::fstream fileOut(filePath.c_str(), std::ios_base::out);
	fileOut.close();
}

bool Program::initializeAudioDevice()
{
	BASS_Free();
	if (BASS_Init(-1, 44100, BASS_DEVICE_3D, NULL, NULL))
	{
		loadPlugins();
		BASS_SetConfig(BASS_CONFIG_NET_PLAYLIST, 1);
		BASS_SetConfig(BASS_CONFIG_NET_TIMEOUT, settings->connectTimeout);
		BASS_SetConfig(BASS_CONFIG_WMA_BASSFILE, 1);
		BASS_SetEAXParameters(-1, 0.0f, -1.0f, -1.0f);
		return true;
	}
	logText(boost::str(boost::format("Error initializing audio device: %1%") % core->getAudio()->getErrorMessage()));
	return false;
}

void Program::loadPlugins()
{
	const char *pluginNames[] =
	{
		"bass_aac.dll",
		"bass_ac3.dll",
		"bass_alac.dll",
		"bass_ape.dll",
		"bass_mpc.dll",
		"bass_spx.dll",
		"bass_tta.dll",
		"bassflac.dll",
		"bassmidi.dll",
		"basswma.dll",
		"basswv.dll"
	};
	HPLUGIN pluginHandles[sizeof(pluginNames) / sizeof(const char*)];
	unsigned int size = sizeof(pluginHandles) / sizeof(HPLUGIN);
	for (unsigned int i = 0; i < size; ++i)
	{
		pluginHandles[i] = BASS_PluginLoad(boost::str(boost::format("plugins\\%1%") % pluginNames[i]).c_str(), 0);
		if (pluginHandles[i])
		{
			const BASS_PLUGININFO *info = BASS_PluginGetInfo(pluginHandles[i]);
			for (unsigned int j = 0; j < info->formatc; ++j)
			{
				std::string extensions = info->formats[j].exts;
				boost::algorithm::erase_all(extensions, "*");
				std::vector<std::string> splitExtensions;
				boost::algorithm::split(splitExtensions, extensions, boost::algorithm::is_any_of(";"));
				for (std::vector<std::string>::iterator e = splitExtensions.begin(); e != splitExtensions.end(); ++e)
				{
					acceptedFileExtensions.insert(*e);
				}
			}
		}
		else
		{
			logText(boost::str(boost::format("Error loading plugin \"%1%\": %2%") % pluginNames[i] % core->getAudio()->getErrorMessage()));
		}
	}
}

void Program::loadSettings()
{
	std::wstring filePath = boost::str(boost::wformat(L"%1%\\audio.ini") % savePath);
	if (!boost::filesystem::exists(filePath))
	{
		std::fstream fileOut(filePath.c_str(), std::ios_base::out);
		fileOut.close();
	}
	CSimpleIniW ini(true, false, true);
	SI_Error error = ini.LoadFile(filePath.c_str());
	if (!error)
	{
		bool modified = false;
		const wchar_t *value[8];
		value[0] = ini.GetValue(L"settings", L"allow_radio_station_adjustment");
		value[1] = ini.GetValue(L"settings", L"connect_attempts");
		value[2] = ini.GetValue(L"settings", L"connect_delay");
		value[3] = ini.GetValue(L"settings", L"connect_timeout");
		value[4] = ini.GetValue(L"settings", L"enable_logging");
		value[5] = ini.GetValue(L"settings", L"network_timeout");
		value[6] = ini.GetValue(L"settings", L"stream_files_from_internet");
		value[7] = ini.GetValue(L"settings", L"transfer_files_from_server");
		if (value[0])
		{
			try
			{
				settings->allowRadioStationAdjustment = boost::lexical_cast<bool>(value[0]);
			}
			catch (boost::bad_lexical_cast &) {}
		}
		else
		{
			ini.SetValue(L"settings", L"allow_radio_station_adjustment", boost::lexical_cast<std::wstring>(settings->allowRadioStationAdjustment).c_str());
			modified = true;
		}
		if (value[1])
		{
			try
			{
				settings->connectAttempts = boost::lexical_cast<unsigned int>(value[1]);
			}
			catch (boost::bad_lexical_cast &) {}
		}
		else
		{
			ini.SetValue(L"settings", L"connect_attempts", boost::lexical_cast<std::wstring>(settings->connectAttempts).c_str());
			modified = true;
		}
		if (value[2])
		{
			try
			{
				settings->connectDelay = boost::lexical_cast<unsigned int>(value[2]) * 1000;
			}
			catch (boost::bad_lexical_cast &) {}
		}
		else
		{
			ini.SetValue(L"settings", L"connect_delay", boost::lexical_cast<std::wstring>(settings->connectDelay / 1000).c_str());
			modified = true;
		}
		if (value[3])
		{
			try
			{
				settings->connectTimeout = boost::lexical_cast<unsigned int>(value[3]) * 1000;
			}
			catch (boost::bad_lexical_cast &) {}
		}
		else
		{
			ini.SetValue(L"settings", L"connect_timeout", boost::lexical_cast<std::wstring>(settings->connectTimeout / 1000).c_str());
			modified = true;
		}
		if (value[4])
		{
			try
			{
				settings->enableLogging = boost::lexical_cast<bool>(value[4]);
			}
			catch (boost::bad_lexical_cast &) {}
		}
		else
		{
			ini.SetValue(L"settings", L"enable_logging", boost::lexical_cast<std::wstring>(settings->enableLogging).c_str());
			modified = true;
		}
		if (value[5])
		{
			try
			{
				settings->networkTimeout = boost::lexical_cast<unsigned int>(value[5]) * 1000;
			}
			catch (boost::bad_lexical_cast &) {}
			if (settings->networkTimeout < 20000)
			{
				settings->networkTimeout = 20000;
			}
		}
		else
		{
			ini.SetValue(L"settings", L"network_timeout", boost::lexical_cast<std::wstring>(settings->networkTimeout / 1000).c_str());
			modified = true;
		}
		if (value[6])
		{
			try
			{
				settings->streamFiles = boost::lexical_cast<bool>(value[6]);
			}
			catch (boost::bad_lexical_cast &) {}
		}
		else
		{
			ini.SetValue(L"settings", L"stream_files_from_internet", boost::lexical_cast<std::wstring>(settings->streamFiles).c_str());
			modified = true;
		}
		if (value[7])
		{
			try
			{
				settings->transferFiles = boost::lexical_cast<bool>(value[7]);
			}
			catch (boost::bad_lexical_cast &) {}
		}
		else
		{
			ini.SetValue(L"settings", L"transfer_files_from_server", boost::lexical_cast<std::wstring>(settings->transferFiles).c_str());
			modified = true;
		}
		if (modified)
		{
			ini.SaveFile(filePath.c_str());
		}
	}
	if (settings->enableLogging)
	{
		createLogFile();
	}
}

void Program::logText(const std::string &buffer)
{
	if (settings->enableLogging)
	{
		SYSTEMTIME time;
		GetLocalTime(&time);
		std::wstring filePath = boost::str(boost::wformat(L"%1%\\audio.txt") % savePath);
		std::string textBuffer = boost::str(boost::format("[%02d:%02d:%02d] %s\n") % time.wHour % time.wMinute % time.wSecond % buffer);
		std::fstream fileOut(filePath.c_str(), std::ios_base::out | std::ios_base::app);
		fileOut.write(textBuffer.c_str(), textBuffer.length());
		fileOut.close();
	}
}

bool Program::readCommandLine()
{
	std::wstring commandLine = GetCommandLineW();
	std::vector<std::wstring> splitCommandLine;
	if (commandLine.find(L"-c") != std::wstring::npos)
	{
		boost::algorithm::split(splitCommandLine, commandLine.substr(commandLine.find(L"-c")), boost::algorithm::is_any_of(L" "));
	}
	if (splitCommandLine.size() < 7)
	{
		logText("Error reading command line: Parameter count mismatch");
		return false;
	}
	for (std::vector<std::wstring>::iterator s = splitCommandLine.begin(); s != splitCommandLine.end(); ++s)
	{
		if (!s->compare(L"-n"))
		{
			std::advance(s, 1);
			name = core->wstrtostr(*s);
		}
		else if (!s->compare(L"-h"))
		{
			std::advance(s, 1);
			address = core->wstrtostr(*s);
		}
		else if (!s->compare(L"-p"))
		{
			std::advance(s, 1);
			port = core->wstrtostr(*s);
		}
	}
	if (name.empty())
	{
		logText("Error reading command line: Could not obtain player name");
		return false;
	}
	if (address.empty())
	{
		logText("Error reading command line: Could not obtain server address");
		return false;
	}
	if (port.empty())
	{
		logText("Error reading command line: Could not obtain server port");
		return false;
	}
	return true;
}

void Program::start()
{
	if (!readCommandLine())
	{
		return;
	}
	if (!initializeAudioDevice())
	{
		return;
	}
	boost::system::error_code error;
	core->io_service.run(error);
}

void Program::stop()
{
	core->getNetwork()->closeConnection();
	core->io_service.stop();
	logText("SA-MP Audio Plugin unloaded");
}
