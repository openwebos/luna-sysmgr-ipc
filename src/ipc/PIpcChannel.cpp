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


#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#include <stdint.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include <signal.h>




#include "PIpcChannel.h"

#include "PIpcChannelListener.h"
#include "PIpcSocketSource.h"
#include "PIpcMessage.h"

PIpcChannel::PIpcChannel(GMainLoop* loop, int socketFd)
	: m_mainLoop(loop)
	, m_socketFd(socketFd)
	, m_socketSource(0)
	, m_listener(0)
	, m_blockedOnWrite(false)
	, m_remoteIsSuspended(false)
	, m_nestedMainContext(0)
	, m_nestedMainLoop(0)
	, m_nestedSocketSource(0)
	, m_nestedTimeoutSource(0)
	, m_inNestedLoop(false)
	, m_syncCallTimedOut(false)
	, m_currSyncMessageId(0)
	, m_currSyncReply(0)
	, m_replyForIncomingSyncMessage(0)
	, m_creatingThread(pthread_self())
	, m_incomingMessage(0)
	, m_incomingMessagePayloadLength(0)
	, m_incomingAsyncMessagesDispatcherSrc(0)
	, m_asyncCaller(new PIpcAsyncCaller<PIpcChannel>(this, &PIpcChannel::asyncCallerDispatch, loop))
			
{
	pthread_mutex_init(&m_otherThreadOutgoingMessagesMutex, NULL);
	
	m_currOutgoingMessageInfo.reset();
	m_currIncomingMessageInfo.reset();

	init();
}

PIpcChannel::~PIpcChannel()
{
	if (m_inNestedLoop) {
		
		g_main_loop_quit(m_nestedMainLoop);

		delete m_currSyncReply;
		m_currSyncReply = 0;

		if (m_nestedSocketSource) {
			g_source_destroy(m_nestedSocketSource);
			g_source_unref(m_nestedSocketSource);
			m_nestedSocketSource = 0;
		}

		if (m_nestedTimeoutSource) {
			g_source_destroy(m_nestedTimeoutSource);
			g_source_unref(m_nestedTimeoutSource);
			m_nestedTimeoutSource = 0;
		}
		
		m_inNestedLoop = false;
	}

	g_main_loop_unref(m_nestedMainLoop);
	g_main_context_unref(m_nestedMainContext);
	m_nestedMainLoop = 0;
	m_nestedMainContext = 0;

	if (m_incomingAsyncMessagesDispatcherSrc) {
		g_source_destroy(m_incomingAsyncMessagesDispatcherSrc);
		g_source_unref(m_incomingAsyncMessagesDispatcherSrc);
		m_incomingAsyncMessagesDispatcherSrc = 0;
	}

	if (m_socketSource) {
		g_source_destroy(m_socketSource);
		g_source_unref(m_socketSource);
		m_socketSource = 0;
	}

	if (m_socketFd >= 0) {
		::close(m_socketFd);
		m_socketFd = -1;
	}

	pthread_mutex_destroy(&m_otherThreadOutgoingMessagesMutex);
	
	delete m_asyncCaller;

	// FIXME: More cleanup needed
	// * m_incomingAsyncMessages
	// * m_outgoingMessages
	// * m_currSyncReply
	// * m_incomingMessage
}

void PIpcChannel::init()
{
	// Mark the socket as non-blocking
	long socketFdFlags = ::fcntl(m_socketFd, F_GETFL, NULL);
	if (socketFdFlags >= 0) {
		socketFdFlags |= O_NONBLOCK;
		::fcntl(m_socketFd, F_SETFL, socketFdFlags);	
	}
	else {
		g_critical("%s: %d Failed to get socketFd flags: %s",
				   __PRETTY_FUNCTION__, __LINE__, strerror(errno));
	}

	// Create the source to watch over the socket
	m_socketSource = (GSource*) PIpcSocketSourceCreate(m_socketFd,
													   (GIOCondition) (G_IO_IN | G_IO_HUP),
													   (PIpcSocketSourceCallback) PIpcChannel::ioCallback,
													   this);
    g_source_attach(m_socketSource, g_main_loop_get_context(m_mainLoop));

	m_nestedMainContext = g_main_context_new();
	m_nestedMainLoop = g_main_loop_new(m_nestedMainContext, FALSE);
}

