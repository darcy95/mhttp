/**
 * Multipath Multi-source HTTP (mHTTP)
 *
 * Developers: Juhoon Kim (kimjuhoon@gmail.com), Karl Fischer
 *
 */

#ifndef __MPSOCK_ALGORITHM_H__
#define __MPSOCK_ALGORITHM_H__

#include "libmpsocket.h"
#include "mpsock_scheduler.h"

/*
* Dynamic alpha algorithm to adjust alpha in size = alpha*B*rtt
*/
size_t dynamic_alpha_algorithm(struct mpsock_scheduler *scheduler);

/*
* Time chunk algorithm
*/
size_t time_chunk_algorithm(struct mpsock_scheduler *scheduler);

#endif
