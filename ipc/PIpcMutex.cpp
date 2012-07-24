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
#include <sys/shm.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>

#include <glib.h>

#include "PIpcMutex.h"

static const uint32_t s_marker = 0xF00DF00D;

struct PIpcMutexData {
	uint32_t marker1;
	pthread_mutex_t mutex;
	uint32_t marker2;
};

PIpcMutex* PIpcMutex::create()
{
	int key = -1;
	
    while (key < 0) {
		
		struct timeval tv;
		gettimeofday(&tv, NULL);
        // ftok has unspecified behavior if the lower 8-bits are 0
        if ((tv.tv_usec & 0xFF) == 0)
            tv.tv_usec++;
		key_t ipckey = ftok(".", tv.tv_usec);
		key = ::shmget(ipckey, sizeof(PIpcMutexData), 0644 | IPC_CREAT | IPC_EXCL);
		if (key == -1 && errno != EEXIST) {
			g_critical("%s: failed to create mutex: %s (requested size: %lu, id: %ld)", 
                __PRETTY_FUNCTION__, strerror(errno), (long unsigned int)sizeof(PIpcMutexData), tv.tv_usec);
			return 0;
		}
	}

	void* data = ::shmat(key, NULL, 0);
	if ((void*)-1 == data) {
		g_critical("%s: failed to attach to shared memory key %d: %s", __PRETTY_FUNCTION__, key, strerror(errno));
		return 0;
	}
	
	// Auto delete when all processes detach
	::shmctl(key, IPC_RMID, NULL);

	PIpcMutex* m = new PIpcMutex;
	m->m_key = key;
	m->m_data = (PIpcMutexData*) data;

	m->m_data->marker1 = s_marker;
	m->m_data->marker2 = s_marker;

	pthread_mutexattr_t attr;
	pthread_mutexattr_init (&attr);
	pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	pthread_mutexattr_setrobust_np(&attr, PTHREAD_MUTEX_ROBUST_NP);

	pthread_mutex_t* mutex = (pthread_mutex_t*) &m->m_data->mutex;
	pthread_mutex_init(mutex, &attr);
	pthread_mutexattr_destroy(&attr);

	return m;
}

PIpcMutex* PIpcMutex::attach(int key)
{
	void* data = ::shmat(key, NULL, 0);
	if ((void*)-1 == data) {
		g_critical("%s: failed to attach to shared memory key %d: %s", __PRETTY_FUNCTION__, key, strerror(errno));
		return 0;
	}
	
	PIpcMutex* m = new PIpcMutex;
	m->m_key = key;
	m->m_data = (PIpcMutexData*) data;

	return m;
}

PIpcMutex::PIpcMutex()
	: m_key(-1)
	, m_data(0)
{    
}

PIpcMutex::~PIpcMutex()
{
	// pthread_mutex_destroy is deliberately not called to avoid
	// the issue of ownership. on Linux pthread_mutex_destroy does
	// nothing apart from checking that the mutex is unlocked
	
    if (m_data > 0)
		::shmdt(m_data);
}

bool PIpcMutex::lock()
{
	if (!isValid()) {
		g_critical("%s: mutex corrupted", __PRETTY_FUNCTION__);
		return false;
	}

	pthread_mutex_t* mutex = &m_data->mutex;

	int ret = pthread_mutex_lock(mutex);
	if (ret != 0) {
		if (ret == EOWNERDEAD)
			pthread_mutex_consistent_np(mutex);
		else {
			g_critical("%s: %s", __PRETTY_FUNCTION__, strerror(ret));
			return false;
		}
	}

	return true;
}

bool PIpcMutex::tryLock()
{
	if (!isValid()) {
		g_critical("%s: mutex corrupted", __PRETTY_FUNCTION__);
		return false;
	}

	pthread_mutex_t* mutex = &m_data->mutex;
	
	int ret = pthread_mutex_trylock(mutex);
	if (ret != 0) {
		if (ret == EOWNERDEAD)
			pthread_mutex_consistent_np(mutex);
		else if (ret == EBUSY)
			return false;
		else {
			g_critical("%s: %s", __PRETTY_FUNCTION__, strerror(ret));
			return false;			
		}
	}

	return true;		
}

bool PIpcMutex::unlock()
{
	if (!isValid()) {
		g_critical("%s: mutex corrupted", __PRETTY_FUNCTION__);
		return false;
	}
    
	pthread_mutex_t* mutex = &m_data->mutex;

	int ret = pthread_mutex_unlock(mutex);
	if (ret != 0) {
		g_critical("%s: %s", __PRETTY_FUNCTION__, strerror(ret));
		return false;
	}

	return true;
}

bool PIpcMutex::isValid() const
{
    if (G_UNLIKELY(m_data == 0))
		return false;

	if (G_UNLIKELY(m_data->marker1 != s_marker ||
				   m_data->marker2 != s_marker))
		return false;

	return true;
}
