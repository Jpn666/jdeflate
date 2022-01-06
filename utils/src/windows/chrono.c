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


#if CTB_PLATFORM != CTB_PLATFORM_WINDOWS
#	error "wrong platform"
#else
#	if !defined(WIN32_LEAN_AND_MEAN)
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <windows.h>
#	include <mmsystem.h>
#	define CHRONO_WINDOWS_CLOCK
#endif

/* */
#if !defined(CHRONO_CFG_USE_PERFORMANCE_COUNTER)
#	define CHRONO_CFG_USE_PERFORMANCE_COUNTER 1
#endif


static bool hirestimer;     /* hi-resolution timer flag */
static bool chronostarted;  /* flag */

/* raw clock base time, first tick value */
static DWORD starttime;

#if CHRONO_CFG_USE_PERFORMANCE_COUNTER
static LARGE_INTEGER countstart;
static LARGE_INTEGER pfrecuency;
#endif


void
chrono_initialize(void)
{
	static volatile intxx iflag = 1;
	if (iflag == 0) {
		return;
	}
	iflag = 0;
	
	if (chronostarted) {
		return;
	}
	chronostarted = 1;
	
#if CHRONO_CFG_USE_PERFORMANCE_COUNTER
	hirestimer = 1;
	if (QueryPerformanceFrequency(&pfrecuency)) {
		QueryPerformanceCounter(&countstart);
		return;
	}
#endif
	timeBeginPeriod(2);
	starttime  = timeGetTime();
	hirestimer = 0;
}

void
chrono_sleep(uint32 ms)
{
	Sleep(ms);
}

uint32
chrono_ticks(void)
{
	uint32 ticks;
	
#if CHRONO_CFG_USE_PERFORMANCE_COUNTER
	LARGE_INTEGER htime;
#endif
	if (chronostarted == 0){ 
		chrono_initialize();
	}
	
#if CHRONO_CFG_USE_PERFORMANCE_COUNTER
	if (hirestimer) {
		QueryPerformanceCounter(&htime);
		htime.QuadPart -= countstart.QuadPart;
		htime.QuadPart *= 1000;
		htime.QuadPart /= pfrecuency.QuadPart;
		return (uint32) htime.QuadPart;
	}
#endif
	ticks = timeGetTime();
	return ticks - starttime;
}
