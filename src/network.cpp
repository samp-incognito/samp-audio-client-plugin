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

#include "network.h"

#include "core.h"
#include "plugin.h"

#include <BASS/bass.h>
#include <BASS/bassmix.h>

#include <boost/algorithm/string.hpp>
#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/crc.hpp>
#include <boost/cstdint.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include <urdl/read_stream.hpp>

#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <queue>
#include <vector>

Network::Network(boost::asio::io_service &io_service) : clientSocket(io_service), connectTimer(io_service), mainTimer(io_service), readStream(io_service), resolver(io_service), timeoutTimer(io_service)
{
	attempts = 0;
	authenticated = false;
	connected = false;
	connecting = false;
	lastCommunication = 0;
	writeInProgress = false;
	startMainTimer();
}

void Network::handleConnect(const boost::system::error_code &error, boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
{
	if (!error)
	{
		core->getProgram()->logText(boost::str(boost::format("Connected to %1%") % endpoint_iterator->endpoint()));
		sendAsync(boost::str(boost::format("%1%\t%2%\t%3%\n") % Client::Authenticate % core->getProgram()->name % PLUGIN_VERSION));
		clientSocket.async_read_some(boost::asio::buffer(receivedData), boost::bind(&Network::handleRead, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
		connecting = false;
		connected = true;
	}
	else
	{
		if (clientSocket.is_open())
		{
			core->getProgram()->logText(boost::str(boost::format("Could not connect to %1% (%2%)") % endpoint_iterator->endpoint() % error.message()));
			stopAsync();
		}
		else
		{
			core->getProgram()->logText(boost::str(boost::format("Could not connect to %1% (Connection timed out)") % endpoint_iterator->endpoint()));
		}
		startConnectTimer(endpoint_iterator);
	}
	if (connected)
	{
		lastCommunication = GetTickCount();
	}
}

void Network::handleConnectTimer(const boost::system::error_code &error, boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
{
	if (!error)
	{
		if (endpoint_iterator != boost::asio::ip::tcp::resolver::iterator())
		{
			if (!core->getGame()->open)
			{
				connectTimer.expires_from_now(boost::posix_time::seconds(1));
				connectTimer.async_wait(boost::bind(&Network::handleConnectTimer, this, boost::asio::placeholders::error, endpoint_iterator));
			}
			else
			{
				core->getProgram()->logText(boost::str(boost::format("Connecting to %1% (attempt %2% of %3%)...") % endpoint_iterator->endpoint() % attempts % core->getProgram()->settings->connectAttempts));
				clientSocket.async_connect(endpoint_iterator->endpoint(), boost::bind(&Network::handleConnect, this, boost::asio::placeholders::error, endpoint_iterator));
				startTimeoutTimer();
			}
		}
		else
		{
			stopAsync();
			stopMainTimer();
		}
	}
}

void Network::handleMainTimer(const boost::system::error_code &error)
{
	if (!error)
	{
		if (core->getGame()->started && !connected && !connecting)
		{
			stopAsync();
			startAsync();
		}
		if (connected)
		{
			DWORD timeElapsed = GetTickCount() - lastCommunication;
			if (timeElapsed > core->getProgram()->settings->networkTimeout)
			{
				closeConnection();
			}
		}
		startMainTimer();
	}
}

void Network::handleOpenStream(const boost::system::error_code &error)
{
	if (!error)
	{
		if (file)
		{
			file->size = readStream.content_length();
			std::fstream fileHandle(file->path.c_str(), std::ios_base::in | std::ios_base::binary | std::ios_base::ate);
			if (fileHandle)
			{
				if (file->size == fileHandle.tellg())
				{
					core->getProgram()->logText(boost::str(boost::format("Remote file \"%1%\" passed file size check") % file->url));
					sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Check));
					core->getAudio()->files.insert(std::make_pair(file->id, file->name));
					file.reset();
				}
				fileHandle.close();
			}
			if (file)
			{
				file->handle.open(file->path.c_str(), std::ios_base::out | std::ios_base::binary);
				if (!file->handle)
				{
					core->getProgram()->logText(boost::str(boost::format("Error opening \"%1%\" for writing") % file->name));
					sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Error));
					file.reset();
				}
			}
		}
	}
	else
	{
		if (file)
		{
			core->getProgram()->logText(boost::str(boost::format("Error opening stream for remote file \"%1%\": %2%") % file->url % error.message()));
			sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Error));
			file.reset();
		}
	}
	if (clientSocket.is_open() && readStream.is_open())
	{
		if (file)
		{
			core->getProgram()->logText(boost::str(boost::format("Transferring remote file \"%1%\" (%2%)...") % file->url % outputFileSize(file->size)));
			readStream.async_read_some(boost::asio::buffer(file->buffer.c_array(), file->buffer.size()), boost::bind(&Network::handleReadStream, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
		}
		else
		{
			clientSocket.async_read_some(boost::asio::buffer(receivedData), boost::bind(&Network::handleRead, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
			boost::system::error_code error;
			readStream.close(error);
		}
	}
}

void Network::handleRead(const boost::system::error_code &error, std::size_t transferredBytes)
{
	if (!error)
	{
		std::string buffer(receivedData);
		buffer.resize(transferredBytes);
		boost::algorithm::erase_last(buffer, "\n");
		boost::algorithm::split(messageTokens, buffer, boost::algorithm::is_any_of("\n"));
		if (messageTokens.empty())
		{
			parseBuffer(buffer);
		}
		else
		{
			for (std::vector<std::string>::iterator i = messageTokens.begin(); i != messageTokens.end(); ++i)
			{
				parseBuffer(*i);
			}
		}
		if (file)
		{
			if (file->url.empty())
			{
				return;
			}
		}
		clientSocket.async_read_some(boost::asio::buffer(receivedData), boost::bind(&Network::handleRead, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
		lastCommunication = GetTickCount();
	}
	else
	{
		closeConnection();
	}
}

void Network::handleReadFile(const boost::system::error_code &error, std::size_t transferredBytes)
{
	if (!error)
	{
		if (file)
		{
			if (transferredBytes)
			{
				if (boost::algorithm::equals(file->buffer.c_array(), "CANCEL"))
				{
					core->getProgram()->logText(boost::str(boost::format("Transfer of local file \"%1%\" canceled server-side") % file->name));
					file.reset();
				}
				else
				{
					file->handle.write(file->buffer.c_array(), static_cast<std::streamsize>(transferredBytes));
					if (file->handle.tellp() >= static_cast<std::streamsize>(file->size))
					{
						core->getProgram()->logText(boost::str(boost::format("Transfer of local file \"%1%\" complete") % file->name));
						core->getAudio()->files.insert(std::make_pair(file->id, file->name));
						file.reset();
					}
				}
			}
			else
			{
				core->getProgram()->logText(boost::str(boost::format("Error reading data for local file \"%1%\" during transfer: No data received") % file->name));
				sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Error));
				file.reset();
			}
		}
	}
	else
	{
		if (file)
		{
			core->getProgram()->logText(boost::str(boost::format("Error reading data for local file \"%1%\" during transfer: %2%") % file->name % error.message()));
			sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Error));
			file.reset();
		}
	}
	if (clientSocket.is_open())
	{
		if (file)
		{
			clientSocket.async_read_some(boost::asio::buffer(file->buffer.c_array(), file->buffer.size()), boost::bind(&Network::handleReadFile, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
		}
		else
		{
			clientSocket.async_read_some(boost::asio::buffer(receivedData), boost::bind(&Network::handleRead, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
		}
		lastCommunication = GetTickCount();
	}
}

void Network::handleReadStream(const boost::system::error_code &error, std::size_t transferredBytes)
{
	if (!error)
	{
		if (file)
		{
			if (transferredBytes)
			{
				file->handle.write(file->buffer.c_array(), static_cast<std::streamsize>(transferredBytes));
			}
			else
			{
				core->getProgram()->logText(boost::str(boost::format("Error reading stream for remote file \"%1%\" during transfer: No data received") % file->url));
				sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Error));
				file.reset();
				boost::system::error_code error;
				readStream.close(error);
			}
		}
	}
	else
	{
		if (file)
		{
			if (error != boost::asio::error::eof)
			{
				core->getProgram()->logText(boost::str(boost::format("Error reading stream for remote file \"%1%\" during transfer: %2%") % file->url % error.message()));
				sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Error));
			}
			else
			{
				core->getProgram()->logText(boost::str(boost::format("Transfer of remote file \"%1%\" complete") % file->url));
				sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Remote));
				core->getAudio()->files.insert(std::make_pair(file->id, file->name));
			}
			file.reset();
			boost::system::error_code error;
			readStream.close(error);
		}
	}
	if (readStream.is_open())
	{
		if (file)
		{
			readStream.async_read_some(boost::asio::buffer(file->buffer.c_array(), file->buffer.size()), boost::bind(&Network::handleReadStream, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
		}
	}
}

void Network::handleResolve(const boost::system::error_code &error, boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
{
	if (!error)
	{
		attempts = 0;
		startConnectTimer(endpoint_iterator);
	}
	else
	{
		core->getProgram()->logText(boost::str(boost::format("Error resolving server address: %1%") % error.message()));
	}
}

void Network::handleTimeoutTimer(const boost::system::error_code &error)
{
	if (!error && !connected)
	{
		stopAsync();
	}
}

void Network::handleWrite(const boost::system::error_code &error)
{
	writeInProgress = false;
	if (!error)
	{
		if (!pendingMessages.empty())
		{
			sendAsync(pendingMessages.front());
			pendingMessages.pop();
		}
	}
	else
	{
		pendingMessages = std::queue<std::string>();
	}
}

void Network::sendAsync(const std::string &buffer)
{
	if (writeInProgress)
	{
		pendingMessages.push(buffer);
	}
	else
	{
		sentData = buffer;
		writeInProgress = true;
		boost::asio::async_write(clientSocket, boost::asio::buffer(sentData, sentData.length()), boost::bind(&Network::handleWrite, this, boost::asio::placeholders::error));
	}
}

void Network::startAsync()
{
	boost::asio::ip::tcp::resolver::query query(boost::asio::ip::tcp::v4(), core->getProgram()->address, core->getProgram()->port);
	resolver.async_resolve(query, boost::bind(&Network::handleResolve, this, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
	connecting = true;
}

void Network::stopAsync()
{
	if (clientSocket.is_open())
	{
		if (connected)
		{
			authenticated = false;
			connected = false;
			file.reset();
			pendingMessages = std::queue<std::string>();
			writeInProgress = false;
		}
		boost::system::error_code error;
		clientSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
		clientSocket.close(error);
		readStream.close(error);
		resolver.cancel();
		stopConnectTimer();
		stopTimeoutTimer();
	}
}

void Network::startConnectTimer(boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
{
	connectTimer.expires_from_now(boost::posix_time::milliseconds(core->getProgram()->settings->connectDelay));
	if (attempts < core->getProgram()->settings->connectAttempts)
	{
		++attempts;
		connectTimer.async_wait(boost::bind(&Network::handleConnectTimer, this, boost::asio::placeholders::error, endpoint_iterator));
	}
	else
	{
		attempts = 1;
		connectTimer.async_wait(boost::bind(&Network::handleConnectTimer, this, boost::asio::placeholders::error, ++endpoint_iterator));
	}
}

void Network::startMainTimer()
{
	mainTimer.expires_from_now(boost::posix_time::milliseconds(NETWORK_TIMER_TICK));
	mainTimer.async_wait(boost::bind(&Network::handleMainTimer, this, boost::asio::placeholders::error));
}

void Network::startTimeoutTimer()
{
	timeoutTimer.expires_from_now(boost::posix_time::milliseconds(core->getProgram()->settings->connectTimeout));
	timeoutTimer.async_wait(boost::bind(&Network::handleTimeoutTimer, this, boost::asio::placeholders::error));
}

void Network::stopConnectTimer()
{
	boost::system::error_code error;
	connectTimer.cancel(error);
}

void Network::stopMainTimer()
{
	boost::system::error_code error;
	mainTimer.cancel(error);
}

void Network::stopTimeoutTimer()
{
	boost::system::error_code error;
	timeoutTimer.cancel(error);
}

void Network::closeConnection()
{
	if (connected)
	{
		core->getProgram()->logText("Disconnected from server");
		core->getProgram()->downloadPath.clear();
	}
	core->getAudio()->freeMemory();
	stopAsync();
}

void Network::parseBuffer(const std::string &buffer)
{
	if (buffer.empty())
	{
		sendAsync("\n");
		return;
	}
	boost::algorithm::split(commandTokens, buffer, boost::algorithm::is_any_of("\t"));
	if (commandTokens.empty())
	{
		commandTokens.push_back(buffer);
	}
	for (std::vector<std::string>::iterator i = commandTokens.begin(); i != commandTokens.end(); ++i)
	{
		if (i->empty())
		{
			return;
		}
	}
	int command = 0;
	try
	{
		command = boost::lexical_cast<int>(commandTokens.at(0));
	}
	catch (boost::bad_lexical_cast &)
	{
		return;
	}
	switch (command)
	{
		case Server::Connect:
		{
			return performConnect();
		}
		case Server::Message:
		{
			return performMessage();
		}
		case Server::Name:
		{
			return performName();
		}
		case Server::Transfer:
		{
			return performTransfer();
		}
		case Server::Play:
		{
			return performPlay();
		}
		case Server::PlaySequence:
		{
			return performPlaySequence();
		}
		case Server::Pause:
		{
			return performPause();
		}
		case Server::Resume:
		{
			return performResume();
		}
		case Server::Stop:
		{
			return performStop();
		}
		case Server::Restart:
		{
			return performRestart();
		}
		case Server::GetPosition:
		{
			return performGetPosition();
		}
		case Server::SetPosition:
		{
			return performSetPosition();
		}
		case Server::SetVolume:
		{
			return performSetVolume();
		}
		case Server::SetFX:
		{
			return performSetFX();
		}
		case Server::RemoveFX:
		{
			return performRemoveFX();
		}
		case Server::Set3DPosition:
		{
			return performSet3DPosition();
		}
		case Server::Remove3DPosition:
		{
			return performRemove3DPosition();
		}
		case Server::SetRadioStation:
		{
			return performSetRadioStation();
		}
		case Server::StopRadio:
		{
			return performStopRadio();
		}
	}
}

std::string Network::outputFileSize(std::size_t bytes)
{
	std::string fileSize;
	if (bytes == std::numeric_limits<std::size_t>::max())
	{
		fileSize = "Unknown Size";
	}
	else if (bytes >= 1048576)
	{
		fileSize = boost::str(boost::format("%.1lf MB") % (static_cast<float>(bytes) / 1048576.0f));
	}
	else if (bytes >= 1024)
	{
		fileSize = boost::str(boost::format("%.1lf KB") % (static_cast<float>(bytes) / 1024.0f));
	}
	else
	{
		fileSize = boost::str(boost::format("%1% bytes") % bytes);
	}
	return fileSize;
}

void Network::performConnect()
{
	if (commandTokens.size() == 1 || commandTokens.size() == 2)
	{
		if (!authenticated)
		{
			core->getProgram()->logText("Authenticated to server");
			authenticated = true;
		}
	}
	if (commandTokens.size() == 2)
	{
		for (std::set<std::string>::iterator i = core->getProgram()->illegalCharacters.begin(); i != core->getProgram()->illegalCharacters.begin(); ++i)
		{
			if (boost::algorithm::icontains(commandTokens.at(1), *i))
			{
				core->getProgram()->logText(boost::str(boost::format("Download path could not be set to \"audiopacks\\%1%\" (illegal characters)") % commandTokens.at(1)));
				return;
			}
		}
		core->getProgram()->logText(boost::str(boost::format("Download path set to \"audiopacks\\%1%\"") % commandTokens.at(1)));
		core->getProgram()->downloadPath = boost::str(boost::wformat(L"%1%\\audiopacks\\%2%") % core->getProgram()->savePath % core->strtowstr(commandTokens.at(1)));
		if (!boost::filesystem::exists(core->getProgram()->downloadPath))
		{
			boost::filesystem::create_directories(core->getProgram()->downloadPath);
		}
	}
}

void Network::performMessage()
{
	if (commandTokens.size() != 2)
	{
		return;
	}
	core->getProgram()->logText(boost::str(boost::format("Message from server: %1%") % commandTokens.at(1)));
}

void Network::performName()
{
	if (commandTokens.size() != 2)
	{
		return;
	}
	core->getProgram()->name = commandTokens.at(1);
}

void Network::performTransfer()
{
	if (commandTokens.size() == 6)
	{
		if (core->getProgram()->downloadPath.empty())
		{
			core->getProgram()->logText(boost::str(boost::format("Transfer of file \"%1%\" rejected (no download path specified)") % commandTokens.at(3)));
			sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Error));
			return;
		}
		bool remote = false, transferable = false;
		file.reset(new File);
		try
		{
			transferable = boost::lexical_cast<bool>(commandTokens.at(1));
			file->id = boost::lexical_cast<int>(commandTokens.at(2));
			file->size = boost::lexical_cast<std::size_t>(commandTokens.at(4));
		}
		catch (boost::bad_lexical_cast &)
		{
			sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Error));
			file.reset();
			return;
		}
		if (boost::algorithm::istarts_with(commandTokens.at(3), "http://"))
		{
			remote = true;
			file->url = commandTokens.at(3);
		}
		if (remote)
		{
			std::size_t fileLocation = commandTokens.at(3).find_last_of('/');
			file->name = commandTokens.at(3).substr(fileLocation + 1);
		}
		else
		{
			file->name = commandTokens.at(3);
		}
		bool result = false;
		for (std::set<std::string>::iterator i = core->getProgram()->acceptedFileExtensions.begin(); i != core->getProgram()->acceptedFileExtensions.end(); ++i)
		{
			if (boost::algorithm::iends_with(file->name, *i))
			{
				result = true;
				break;
			}
		}
		if (!result)
		{
			core->getProgram()->logText(boost::str(boost::format("Transfer of file \"%1%\" rejected (invalid file type)") % commandTokens.at(3)));
			sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Error));
			file.reset();
			return;
		}
		for (std::set<std::string>::iterator i = core->getProgram()->illegalCharacters.begin(); i != core->getProgram()->illegalCharacters.begin(); ++i)
		{
			if (boost::algorithm::icontains(file->name, *i))
			{
				core->getProgram()->logText(boost::str(boost::format("Transfer of file \"%1%\" rejected (illegal characters)") % commandTokens.at(3)));
				sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Error));
				file.reset();
				return;
			}
		}
		file->path = boost::str(boost::wformat(L"%1%\\%2%") % core->getProgram()->downloadPath % core->strtowstr(file->name));
		if (!remote)
		{
			std::fstream fileHandle(file->path.c_str(), std::ios_base::in | std::ios_base::binary);
			if (fileHandle)
			{
				if (transferable)
				{
					char fileBuffer[MAX_BUFFER];
					boost::uint32_t fileChecksum = 0;
					boost::crc_32_type fileDigest;
					while (fileHandle)
					{
						fileHandle.read(fileBuffer, MAX_BUFFER);
						fileDigest.process_bytes(fileBuffer, static_cast<std::size_t>(fileHandle.gcount()));
					}
					fileHandle.close();
					fileChecksum = fileDigest.checksum();
					if (!commandTokens.at(5).compare(boost::str(boost::format("%X") % fileChecksum)))
					{
						core->getProgram()->logText(boost::str(boost::format("Local file \"%1%\" passed CRC check") % commandTokens.at(3)));
						sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Check));
						core->getAudio()->files.insert(std::make_pair(file->id, file->name));
						file.reset();
						return;
					}
				}
				else
				{
					core->getProgram()->logText(boost::str(boost::format("Local file \"%1%\" exists") % commandTokens.at(3)));
					sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Check));
					core->getAudio()->files.insert(std::make_pair(file->id, file->name));
					file.reset();
					return;
				}
			}
			else
			{
				if (!transferable)
				{
					core->getProgram()->logText(boost::str(boost::format("Local file \"%1%\" does not exist") % commandTokens.at(3)));
					sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Error));
					file.reset();
					return;
				}
			}
		}
		if (!core->getProgram()->settings->transferFiles)
		{
			core->getProgram()->logText(boost::str(boost::format("Transfer of file \"%1%\" rejected (file transfer requests disabled)") % commandTokens.at(3)));
			sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Error));
			file.reset();
			return;
		}
		if (remote)
		{
			readStream.async_open(file->url, boost::bind(&Network::handleOpenStream, this, boost::asio::placeholders::error));
		}
		else
		{
			file->handle.open(file->path.c_str(), std::ios_base::out | std::ios_base::binary);
			if (!file->handle)
			{
				core->getProgram()->logText(boost::str(boost::format("Error opening \"%1%\" for writing") % commandTokens.at(3)));
				sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Error));
				file.reset();
				return;
			}
			core->getProgram()->logText(boost::str(boost::format("Transferring local file \"%1%\" (%2%)...") % file->name % outputFileSize(file->size)));
			sendAsync(boost::str(boost::format("%1%\t%2%\n") % Client::Transfer % Client::Local));
			clientSocket.async_read_some(boost::asio::buffer(file->buffer.c_array(), file->buffer.size()), boost::bind(&Network::handleReadFile, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
		}
	}
	else if (commandTokens.size() == 1)
	{
		core->getProgram()->logText("All files processed");
	}
}

