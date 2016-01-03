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

#include "game.h"

#include "core.h"
#include "plugin.h"

#include <BASS/bass.h>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/format.hpp>

#include <map>

#include <windows.h>

Game::Game(boost::asio::io_service &io_service) : mainTimer(io_service)
{
	camera.reset(new Camera);
	open = false;
	radioStation = 0;
	started = false;
	startMainTimer();
}

Game::Camera::Camera()
{
	frontVector = BASS_3DVECTOR(0.0f, 0.0f, 0.0f);
	positionVector = BASS_3DVECTOR(0.0f, 0.0f, 0.0f);
	topVector = BASS_3DVECTOR(0.0f, 0.0f, 0.0f);
	velocityVector = BASS_3DVECTOR(0.0f, 0.0f, 0.0f);
}

void Game::handleMainTimer(const boost::system::error_code &error)
{
	if (!error)
	{
		if (core->getAudio()->stopped)
		{
			BASS_Start();
		}
		if (*(DWORD*)PLAYER_POINTER_2 != NULL)
		{
			bool focused = *(BYTE*)IN_FOREGROUND != 0;
			bool paused = *(BYTE*)IN_MENU != 0;
			started = true;
			if (focused && !paused)
			{
				BYTE radioVolume = *(BYTE*)RADIO_VOLUME;
				BASS_SetConfig(BASS_CONFIG_GVOL_STREAM, (static_cast<float>(radioVolume) / 64.0f) * 10000);
				if (core->getNetwork()->connected)
				{
					updateCamera();
					adjustChannelVolumes();
					checkRadioStation();
				}
				open = true;
			}
			else
			{
				if (open)
				{
					BASS_SetConfig(BASS_CONFIG_GVOL_STREAM, 0);
					open = false;
				}
			}
		}
		else
		{
			started = false;
		}
		startMainTimer();
	}
}

void Game::startMainTimer()
{
	mainTimer.expires_from_now(boost::posix_time::milliseconds(GAME_TIMER_TICK));
	mainTimer.async_wait(boost::bind(&Game::handleMainTimer, this, boost::asio::placeholders::error));
}

void Game::adjustChannelVolumes()
{
	for (std::map<int, Audio::Stream>::iterator s = core->getAudio()->streams.begin(); s != core->getAudio()->streams.end(); ++s)
	{
		if (s->second.position)
		{
			float distance = checkDistance3D(camera->positionVector.x, camera->positionVector.y, camera->positionVector.z, s->second.position->vector.x, s->second.position->vector.y, s->second.position->vector.z), volume = 0.0f;
			if (distance <= s->second.position->distance)
			{
				volume = 1.0f - (distance / s->second.position->distance);
			}
			else
			{
				volume = 0.0f;
			}
			BASS_ChannelSetAttribute(s->second.mixer, BASS_ATTRIB_VOL, volume);
		}
	}
}

void Game::checkRadioStation()
{
	if (*(DWORD*)VEHICLE_POINTER_2 != NULL)
	{
		BYTE station = getRadioStation();
		if (station != radioStation)
		{
			core->getNetwork()->sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::RadioStation % static_cast<int>(station)));
			radioStation = station;
		}
	}
}

BYTE Game::getRadioStation()
{
	DWORD function = RADIO_STATION;
	BYTE value = 0;
	_asm
	{
		mov		ecx, RADIO_TRACK_MANAGER
		call	function
		mov		value, al
	}
	if (value < 0 || value > 12)
	{
		value = 0;
	}
	return value;
}

void Game::setRadioStation(DWORD station)
{
	if (!core->getProgram()->settings->allowRadioStationAdjustment)
	{
		return;
	}
	if (station < 0 || station > 12)
	{
		return;
	}
	DWORD function = START_RADIO;
	_asm
	{
		push	0
		push	station
		mov		ecx, AUDIO_ENGINE
		call	function
	}
}

void Game::stopRadio()
{
	if (!core->getProgram()->settings->allowRadioStationAdjustment)
	{
		return;
	}
	DWORD function = STOP_RADIO;
	_asm
	{
		push	0
		push	0
		mov		ecx, AUDIO_ENGINE
		call	function
	}
}

void Game::updateCamera()
{
	camera->frontVector.x = *(float*)(CAMERA_MATRIX + 0x20);
	camera->frontVector.y = *(float*)(CAMERA_MATRIX + 0x24);
	camera->frontVector.z = *(float*)(CAMERA_MATRIX + 0x28);
	camera->positionVector.x = *(float*)(CAMERA_MATRIX + 0x30);
	camera->positionVector.y = *(float*)(CAMERA_MATRIX + 0x34);
	camera->positionVector.z = *(float*)(CAMERA_MATRIX + 0x38);
	camera->topVector.x = *(float*)(CAMERA_MATRIX + 0x10);
	camera->topVector.y = *(float*)(CAMERA_MATRIX + 0x14);
	camera->topVector.z = *(float*)(CAMERA_MATRIX + 0x18);
	if (*(DWORD*)VEHICLE_POINTER_2 != NULL)
	{
		if (*(DWORD*)VEHICLE_POINTER_1 != NULL)
		{
			camera->velocityVector.x = *(float*)(*(DWORD*)VEHICLE_POINTER_1 + 0x44);
			camera->velocityVector.y = *(float*)(*(DWORD*)VEHICLE_POINTER_1 + 0x48);
			camera->velocityVector.z = *(float*)(*(DWORD*)VEHICLE_POINTER_1 + 0x4C);
		}
	}
	else
	{
		if (*(DWORD*)PLAYER_POINTER_1 != NULL)
		{
			camera->velocityVector.x = *(float*)(*(DWORD*)PLAYER_POINTER_1 + 0x44);
			camera->velocityVector.y = *(float*)(*(DWORD*)PLAYER_POINTER_1 + 0x48);
			camera->velocityVector.z = *(float*)(*(DWORD*)PLAYER_POINTER_1 + 0x4C);
		}
	}
	BASS_Set3DPosition(&camera->positionVector, &camera->velocityVector, &camera->frontVector, &camera->topVector);
	BASS_Apply3D();
}
