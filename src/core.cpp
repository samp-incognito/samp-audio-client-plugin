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

#include "core.h"

#include <boost/asio.hpp>
#include <boost/scoped_ptr.hpp>

#include <string>

#include <windows.h>

boost::scoped_ptr<Core> core;

Core::Core()
{
	program.reset(new Program);
	audio.reset(new Audio);
	game.reset(new Game(io_service));
	network.reset(new Network(io_service));
}

std::wstring Core::strtowstr(const std::string &input)
{
	wchar_t *buffer = new wchar_t[input.length() + 1];
	buffer[input.size()] = L'\0';
	MultiByteToWideChar(CP_ACP, 0, input.c_str(), -1, buffer, static_cast<int>(input.length()));
	std::wstring output;
	output = buffer;
	delete[] buffer;
	return output;
}

std::string Core::wstrtostr(const std::wstring &input)
{
	char *buffer = new char[input.length() + 1];
	buffer[input.size()] = '\0';
	WideCharToMultiByte(CP_ACP, 0, input.c_str(), -1, buffer, static_cast<int>(input.length()), NULL, NULL);
	std::string output;
	output = buffer;
	delete[] buffer;
	return output;
}
