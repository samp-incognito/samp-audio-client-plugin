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

#include "audio.h"

#include "core.h"

#include <BASS/bass.h>
#include <BASS/bassmix.h>
#include <BASS/basswma.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include <map>
#include <string>
#include <vector>

Audio::Audio()
{
	int errorCodes[] =
	{
		BASS_OK,
		BASS_ERROR_MEM,
		BASS_ERROR_FILEOPEN,
		BASS_ERROR_DRIVER,
		BASS_ERROR_BUFLOST,
		BASS_ERROR_HANDLE,
		BASS_ERROR_FORMAT,
		BASS_ERROR_POSITION,
		BASS_ERROR_INIT,
		BASS_ERROR_START,
		BASS_ERROR_ALREADY,
		BASS_ERROR_NOCHAN,
		BASS_ERROR_ILLTYPE,
		BASS_ERROR_ILLPARAM,
		BASS_ERROR_NO3D,
		BASS_ERROR_NOEAX,
		BASS_ERROR_DEVICE,
		BASS_ERROR_NOPLAY,
		BASS_ERROR_FREQ,
		BASS_ERROR_NOTFILE,
		BASS_ERROR_NOHW,
		BASS_ERROR_EMPTY,
		BASS_ERROR_NONET,
		BASS_ERROR_CREATE,
		BASS_ERROR_NOFX,
		BASS_ERROR_NOTAVAIL,
		BASS_ERROR_DECODE,
		BASS_ERROR_DX,
		BASS_ERROR_TIMEOUT,
		BASS_ERROR_FILEFORM,
		BASS_ERROR_SPEAKER,
		BASS_ERROR_VERSION,
		BASS_ERROR_CODEC,
		BASS_ERROR_ENDED,
		BASS_ERROR_BUSY,
		BASS_ERROR_UNKNOWN
	};
	const char *errorMessages[] =
	{
		"No error",
		"Memory error",
		"Cannot open the file",
		"Cannot find a free or valid driver",
		"The sample buffer was lost",
		"Invalid handle",
		"Unsupported sample format",
		"Invalid position",
		"BASS_Init has not been successfully called",
		"BASS_Start has not been successfully called",
		"Already initialized",
		"Cannot get a free channel",
		"An illegal type was specified",
		"An illegal parameter was specified",
		"No 3D support",
		"No EAX support",
		"Illegal device number",
		"Not playing",
		"Illegal sample rate",
		"The stream is not a file stream",
		"No hardware voices available",
		"The MOD music has no sequence data",
		"No connection could be opened",
		"Could not create the file",
		"Effects are not available",
		"Requested data is not available",
		"The channel is a decoding channel",
		"A sufficient DirectX version is not installed",
		"Connection timed out",
		"Unsupported file format",
		"Unavailable speaker",
		"Invalid BASS version",
		"Codec is not available or supported",
		"The channel or file has ended",
		"The device is busy",
		"Unknown error"
	};
	for (std::size_t i = 0; i < sizeof(errorCodes) / sizeof(int); ++i)
	{
		errors.insert(std::make_pair(errorCodes[i], errorMessages[i]));
	}
	stopped = false;
}

Audio::Stream::Stream()
{
	for (int i = 0; i < 9; ++i)
	{
		effects[i] = 0;
	}
	channel = 0;
	mixer = 0;
}

Audio::Stream::Position::Position()
{
	distance = 0.0f;
	vector = BASS_3DVECTOR(0.0f, 0.0f, 0.0f);
}

Audio::Stream::Sequence::Sequence()
{
	count = 0;
	downmix = false;
	id = 0;
	loop = false;
	pause = false;
}

void Audio::freeMemory()
{
	files.clear();
	streams.clear();
	stopped = true;
	BASS_Stop();
}

std::string Audio::getErrorMessage()
{
	int errorCode = BASS_ErrorGetCode();
	std::map<int, std::string>::iterator e = errors.find(errorCode);
	if (e != errors.end())
	{
		return e->second;
	}
	return "Error code not found";
}

bool Audio::isModuleFile(std::string fileName)
{
	const char *fileExtensions[] =
	{
		".it",
		".mo3",
		".mod",
		".mtm",
		".s3m",
		".umx",
		".xm"
	};
	for (std::size_t i = 0; i < sizeof(fileExtensions) / sizeof(const char*); ++i)
	{
		if (boost::algorithm::iends_with(fileName, fileExtensions[i]))
		{
			return true;
		}
	}
	return false;
}

