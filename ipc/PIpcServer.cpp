/**
 *  Copyright (c) 2009-2012 Hewlett-Packard Development Company, L.P.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */


#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "PIpcServer.h"

#include "PIpcChannel.h"
#include "PIpcSocketSource.h"

static const std::string kSocketPathPrefix = "/tmp/pipcserver.";
static const int kMaxConnections = 100;											 

PIpcServer::PIpcServer(const std::string& name, GMainLoop* loop)
	: m_mainLoop(loop)
	, m_socketFd(-1)
{
	m_socketPath = kSocketPathPrefix + name;
	
	init();
}

PIpcServer::~PIpcServer()
{
	if (m_socketSource) {
		g_source_destroy(m_socketSource);
		g_source_unref(m_socketSource);
		m_socketSource = 0;
	}
	
	if (m_socketFd != -1) {
		::close(m_socketFd);
		m_socketFd = -1;
	}

	::unlink(m_socketPath.c_str());	   
}

GMainLoop* PIpcServer::mainLoop() const
{
    return m_mainLoop;
}

void PIpcServer::init()
{
	::unlink(m_socketPath.c_str());

    m_socketFd = ::socket(PF_LOCAL, SOCK_STREAM, 0);
    if (m_socketFd < 0) {
		g_critical("%s: %d Failed to create socket: %s",
				   __PRETTY_FUNCTION__, __LINE__, strerror(errno));
		exit(-1);
    }

	struct sockaddr_un socketAddr;    
    socketAddr.sun_family = AF_LOCAL;
    ::strncpy(socketAddr.sun_path, m_socketPath.c_str(), G_N_ELEMENTS(socketAddr.sun_path));
	socketAddr.sun_path[G_N_ELEMENTS(socketAddr.sun_path)-1] = '\0';
	
    if (::bind(m_socketFd, (struct sockaddr*) &socketAddr, SUN_LEN(&socketAddr)) != 0) {
		g_critical("%s: %d Failed to bind socket: %s",
				   __PRETTY_FUNCTION__, __LINE__,  strerror(errno));
		exit(-1);
   }

    if (::listen(m_socketFd, kMaxConnections) != 0) {
		g_critical("%s: %d Failed to listen on socket: %s",
				   __PRETTY_FUNCTION__, __LINE__,  strerror(errno));
		exit(-1);
    }

	m_socketSource = (GSource*) PIpcSocketSourceCreate(m_socketFd,
													   (GIOCondition) (G_IO_IN | G_IO_HUP),
													   (PIpcSocketSourceCallback) PIpcServer::ioCallback,
													   this);
    g_source_attach(m_socketSource, g_main_loop_get_context(m_mainLoop));    
}

gboolean PIpcServer::ioCallback(PIpcSocketSource* src, GIOCondition condition, gpointer userData)
{
	PIpcServer* server = (PIpcServer*) userData;
	server->socketCallback(condition);
	return TRUE;
}

void PIpcServer::socketCallback(GIOCondition condition)
{	
	struct sockaddr_un  socketAddr;
    socklen_t           socketAddrLen;
    int                 socketFd = -1;

    memset(&socketAddr, 0, sizeof(socketAddr));
    memset(&socketAddrLen, 0, sizeof(socketAddrLen));

    socketFd = ::accept(m_socketFd, (struct sockaddr*) &socketAddr, &socketAddrLen);
	if (-1 == socketFd) {
		g_critical("%s: %d Failed to accept inbound connection: %s",
				   __PRETTY_FUNCTION__, __LINE__, strerror(errno));
		return;
	}

	static const int kClientNameLen = 256;
	char clientName[kClientNameLen];
	
	// client is connected now. read the initial packet sent to us which contains the
	// name of the client
	int index = 0;
	int len = kClientNameLen;
	
	while (len > 0) {
		int count = ::recv(socketFd, &clientName[index], len, MSG_WAITALL);
		if (count <= 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			else {
				g_critical("%s: %d Failed to receive initial packet from client: %s",
						   __PRETTY_FUNCTION__, __LINE__,  strerror(errno));
				close(socketFd);
				return;
			}
		}

		index += count;
		len -= count;
	}

	// Mark the socket as non-blocking
	long socketFdFlags = ::fcntl(socketFd, F_GETFL, NULL);
	if (socketFdFlags < 0) {
		g_critical("%s: %d Failed to get socketFd flags: %s",
				   __PRETTY_FUNCTION__, __LINE__, strerror(errno));
		close(socketFd);
		return;
	}

	socketFdFlags |= O_NONBLOCK;
	::fcntl(socketFd, F_SETFL, socketFdFlags);

	pid_t pid;
	memcpy(&pid, clientName, sizeof(pid));
	clientConnected(pid, clientName + sizeof(pid), new PIpcChannel(m_mainLoop, socketFd));
}