void Network::performPlay()
{
	if (commandTokens.size() != 6)
	{
		return;
	}
	int handleID = 0;
	bool downmix = false, loop = false, pause = false;
	try
	{
		handleID = boost::lexical_cast<int>(commandTokens.at(2));
		pause = boost::lexical_cast<bool>(commandTokens.at(3));
		loop = boost::lexical_cast<bool>(commandTokens.at(4));
		downmix = boost::lexical_cast<bool>(commandTokens.at(5));
	}
	catch (boost::bad_lexical_cast &)
	{
		return;
	}
	Audio::Stream stream;
	stream.name = commandTokens.at(1);
	core->getAudio()->streams.insert(std::make_pair(handleID, stream));
	core->getAudio()->playStream(handleID, pause, loop, downmix);
}

void Network::performPlaySequence()
{
	if (commandTokens.size() != 3 && commandTokens.size() != 7)
	{
		return;
	}
	std::map<int, Audio::Stream>::iterator s = core->getAudio()->streams.end();
	std::vector<std::string> inputTokens;
	if (commandTokens.size() == 3)
	{
		int handleID = 0;
		try
		{
			handleID = boost::lexical_cast<int>(commandTokens.at(1));
		}
		catch (boost::bad_lexical_cast &)
		{
			return;
		}
		s = core->getAudio()->streams.find(handleID);
		boost::algorithm::split(inputTokens, commandTokens.at(2), boost::algorithm::is_any_of(" "));
	}
	else if (commandTokens.size() == 7)
	{
		bool downmix = false, loop = false, pause = false;
		int handleID = 0, sequenceID = 0;
		try
		{
			sequenceID = boost::lexical_cast<int>(commandTokens.at(1));
			handleID = boost::lexical_cast<int>(commandTokens.at(2));
			pause = boost::lexical_cast<bool>(commandTokens.at(3));
			loop = boost::lexical_cast<bool>(commandTokens.at(4));
			downmix = boost::lexical_cast<bool>(commandTokens.at(5));
		}
		catch (boost::bad_lexical_cast &)
		{
			return;
		}
		Audio::Stream stream;
		stream.sequence.reset(new Audio::Stream::Sequence);
		stream.sequence->downmix = downmix;
		stream.sequence->id = sequenceID;
		stream.sequence->loop = loop;
		stream.sequence->pause = pause;
		core->getAudio()->streams.insert(std::make_pair(handleID, stream));
		s = core->getAudio()->streams.find(handleID);
		boost::algorithm::split(inputTokens, commandTokens.at(6), boost::algorithm::is_any_of(" "));
	}
	if (s == core->getAudio()->streams.end())
	{
		return;
	}
	if (!s->second.sequence)
	{
		return;
	}
	if (inputTokens.size () < 2)
	{
		return;
	}
	for (std::vector<std::string>::iterator i = inputTokens.begin(); i != inputTokens.end(); ++i)
	{
		if (!i->length())
		{
			continue;
		}
		if (boost::algorithm::equals(*i, "F"))
		{
			core->getAudio()->initializeSequence(s->first);
			return;
		}
		if (boost::algorithm::equals(*i, "U"))
		{
			sendAsync(boost::str(boost::format("%1%\t%2%\t%3%\n") % Client::Sequence % s->second.sequence->id % s->first));
			return;
		}
		int audioID = 0;
		try
		{
			audioID = boost::lexical_cast<int>(*i);
		}
		catch (boost::bad_lexical_cast &)
		{
			continue;
		}
		s->second.sequence->audioIDs.push_back(audioID);
	}
}