gboolean PIpcChannel::ioCallback(PIpcSocketSource* src, GIOCondition condition, gpointer userData)
{
	PIpcChannel* channel = (PIpcChannel*) userData;
	channel->socketCallback(src, condition);
	return TRUE;
}

void PIpcChannel::socketCallback(PIpcSocketSource* src, GIOCondition condition)
{
    if (condition & G_IO_HUP) {		
		disconnected();
		return;
	}

	if (condition & G_IO_OUT) {		
		m_blockedOnWrite = false;
		// Remove the OUT watch condition
		PIpcSocketSourceSetCondition((PIpcSocketSource*) src,
									 (GIOCondition) (G_IO_IN | G_IO_HUP));
		processOutgoingMessages();
		return;
	}

	// Ok. Data is available for reading
	if (condition & G_IO_IN) {
		bool ret;
		do {
			ret = processIncomingMessages();
		} while (ret);
	}
}

void PIpcChannel::setListener(PIpcChannelListener* listener)
{
	if (G_UNLIKELY(!pthread_equal(pthread_self(), m_creatingThread))) {
		g_critical("%s called from wrong thread", __PRETTY_FUNCTION__);
		return;
	}

	m_listener = listener;
	m_listener->setChannel(this);
}

PIpcChannelListener* PIpcChannel::listener() const
{
	if (G_UNLIKELY(!pthread_equal(pthread_self(), m_creatingThread))) {
		g_critical("%s called from wrong thread", __PRETTY_FUNCTION__);
		return 0;
	}

	return m_listener;
}


void PIpcChannel::sendAsyncMessage(PIpcMessage* msg)
{
	if (G_LIKELY(pthread_equal(pthread_self(), m_creatingThread))) {

		m_outgoingMessages.push(msg);
		processOutgoingMessages();		
	}
	else {
		queueAsyncMessage(msg);
	}
}

void PIpcChannel::queueAsyncMessage(PIpcMessage* msg)
{
    pthread_mutex_lock(&m_otherThreadOutgoingMessagesMutex);
    m_otherThreadOutgoingMessages.push(msg);
    pthread_mutex_unlock(&m_otherThreadOutgoingMessagesMutex);

    m_asyncCaller->call();    
}

bool PIpcChannel::sendSyncMessage(PIpcMessage* msg, PIpcMessage*& reply, int timeoutMs)
{
	if (G_UNLIKELY(!pthread_equal(pthread_self(), m_creatingThread))) {
		g_critical("%s called from wrong thread", __PRETTY_FUNCTION__);
		return false;
	}

	if (G_UNLIKELY(m_remoteIsSuspended)) {
		g_warning("%s Remote process is suspended.", __PRETTY_FUNCTION__);
		return false;
	}

	reply = 0;

	m_inNestedLoop = true;
	m_currSyncMessageId = msg->message_id();
	m_currSyncReply = 0;

	msg->set_sync();

	m_outgoingMessages.push(msg);

	processOutgoingMessages();

	// Disconnected?
	if (m_socketFd < 0) {

		m_inNestedLoop = false;
		m_currSyncMessageId = 0;
		m_currSyncReply = 0;
		return false;
	}

	m_syncCallTimedOut = false;
	
	if (timeoutMs > 0) {
		m_nestedTimeoutSource = g_timeout_source_new(timeoutMs);
		g_source_set_callback(m_nestedTimeoutSource,
							  PIpcChannel::nestedLoopTimeoutCallback,
							  this, NULL);
		g_source_attach(m_nestedTimeoutSource, m_nestedMainContext);
	}
	
	m_nestedSocketSource = (GSource*) PIpcSocketSourceCreate(m_socketFd,
															 m_blockedOnWrite ?
															 (GIOCondition) (G_IO_IN | G_IO_HUP | G_IO_OUT) :
															 (GIOCondition) (G_IO_IN | G_IO_HUP),
															 (PIpcSocketSourceCallback) PIpcChannel::ioCallback,
															 this);
	g_source_attach(m_nestedSocketSource, m_nestedMainContext);
	g_main_loop_run(m_nestedMainLoop);

	if (m_currSyncReply) {

		if (m_currSyncReply->is_reply_error()) {
			reply = 0;
			delete m_currSyncReply;
		}
		else
			reply = m_currSyncReply;
	}

	if (m_nestedTimeoutSource) {
		g_source_destroy(m_nestedTimeoutSource);
		g_source_unref(m_nestedTimeoutSource);
		m_nestedTimeoutSource = 0;
	}

	if (m_nestedSocketSource) {
		g_source_destroy(m_nestedSocketSource);
		g_source_unref(m_nestedSocketSource);
		m_nestedSocketSource = 0;
	}

	m_inNestedLoop = false;
	m_currSyncMessageId = 0;
	m_currSyncReply = 0;

	// Did we get disconnected in the nested loop while waiting for a sync reply?
	if (m_socketFd < 0) {
		return false;
	}
	
	if (!m_incomingAsyncMessages.empty() && !m_incomingAsyncMessagesDispatcherSrc) {
		m_incomingAsyncMessagesDispatcherSrc = g_idle_source_new();
		g_source_set_priority(m_incomingAsyncMessagesDispatcherSrc, G_PRIORITY_DEFAULT);
		g_source_set_callback(m_incomingAsyncMessagesDispatcherSrc,
							  PIpcChannel::dispatchIncomingAsyncMessagesCallback,
							  this, NULL);
		g_source_attach(m_incomingAsyncMessagesDispatcherSrc, g_main_loop_get_context(m_mainLoop));
	}

	// Mark original socket src as non-blocking and try pushing out any
	// pending messages
	m_blockedOnWrite = false;
	PIpcSocketSourceSetCondition((PIpcSocketSource*) m_socketSource,
								 (GIOCondition) (G_IO_IN | G_IO_HUP));
	processOutgoingMessages();

	if (m_syncCallTimedOut || !reply)
		return false;

	return true;
}

