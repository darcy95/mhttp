#include <stdio.h>
#include "mpsock_socket.h"
#include "mpsock_connection.h"
#include "mpsock_def.h"

mpsock_socket* create_mpsocket(int sd, size_t initial_chunk_size, size_t num_conns, mpsock_buffer *buffer, int max_req_con, int max_req_serv, int max_req_mpsocket, int initial_alpha, int version, int alpha_max, int processing_skips, int use_initial_second_path, int use_random_path, int log_decisions, int log_traffic, int log_metrics, int scheduler_algo)
{
	mpsock_socket *sock = (mpsock_socket*)malloc(sizeof(mpsock_socket));

	sock->num_sends = 0;
	sock->max_req_con = max_req_con;
	sock->max_req_serv = max_req_serv;
	sock->max_req_mpsocket = max_req_mpsocket;
	sock->initial_alpha = initial_alpha;
	sock->alpha_max = alpha_max;
	sock->scheduler_version = version;
	sock->processing_skips = processing_skips;
	sock->use_initial_second_path = use_initial_second_path;
	sock->use_random_path = use_random_path;
	sock->log_decisions = log_decisions;
	sock->log_traffic = log_traffic;
	sock->log_metrics = log_metrics;
	sock->scheduler_algo = scheduler_algo;

	// set chunk size
	sock->initial_chunk_size = initial_chunk_size;

	// connection count...
	sock->max_connections = num_conns;

	// create pool
	mpsock_pool *pool = create_pool(sock,buffer,sd);
	sock->pool = pool;

	// set main socket descriptor
	sock->m_sd = sd;

	if(USE_ASSERTS) assert(HASH_COUNT(pool->connection_table) == 1);
	if(USE_ASSERTS) assert(get_random_free_connection(pool)->sd == sd);

	// add new socket to hash table
	HASH_ADD_INT(mpsock_socket_table, m_sd, sock);

	LOG_INFO("%stotal number of open mpsockets: %d",RESULT_EVENT,HASH_COUNT(mpsock_socket_table));

	return sock;
}

void free_socket(mpsock_socket *sock)
{
	// TODO: HASH_DEL
	free_pool(sock->pool);
	HASH_DEL(mpsock_socket_table,sock);
}

mpsock_socket* find_mpsocket(int m_sd)
{
	mpsock_socket *sock;
    HASH_FIND_INT(mpsock_socket_table, &m_sd, sock);
    return sock;
}

int is_mpsocket(int m_sd)
{
	mpsock_socket *sock = find_mpsocket(m_sd);
	if(sock == NULL)
	{
		return FALSE;
	}
	else
	{
		return TRUE;
	}
}

size_t socket_bytes_ready(mpsock_socket *sock)
{
	if(!sock->pool->is_response_created)
	{
		// we do not give back partwise responses, because only after fully parsing response
		// we can verify whether there is a server problem to react upon or not
		return 0;
	}
	else
	{
		return pool_bytes_ready(sock->pool);
	}
}

size_t read_data_from_socket(mpsock_socket *sock, void *buf, size_t count, int flags)
{
	return read_data_from_pool(sock->pool,buf,count,flags);
}

void set_port(mpsock_socket *sock, int port)
{
	LOG_INFO("%sset_port(%d)",FUN_EVENT,port);
	sock->port = port;
}

void set_hostname(mpsock_socket *sock, const char *host, size_t len)
{
	sock->hostname[len] = '\0';
	strncpy(sock->hostname,host,len);
}

void advance_socket_save_pointer(mpsock_socket *sock)
{
	advance_pool_save_pointer(sock->pool);
}

void strip_mpsock_from_socket(mpsock_socket *sock)
{
	HASH_DEL(mpsock_socket_table,sock);
	// TODO: free structures
}
