#ifndef __MPSOCK_CONNECTION_H__
#define __MPSOCK_CONNECTION_H__

#include "libmpsocket.h"
#include "mpsock_scheduler.h"
#include "mpsock_interface.h"
#include "mpsock_dns.h"

typedef struct mpsock_connection
{
	int sd; // key: real socket descriptor
	size_t chunk_size;	// the chunk size of every following request this connection might make -> this might vary over time!
	struct mpsock_chunk *current_chunk; // the current chunk being requested
	struct mpsock_pool *pool;	// the pool this connection is belonging to
	struct http_parser *parser;	// the http_parser for this connection

	/* flags */
	int is_used;	// flag to indicate whether this connection is currently receiving data
	int is_corrupted;	// determines whether this connection is corrupted
	int is_normal_get;	// when set, this connection behaves like a normal GET without chunks
	int is_chunked_encoding;	// determines whether we got Transfer-Encoding: chunked
	int out_of_bounds_request;	// out of bounds request - happens with 2 initial paths

	size_t corrupted_payload_read;	// counting how many corrupted bytes have been read
	pthread_t pid;	// collector thread
	pthread_t new_con_pid;	// thread that tries to open a new connection
	size_t num_opening;	// num active threads trying to open subcon
	char volatile_buffer[VOLATILE_BUFFER_SIZE];	// received data from socket is first stored here, before being parsed and stored in chunk buffers
	char request_buffer[MAXIMUM_HEADER_SIZE];	// the buffer for the current request this connection is making to a server
	size_t request_len;	// the length of the current request
	UT_hash_handle hh;	// hash handle

	/* connection info */
	struct mpsock_interface *interface;	// interface this connection is using
	unsigned long ip;	// the server ip
	struct mpsock_addrs *adrs;	// server address struct
	int adrs_index;	// the map index of the ip we use

	int waiting;	// wait to finish dead data
	size_t waiting_bytes;	// number of bytes we wait for

	size_t bytes_received;	// the amont of bytes received by this connection

	/* scheduler */
	struct mpsock_scheduler *scheduler;	// the chunk size scheduler for this connection
} mpsock_connection;

/*
 * create the connection and a belonging parser and finally add connections to pool's connection_table
 */
mpsock_connection* create_connection(struct mpsock_pool *pool, int sd);

/*
 * get a random free sub connection from the data pool
 */
mpsock_connection* get_random_free_connection(struct mpsock_pool *pool);

/*
 * returns the next chunk that needs to be requested for this connection
 */
struct mpsock_chunk *next_chunk_for_connection(mpsock_connection *conn);

/*
 * calculates and sets current 
 */
void calculate_chunk_size(mpsock_connection *conn);

/*
 * reinitializes an existing connection
 */
void reinit_connection(mpsock_connection *conn);

/*
 * request send is imminent -> start time measure
 */
void connection_send_event(mpsock_connection *conn);

/*
 * read data -> start measure
 */
void connection_read_event(mpsock_connection *connection, int bytes);

/*
 * free the connection
 */
void free_connection(mpsock_connection *conn);

/*
 * handle a bad connection performance
 */
//void handle_bad_performance(mpsock_connection *conn);

#endif