void Network::performPause()
{
	if (commandTokens.size() != 2)
	{
		return;
	}
	int handleID = 0;
	try
	{
		handleID = boost::lexical_cast<int>(commandTokens.at(1));
	}
	catch (boost::bad_lexical_cast &)
	{
		return;
	}
	std::map<int, Audio::Stream>::iterator s = core->getAudio()->streams.find(handleID);
	if (s != core->getAudio()->streams.end())
	{
		if (BASS_ChannelPause(s->second.mixer))
		{
			core->getProgram()->logText(boost::str(boost::format("Paused: \"%1%\"") % s->second.name));
		}
	}
}

void Network::performResume()
{
	if (commandTokens.size() != 2)
	{
		return;
	}
	int handleID = 0;
	try
	{
		handleID = boost::lexical_cast<int>(commandTokens.at(1));
	}
	catch (boost::bad_lexical_cast &)
	{
		return;
	}
	std::map<int, Audio::Stream>::iterator s = core->getAudio()->streams.find(handleID);
	if (s != core->getAudio()->streams.end())
	{
		if (BASS_ChannelPlay(s->second.mixer, false))
		{
			core->getProgram()->logText(boost::str(boost::format("Resumed: \"%1%\"") % s->second.name));
		}
	}
}

void Network::performStop()
{
	if (commandTokens.size() != 2)
	{
		return;
	}
	int handleID = 0;
	try
	{
		handleID = boost::lexical_cast<int>(commandTokens.at(1));
	}
	catch (boost::bad_lexical_cast &)
	{
		return;
	}
	std::map<int, Audio::Stream>::iterator s = core->getAudio()->streams.find(handleID);
	if (s != core->getAudio()->streams.end())
	{
		BASS_ChannelStop(s->second.mixer);
	}
}

