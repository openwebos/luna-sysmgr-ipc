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


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "PIpcAsyncCaller.h"

PIpcAsyncCallerBase::PIpcAsyncCallerBase(GMainLoop* loop)
{
	int retval = ::pipe(m_pipeFd);
	if (retval) {}

	// setup an iochannel on the read end of the pipe
	m_ioChannel =  g_io_channel_unix_new(m_pipeFd[0]);
	m_ioSource = g_io_create_watch(m_ioChannel, (GIOCondition) G_IO_IN);
	g_source_set_callback(m_ioSource, (GSourceFunc) callback, this, NULL);
	g_source_set_can_recurse(m_ioSource, true);

	GMainContext* ctxt = g_main_loop_get_context(loop);
	g_source_attach(m_ioSource, ctxt);

	pthread_mutex_init(&m_mutex, NULL);
}

PIpcAsyncCallerBase::~PIpcAsyncCallerBase()
{
	g_source_destroy(m_ioSource);
	g_io_channel_unref(m_ioChannel);
	g_source_unref(m_ioSource);
	
    ::close(m_pipeFd[0]);
	::close(m_pipeFd[1]);

	pthread_mutex_destroy(&m_mutex);
}

void PIpcAsyncCallerBase::call()
{	
	char byte = 1;

	pthread_mutex_lock(&m_mutex);
	int retval = ::write(m_pipeFd[1], &byte, 1);
	if (retval) {}
	pthread_mutex_unlock(&m_mutex);
}

gboolean PIpcAsyncCallerBase::callback(GIOChannel* channel, GIOCondition condition, gpointer arg)
{
	char byte = 0;
	if (::read(g_io_channel_unix_get_fd(channel), &byte, 1) != 1)
		return TRUE;
	
	PIpcAsyncCallerBase* caller = (PIpcAsyncCallerBase*) arg;
	caller->dispatch();

	return TRUE;
}
