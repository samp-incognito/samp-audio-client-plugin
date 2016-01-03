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

#ifndef AUDIO_H
#define AUDIO_H

#include <BASS/bass.h>

#include <boost/shared_ptr.hpp>

#include <map>
#include <string>
#include <vector>

class Audio
{
public:
	Audio();

	struct Stream
	{
		Stream();

		struct Position
		{
			Position();

			float distance;
			BASS_3DVECTOR vector;
		};

		boost::shared_ptr<Position> position;

		struct Sequence
		{
			Sequence();

			bool downmix;
			bool loop;
			bool pause;

			int count;
			int id;

			std::vector<int> audioIDs;
		};

		boost::shared_ptr<Sequence> sequence;

		HFX effects[9];

		DWORD channel;
		DWORD mixer;

		std::string name;
		std::string meta;
	};

	std::map<int, std::string> files;
	std::map<int, Stream> streams;

	bool stopped;

	void freeMemory();
	std::string getErrorMessage();

	void initializeSequence(int handleID);
	void playNextFileInSequence(int handleID);
	void playStream(int handleID, bool pause, bool loop, bool downmix);
	void updateMeta(int handleID);

	static void CALLBACK onMetaChange(HSYNC handle, DWORD channel, DWORD data, void *user);
	static void CALLBACK onStreamEnd(HSYNC handle, DWORD channel, DWORD data, void *user);
	static void CALLBACK onStreamFree(HSYNC handle, DWORD channel, DWORD data, void *user);
private:
	std::map<int, std::string> errors;

	bool isModuleFile(std::string fileName);
};

#endif
