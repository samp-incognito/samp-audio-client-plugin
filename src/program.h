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

#ifndef PROGRAM_H
#define PROGRAM_H

#include <boost/scoped_ptr.hpp>

#include <set>
#include <string>

class Program
{
public:
	Program();

	void logText(const std::string &buffer);
	void start();
	void stop();

	struct Settings
	{
		Settings();

		bool allowRadioStationAdjustment;
		unsigned int connectAttempts;
		unsigned int connectDelay;
		unsigned int connectTimeout;
		bool enableLogging;
		unsigned int networkTimeout;
		bool streamFiles;
		bool transferFiles;
	};

	boost::scoped_ptr<Settings> settings;

	std::string address;
	std::string name;
	std::string port;

	std::wstring downloadPath;
	std::wstring savePath;

	std::set<std::string> acceptedFileExtensions;
	std::set<std::string> illegalCharacters;
private:
	void createLogFile();
	bool initializeAudioDevice();
	void loadPlugins();
	void loadSettings();
	bool readCommandLine();
};

#endif