void Audio::initializeSequence(int handleID)
{
	std::map<int, Stream>::iterator s = streams.find(handleID);
	if (s == streams.end())
	{
		return;
	}
	if (!s->second.sequence)
	{
		return;
	}
	s->second.name = boost::str(boost::format("Sequence ID: %1%") % s->second.sequence->id);
	if (!s->second.sequence->downmix)
	{
		s->second.mixer = BASS_Mixer_StreamCreate(44100, 2, BASS_SAMPLE_FLOAT | BASS_MIXER_END | BASS_STREAM_AUTOFREE);
	}
	else
	{
		s->second.mixer = BASS_Mixer_StreamCreate(44100, 1, BASS_SAMPLE_FLOAT | BASS_SAMPLE_3D | BASS_MIXER_END | BASS_STREAM_AUTOFREE);
		BASS_ChannelSet3DAttributes(s->second.mixer, BASS_3DMODE_RELATIVE, 1.0f, 0.5f, 360, 360, 1.0f);
		BASS_Apply3D();
	}
	if (!s->second.mixer)
	{
		core->getProgram()->logText(boost::str(boost::format("Error creating mixer for playback of \"%1%\": %2%") % s->second.name % core->getAudio()->getErrorMessage()));
		core->getNetwork()->sendAsync(boost::str(boost::format("%1%\t%2%\t%3%\n") % Client::Play % handleID % Client::Failure));
		streams.erase(s);
		return;
	}
	playNextFileInSequence(s->first);
	if (!s->second.channel)
	{
		core->getNetwork()->sendAsync(boost::str(boost::format("%1%\t%2%\t%3%\n") % Client::Play % handleID % Client::Failure));
		streams.erase(s);
		return;
	}
	BASS_ChannelPlay(s->second.mixer, false);
	if (s->second.sequence->pause)
	{
		BASS_ChannelPause(s->second.mixer);
	}
	core->getProgram()->logText(boost::str(boost::format("Started: \"%1%\"") % s->second.name));
	core->getNetwork()->sendAsync(boost::str(boost::format("%1%\t%2%\t%3%\n") % Client::Play % handleID % Client::Success));
	BASS_ChannelSetSync(s->second.mixer, BASS_SYNC_END | BASS_SYNC_MIXTIME, 0, &onStreamEnd, NULL);
	BASS_ChannelSetSync(s->second.mixer, BASS_SYNC_FREE, 0, &onStreamFree, NULL);
}

void Audio::playNextFileInSequence(int handleID)
{
	std::map<int, Stream>::iterator s = streams.find(handleID);
	if (s == streams.end())
	{
		return;
	}
	if (!s->second.sequence)
	{
		return;
	}
	if (s->second.sequence->count == s->second.sequence->audioIDs.size())
	{
		if (!s->second.sequence->loop)
		{
			return;
		}
		s->second.sequence->count = 0;
	}
	std::vector<int>::iterator b = s->second.sequence->audioIDs.begin();
	for (std::vector<int>::iterator a = s->second.sequence->audioIDs.begin(); a != s->second.sequence->audioIDs.end(); ++a)
	{
		if (std::distance(b, a) == s->second.sequence->count)
		{
			std::wstring filePath;
			std::map<int, std::string>::iterator f = files.find(*a);
			if (f != files.end())
			{
				filePath = boost::str(boost::wformat(L"%1%\\%2%") % core->getProgram()->downloadPath % core->strtowstr(f->second));
				if (!boost::filesystem::exists(filePath))
				{
					core->getProgram()->logText(boost::str(boost::format("Error creating stream for playback of \"%1%\": File does not exist") % f->second));
					return;
				}
			}
			else
			{
				core->getNetwork()->sendAsync(boost::str(boost::format("%1%\t%2%\t%3%\n") % Client::Play % handleID % Client::Failure));
				return;
			}
			if (isModuleFile(f->second))
			{
				s->second.channel = BASS_MusicLoad(false, filePath.c_str(), 0, 0, BASS_SAMPLE_FLOAT | BASS_MUSIC_PRESCAN | BASS_MUSIC_DECODE | BASS_UNICODE, 0);
			}
			else
			{
				s->second.channel = BASS_StreamCreateFile(false, filePath.c_str(), 0, 0, BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_UNICODE);
			}
			if (!s->second.channel)
			{
				core->getProgram()->logText(boost::str(boost::format("Error creating stream for playback of \"%1%\": %2%") % f->second % core->getAudio()->getErrorMessage()));
				return;
			}
			DWORD channelFlags = BASS_STREAM_AUTOFREE | BASS_MIXER_NORAMPIN;
			if (s->second.sequence->downmix)
			{
				channelFlags |= BASS_MIXER_DOWNMIX;
			}
			BASS_Mixer_StreamAddChannel(s->second.mixer, s->second.channel, channelFlags);
			BASS_ChannelSetPosition(s->second.mixer, 0, BASS_POS_BYTE);
			break;
		}
	}
	++s->second.sequence->count;
}