void Network::performRestart()
{
	if (commandTokens.size() != 2)
	{
		return;
	}
	int handleID = 0;
	try
	{
		handleID = boost::lexical_cast<int>(commandTokens.at(1));
	}
	catch (boost::bad_lexical_cast &)
	{
		return;
	}
	std::map<int, Audio::Stream>::iterator s = core->getAudio()->streams.find(handleID);
	if (s != core->getAudio()->streams.end())
	{
		if (BASS_ChannelSetPosition(s->second.channel, 0, BASS_POS_BYTE) && BASS_ChannelPlay(s->second.mixer, false))
		{
			core->getProgram()->logText(boost::str(boost::format("Restarted: \"%1%\"") % s->second.name));
		}
	}
}

void Network::performGetPosition()
{
	if (commandTokens.size() != 3)
	{
		return;
	}
	int handleID = 0, requestID = 0;
	try
	{
		requestID = boost::lexical_cast<int>(commandTokens.at(1));
		handleID = boost::lexical_cast<int>(commandTokens.at(2));
	}
	catch (boost::bad_lexical_cast &)
	{
		return;
	}
	double seconds = 0.0f;
	std::map<int, Audio::Stream>::iterator s = core->getAudio()->streams.find(handleID);
	if (s != core->getAudio()->streams.end())
	{
		seconds = BASS_ChannelBytes2Seconds(s->second.channel, BASS_ChannelGetPosition(s->second.channel, BASS_POS_BYTE));
	}
	sendAsync(boost::str(boost::format("%1%\t%2%\t%3%\t%4%\n") % Client::Position % requestID % handleID % static_cast<int>(seconds)));
}

