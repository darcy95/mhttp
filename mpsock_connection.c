#include "mpsock_connection.h"
#include "mpsock_pool.h"
#include "mpsock_socket.h"
#include "mpsock_collector.h"
#include "http_parser.h"

void reinit_connection(mpsock_connection *conn)
{
	// TODO: maybe lock
	if(USE_ASSERTS) assert(conn->is_used == FALSE);
	if(USE_ASSERTS) assert(conn->waiting == FALSE);

	http_parser_soft_init(conn->parser,HTTP_BOTH);
	// reset chunk_size only if too small
	// TODO: verify
	if(conn->chunk_size < conn->pool->mpsocket->initial_chunk_size)
	{
		conn->chunk_size = conn->pool->mpsocket->initial_chunk_size;
	}

	conn->out_of_bounds_request = FALSE;
	conn->is_corrupted = FALSE;
	conn->is_normal_get = FALSE;
	conn->is_chunked_encoding = FALSE;
	conn->is_used = FALSE;
	conn->corrupted_payload_read = 0;
	conn->waiting = FALSE;
	conn->waiting_bytes = 0;
	conn->bytes_received = 0;
	// TODO: test
	conn->current_chunk = NULL;

	reinit_scheduler(conn->scheduler);

	if(USE_ASSERTS) assert(conn->is_used == FALSE);
	if(USE_ASSERTS) assert(conn->waiting == FALSE);
} 

http_parser* create_parser(mpsock_connection *connection)
{
	// create the parser for the given connection
	http_parser *parser = (http_parser*)malloc(sizeof(http_parser));
	parser->connection = connection;
	http_parser_init(parser,HTTP_BOTH);
	return parser;
}

mpsock_connection* create_connection(mpsock_pool *pool, int sd)
{
	// create the connection
	mpsock_connection *conn = (mpsock_connection*)malloc(sizeof(mpsock_connection));
	conn->pool = pool;
	conn->sd = sd;
	conn->chunk_size = conn->pool->mpsocket->initial_chunk_size;
	conn->is_used = FALSE;
	conn->waiting = FALSE;
	conn->waiting_bytes = 0;
	conn->out_of_bounds_request = FALSE;
	conn->is_corrupted = FALSE;
	conn->is_normal_get = FALSE;
	conn->is_chunked_encoding = FALSE;
	conn->corrupted_payload_read = 0;
	conn->scheduler = create_scheduler(conn);
	conn->parser = create_parser(conn);
	conn->interface = NULL;
	conn->current_chunk = NULL;
	conn->ip = 0;
	conn->adrs = NULL;
	conn->adrs_index = -1;
	conn->num_opening = 0;
	conn->bytes_received = 0;

	// add connection to pool's connection table
	HASH_ADD_INT(pool->connection_table,sd,conn);

	if(pool->best_connection == NULL)
	{
		pool->best_connection = conn;
	}

	return conn;
}

mpsock_connection* get_random_free_connection(mpsock_pool *pool)
{
	mpsock_connection *cur_conn;
	mpsock_connection *best_conn = NULL;

	// check whether the best connection is free
	// TODO: verify
	//if(pool->best_connection != NULL)
	//{
	//	if(!pool->best_connection->is_used && !pool->best_connection->scheduler->bad_connection)
	//	{
	//		return pool->best_connection;
	//	}
	//}

	// find the best free connection
	for(cur_conn=pool->connection_table; cur_conn != NULL; cur_conn = cur_conn->hh.next)
	{
		if(cur_conn->is_used || cur_conn->scheduler->bad_connection) continue;

		if(best_conn == NULL)
		{
			best_conn = cur_conn;
		}
		// TODO: is bandwidth metric alone enough to determine best connection?
		else if(best_conn->scheduler->bandwidth < cur_conn->scheduler->bandwidth)
		{
			best_conn = cur_conn;
		}
	}

	return best_conn;

	/*
	// check every connection whether it is free
	for(cur_conn=pool->connection_table; cur_conn != NULL; cur_conn = cur_conn->hh.next)
	{
		// TODO: verify
		if(!cur_conn->is_used && !cur_conn->scheduler->bad_connection)
		{
			return cur_conn;
		}
	}

	return NULL;
	*/
}

mpsock_chunk *next_chunk_for_connection(mpsock_connection *conn)
{
	return next_chunk_for_pool(conn,conn->chunk_size);
}

