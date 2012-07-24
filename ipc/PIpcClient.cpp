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


#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>

#include "PIpcClient.h"

#include "PIpcChannel.h"

static const std::string kSocketPathPrefix = "/tmp/pipcserver.";

PIpcClient::PIpcClient(const std::string& serverName, const std::string& name,
					   GMainLoop* loop)
	: m_socketPath(kSocketPathPrefix + serverName)
	, m_name(name)
	, m_mainLoop(loop)
{
	GSource* src = g_timeout_source_new(0);
	g_source_set_callback(src, (GSourceFunc) initTimeoutCallback, this, NULL);
	g_source_attach(src, g_main_loop_get_context(m_mainLoop));
	g_source_unref(src);
}

PIpcClient::~PIpcClient()
{
}

void PIpcClient::init()
{
    struct sockaddr_un socketAddr;
	int socketFd;

	socketFd = ::socket(PF_LOCAL, SOCK_STREAM, 0);
	if (socketFd < 0) {
		g_critical("%s: %d Failed to create socket: %s",
				   __PRETTY_FUNCTION__, __LINE__, strerror(errno));				   
		serverDisconnected();
		return;
	}

	memset(&socketAddr, 0, sizeof(socketAddr));
	socketAddr.sun_family = AF_LOCAL;
	strncpy(socketAddr.sun_path, m_socketPath.c_str(), G_N_ELEMENTS(socketAddr.sun_path));
	socketAddr.sun_path[G_N_ELEMENTS(socketAddr.sun_path)-1] = '\0';

	if (::connect(socketFd, (struct sockaddr*) &socketAddr,
				  SUN_LEN(&socketAddr)) != 0) {
		g_critical("%s:%d Failed to connect to socket: %s",
				   __PRETTY_FUNCTION__, __LINE__, strerror(errno));				   
		serverDisconnected();
		return;
	}

	static const int kClientNameLen = 256;
	char clientName[kClientNameLen];

	pid_t pid = ::getpid();
	memcpy(clientName, &pid, sizeof(pid));
	strncpy(clientName + sizeof(pid), m_name.c_str(), G_N_ELEMENTS(clientName) - sizeof(pid));
	clientName[G_N_ELEMENTS(clientName)-1] = '\0';

	int index = 0;
	int len = kClientNameLen;
	
	while (len > 0) {
		int count = ::send(socketFd, &clientName[index], len, 0);
		if (count <= 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			else {
				g_critical("%s: %d Failed to send initial packet to server: %s",
						   __PRETTY_FUNCTION__, __LINE__,  strerror(errno));
				close(socketFd);
				serverDisconnected();
				return;
			}
		}

		index += count;
		len -= count;
	}
			   
	serverConnected(new PIpcChannel(m_mainLoop, socketFd));	
}

gboolean PIpcClient::initTimeoutCallback(gpointer arg)
{
	PIpcClient* client = (PIpcClient*) arg;
	client->init();	
	return FALSE;    
}