void Network::performSetPosition()
{
	if (commandTokens.size() != 3)
	{
		return;
	}
	int handleID = 0, seconds = 0;
	try
	{
		handleID = boost::lexical_cast<int>(commandTokens.at(1));
		seconds = boost::lexical_cast<int>(commandTokens.at(2));
	}
	catch (boost::bad_lexical_cast &)
	{
		return;
	}
	if (seconds < 0)
	{
		return;
	}
	std::map<int, Audio::Stream>::iterator s = core->getAudio()->streams.find(handleID);
	if (s != core->getAudio()->streams.end())
	{
		BASS_ChannelSetPosition(s->second.channel, BASS_ChannelSeconds2Bytes(s->second.channel, seconds), BASS_POS_BYTE);
	}
}

void Network::performSetVolume()
{
	if (commandTokens.size() != 3)
	{
		return;
	}
	int handleID = 0;
	float volume = 0.0f;
	try
	{
		handleID = boost::lexical_cast<int>(commandTokens.at(1));
		volume = boost::lexical_cast<float>(commandTokens.at(2));
	}
	catch (boost::bad_lexical_cast &)
	{
		return;
	}
	if (volume < 0.0f || volume > 100.0f)
	{
		return;
	}
	std::map<int, Audio::Stream>::iterator s = core->getAudio()->streams.find(handleID);
	if (s != core->getAudio()->streams.end())
	{
		if (!s->second.position)
		{
			BASS_ChannelSetAttribute(s->second.mixer, BASS_ATTRIB_VOL, volume / 100.0f);
		}
	}
}

