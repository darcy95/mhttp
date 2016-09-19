#ifndef __MPSOCK_SCHEDULER_H__
#define __MPSOCK_SCHEDULER_H__

#include "libmpsocket.h"

typedef struct mpsock_scheduler
{
	struct mpsock_connection *connection;	// the connection this scheduler is responsible for

	struct timeval send_start;	// indicating when the request was sent
	struct timeval first_recv;	// first bytes received
	struct timeval first_read; 	// time when the first bytes relevant for measurement were read
	struct timeval last_read;	// indicating when last read occured
	struct timeval tmp_time;	// temporary time for measurements
	struct timeval last_byte_read;	// when num butes was > 0
	size_t total_number_bytes;	// number of bytes read since first_read
	double rtt;	// estimated rtt for connection (in s)
	size_t last_bandwidth;	// previous bandwidth sample estimate when chunk_size was determined
	size_t bandwidth; // estimated bandwidth for connection (in bytes/s). Updated with every read operation
	size_t num_samples;	// number of bandwidth samples for harmonic mean
	double alpha;	// factor for cwnd
	size_t num_reads;	// number of reads commited
	size_t num_sends;	// number of requests send for this object
	int bad_connection;	// flag for bad connection

	/* expirimental */
	int is_decreased;
} mpsock_scheduler;

// address table from libmpsocket.h
extern struct mpsock_addrs *mpsock_address_table;

// interface table from libmpsocket.h
extern struct mpsock_interface *mpsock_interface_table;

//================== DESIGN HANDLING ================

/*
 * reinitialize the given scheduler
 */
void reinit_scheduler(mpsock_scheduler *scheduler);

/*
 * create a scheduler for the given connection
 */
mpsock_scheduler *create_scheduler(struct mpsock_connection *connection);

// ================ SCHEDULING FUNCTIONS ==================

/*
 * new connection because enough resources?
 */
int enough_res_for_conn(mpsock_scheduler *scheduler);

/*
 * start of request send event
 */
void scheduler_send_event(mpsock_scheduler *scheduler);

/*
 * number of given bytes read
 */
void scheduler_read_event(mpsock_scheduler *scheduler, int bytes);

/*
 * return the optimal chunk size
 */
size_t scheduler_get_chunk_size(mpsock_scheduler *scheduler);

/*
 * determines whether a new connections needs to be opened due to connection request limit
 */
int needs_new_connection(mpsock_scheduler *scheduler);

/*
 * determines whether the connection needs to be closed due to request limit
 */
int reached_maximum(mpsock_scheduler *scheduler);

/*
 * indicates bad performance -> connection will close if true
 */
int performs_bad(mpsock_scheduler *scheduler);

/*
 * when connection shuts down early, checks whether the partial chunk should be requested over a newly opened connection
 */
int partial_req_over_new_con(mpsock_scheduler *scheduler);

/*
 * get next ip address to connect to
 */
unsigned long scheduler_get_ip(mpsock_scheduler *scheduler, const char *host);

/*
 * returns the name of the interface that should be used next
 */
struct mpsock_interface *scheduler_get_interface(mpsock_scheduler *scheduler);

/*
 * gets the first occurrence of this ip from our table
 * this is called by connect(), when very first connection is established
 */
unsigned long scheduler_get_first_ip(mpsock_scheduler *scheduler, unsigned long ip);

#endif
