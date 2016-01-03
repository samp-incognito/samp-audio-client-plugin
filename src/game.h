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

#ifndef GAME_H
#define GAME_H

#define AUDIO_ENGINE (0xB6BC90)
#define CAMERA_MATRIX (0xB6F99C)
#define IN_FOREGROUND (0x8D621C)
#define IN_MENU (0xBA67A4)
#define PLAYER_POINTER_1 (0xB6F5F0)
#define PLAYER_POINTER_2 (0xB7CD98)
#define RADIO_STATION (0x4E83F0)
#define RADIO_TRACK_MANAGER (0x8CB6F8)
#define RADIO_VOLUME (0xBA6798)
#define START_RADIO (0x507DC0)
#define STOP_RADIO (0x506F70)
#define VEHICLE_POINTER_1 (0xB6F980)
#define VEHICLE_POINTER_2 (0xBA18FC)

#include <BASS/bass.h>

#include <boost/asio.hpp>
#include <boost/scoped_ptr.hpp>

#include <windows.h>

class Game
{
public:
	Game(boost::asio::io_service &io_service);

	void startMainTimer();

	BYTE getRadioStation();
	void setRadioStation(DWORD station);
	void stopRadio();

	bool open;
	bool started;
private:
	void handleMainTimer(const boost::system::error_code &error);

	void adjustChannelVolumes();
	void checkRadioStation();
	void updateCamera();
	void updatePosition();

	inline float checkDistance3D(float x1, float y1, float z1, float x2, float y2, float z2)
	{
		return (((x1 - x2) * (x1 - x2)) + ((y1 - y2) * (y1 - y2)) + ((z1 - z2) * (z1 - z2)));
	}

	struct Camera
	{
		Camera();

		BASS_3DVECTOR frontVector;
		BASS_3DVECTOR positionVector;
		BASS_3DVECTOR topVector;
		BASS_3DVECTOR velocityVector;
	};

	boost::scoped_ptr<Camera> camera;

	BYTE radioStation;

	boost::asio::deadline_timer mainTimer;
};

#endif