void Network::performSetFX()
{
	if (commandTokens.size() != 3)
	{
		return;
	}
	int handleID = 0, type = 0;
	try
	{
		handleID = boost::lexical_cast<int>(commandTokens.at(1));
		type = boost::lexical_cast<int>(commandTokens.at(2));
	}
	catch (boost::bad_lexical_cast &)
	{
		return;
	}
	if (type < 0 || type > 8)
	{
		return;
	}
	std::map<int, Audio::Stream>::iterator s = core->getAudio()->streams.find(handleID);
	if (s != core->getAudio()->streams.end())
	{
		if (!s->second.effects[type])
		{
			s->second.effects[type] = BASS_ChannelSetFX(s->second.mixer, type, 0);
		}
	}
}

void Network::performRemoveFX()
{
	if (commandTokens.size() != 3)
	{
		return;
	}
	int handleID = 0, type = 0;
	try
	{
		handleID = boost::lexical_cast<int>(commandTokens.at(1));
		type = boost::lexical_cast<int>(commandTokens.at(2));
	}
	catch (boost::bad_lexical_cast &)
	{
		return;
	}
	if (type < 0 || type > 8)
	{
		return;
	}
	std::map<int, Audio::Stream>::iterator s = core->getAudio()->streams.find(handleID);
	if (s != core->getAudio()->streams.end())
	{
		BASS_ChannelRemoveFX(s->second.mixer, s->second.effects[type]);
		s->second.effects[type] = 0;
	}
}

