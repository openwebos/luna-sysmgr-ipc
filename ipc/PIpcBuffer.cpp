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


#include <pthread.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>

#include <glib.h>

#include "PIpcBuffer.h"

// Can't include this due to compile errors
//#include <linux/shm.h>

#ifndef SHM_CACHE_WRITETHROUGH
#define SHM_CACHE_WRITETHROUGH   0200000	/* custom! */
#endif

static const uint32_t s_marker = 0xF00DF00D;

struct PIpcBufferHeader {
	
	uint32_t marker1;
	pthread_mutex_t mutex;
	pthread_mutex_t mutex2;
	int usableSize;
	int transitionBufferKey;	
	int resizedBufferKey;
	uint32_t marker2;
};

PIpcBuffer* PIpcBuffer::create(int size)
{
	if (size <= 0)
		return 0;
	
	int key = -1;

	// Add extra page so we can round up the starting address to be page-aligned
	int totalSize = size + sizeof(PIpcBufferHeader) + getpagesize();	
	
    while (key < 0) {
		
		struct timeval tv;
		gettimeofday(&tv, NULL);
        // ftok has unspecified behavior if the lower 8-bits are 0
        if ((tv.tv_usec & 0xFF) == 0)
            tv.tv_usec++;
		key_t ipckey = ftok(".", tv.tv_usec);
		key = ::shmget(ipckey, totalSize, 0644 | IPC_CREAT | IPC_EXCL);
		if (key == -1 && errno != EEXIST) {
			g_critical("%s: failed to create buffer: %s (requested bytes: %d, total bytes: %d, id: %ld)", 
                __PRETTY_FUNCTION__, strerror(errno), size, totalSize, tv.tv_usec);
			return 0;
		}
	}

	void* data = ::shmat(key, NULL, SHM_CACHE_WRITETHROUGH);
	if ((void*)-1 == data) {
		g_critical("%s: failed to attach to shared memory key %d: %s", __PRETTY_FUNCTION__, key, strerror(errno));
		return 0;
	}

	// Auto delete when all processes detach
	::shmctl(key, IPC_RMID, NULL);
	
	PIpcBufferHeader* h = (PIpcBufferHeader*) data;
	h->marker1 = s_marker;
	h->marker2 = s_marker;
	h->usableSize = size;
	h->transitionBufferKey = 0;
	h->resizedBufferKey = 0;

	pthread_mutexattr_t attr;
	pthread_mutexattr_init (&attr);
	pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	pthread_mutexattr_setrobust_np(&attr, PTHREAD_MUTEX_ROBUST_NP);

	// initialize the buffer lock mutex
	pthread_mutex_t* mutex = (pthread_mutex_t*) &h->mutex;
	pthread_mutex_init(mutex, &attr);

	// initialize the second buffer lock mutex
	pthread_mutex_t* mutex2 = (pthread_mutex_t*) &h->mutex2;
	pthread_mutex_init(mutex2, &attr);
	pthread_mutexattr_destroy(&attr);
	

	PIpcBuffer* b = new PIpcBuffer;
	b->m_key = key;
	b->m_data = data;
	b->m_size = totalSize;

	b->m_usableData = (char*)data + sizeof(PIpcBuffer);
	// Ensure the address given is page aligned
	b->m_usableData = (void*) (((long)(b->m_usableData) & ~(getpagesize() - 1)) + getpagesize());

	// Lock pages into RAM for the GPU
	(void) ::mlock(b->m_usableData, h->usableSize);
	
	return b;
}

PIpcBuffer* PIpcBuffer::attach(int key, int size)
{
	void* data = ::shmat(key, NULL, SHM_CACHE_WRITETHROUGH);
	if ((void*)-1 == data) {
		g_critical("%s: failed to attach to shared memory key %d: %s", __PRETTY_FUNCTION__, key, strerror(errno));
		return 0;
	}

	if (size <= 0) {

		struct shmid_ds ds;
		if (::shmctl(key, IPC_STAT, &ds) != 0) {
			g_critical("%s: failed to do IPC_STAT on shared memory key %d: %s", __PRETTY_FUNCTION__, key, strerror(errno));
			::shmdt(data);
			return 0;
		}
		
		size = ds.shm_segsz;
	}
	
	PIpcBuffer* b = new PIpcBuffer;
	b->m_key = key;
	b->m_data = data;
	b->m_size = size;
	b->m_usableData = (char*)data + sizeof(PIpcBuffer);
	// Ensure the address given is page aligned
	b->m_usableData = (void*) (((long)b->m_usableData & ~(getpagesize() - 1)) + getpagesize());
	
	return b;
}

PIpcBuffer::PIpcBuffer()
	: m_key(-1)
	, m_data(0)
	, m_size(0)
	, m_lockCount(0)
	, m_lockCount2(0)
{
    
}

PIpcBuffer::~PIpcBuffer()
{
	// pthread_mutex_destroy is deliberately not called to avoid
	// the issue of ownership. on Linux pthread_mutex_destroy does
	// nothing apart from checking that the mutex is unlocked
    if (m_data > 0) {

		if (m_lockCount)
			unlock();

		if (m_lockCount2)
			unlock2();

		PIpcBufferHeader* h = (PIpcBufferHeader*) m_data;
		(void) ::munlock(m_usableData, h->usableSize);		
		::shmdt(m_data);
	}
}

