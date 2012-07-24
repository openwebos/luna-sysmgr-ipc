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


#include "PIpcSocketSource.h"

static gboolean PrvPrepare(GSource* src, gint* timeout);
static gboolean PrvCheck(GSource* src);
static gboolean PrvDispatch(GSource* src, GSourceFunc callback, gpointer userData);
static void     PrvFinalize(GSource* src);

struct PIpcSocketSource : public GSource
{
	GPollFD pollFd;
	GIOCondition condition;
};

GSourceFuncs PIpcSocketSourceFuncs = {
	PrvPrepare,
	PrvCheck,
	PrvDispatch,
	PrvFinalize
};

PIpcSocketSource* PIpcSocketSourceCreate(int socketFd, GIOCondition condition,
										 PIpcSocketSourceCallback callback,
										 gpointer userData)
{
	if (socketFd < 0 || !callback)
		return 0;		
	
	GSource* s = g_source_new(&PIpcSocketSourceFuncs, sizeof(PIpcSocketSource));
	PIpcSocketSource* src = (PIpcSocketSource*) s;

	src->condition = condition;
	src->pollFd.fd = socketFd;
	src->pollFd.events = condition;

	g_source_add_poll(s, &src->pollFd);
	g_source_set_callback(s, (GSourceFunc) callback, userData, NULL);
	g_source_set_can_recurse(s, true);
	
	return src;
}

void PIpcSocketSourceSetCondition(PIpcSocketSource* src, GIOCondition condition)
{
	if (src->condition == condition)
		return;

	src->condition = condition;
	src->pollFd.events = condition;
}

static gboolean
PrvPrepare(GSource* s, gint* timeout)
{
	*timeout = -1;
	return FALSE;
}

static gboolean
PrvCheck(GSource* s)
{
	PIpcSocketSource* src = (PIpcSocketSource*) s;
	return (src->pollFd.revents & src->condition);
}

static gboolean
PrvDispatch(GSource* s, GSourceFunc c, gpointer userData)
{
	PIpcSocketSourceCallback callback = (PIpcSocketSourceCallback) c;
	PIpcSocketSource* src = (PIpcSocketSource*) s;

	return (*callback)(src, (GIOCondition) (src->condition & src->pollFd.revents),
					   userData);
}

static void
PrvFinalize(GSource* src)
{
}