void PIpcChannel::disconnected()
{
	if (m_socketFd >= 0) {
		::close(m_socketFd);
		m_socketFd = -1;
	}

	if (m_socketSource) {
		g_source_destroy(m_socketSource);
		g_source_unref(m_socketSource);
		m_socketSource = 0;
	}

	if (m_inNestedLoop)
		g_main_loop_quit(m_nestedMainLoop);

	PIpcMessage* disconnectMessage = new PIpcMessage(MSG_ROUTING_NONE, 0, 0);
	disconnectMessage->set_disconnect();
	m_incomingAsyncMessages.push_front(disconnectMessage);

	if (!m_incomingAsyncMessagesDispatcherSrc) {
		m_incomingAsyncMessagesDispatcherSrc = g_idle_source_new();
		g_source_set_priority(m_incomingAsyncMessagesDispatcherSrc, G_PRIORITY_DEFAULT);
		g_source_set_callback(m_incomingAsyncMessagesDispatcherSrc,
							  PIpcChannel::dispatchIncomingAsyncMessagesCallback,
							  this, NULL);
		g_source_attach(m_incomingAsyncMessagesDispatcherSrc, g_main_loop_get_context(m_mainLoop));
	}
}

void PIpcChannel::processOutgoingMessages()
{
	if (m_socketFd < 0)
		return;
	
	if (m_blockedOnWrite)
		return;

	if (m_outgoingMessages.empty()) {
		return;
	}

	while (processOneOutgoingMessage()) {}
}

bool PIpcChannel::processOneOutgoingMessage()
{
	if (m_outgoingMessages.empty()) {
		return false;
	}

	PIpcMessage* msg = m_outgoingMessages.front();

	int payloadSize = msg->size();
	int bytesToWrite, bytesWritten;
	char* buffer;

	// Have we finished sending the header?
	if (m_currOutgoingMessageInfo.m_lengthBytesHandled < (int) sizeof(int)) {

		bytesToWrite = sizeof(int) - m_currOutgoingMessageInfo.m_lengthBytesHandled;
		buffer = (char*) &payloadSize + m_currOutgoingMessageInfo.m_lengthBytesHandled;
		bytesWritten = writeToSocket(buffer, bytesToWrite);
		if (bytesWritten < 0) {
			disconnected();
			return false;
		}

		m_currOutgoingMessageInfo.m_lengthBytesHandled += bytesWritten;

		// EAGAIN scenario
		if (bytesWritten < bytesToWrite)
			return false;
	}

	// Sending payload
	bytesToWrite = payloadSize - m_currOutgoingMessageInfo.m_payloadBytesHandled;
	buffer = msg->payload() + m_currOutgoingMessageInfo.m_payloadBytesHandled;
	bytesWritten = writeToSocket(buffer, bytesToWrite);
	if (bytesWritten < 0) {
		disconnected();
		return false;
	}

	m_currOutgoingMessageInfo.m_payloadBytesHandled += bytesWritten;

	// EAGAIN scenario
	if (bytesWritten < bytesToWrite)
		return false;

	
	// Packet sent completely
	m_currOutgoingMessageInfo.reset();	

	m_outgoingMessages.pop();

	delete msg;

	return true;
}

