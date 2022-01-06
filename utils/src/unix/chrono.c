/*
 * Copyright (C) 2014, jpn jpn@gsforce.net
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../../chrono.h"


#if CTB_PLATFORM != CTB_PLATFORM_UNIX
#	error "wrong platform"
#else
#	include <unistd.h>
#	if defined(_POSIX_TIMERS) && _POSIX_TIMERS > 0
#		if defined(CLOCK_MONOTONIC)
#			define CHRONO_MONOTONIC
#		endif
#	endif
#	include <sys/time.h>
#	include <time.h>
#	include <errno.h>
#	define CHRONO_UNIX_CLOCK
#endif

/* flag to tweak the chrono_sleep function if nanosleep is not avaible */
#if !defined(CHRONO_CFG_USE_NANOSLEEP)
#	define CHRONO_CFG_USE_NANOSLEEP 1
#endif

/* */
#if !defined(CHRONO_CFG_FORCE_MONOTONIC)
#	define CHRONO_CFG_FORCE_MONOTONIC 0
#endif


#if CHRONO_CFG_FORCE_MONOTONIC && !defined(CHRONO_MONOTONIC)
#	error "monotonic clock not avaible"
#endif


static bool monotonictime;  /* monotonic flag */
static bool chronostarted;  /* flag */

/* raw clock base time, first tick value */
#if defined(CHRONO_MONOTONIC)
static struct timespec tmsstart;
#endif
static struct timeval  tmvstart;


void
chrono_initialize(void)
{
	static volatile intxx iflag = 1;
	if (!iflag)
		return;
	iflag = 0;
	if (chronostarted)
		return;
	chronostarted = true;
#if defined(CHRONO_MONOTONIC)
	monotonictime = true;
	if (clock_gettime(CLOCK_MONOTONIC, &tmsstart) != -1)
		return;
#endif
	monotonictime = false;
	gettimeofday(&tmvstart, NULL);
}


#define CHRONO_NSEC_SCALE 1000000  /* nanoseconds  */
#define CHRONO_USEC_SCALE    1000  /* microseconds */


void
chrono_sleep(uint32 ms)
{
	uintxx res;
#if CHRONO_CFG_USE_NANOSLEEP
	struct timespec tms;
	struct timespec rem;
	tms.tv_sec  = ms / 1000;
	tms.tv_nsec = 1000000 * (ms % 1000);
	do {
		errno = 0;
		res   = nanosleep(&tms, &rem);
	} while (res && (errno == EINTR));
#else
	uint32 t2;
	uint32 t1;
	uint32 delta;
	struct timeval tmv;
	t1 = chrono_ticks();
	do {
		errno = 0;
		t2    = chrono_ticks();
		delta = t2 - t1;
		t1    = t2;
		if (delta >= ms)
			break;
		ms -= delta;
		tmv.tv_sec  = ms / 1000;
		tmv.tv_usec = 1000 * (ms % 1000);
		res = select(0, NULL, NULL, NULL, &tmv);
	} while (res && (errno == EINTR));
#endif
}


#define CHRONO_GETTM() ((tx.tv_nsec - tmsstart.tv_nsec) / CHRONO_NSEC_SCALE)
#define CHRONO_GETTN() ((tx.tv_usec - tmvstart.tv_usec) / CHRONO_USEC_SCALE)

uint32
chrono_ticks(void)
{
	uint32 ticks;
	if (!chronostarted)
		chrono_initialize();
	if (monotonictime) {
#if defined(CHRONO_MONOTONIC)
		struct timespec tx;
		clock_gettime(CLOCK_MONOTONIC, &tx);
		ticks = (tx.tv_sec - tmsstart.tv_sec) * 1000 + CHRONO_GETTM();
#else
		ticks = 0;  /* to avoid compiler warning */
#endif
	}
	else {
		struct timeval tx;
		gettimeofday(&tx, NULL);
		ticks = (tx.tv_sec - tmvstart.tv_sec) * 1000 + CHRONO_GETTN();
	}
	return ticks;
}
