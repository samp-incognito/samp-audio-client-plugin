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

#ifndef CORE_H
#define CORE_H

#include "audio.h"
#include "game.h"
#include "network.h"
#include "program.h"

#include <boost/asio.hpp>
#include <boost/scoped_ptr.hpp>

#include <string>

class Core
{
public:
	Core();

	inline Audio *getAudio()
	{
		return audio.get();
	}

	inline Game *getGame()
	{
		return game.get();
	}

	inline Network *getNetwork()
	{
		return network.get();
	}

	inline Program *getProgram()
	{
		return program.get();
	}

	std::wstring strtowstr(const std::string &input);
	std::string wstrtostr(const std::wstring &input);

	boost::asio::io_service io_service;
private:
	boost::scoped_ptr<Audio> audio;
	boost::scoped_ptr<Game> game;
	boost::scoped_ptr<Network> network;
	boost::scoped_ptr<Program> program;
};

extern boost::scoped_ptr<Core> core;

#endif
