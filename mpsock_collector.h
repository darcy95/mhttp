#ifndef __MPSOCK_COLLECTOR_H__
#define __MPSOCK_COLLECTOR_H__

#include "libmpsocket.h"

struct mpsock_connection;

/*
 * this method is run in a thread and collects data from the given connection and moves it to the pool chunk ring buffer
 */
void* collector_thread(void *connection);

/*
 * starts a connection (first request + collector_thread)
 */
int start_connection(struct mpsock_connection *connection);

/*
 * stops the connection and opens a new one if necessary
 */
void stop_connection(struct mpsock_connection *connection);

/*
 * react accordingly to premature shutdown
 */
void handle_bad_performance(struct mpsock_connection *connection, int bad_perf);

#endif