// Returns true if we can safely call it again, otherwise
// need to return to event loop
bool PIpcChannel::processIncomingMessages()
{
	int bytesToRead, bytesRead;
	char* buffer;

	bool noError = true;
	
	// Have we finished sending the header?
	if (m_currIncomingMessageInfo.m_lengthBytesHandled < (int) sizeof(int)) {

		bytesToRead = sizeof(int) - m_currIncomingMessageInfo.m_lengthBytesHandled;
		buffer = (char*) &m_incomingMessagePayloadLength + m_currIncomingMessageInfo.m_lengthBytesHandled;
		bytesRead = readFromSocket(buffer, bytesToRead, noError);
		if (bytesRead < 0) {
			disconnected();
			return false;
		}

		m_currIncomingMessageInfo.m_lengthBytesHandled += bytesRead;

		// EAGAIN scenario
		if (!noError || (bytesRead < bytesToRead))
			return false;

		assert(m_incomingMessagePayloadLength != 0);
		assert(m_incomingMessage == 0);
		
		m_incomingMessage = new PIpcMessage(m_incomingMessagePayloadLength);
	}

	assert(m_incomingMessage != 0);

	// Receiving payload
	bytesToRead = m_incomingMessagePayloadLength - m_currIncomingMessageInfo.m_payloadBytesHandled;
	buffer = m_incomingMessage->payload() + m_currIncomingMessageInfo.m_payloadBytesHandled;
	bytesRead = readFromSocket(buffer, bytesToRead, noError);
	if (bytesRead < 0) {
		disconnected();
		return false;
	}

	m_currIncomingMessageInfo.m_payloadBytesHandled += bytesRead;

	// EAGAIN scenario
	if (!noError || (bytesRead < bytesToRead))
		return false;

	// Packet received completely.

	PIpcMessage* incomingMessage = m_incomingMessage;
	m_incomingMessage = 0;
	m_currIncomingMessageInfo.reset();
	
	if (m_inNestedLoop)
		handleIncomingMessageInNestedMode(incomingMessage);
	else
		handleIncomingMessageInNormalMode(incomingMessage);

	return true;
}	

int PIpcChannel::writeToSocket(char* buffer, int len)
{
	int index = 0;
	while (len > 0) {

		int count = ::send(m_socketFd, &buffer[index], len, MSG_DONTWAIT|MSG_NOSIGNAL);
		if (count <= 0) {

			if (errno == EINTR)
				continue;
			else if (errno == EAGAIN) {
				m_blockedOnWrite = true;
				PIpcSocketSourceSetCondition((PIpcSocketSource*) (m_inNestedLoop ?
																  m_nestedSocketSource :
																  m_socketSource),
											 (GIOCondition) (G_IO_IN | G_IO_OUT | G_IO_HUP));
				return index;				
			}
			else {
				g_critical("%s:%d Failed to write to socket. Error: %s",
						   __PRETTY_FUNCTION__, __LINE__, strerror(errno));
				return -1;
			}
		}

		index += count;
		len -= count;
	}

	return index;
}

int PIpcChannel::readFromSocket(char* buffer, int len, bool& noError)
{
	noError = true;
	
	int index = 0;
	while (len > 0) {

		int count = ::recv(m_socketFd, &buffer[index], len, MSG_DONTWAIT|MSG_NOSIGNAL);
		if (count <= 0) {

			if (errno == EINTR)
				continue;
			else if (errno == EAGAIN) {
				noError = false;
				return index;
			}
			else {
				g_critical("%s:%d Failed to read from socket. Error: %s",
						   __PRETTY_FUNCTION__, __LINE__, strerror(errno));
				noError = false;
				return -1;
			}
		}

		index += count;
		len -= count;
	}

	return index;
}