void Network::performSet3DPosition()
{
	if (commandTokens.size() != 6)
	{
		return;
	}
	int handleID = 0;
	try
	{
		handleID = boost::lexical_cast<int>(commandTokens.at(1));
	}
	catch (boost::bad_lexical_cast &)
	{
		return;
	}
	std::map<int, Audio::Stream>::iterator s = core->getAudio()->streams.find(handleID);
	if (s != core->getAudio()->streams.end())
	{
		s->second.position = boost::shared_ptr<Audio::Stream::Position>(new Audio::Stream::Position);
		try
		{
			s->second.position->vector.x = boost::lexical_cast<float>(commandTokens.at(2));
			s->second.position->vector.y = boost::lexical_cast<float>(commandTokens.at(3));
			s->second.position->vector.z = boost::lexical_cast<float>(commandTokens.at(4));
			s->second.position->distance = boost::lexical_cast<float>(commandTokens.at(5));
		}
		catch (boost::bad_lexical_cast &)
		{
			s->second.position.reset();
			return;
		}
		s->second.position->distance *= s->second.position->distance;
		BASS_ChannelSet3DAttributes(s->second.mixer, BASS_3DMODE_NORMAL, 1.0f, 0.5f, 360, 360, 1.0f);
		BASS_ChannelSet3DPosition(s->second.mixer, &s->second.position->vector, NULL, NULL);
		BASS_Apply3D();
	}
}

