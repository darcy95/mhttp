/**
 * Multipath Multi-source HTTP (mHTTP)
 *
 * Developers: Juhoon Kim (kimjuhoon@gmail.com), Karl Fischer
 *
 */

#ifndef __MPSOCK_POOL_H__
#define __MPSOCK_POOL_H__

#include "libmpsocket.h"
#include "mpsock_buffer.h"

/*
 * this is the chunk container. It is a single linked list
 */
typedef struct mpsock_chunk
{
	size_t start_byte;	// key: first byte this chunk contains
	size_t pos_save;	// pointer until which data is stored
	size_t pos_read;	// pointer until which data has been read
	size_t data_size;	// size of data stored in this chunk
	void *buffer;	// data storage
	size_t buffer_size;	// size of actual buffer for this chunk (might be bigger than data_size)
	struct mpsock_chunk *next; // next list element
	UT_hash_handle hh;	// hashtable handle
} mpsock_chunk;

/*
 * this structure handles the chunk collection
 */
typedef struct mpsock_pool
{
	struct mpsock_socket *mpsocket;	// socket this pool belongs to
	struct mpsock_connection *connection_table; // a table with all connections for this pool
	size_t next_start_byte;	// the next start byte that needs to be requested for this pool
	mpsock_chunk *current_chunk_request;	// the current chunk to be requested
	mpsock_chunk *current_chunk_read;	// the current chunk to be read
	mpsock_chunk *current_chunk_save;	// the current chunk the save pointer is at
	mpsock_chunk *partial_chunk_table;	// table with partial chunks that need rerequesting
	size_t current_file_size;	// the size of the current file (without response header)
	size_t current_request_size;	// size of the current request
	char *current_request;	// buffer to store the current original request from application
	int is_response_created;	// flag to indicate whether the server's response is already parsed
	size_t current_response_size;	// current response size
	char current_response[MAXIMUM_HEADER_SIZE];	// buffer for response
	struct mpsock_buffer *data_buffer;	// the chunks payload buffer

	/* virtual buffer pointers for application level */
	size_t pos_buffer_read;	// pointer to indicate until which point the application read the data
	size_t pos_buffer_save;	// pointer to indicate until which point the data for the application is stored

	/* synching stuff */
	pthread_mutex_t save_lock;	// this lock is used when a collector thread wants to advance the save pointer of this pool
	pthread_mutex_t next_chunk_lock;	// lock used when collector thread wants to determine the next chunk to request
	pthread_mutex_t threading_lock;	// create/start/stop/free connection lock
	pthread_mutex_t calc_lock;	// lock for calculation purposes
	pthread_mutex_t scheduler_lock;	// lock for scheduling purposes
	pthread_mutex_t interface_lock;	// lock for path scheduling purposes
	pthread_mutex_t con_lock;	// lock for connection creation

	/* official status code to application */
	int status_code;

	/* breaking */
	size_t breaks_in_row;	// how many times in a row connections were lost
	int is_broken;	// indicates whether this pool is broken due to connection loss

	/* workarounds */
	int has_length_set;
	int num_sends;	// total number of sent requests for current object
	int finishing;	// determines whether this file is downloaded soon (for scheduling)
	size_t first_iface;	// number of connections that are using the first interface
	size_t second_iface;	// number of connections that are using the second interface
	struct timeval req_start;	// time when download was started
	mpsock_chunk *requested_chunk_table;	// table with already requested chunks -> used for baseline algorithm
	int block_new_conns;	// do not establish any new connections (for baseline scheduler)

	/* scheduling */
	struct mpsock_connection *best_connection; // the currently best connection of the pool (in terms of bandwidth)
} mpsock_pool;

/*
 * initializes (or resets) the pool, so that it can be used for another request again
 */
void init_pool(mpsock_pool *pool);

/*
 * frees the given pool (and all substructures)
 */
void free_pool(mpsock_pool *pool);

/*
 * free read chunk
 */
void free_chunk(mpsock_pool *pool, mpsock_chunk *chunk);

/*
 * create a data pool for given socket adding given sd as first connection with real socket.
 */
mpsock_pool* create_pool(struct mpsock_socket *mpsocket, struct mpsock_buffer *buffer, int sd);

/*
 * returns the next chunk of given size that needs to be requested for this pool
 */
mpsock_chunk *next_chunk_for_pool(struct mpsock_connection *conn, size_t size);

/*
 * read available amount of data from pool, but with count as maximum
 */
ssize_t read_data_from_pool(mpsock_pool *pool, void *buf, size_t count, int flags);

/*
 * get all real socket descriptors in given fd_set - returns number of set descriptors
 */
size_t get_fd_set(mpsock_pool *pool, fd_set *con_set);

/*
 * returns the number of bytes ready to read
 */ 
size_t pool_bytes_ready(mpsock_pool *pool);

/*
 * advances the pos_buffer_save pointer as far as data is coherent
 */
void advance_pool_save_pointer(mpsock_pool *pool);

/*
 * creates a clone of the given connection - tries to reuse already open and free connections to the server
 */
struct mpsock_connection *create_sub_connection(mpsock_pool *pool, struct mpsock_connection *connection);

/*
 * determines whether this connection is making the very first request for the requested resource
 */
int is_first_connection_and_first_request(struct mpsock_connection *connection);

/*
 * locks pool for determining next requested chunk
 */
void lock_for_next_chunk(mpsock_pool *pool);

/*
 * unlocks after determining next requested chunk
 */
void unlock_for_next_chunk(mpsock_pool *pool);

/*
 * locks this pool to advance the pos_buffer_save pointer
 */
void lock_for_save(mpsock_pool *pool);

/*
 * unlocks this pool for further write pointer advances
 */
void unlock_for_save(mpsock_pool *pool);

/*
 * locks this pool for interface scheduling
 */
void lock_for_interface(mpsock_pool *pool);

/*
 * unlocks this pool for interface scheduling
 */
void unlock_for_interface(mpsock_pool *pool);

/*
 * locks this pool for threading purposes
 */
void lock_for_threading(mpsock_pool *pool);

/*
 * unlocks this pool for threading purposes
 */
void unlock_for_threading(mpsock_pool *pool);

/*
 * lock for calculation purposes
 */
void lock_for_calc(mpsock_pool *pool);

/*
 * unlock for calculation purposes
 */
void unlock_for_calc(mpsock_pool *pool);

/*
 * lock for scheduling purposes
 */
void lock_for_scheduling(mpsock_pool *pool);

/*
 * unlock for scheduling purposes
 */
void unlock_for_scheduling(mpsock_pool *pool);

/*
 * lock for new connection
 */
void lock_for_con(mpsock_pool *pool);

/*
 * unlock for new connection
 */
void unlock_for_con(mpsock_pool *pool);

/*
 * returns the first connection (connection whose sd is the same as mpsockets m_sd)
 */
struct mpsock_connection *get_first_connection(mpsock_pool *pool);

/*
 * returns the connection with highest throughput
 */
struct mpsock_connection *get_best_connection(mpsock_pool *pool);

/*
 * returns the number of active connections
 */
int num_active_connections(mpsock_pool *pool);

/*
 * returns the number of waiting connections
 */
int num_waiting_connections(mpsock_pool *pool);

#endif