void Audio::playStream(int handleID, bool pause, bool loop, bool downmix)
{
	std::map<int, Stream>::iterator s = streams.find(handleID);
	if (s == streams.end())
	{
		return;
	}
	bool remote = false;
	std::wstring filePath;
	if (boost::algorithm::icontains(s->second.name, "://"))
	{
		remote = true;
	}
	if (!remote)
	{
		int audioID = 0;
		try
		{
			audioID = boost::lexical_cast<int>(s->second.name);
		}
		catch (boost::bad_lexical_cast &)
		{
			audioID = 0;
		}
		std::map<int, std::string>::iterator f = files.find(audioID);
		if (f != files.end())
		{
			s->second.name = f->second;
			filePath = boost::str(boost::wformat(L"%1%\\%2%") % core->getProgram()->downloadPath % core->strtowstr(f->second));
			if (!boost::filesystem::exists(filePath))
			{
				core->getProgram()->logText(boost::str(boost::format("Error opening \"%1%\" for playback: File does not exist") % s->second.name));
				core->getNetwork()->sendAsync(boost::str(boost::format("%1%\t%2%\t%3%\n") % Client::Play % handleID % Client::Failure));
				streams.erase(s);
				return;
			}
		}
		else
		{
			core->getNetwork()->sendAsync(boost::str(boost::format("%1%\t%2%\t%3%\n") % Client::Play % handleID % Client::Failure));
			streams.erase(s);
			return;
		}
	}
	else
	{
		if (!core->getProgram()->settings->streamFiles)
		{
			core->getProgram()->logText(boost::str(boost::format("Playback of \"%1%\" rejected (file streaming disabled)") % s->second.name));
			core->getNetwork()->sendAsync(boost::str(boost::format("%1%\t%2%\t%3%\n") % Client::Play % handleID % Client::Failure));
			streams.erase(s);
			return;
		}
	}
	if (!downmix)
	{
		s->second.mixer = BASS_Mixer_StreamCreate(44100, 2, BASS_SAMPLE_FLOAT | BASS_MIXER_END | BASS_STREAM_AUTOFREE);
	}
	else
	{
		s->second.mixer = BASS_Mixer_StreamCreate(44100, 1, BASS_SAMPLE_FLOAT | BASS_SAMPLE_3D | BASS_MIXER_END | BASS_STREAM_AUTOFREE);
		BASS_ChannelSet3DAttributes(s->second.mixer, BASS_3DMODE_RELATIVE, 1.0f, 0.5f, 360, 360, 1.0f);
		BASS_Apply3D();
	}
	if (!s->second.mixer)
	{
		core->getProgram()->logText(boost::str(boost::format("Error creating mixer for playback of \"%1%\": %2%") % s->second.name % core->getAudio()->getErrorMessage()));
		core->getNetwork()->sendAsync(boost::str(boost::format("%1%\t%2%\t%3%\n") % Client::Play % handleID % Client::Failure));
		streams.erase(s);
		return;
	}
	if (!remote)
	{
		if (isModuleFile(s->second.name))
		{
			s->second.channel = BASS_MusicLoad(false, filePath.c_str(), 0, 0, BASS_SAMPLE_FLOAT | BASS_MUSIC_PRESCAN | BASS_MUSIC_DECODE | BASS_UNICODE, 0);
		}
		else
		{
			s->second.channel = BASS_StreamCreateFile(false, filePath.c_str(), 0, 0, BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_UNICODE);
		}
	}
	else
	{
		s->second.channel = BASS_StreamCreateURL(s->second.name.c_str(), 0, BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_STREAM_STATUS, NULL, NULL);
	}
	if (!s->second.channel)
	{
		core->getProgram()->logText(boost::str(boost::format("Error creating stream for playback of \"%1%\": %2%") % s->second.name % core->getAudio()->getErrorMessage()));
		core->getNetwork()->sendAsync(boost::str(boost::format("%1%\t%2%\t%3%\n") % Client::Play % handleID % Client::Failure));
		streams.erase(s);
		return;
	}
	if (loop)
	{
		BASS_ChannelFlags(s->second.channel, BASS_SAMPLE_LOOP, BASS_SAMPLE_LOOP);
	}
	DWORD channelFlags = BASS_STREAM_AUTOFREE;
	if (downmix)
	{
		channelFlags |= BASS_MIXER_DOWNMIX;
	}
	BASS_Mixer_StreamAddChannel(s->second.mixer, s->second.channel, channelFlags);
	BASS_ChannelPlay(s->second.mixer, false);
	if (pause)
	{
		BASS_ChannelPause(s->second.mixer);
	}
	core->getProgram()->logText(boost::str(boost::format("%1%: \"%2%\"") % (remote ? "Streaming" : (pause ? "Paused" : (loop ? "Looping" : "Playing"))) % s->second.name));
	core->getNetwork()->sendAsync(boost::str(boost::format("%1%\t%2%\t%3%\n") % Client::Play % handleID % Client::Success));
	if (remote)
	{
		const char *station = NULL;
		std::string stationBuffer;
		if (BASS_ChannelGetTags(s->second.channel, BASS_TAG_WMA))
		{
			station = BASS_ChannelGetTags(s->second.channel, BASS_TAG_WMA);
			if (station)
			{
				for ( ; *station; station += strlen(station) + 1)
				{
					stationBuffer = station;
					if (boost::algorithm::istarts_with(stationBuffer, "title="))
					{
						stationBuffer.erase(0, 6);
						boost::algorithm::trim(stationBuffer);
						core->getProgram()->logText(boost::str(boost::format("Listening to: \"%1%\"") % stationBuffer));
					}
				}
				if (BASS_ChannelGetTags(s->second.channel, BASS_TAG_WMA))
				{
					updateMeta(handleID);
				}
				BASS_ChannelSetSync(s->second.channel, BASS_SYNC_WMA_META, 0, &onMetaChange, NULL);
			}
		}
		else
		{
			if (BASS_ChannelGetTags(s->second.channel, BASS_TAG_ICY))
			{
				station = BASS_ChannelGetTags(s->second.channel, BASS_TAG_ICY);
			}
			else if (BASS_ChannelGetTags(s->second.channel, BASS_TAG_HTTP))
			{
				station = BASS_ChannelGetTags(s->second.channel, BASS_TAG_HTTP);
			}
			if (station)
			{
				for ( ; *station; station += strlen(station) + 1)
				{
					stationBuffer = station;
					if (boost::algorithm::istarts_with(stationBuffer, "icy-name:"))
					{
						stationBuffer.erase(0, 9);
						boost::algorithm::trim(stationBuffer);
						core->getProgram()->logText(boost::str(boost::format("Listening to: \"%1%\"") % stationBuffer));
					}
				}
				if (BASS_ChannelGetTags(s->second.channel, BASS_TAG_META) || BASS_ChannelGetTags(s->second.channel, BASS_TAG_OGG))
				{
					updateMeta(handleID);
				}
				BASS_ChannelSetSync(s->second.channel, BASS_SYNC_META, 0, &onMetaChange, NULL);
				BASS_ChannelSetSync(s->second.channel, BASS_SYNC_OGG_CHANGE, 0, &onMetaChange, NULL);
			}
		}
	}
	BASS_ChannelSetSync(s->second.mixer, BASS_SYNC_FREE, 0, &onStreamFree, NULL);
}

