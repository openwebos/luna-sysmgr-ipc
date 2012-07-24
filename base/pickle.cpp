// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright (c) 2009-2012 Hewlett-Packard Development Company, L.P.

#include "pickle.h"

#include <stdlib.h>
#include <string.h>

#include <string>

//------------------------------------------------------------------------------

// Payload is uint32_t aligned.

Pickle::Pickle(size_t capacity)
    : payload_(0),
      position_(0),
      capacity_(alignInt(capacity, sizeof(uint32_t))) {

  payload_ = reinterpret_cast<char*>(::malloc(capacity_));
  if (payload_ != 0)
    memset(payload_, 0, capacity_);
}

Pickle::~Pickle() {

  ::free(payload_);
}

bool Pickle::readBool(bool* result) const {

  int32_t tmp;
  if (!readInt32(&tmp))
    return false;
  *result = tmp ? true : false;
  return true;
}

bool Pickle::readChar(char* result) const {

  if (!hasRoomFor(sizeof(*result)))
    return false;

  *result = *(payload_ + position_);

  updateIter(sizeof(*result));
  return true;
}

bool Pickle::readInt16(int16_t* result) const {

  if (!hasRoomFor(sizeof(*result)))
    return false;

  *result = *reinterpret_cast<int16_t*>(payload_ + position_);

  updateIter(sizeof(*result));
  return true;
}

bool Pickle::readInt32(int32_t* result) const {

  if (!hasRoomFor(sizeof(*result)))
    return false;

  *result = *reinterpret_cast<int32_t*>(payload_ + position_);

  updateIter(sizeof(*result));
  return true;
}

bool Pickle::readSize(size_t* result) const {

  if (!hasRoomFor(sizeof(*result)))
    return false;

  *result = *reinterpret_cast<size_t*>(payload_ + position_);

  updateIter(sizeof(*result));
  return true;
}

bool Pickle::readInt64(int64_t* result) const {

  if (!hasRoomFor(sizeof(*result)))
    return false;

  memcpy(result, payload_ + position_, sizeof(*result));

  updateIter(sizeof(*result));
  return true;
}

bool Pickle::readDouble(double* result) const {

  if (!hasRoomFor(sizeof(*result)))
    return false;

  memcpy(result, payload_ + position_, sizeof(*result));

  updateIter(sizeof(*result));
  return true;
}

bool Pickle::readString(std::string* result) const {

  int32_t len;
  if (!readLength(&len))
    return false;
  if (!hasRoomFor(len))
    return false;

  result->assign(payload_ + position_, len);

  updateIter(len);
  return true;
}

bool Pickle::readBytes(const char** data, int length) const {

  if (!hasRoomFor(length))
    return false;

  *data = reinterpret_cast<const char*>(payload_ + position_);

  updateIter(length);
  return true;
}

bool Pickle::readData(const char** data, int* length) const {

  if (!readLength(length))
    return false;

  return readBytes(data, *length);
}

bool Pickle::readLength(int32_t* result) const {

  if (!readInt32(result))
    return false;
  return ((*result) >= 0);
}

bool Pickle::writeBytes(const void* data, int32_t data_len) {

  if (position_ + data_len > capacity_)
    return false;

  memcpy(payload_ + position_, data, data_len);

  updateIter(data_len);
  return true;
}

bool Pickle::writeString(const std::string& value) {

  if (!writeInt32(static_cast<int32_t>(value.size())))
    return false;

  return writeBytes(value.data(), static_cast<uint32_t>(value.size()));
}

bool Pickle::writeData(const char* data, int length) {
  return length >= 0 && writeInt32(length) && writeBytes(data, length);
}