void Network::performRemove3DPosition()
{
	if (commandTokens.size() != 2)
	{
		return;
	}
	int handleID = 0;
	try
	{
		handleID = boost::lexical_cast<int>(commandTokens.at(1));
	}
	catch (boost::bad_lexical_cast &)
	{
		return;
	}
	std::map<int, Audio::Stream>::iterator s = core->getAudio()->streams.find(handleID);
	if (s != core->getAudio()->streams.end())
	{
		s->second.position.reset();
		BASS_ChannelSetAttribute(s->second.mixer, BASS_ATTRIB_VOL, 1.0f);
		BASS_ChannelSet3DAttributes(s->second.mixer, BASS_3DMODE_RELATIVE, 1.0f, 0.5f, 360, 360, 1.0f);
		BASS_ChannelSet3DPosition(s->second.mixer, &BASS_3DVECTOR(0.0f, 0.0f, 0.0f), NULL, NULL);
		BASS_Apply3D();
	}
}

void Network::performSetRadioStation()
{
	if (commandTokens.size() != 2)
	{
		return;
	}
	int radioStation = 0;
	try
	{
		radioStation = boost::lexical_cast<int>(commandTokens.at(1));
	}
	catch (boost::bad_lexical_cast &)
	{
		return;
	}
	core->getGame()->setRadioStation(radioStation);
}

void Network::performStopRadio()
{
	if (commandTokens.size() != 1)
	{
		return;
	}
	core->getGame()->stopRadio();
}