void Audio::updateMeta(int handleID)
{
	std::map<int, Stream>::iterator s = streams.find(handleID);
	if (s == streams.end())
	{
		return;
	}
	const char *meta = NULL;
	std::string metaBuffer;
	bool retrieved = false;
	if (BASS_ChannelGetTags(s->second.channel, BASS_TAG_WMA_META))
	{
		meta = BASS_ChannelGetTags(s->second.channel, BASS_TAG_WMA_META);
		if (meta)
		{
			metaBuffer = meta;
			boost::iterator_range<std::string::iterator> f = boost::algorithm::ifind_first(metaBuffer, "caption=");
			if (f)
			{
				metaBuffer.erase(f.begin(), f.begin() + 8);
				boost::algorithm::trim(metaBuffer);
				if (metaBuffer.compare(s->second.meta) != 0)
				{
					retrieved = true;
				}
			}
		}
	}
	else if (BASS_ChannelGetTags(s->second.channel, BASS_TAG_META))
	{
		meta = BASS_ChannelGetTags(s->second.channel, BASS_TAG_META);
		if (meta)
		{
			metaBuffer = meta;
			boost::iterator_range<std::string::iterator> f = boost::algorithm::ifind_first(metaBuffer, "streamtitle='");
			if (f)
			{
				metaBuffer.erase(f.begin(), f.begin() + 13);
			}
			boost::iterator_range<std::string::iterator> g = boost::algorithm::find_first(metaBuffer, "';");
			if (g)
			{
				metaBuffer.erase(g.begin(), metaBuffer.end());
			}
			if (f && g)
			{
				boost::algorithm::replace_all(metaBuffer, "*", "");
				boost::algorithm::trim(metaBuffer);
				if (metaBuffer.compare(s->second.meta) != 0)
				{
					retrieved = true;
				}
			}
		}
	}
	else if (BASS_ChannelGetTags(s->second.channel, BASS_TAG_OGG))
	{
		meta = BASS_ChannelGetTags(s->second.channel, BASS_TAG_OGG);
		if (meta)
		{
			std::string artist, title;
			for ( ; *meta; meta += strlen(meta) + 1)
			{
				metaBuffer = meta;
				if (boost::algorithm::istarts_with(metaBuffer, "artist="))
				{
					artist = metaBuffer.substr(7, metaBuffer.length());
					boost::algorithm::replace_all(artist, "*", "");
					boost::algorithm::trim(artist);
				}
				if (boost::algorithm::istarts_with(metaBuffer, "title="))
				{
					title = metaBuffer.substr(6, metaBuffer.length());
					boost::algorithm::replace_all(title, "*", "");
					boost::algorithm::trim(title);
				}
			}
			if (artist.length() && title.length())
			{
				metaBuffer = boost::str(boost::format("%1% - %2%") % artist % title);
				if (metaBuffer.compare(s->second.meta) != 0)
				{
					retrieved = true;
				}
			}
		}
	}
	if (retrieved)
	{
		s->second.meta = metaBuffer;
		core->getProgram()->logText(boost::str(boost::format("Playing: \"%1%\"") % metaBuffer));
		core->getNetwork()->sendAsync(boost::str(boost::format("%1%\t%2%\t%3%\n") % Client::Track % s->first % metaBuffer));
	}
}

