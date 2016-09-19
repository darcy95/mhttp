#ifndef __MPSOCK_SCHEDULER_MINIMUM_THROUGHPUT_ASSURANCE_H__
#define __MPSOCK_SCHEDULER_MINIMUM_THROUGHPUT_ASSURANCE_H__

#include "libmpsocket.h"
#include "mpsock_scheduler.h"

#define TIME_LEFT_UNTIL_SHUTDOWN 0.1 // in s
#define BANDWIDTH_DIFF 0.9

/*
 * in case of no more new chunks for requesting:
 * checks whether the connection should shut down so the its can requested by a faster connection
 */
int optimize_final_chunk_request(struct mpsock_scheduler *scheduler);

#endif