void calculate_chunk_size(mpsock_connection *conn)
{
	LOG_DEBUG("%scalculate_chunk_size",FUN_EVENT);

	// check if first response was already parsed
	if(!conn->pool->is_response_created)
	{
		conn->chunk_size = conn->pool->mpsocket->initial_chunk_size;
		//conn->chunk_size = conn->scheduler->rtt * ALPHA_MIN * conn->scheduler->bandwidth;
		//if(conn->chunk_size == 0) conn->chunk_size = conn->pool->mpsocket->initial_chunk_size;
		LOG_INFO("%sfirst request with chunk_size %zu KB",COND_EVENT,conn->chunk_size/1024);
		return;
	}

	conn->chunk_size = scheduler_get_chunk_size(conn->scheduler);

	struct timeval tmp_ts;
	gettimeofday(&tmp_ts,NULL);
	double tmp_diff = tmp_ts.tv_sec - global_start_ts.tv_sec;
	tmp_diff += (double)(tmp_ts.tv_usec - global_start_ts.tv_usec)/1000000;
	LOG_INFO("%ssd#%d at %fs scheduler determined chunk_size %zu KB",RESULT_EVENT,conn->sd,tmp_diff,conn->chunk_size/1024);
	//LOG_SYNC("sd#%d %f %zu\n",conn->sd,tmp_diff,conn->chunk_size/1024);

	/* MOVED TO SCHEDULER
	// adjust in case it is too big
	if(conn->chunk_size > conn->pool->current_file_size - conn->pool->current_chunk_request->start_byte)
	{
		conn->chunk_size = conn->pool->current_file_size - conn->pool->current_chunk_request->start_byte;
		LOG_INFO("%ssd#%d due to file_size reduced chunk_size to %zu KB",RESULT_EVENT,conn->sd,conn->chunk_size/1024);
	}

	// in case it is too high
	if(conn->chunk_size > MAXIMUM_CHUNK_SIZE)
	{
		conn->chunk_size = MAXIMUM_CHUNK_SIZE;
		LOG_INFO("%ssd#%d due to maximum_size reduced chunk_size to %zu KB",RESULT_EVENT,conn->sd,conn->chunk_size/1024);
	}
	*/
}

void connection_send_event(mpsock_connection *conn)
{
	scheduler_send_event(conn->scheduler);
}

void connection_read_event(mpsock_connection *connection, int bytes)
{
	scheduler_read_event(connection->scheduler, bytes);
}

void free_connection(mpsock_connection *conn)
{
	if(USE_ASSERTS) assert(conn->interface != NULL);
	//if(USE_ASSERTS) assert(conn->is_used == FALSE);
	//if(USE_ASSERTS) assert(conn->waiting == FALSE);

	if(conn->is_used)
	{
		if(USE_ASSERTS) assert(conn->waiting == TRUE);
	}

	conn->is_used = FALSE;
	conn->waiting = FALSE;

	// mark interface as free
	//if(conn->interface->used_by == conn)
	//{
	//	conn->interface->in_use = FALSE;
	//}
	//else
	//{
	//	LOG_ERROR("%ssd#%d interface %s still used by another connection",RESULT_EVENT,conn->sd,conn->interface->name);
	//}

	lock_for_interface(conn->pool);
	conn->interface->num_used--;
	if(conn->interface->if_id == 0)
	{
		conn->pool->first_iface--;
	}
	else if(conn->interface->if_id == 1)
	{
		conn->pool->second_iface--;
	}

	if(USE_ASSERTS) assert(conn->pool->first_iface >= 0);
	if(USE_ASSERTS) assert(conn->pool->second_iface >= 0);
	if(USE_ASSERTS) assert(conn->interface->num_used >= 0);
	unlock_for_interface(conn->pool);

	// mark ip as unused
	if(USE_ASSERTS) assert(conn->adrs_index >= 0);
	if(USE_ASSERTS) assert(conn->adrs_index < conn->adrs->count);
	if(USE_ASSERTS) assert(conn->adrs != NULL);
	//if(conn->adrs != NULL && conn->adrs_index >= 0)
	//{
		conn->adrs->ipset[conn->adrs_index].inuse = FALSE;
	//}

	lock_for_scheduling(conn->pool);

	// close socket
	f_close(conn->sd);
	LOG_INFO("%sclosed socket sd#%d",FUN_EVENT,conn->sd);

	HASH_DEL(conn->pool->connection_table,conn);

	unlock_for_scheduling(conn->pool);

	// TODO: free structure
}

/*
void handle_bad_performance(mpsock_connection *connection)
{
	lock_for_next_chunk(connection->pool);
	LOG_INFO("%ssd#%d handle bad performance",FUN_EVENT,connection->sd);

	if(USE_ASSERTS) assert(connection->scheduler->bad_connection == FALSE);
	if(USE_ASSERTS) assert(connection->current_chunk->pos_save <= connection->current_chunk->data_size);
	// set connection as bad
	connection->scheduler->bad_connection = TRUE;

	if(connection->current_chunk->pos_save < connection->current_chunk->data_size)
	{
		// add chunk to partial chunk table
		HASH_ADD_INT(connection->pool->partial_chunk_table,start_byte,connection->current_chunk);
		
		unlock_for_next_chunk(connection->pool);
		
		// TODO: verify MAX_BREAKS_IN_ROW
		if(partial_req_over_new_con(connection->scheduler) && connection->pool->breaks_in_row < MAX_BREAKS_IN_ROW)
		{
			// create new connection for requests
			lock_for_threading(connection->pool);
			mpsock_connection *conn = create_sub_connection(connection->pool,connection);
			start_connection(conn);
			unlock_for_threading(connection->pool);
		}
	}
	else
	{
		unlock_for_next_chunk(connection->pool);
	}
}
*/
