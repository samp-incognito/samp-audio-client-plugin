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

#ifndef NETWORK_H
#define NETWORK_H

#include "plugin.h"

#include <boost/array.hpp>
#include <boost/asio.hpp>

#include <urdl/read_stream.hpp>

#include <fstream>
#include <string>
#include <queue>
#include <vector>

class Network
{
public:
	Network(boost::asio::io_service &io_service);

	void sendAsync(const std::string &buffer);

	void startMainTimer();

	void closeConnection();

	bool connected;
private:
	void handleConnect(const boost::system::error_code &error, boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
	void handleConnectTimer(const boost::system::error_code &error, boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
	void handleMainTimer(const boost::system::error_code &error);
	void handleOpenStream(const boost::system::error_code &error);
	void handleRead(const boost::system::error_code &error, std::size_t transferredBytes);
	void handleResolve(const boost::system::error_code &error, boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
	void handleReadFile(const boost::system::error_code &error, std::size_t transferredBytes);
	void handleReadStream(const boost::system::error_code &error, std::size_t transferredBytes);
	void handleTimeoutTimer(const boost::system::error_code &error);
	void handleWrite(const boost::system::error_code &error);

	void startAsync();
	void stopAsync();

	void startConnectTimer(boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
	void startTimeoutTimer();
	void stopConnectTimer();
	void stopMainTimer();
	void stopTimeoutTimer();

	void parseBuffer(const std::string &buffer);
	std::string outputFileSize(std::size_t bytes);

	void performConnect();
	void performMessage();
	void performName();
	void performTransfer();
	void performPlay();
	void performPlaySequence();
	void performPause();
	void performResume();
	void performStop();
	void performRestart();
	void performGetPosition();
	void performSetPosition();
	void performSetVolume();
	void performSetFX();
	void performRemoveFX();
	void performSet3DPosition();
	void performRemove3DPosition();
	void performGetRadioStation();
	void performSetRadioStation();
	void performStopRadio();

	struct File
	{
		boost::array<char, MAX_BUFFER> buffer;
		std::fstream handle;
		int id;
		std::string name;
		std::wstring path;
		std::size_t size;
		bool transferable;
		std::string url;
	};

	boost::shared_ptr<File> file;

	unsigned int attempts;
	bool authenticated;
	bool connecting;
	boost::asio::ip::tcp::socket clientSocket;
	boost::asio::deadline_timer connectTimer;
	std::vector<std::string> commandTokens;
	DWORD lastCommunication;
	boost::asio::deadline_timer mainTimer;
	std::vector<std::string> messageTokens;
	std::queue<std::string> pendingMessages;
	char receivedData[MAX_BUFFER];
	urdl::read_stream readStream;
	boost::asio::ip::tcp::resolver resolver;
	std::string sentData;
	boost::asio::deadline_timer timeoutTimer;
	bool writeInProgress;
};

namespace Client
{
	enum Commands
	{
		Authenticate,
		Transfer,
		Play,
		Sequence,
		Stop,
		RadioStation,
		Track,
		Position
	};

	enum PlayCodes
	{
		Success,
		Failure
	};

	enum TransferCodes
	{
		Local,
		Remote,
		Check,
		Error
	};
};

namespace Server
{
	enum Commands
	{
		Connect,
		Message,
		Name,
		Transfer,
		Play,
		PlaySequence,
		Pause,
		Resume,
		Stop,
		Restart,
		GetPosition,
		SetPosition,
		SetVolume,
		SetFX,
		RemoveFX,
		Set3DPosition,
		Remove3DPosition,
		GetRadioStation,
		SetRadioStation,
		StopRadio
	};
};

#endif
