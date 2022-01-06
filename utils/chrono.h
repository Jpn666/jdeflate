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

#ifndef b38f77b5_2b9e_44a1_ad19_072ca21baba7
#define b38f77b5_2b9e_44a1_ad19_072ca21baba7

/*
 * chrono.h
 * Timing fuctions.
 */

#include "../ctoolbox.h"


/* windows flags */
/* #define CHRONO_CFG_USE_PERFORMANCE_COUNTER 1 */

/* unix flags */
/* #define CHRONO_CFG_USE_NANOSLEEP   1 */
/* #define CHRONO_CFG_FORCE_MONOTONIC 1 */


/*
 * Initizalize the chrono system. */
void chrono_initialize(void);

/*
 * Pauses the program the given amount of time (milliseconds). */
void chrono_sleep(uint32 ms);

/*
 * Returns the time in milliseconds since the call to chrono_initialize or
 * the first call to chrono_ticks. */
uint32 chrono_ticks(void);

/* 
 * Checks if the time t2 is greater than t1. */
CTB_INLINE bool chrono_haselapsep(uint32 reference, uint32 time);


/* 
 * Inlines */

CTB_INLINE bool
chrono_haselapsep(uint32 reference, uint32 time)
{
	return ((int32) (time - reference)) <= 0;
}


#endif