bool PIpcBuffer::trylock()
{
	if (!isValid())
		return false;

	PIpcBufferHeader* h = (PIpcBufferHeader*) m_data;
	pthread_mutex_t* mutex = &h->mutex;

	return trylock(mutex, m_lockCount);
}

bool PIpcBuffer::lock()
{
	if (!isValid())
		return false;

	PIpcBufferHeader* h = (PIpcBufferHeader*) m_data;
	pthread_mutex_t* mutex = &h->mutex;

	return lock(mutex, m_lockCount);
}

bool PIpcBuffer::unlock()
{
	if (!isValid())
		return false;

	PIpcBufferHeader* h = (PIpcBufferHeader*) m_data;
	pthread_mutex_t* mutex = &h->mutex;

	return unlock(mutex, m_lockCount);
}


bool PIpcBuffer::trylock2()
{
	if (!isValid())
		return false;

	PIpcBufferHeader* h = (PIpcBufferHeader*) m_data;
	pthread_mutex_t* mutex2 = &h->mutex2;

	return trylock(mutex2, m_lockCount2);
}

bool PIpcBuffer::lock2()
{
	if (!isValid())
		return false;

	PIpcBufferHeader* h = (PIpcBufferHeader*) m_data;
	pthread_mutex_t* mutex2 = &h->mutex2;

	return lock(mutex2, m_lockCount2);
}

bool PIpcBuffer::unlock2()
{
	if (!isValid())
		return false;

	PIpcBufferHeader* h = (PIpcBufferHeader*) m_data;
	pthread_mutex_t* mutex2 = &h->mutex2;

	return unlock(mutex2, m_lockCount2);
}

bool PIpcBuffer::isValid() const
{
    if (G_UNLIKELY(m_data == 0))
		return false;

	PIpcBufferHeader* h = (PIpcBufferHeader*) m_data;

	if (G_UNLIKELY(h->marker1 != s_marker ||
				   h->marker2 != s_marker))
		return false;

	return true;    
}

void* PIpcBuffer::data() const
{
	if (!m_data || !m_usableData)
		return 0;

	return m_usableData;
}

int PIpcBuffer::size() const
{
	if (!m_data)
		return 0;

	PIpcBufferHeader* h = (PIpcBufferHeader*) m_data;
	return h->usableSize;
}

int PIpcBuffer::getTransitionBufferKey() const
{
	PIpcBufferHeader* header = (PIpcBufferHeader*) m_data;
	
	return header->transitionBufferKey;
	
}

void PIpcBuffer::setTransitionBufferKey(int key)
{
	PIpcBufferHeader* header = (PIpcBufferHeader*) m_data;
	
	header->transitionBufferKey = key;
}

int PIpcBuffer::getResizedBufferKey() const
{
	PIpcBufferHeader* header = (PIpcBufferHeader*) m_data;
	
	return header->resizedBufferKey;
	
}

void PIpcBuffer::setResizedBufferKey(int key)
{
	PIpcBufferHeader* header = (PIpcBufferHeader*) m_data;
	
	header->resizedBufferKey = key;
}

bool PIpcBuffer::trylock(pthread_mutex_t* mutex, int& lockCount)
{
	if (lockCount) {
		lockCount++;
		return true;
	}		

	int ret = pthread_mutex_trylock(mutex);
	//printf("PIpcBuffer::trylock: %d, %p, %d\n", m_key, mutex, ret);
	if (ret != 0) {
		if (ret == EOWNERDEAD)
			pthread_mutex_consistent_np(mutex);
		else if (ret == EBUSY) {
			return false;
		}
		else {
			g_critical("%s: %s", __PRETTY_FUNCTION__, strerror(ret));
			return false;
		}
	}

	lockCount++;

	return true;    
    
}

bool PIpcBuffer::lock(pthread_mutex_t* mutex, int& lockCount)
{
	if (lockCount) {
		lockCount++;
		return true;
	}

	int ret = pthread_mutex_lock(mutex);
	//printf("PIpcBuffer::lock: %d, %p, %d\n", m_key, mutex, ret);
	if (ret != 0) {
		if (ret == EOWNERDEAD)
			pthread_mutex_consistent_np(mutex);
		else {
			g_critical("%s: %s", __PRETTY_FUNCTION__, strerror(ret));
			return false;
		}
	}

	lockCount++;

	return true;    
}

bool PIpcBuffer::unlock(pthread_mutex_t* mutex, int& lockCount)
{
	switch (lockCount) {
	case 0:
		return false;
	case 1:
		lockCount--;
		break;
	default:
		lockCount--;
		return true;
	}

	int ret = pthread_mutex_unlock(mutex);
	//printf("PIpcBuffer::unlock: %d, %p, %d\n", m_key, mutex, ret);
	if (ret != 0) {
		g_critical("%s: %s", __PRETTY_FUNCTION__, strerror(ret));
		return false;
	}
	
	return true;    
}