void PIpcChannel::handleIncomingMessageInNestedMode(PIpcMessage* msg)
{
	if (G_UNLIKELY(msg->is_sync()))	{
		handleIncomingSyncMessage(msg);		
	}
	else {

		if (msg->is_reply() || msg->is_reply_error()) {
			
			if (msg->message_id() == m_currSyncMessageId) {
				m_currSyncReply = msg;
				g_main_loop_quit(m_nestedMainLoop);
			}
			else {

				// Got a reply/reply-error message with a message id which is not ours. ignore
				delete msg;
			}
		}
		else {

			// Queue up this message
			m_incomingAsyncMessages.push_back(msg);
		}
	}
}

void PIpcChannel::handleIncomingMessageInNormalMode(PIpcMessage* msg)
{
	if (G_UNLIKELY(msg->is_sync()))
		handleIncomingSyncMessage(msg);
	else if (G_UNLIKELY(msg->is_reply() || msg->is_reply_error())) {

		// got a reply when we were not expecting it. ignore
		delete msg;	
	}
	else {

		m_incomingAsyncMessages.push_back(msg);
		dispatchIncomingAsyncMessages();
	}
}

void PIpcChannel::handleIncomingSyncMessage(PIpcMessage* msg)
{
	bool isSuspendRequest = msg->is_suspend();

	m_replyForIncomingSyncMessage = 0;

	if (m_listener)
		m_listener->onMessageReceived(*msg);

	if (m_replyForIncomingSyncMessage)
		m_replyForIncomingSyncMessage->set_reply();
	else {
		m_replyForIncomingSyncMessage = new PIpcMessage(MSG_ROUTING_NONE, 0, 0);
		m_replyForIncomingSyncMessage->set_reply_error();
	}

	m_replyForIncomingSyncMessage->set_message_id(msg->message_id());
 	delete msg;
		
 	m_outgoingMessages.push(m_replyForIncomingSyncMessage);
	
	m_replyForIncomingSyncMessage = 0;
	processOutgoingMessages();

	if(isSuspendRequest) {
		::kill(getpid(), SIGSTOP);
	}
}	

bool PIpcChannel::dispatchIncomingAsyncMessages()
{
	while (!m_incomingAsyncMessages.empty()) {

		PIpcMessage* msg = m_incomingAsyncMessages.front();
		m_incomingAsyncMessages.pop_front();

		if (G_UNLIKELY(msg->is_disconnect())) {
			delete msg;
			if (m_listener)
				m_listener->onDisconnected();
			return true;
		}

		if (m_listener)
			m_listener->onMessageReceived(*msg);
		delete msg;
	}

	return false;
}

gboolean PIpcChannel::dispatchIncomingAsyncMessagesCallback(gpointer arg)
{
	PIpcChannel* channel = (PIpcChannel*) arg;
	if (channel->dispatchIncomingAsyncMessages())
		return FALSE;

	g_source_destroy(channel->m_incomingAsyncMessagesDispatcherSrc);
	g_source_unref(channel->m_incomingAsyncMessagesDispatcherSrc);
	channel->m_incomingAsyncMessagesDispatcherSrc = 0;
	
	return FALSE;    
}

void PIpcChannel::sendReply(PIpcMessage* msg)
{
	m_replyForIncomingSyncMessage = msg;
}

gboolean PIpcChannel::nestedLoopTimeoutCallback(gpointer arg)
{
	g_critical("%s: Synchronous call timed out", __PRETTY_FUNCTION__);

	PIpcChannel* channel = (PIpcChannel*) arg;
	
	g_source_destroy(channel->m_nestedTimeoutSource);
	g_source_unref(channel->m_nestedTimeoutSource);
	channel->m_nestedTimeoutSource = 0;

	channel->m_syncCallTimedOut = true;

	g_main_loop_quit(channel->m_nestedMainLoop);
	
	return FALSE;
}

void PIpcChannel::asyncCallerDispatch()
{
	pthread_mutex_lock(&m_otherThreadOutgoingMessagesMutex);

	while (!m_otherThreadOutgoingMessages.empty()) {
		PIpcMessage* msg = m_otherThreadOutgoingMessages.front();
		m_otherThreadOutgoingMessages.pop();
		m_outgoingMessages.push(msg);
	}

	pthread_mutex_unlock(&m_otherThreadOutgoingMessagesMutex);
	
	processOutgoingMessages();    
}

void PIpcChannel::setRemoteIsSuspended(bool val)
{
	m_remoteIsSuspended = val;    
}

bool PIpcChannel::remoteIsSuspended() const
{
	return m_remoteIsSuspended;    
}
