/**
 * Multipath Multi-source HTTP (mHTTP)
 *
 * Developers: Juhoon Kim (kimjuhoon@gmail.com), Karl Fischer
 *
 */

#ifndef __MPSOCK_SOCKET_H__
#define __MPSOCK_SOCKET_H__

#include "libmpsocket.h"
#include "mpsock_pool.h"
#include "mpsock_dns.h"
#include "mpsock_interface.h"
#include "mpsock_buffer.h"

/*
 * mpsocket structure to handle fake sockets
 */
typedef struct mpsock_socket
{
	int m_sd; // key: main connections socket descriptor for application layer
	size_t initial_chunk_size;
	struct mpsock_pool *pool; // pool that handles file collection
	size_t max_connections;	// maximum open connections
	int max_req_con;	// maximum requests per connection
	int max_req_serv;	// maximum requests per server
	int max_req_mpsocket;	// maimum requests in total for this socket
	int initial_alpha;	// initial alpha value
	int scheduler_algo;	// define which scheduler algorithm to use
	int scheduler_version;	// version of the scheduler to use
	int use_initial_second_path; // second path initially flag
	int use_random_path;	// use random path
	int log_traffic;	// log traffic distribution
	int log_decisions;	// log scheduler decisions
	int log_metrics;	// log path characterizations
	int alpha_max;	// maximum alpha to use for scheduling
	int processing_skips;	// number of read operations calculus we skip to save computation
	size_t num_sends;	// number of requests sent over this socket
	int port;	// used port
	char hostname[MAX_LENGTH_DOMAIN];	// domain we GET
	UT_hash_handle hh;
} mpsock_socket;

// hashtable from libmpsocket.c
extern mpsock_socket *mpsock_socket_table;

/*
 * create a new fake socket with real given socket sd and store it in the given hash table
 */
mpsock_socket* create_mpsocket(int sd, size_t initial_chunk_size, size_t num_conns, struct mpsock_buffer *buffer, int max_req_con, int max_req_serv, int max_req_mpsocket, int initial_alpha, int version, int alpha_max, int processing_skips, int use_initial_second_path, int use_random_path, int log_decisions, int log_traffic, int log_metrics, int scheduler_algo);

/*
 * find the fake socket with given main socket descriptor
 */
mpsock_socket* find_mpsocket(int m_sd);

/*
 * checks whether the given socket descriptor is an mpsocket stored in table
 */
int is_mpsocket(int fd);

/*
 * returns the number of bytes ready to read
 */
size_t socket_bytes_ready(mpsock_socket *sock);

/*
 * read available amount of data from scoket, but with count as maximum
 */
size_t read_data_from_socket(mpsock_socket *sock, void *buf, size_t count, int flags);

/*
 * set the current port for the connection
 */
void set_port(mpsock_socket *sock, int port);

/*
 * set the host for this socket
 */
void set_hostname(mpsock_socket *sock, const char *host, size_t len);

/*
 * free the current socket
 */
void free_socket(mpsock_socket *sock);

/*
 * advances the save pointers for this socket's buffers
 */
void advance_socket_save_pointer(mpsock_socket *sock);

/*
 * strips off the mpsock wrappers from main connections real socket descriptor, so libmpsocket will not recognize it anymore as an mhttp socket
 */
void strip_mpsock_from_socket(mpsock_socket *sock);

#endif