void CALLBACK Audio::onMetaChange(HSYNC handle, DWORD channel, DWORD data, void *user)
{
	for (std::map<int, Stream>::iterator s = core->getAudio()->streams.begin(); s != core->getAudio()->streams.end(); ++s)
	{
		if (s->second.channel == channel)
		{
			core->getAudio()->updateMeta(s->first);
			break;
		}
	}
}

void CALLBACK Audio::onStreamEnd(HSYNC handle, DWORD channel, DWORD data, void *user)
{
	for (std::map<int, Stream>::iterator s = core->getAudio()->streams.begin(); s != core->getAudio()->streams.end(); ++s)
	{
		if (s->second.mixer == channel)
		{
			core->getAudio()->playNextFileInSequence(s->first);
			break;
		}
	}
}

void CALLBACK Audio::onStreamFree(HSYNC handle, DWORD channel, DWORD data, void *user)
{
	for (std::map<int, Stream>::iterator s = core->getAudio()->streams.begin(); s != core->getAudio()->streams.end(); ++s)
	{
		if (s->second.mixer == channel)
		{
			core->getProgram()->logText(boost::str(boost::format("Stopped: \"%1%\"") % s->second.name));
			core->getNetwork()->sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Stop % s->first));
			core->getAudio()->streams.erase(s);
			break;
		}
	}
}
