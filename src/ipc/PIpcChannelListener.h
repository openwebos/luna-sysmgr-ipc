/**
 *  Copyright (c) 2009-2013 LG Electronics, Inc.
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


#ifndef PIPCCHANNELLISTENER_H
#define PIPCCHANNELLISTENER_H

class PIpcMessage;
class PIpcChannel;

class PIpcChannelListener
{
public:

	PIpcChannelListener() : m_channel(0) {}
	virtual ~PIpcChannelListener() {}

	void setChannel(PIpcChannel* channel) { m_channel = channel; }
	PIpcChannel* channel() const { return m_channel; }

	virtual void onMessageReceived(const PIpcMessage& msg) = 0;
	virtual void onDisconnected() = 0;

protected:

	PIpcChannel* m_channel;		
};

#endif /* PIPCCHANNELLISTENER_H */