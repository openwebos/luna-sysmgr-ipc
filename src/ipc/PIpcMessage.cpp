// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright (c) 2009-2012 Hewlett-Packard Development Company, L.P.

#include <glib.h>

#include "PIpcMessage.h"

static int32_t s_nextMessageId = 0;
static pthread_mutex_t s_nextMessageIdMutex = PTHREAD_MUTEX_INITIALIZER;

//------------------------------------------------------------------------------

PIpcMessage::~PIpcMessage() {
}

PIpcMessage::PIpcMessage(uint32_t capacity)
    : Pickle(capacity),
      header_(0) {

  if (capacity >= sizeof(Header)) {
    header_ = reinterpret_cast<Header*>(payload());
    if (header_) {
      header_->routing = header_->id = header_->type = header_->flags = 0;
      updateIter(sizeof(Header));
    }
  }
}

PIpcMessage::PIpcMessage(int32_t routing_id, uint16_t type, uint32_t payload_size)
    : Pickle(sizeof(Header) + payload_size) {

  header_ = reinterpret_cast<Header*>(payload());
  if (header_) {
    header_->routing = routing_id;
    header_->type = type;
	header_->flags = 0;

	pthread_mutex_lock(&s_nextMessageIdMutex);

	s_nextMessageId++;
	// Avoid wraparound
	if (s_nextMessageId < 0)
		s_nextMessageId = 1;
	
	header_->id = s_nextMessageId;

	pthread_mutex_unlock(&s_nextMessageIdMutex);
	
    updateIter(sizeof(Header));
  }
}

