#include <unistd.h>
#include "mpsock_pool.h"
#include "mpsock_connection.h"
#include "mpsock_socket.h"
#include "mpsock_collector.h"
#include "mpsock_interface.h"


int num_active_connections(mpsock_pool *pool)
{
	int num = 0;

	// iterate over all connections and find the one we need
	mpsock_connection *current_con;
	for(current_con=pool->connection_table; current_con != NULL; current_con = current_con->hh.next)
	{
		if(current_con->is_used)
		{
			num++;
		}
	}

	return num;
}

int num_waiting_connections(mpsock_pool *pool)
{
	int num = 0;

	// iterate over all connections and find the one we need
	mpsock_connection *current_con;
	for(current_con=pool->connection_table; current_con != NULL; current_con = current_con->hh.next)
	{
		if(current_con->waiting)
		{
			num++;
		}
	}

	return num;
}

mpsock_connection *get_first_connection(mpsock_pool *pool)
{
	if(USE_ASSERTS) assert(HASH_COUNT(pool->connection_table) > 0);

	// iterate over all connections and find the one we need
	mpsock_connection *current_con;
	for(current_con=pool->connection_table; current_con != NULL; current_con = current_con->hh.next)
	{
		if(current_con->sd == pool->mpsocket->m_sd)
		{
			break;
		}
	}

	if(USE_ASSERTS) assert(current_con->sd == pool->mpsocket->m_sd);

	return current_con;
}

int is_first_connection_and_first_request(struct mpsock_connection *connection)
{
	if(connection->sd != connection->pool->mpsocket->m_sd)
	{
		// not the first established connection
		return FALSE;
	}

	if(connection->pool->next_start_byte != 0)
	{
		// not the first byte in current request -> cannot be first request
		return FALSE;
	}

	// iterate over all connections and check if they are used
	mpsock_connection *current_con;
	int cnt = 0;
	for(current_con=connection->pool->connection_table; current_con != NULL; current_con = current_con->hh.next)
	{
		if(current_con->is_used)
		{
			cnt++;
		}
	}

	if(cnt != 1) 
	{
		// more than one connection active -> cannot be first request
		return FALSE;
	}

	if(USE_ASSERTS) assert(cnt >= 1);
	if(USE_ASSERTS) assert(cnt <= HASH_COUNT(connection->pool->connection_table));

	return TRUE;
}

void lock_for_calc(mpsock_pool *pool)
{
	// TODO: better
	int ret = pthread_mutex_lock(&(pool->calc_lock));
	if(USE_ASSERTS) assert(ret == 0);
}

void unlock_for_calc(mpsock_pool *pool)
{
	// TODO: better
	int ret = pthread_mutex_unlock(&(pool->calc_lock));
	if(USE_ASSERTS) assert(ret == 0);
}

void lock_for_con(mpsock_pool *pool)
{
	// TODO: better
	int ret = pthread_mutex_lock(&(pool->con_lock));
	if(USE_ASSERTS) assert(ret == 0);
}

void unlock_for_con(mpsock_pool *pool)
{
	// TODO: better
	int ret = pthread_mutex_unlock(&(pool->con_lock));
	if(USE_ASSERTS) assert(ret == 0);
}

void lock_for_interface(mpsock_pool *pool)
{
	// TODO: better
	int ret = pthread_mutex_lock(&(pool->interface_lock));
	if(USE_ASSERTS) assert(ret == 0);
}

void unlock_for_interface(mpsock_pool *pool)
{
	// TODO: better
	int ret = pthread_mutex_unlock(&(pool->interface_lock));
	if(USE_ASSERTS) assert(ret == 0);
}

void lock_for_next_chunk(mpsock_pool *pool)
{
	// TODO: better
	int ret = pthread_mutex_lock(&(pool->next_chunk_lock));
	if(USE_ASSERTS) assert(ret == 0);
}

void unlock_for_next_chunk(mpsock_pool *pool)
{
	// TODO: better
	int ret = pthread_mutex_unlock(&(pool->next_chunk_lock));
	if(USE_ASSERTS) assert(ret == 0);
}

void lock_for_scheduling(mpsock_pool *pool)
{
	// TODO: better
	int ret = pthread_mutex_lock(&(pool->scheduler_lock));
	if(USE_ASSERTS) assert(ret == 0);
}

void unlock_for_scheduling(mpsock_pool *pool)
{
	// TODO: better
	int ret = pthread_mutex_unlock(&(pool->scheduler_lock));
	if(USE_ASSERTS) assert(ret == 0);
}

void lock_for_save(mpsock_pool *pool)
{
	// TODO: better
	int ret = pthread_mutex_lock(&(pool->save_lock));
	if(USE_ASSERTS) assert(ret == 0);
}

void unlock_for_save(mpsock_pool *pool)
{
	// TODO: better
	int ret = pthread_mutex_unlock(&(pool->save_lock));
	if(USE_ASSERTS) assert(ret == 0);
}

void lock_for_threading(mpsock_pool *pool)
{
	// TODO: better
	int ret = pthread_mutex_lock(&(pool->threading_lock));
	if(USE_ASSERTS) assert(ret == 0);
}

void unlock_for_threading(mpsock_pool *pool)
{
	// TODO: better
	int ret = pthread_mutex_unlock(&(pool->threading_lock));
	if(USE_ASSERTS) assert(ret == 0);
}

void free_chunk(mpsock_pool *pool, mpsock_chunk *chunk)
{
	LOG_DEBUG("%sfree chunk",FUN_EVENT);

	// TODO: maybe better lock...
	lock_for_next_chunk(pool);

	if(USE_ASSERTS) assert(chunk->pos_read == chunk->pos_save);
	//if(USE_ASSERTS) assert(chunk->pos_read > 0);

	if(USE_RINGBUFFER)
	{
		free_chunk_in_buffer(pool->data_buffer,chunk);
	}
	else
	{
		free(chunk->buffer);
		free(chunk);
	}

	unlock_for_next_chunk(pool);
}

void init_pool(mpsock_pool *pool)
{
	//while()
	//{
		// TODO: wait for collectors to finish
	//}

	// TODO: verify
	//if(USE_ASSERTS) assert(num_active_connections(pool) - num_waiting_connections(pool) == 0);
	if(USE_ASSERTS) assert(HASH_COUNT(pool->partial_chunk_table) == 0);
	pool->requested_chunk_table = NULL;

	// TODO: verify
	// free old memory
	mpsock_chunk *cur_to_free = pool->current_chunk_read;
	mpsock_chunk *next_to_free = cur_to_free;
	while(next_to_free != NULL)
	{
		cur_to_free = next_to_free;
		next_to_free = next_to_free->next;
		free_chunk(pool,cur_to_free);
	}

	/*
	if(pool->current_request != NULL)
	{
		free(pool->current_request);
	}
	*/

	pool->current_request = NULL;
	pool->current_chunk_read = NULL;
	pool->current_chunk_save = NULL;
	pool->current_chunk_request = NULL;
	pool->best_connection = NULL;
	pool->next_start_byte = 0;
	pool->current_request_size = 0;
	pool->current_response_size = 0;
	pool->current_file_size = 0;
	pool->pos_buffer_read = 0;
	pool->pos_buffer_save = 0;
	pool->is_response_created = FALSE;
	pool->status_code = 0;
	pool->num_sends = 0;
	pool->has_length_set = FALSE;
	pool->breaks_in_row = 0;
	pool->is_broken = FALSE;
	pool->finishing = FALSE;
	pool->req_start.tv_sec=0;
	pool->req_start.tv_usec=0;
	pool->block_new_conns = FALSE;

	// reset interfaces
	reset_interfaces();

	// reinit connections
	mpsock_connection *conn;
	for(conn=pool->connection_table; conn!=NULL; conn=conn->hh.next)
	{
		if(conn->is_used == FALSE && conn->waiting == FALSE)
		{
			reinit_connection(conn);
		}
	}
}

void free_pool(mpsock_pool *pool)
{
	//if(USE_ASSERTS) assert(pool->pos_buffer_read > 0);
	if(pool->status_code < 300)
	{
		if(USE_ASSERTS) assert(pool->pos_buffer_read == pool->pos_buffer_save);
		if(USE_ASSERTS) assert(pool->pos_buffer_save == pool->current_file_size+pool->current_response_size);
	}
	//else
	//{
	//	// polled wait...
	//	while(pool->pos_buffer_save != pool->current_file_size+pool->current_response_size)
	//	{
	//		// TODO: optimize
	//		usleep(10);
	//	}
	//}

	if(USE_ASSERTS) assert(pool->pos_buffer_save == pool->current_file_size+pool->current_response_size);

	// TODO: do correctly

	// close connections
	mpsock_connection *conn;
	for(conn=pool->connection_table; conn!=NULL; conn=conn->hh.next)
	{
		free_connection(conn);
	}

	free(pool);
}

mpsock_pool* create_pool(mpsock_socket *mpsocket, mpsock_buffer *buffer, int sd)
{
	LOG_DEBUG("%screate_pool(sd=%d)",FUN_EVENT,sd);

	// set socket, buffer and connection table
	mpsock_pool *pool = (mpsock_pool*)malloc(sizeof(mpsock_pool));
	pool->mpsocket = mpsocket;
	pool->connection_table = NULL;
	pool->partial_chunk_table = NULL;
	pool->data_buffer = buffer;
	pool->current_chunk_read = NULL;
	pool->current_chunk_save = NULL;
	pool->current_chunk_request = NULL;
	pool->first_iface = 0;
	pool->second_iface = 0;

	// initialize the locks
	pthread_mutex_init(&(pool->save_lock),NULL);
	pthread_mutex_init(&(pool->next_chunk_lock),NULL);
	pthread_mutex_init(&(pool->threading_lock),NULL);
	pthread_mutex_init(&(pool->calc_lock),NULL);
	pthread_mutex_init(&(pool->scheduler_lock),NULL);
	pthread_mutex_init(&(pool->interface_lock),NULL);
	pthread_mutex_init(&(pool->con_lock),NULL);

	// create first connection for this pool
	create_connection(pool,sd);

	// intiialize the pool
	init_pool(pool);

	return pool;
}

/*
 * determines whether the given pool already parsed stored data of an object
 */
int is_all_stored(mpsock_pool *pool)
{
	// TODO: check which one is better
	//return (pool->pos_buffer_save > 0 && pool->pos_buffer_save == pool->current_file_size + pool->current_response_size);
	return (pool->pos_buffer_save == pool->current_file_size + pool->current_response_size);
}

/*
 * determines whether everything is read
 */
int is_all_read(mpsock_pool *pool)
{
	return (pool->is_response_created && pool->pos_buffer_read == pool->current_file_size + pool->current_response_size);
}

/*
 * determines whether a chunk has been completely read by the application
 */
int chunk_is_read(mpsock_chunk *chunk)
{
	return chunk->data_size == chunk->pos_read;
}

mpsock_chunk *next_chunk_for_pool(mpsock_connection *conn, size_t size)
{
	/* LOCK START */
	mpsock_pool *pool = conn->pool;
	lock_for_next_chunk(pool);

	mpsock_chunk *chunk = NULL;

	int do_create = TRUE;

	// check if we already determined the file size
	if(pool->is_response_created)
	{
		LOG_DEBUG("%snext_start_byte=%zu, size=%zu",COND_EVENT,pool->next_start_byte,size);

		// check if there are more requests necessary
		if(pool->next_start_byte >= pool->current_file_size)
		{
			// there is no more to request
			do_create = FALSE;
		}

		// adjust size if necessary
		if(pool->next_start_byte + size > pool->current_file_size)
		{
			size = pool->current_file_size - pool->next_start_byte;
			LOG_DEBUG("%sadjusted size to %zu",COND_EVENT,size);
		}
	}

	//mpsock_chunk *chunk = NULL;

	if(HASH_COUNT(pool->partial_chunk_table) > 0)
	{
		// we got unfinished chunks
		LOG_DEBUG("%snext chunk is partial",RESULT_EVENT);
		do_create = FALSE;
		chunk = pool->partial_chunk_table;
		HASH_DEL(pool->partial_chunk_table,chunk);
	}

	if(do_create)
	{
		if(USE_RINGBUFFER)
		{
			chunk = allocate_chunk_in_buffer(pool->data_buffer,pool->next_start_byte,size);
		}
		else
		{
			// create the chunk
			chunk = (mpsock_chunk*)malloc(sizeof(mpsock_chunk));
			chunk->next = NULL;
			chunk->start_byte = pool->next_start_byte;
			chunk->data_size = size;
			chunk->buffer_size = size;
			chunk->pos_save = 0;
			chunk->pos_read = 0;
			chunk->buffer = (void*)malloc(size);
		}

		// check if predecessor exists
		if(pool->current_chunk_request != NULL)
		{
			// append to predecessor
			pool->current_chunk_request->next = chunk;
	
			if(USE_ASSERTS) assert(chunk->start_byte > 0);
		}
		else
		{
			if(USE_ASSERTS) assert(pool->current_chunk_read == NULL);
			if(USE_ASSERTS) assert(pool->current_chunk_save == NULL);
			if(USE_ASSERTS) assert(pool->current_chunk_request == NULL);

			// this is the first chunk -> set all other pointers
			pool->current_chunk_read = chunk;
			pool->current_chunk_save = chunk;
		}

		// set as current
		pool->current_chunk_request = chunk;

		// update next start byte to be requested
		pool->next_start_byte += size;

		if(USE_ASSERTS) assert(pool->next_start_byte > 0);

		LOG_DEBUG("%snext chunk start=%zu, size=%zu",RESULT_EVENT,chunk->start_byte,chunk->data_size);
	}
	/* LOCK END */
	unlock_for_next_chunk(pool);

	return chunk;
}

/*
 * move amount of payload specified by count to given application buffer
 */
void move_payload_to_application_buffer(mpsock_pool *pool, void *buf, size_t offset, size_t count, int flags)
{
	if(USE_ASSERTS) assert(pool->pos_buffer_save - pool->pos_buffer_read >= count);
	if(USE_ASSERTS) assert(pool->is_response_created = TRUE);

	LOG_DEBUG("%spayload to transfer: %zu, current pos_buffer_read=%zu, pos_buffer_save=%zu",FUN_EVENT,count,pool->pos_buffer_read,pool->pos_buffer_save);

	size_t transfered = 0;
	mpsock_chunk *current_chunk = pool->current_chunk_read;
	while(transfered < count)
	{
		// determine whether we have to move to next chunk for reading
		if(chunk_is_read(current_chunk))
		{
			current_chunk = current_chunk->next;

			if((flags & MSG_PEEK) != MSG_PEEK)
			{
				// TODO
				// no peek -> advance pointers and free old space
				LOG_DEBUG("%sstart free",COND_EVENT);
				if(USE_ASSERTS) assert(pool->current_chunk_read->next != NULL);
				mpsock_chunk *to_free = pool->current_chunk_read;
				pool->current_chunk_read = pool->current_chunk_read->next;
				free_chunk(pool,to_free);
				LOG_DEBUG("%sfreed chunk",COND_EVENT);
			}

			LOG_DEBUG("%snext chunk to move --- start_byte=%zu, data_size=%zu, pos_read=%zu, pos_save=%zu",RESULT_EVENT,current_chunk->start_byte,current_chunk->data_size,current_chunk->pos_read,current_chunk->pos_save);
		}

		size_t len = current_chunk->pos_save-current_chunk->pos_read;
		if(transfered + len > count)
		{
			len = count-transfered;
		}

		if(USE_ASSERTS) assert(len <= current_chunk->pos_save - current_chunk->pos_read);
		if(USE_ASSERTS) assert(len > 0);

		// copy data to application
		LOG_DEBUG("%stransfer payload bytes %zu - %zu to application",RESULT_EVENT,current_chunk->start_byte+current_chunk->pos_read,current_chunk->start_byte+current_chunk->pos_read+len);
		if(USE_RINGBUFFER)
		{
			read_from_buffer(pool->data_buffer,buf+offset+transfered,current_chunk->buffer,current_chunk->pos_read,len);
		}
		else
		{
			memcpy(buf+offset+transfered,current_chunk->buffer+current_chunk->pos_read,len);
		}

		transfered += len;

		// try to advance pointers
		if((flags & MSG_PEEK) != MSG_PEEK)
		{
			// no peek -> advance
			current_chunk->pos_read += len;
			pool->pos_buffer_read += len;
		}

		

		if(USE_ASSERTS) assert(current_chunk != NULL);
	}

	return;
}

/*
 * move amount specified by count of response header to given application buffer
 */
void move_header_to_application_buffer(mpsock_pool *pool, void *buf, size_t offset, size_t count, int flags)
{
	LOG_DEBUG("%scurrent_response_size=%zu, pos_buffer_read=%zu, count=%zu",FUN_EVENT,pool->current_response_size,pool->pos_buffer_read,count);
	if(USE_ASSERTS) assert(pool->pos_buffer_read < pool->current_response_size);
	if(USE_ASSERTS) assert(pool->current_response_size - pool->pos_buffer_read >= count);

	LOG_DEBUG("%smove_header(count=%zu), pos_buffer_read=%zu",FUN_EVENT,count,pool->pos_buffer_read);

	memcpy(buf+offset,pool->current_response+pool->pos_buffer_read,count);

	// check if we need to advance the buffer pointer
	if((flags & MSG_PEEK) != MSG_PEEK)
	{
		pool->pos_buffer_read += count;
	}
}

ssize_t read_data_from_pool(mpsock_pool *pool, void *buf, size_t count, int flags)
{
	if(USE_ASSERTS) assert(pool->pos_buffer_read <= pool->pos_buffer_save);

	ssize_t bytes_to_read = pool_bytes_ready(pool);

	LOG_DEBUG("%sbytes_to_read=%zu, max allowed=%zu",RESULT_EVENT,bytes_to_read,count);

	// check if there is data to read
	if(bytes_to_read)
	{
		if(bytes_to_read > count)
		{
			LOG_DEBUG("%sToo many bytes -> reduce bytes to read to %zu",COND_EVENT,count);
			// too much data to read -> limit to count
			bytes_to_read = count;
		}

		// check if response header is delivered
		if(pool->pos_buffer_read < pool->current_response_size)
		{
			// header not yet delivered -> deliver it
			if(pool->current_response_size - pool->pos_buffer_read >= bytes_to_read)
			{
				LOG_DEBUG("%smove only header data, size=%zu",COND_EVENT,pool->current_response_size);
				move_header_to_application_buffer(pool,buf,0,bytes_to_read,flags);
			}
			else
			{
				LOG_DEBUG("%smove header and payload data",COND_EVENT);
				// partwise header and a bit of payload delivered
				size_t rest_header_bytes = pool->current_response_size - pool->pos_buffer_read;
				move_header_to_application_buffer(pool,buf,0,rest_header_bytes,flags);

				// move payload to buffer
				size_t rest_payload = bytes_to_read-rest_header_bytes;
				move_payload_to_application_buffer(pool,buf,rest_header_bytes,rest_payload,flags);
			}
		}
		else
		{
			// move payload to application
			LOG_DEBUG("%smove only payload data",COND_EVENT);
			move_payload_to_application_buffer(pool,buf,0,bytes_to_read,flags);
		}

		if(USE_ASSERTS) assert(pool->pos_buffer_read <= pool->current_file_size+pool->current_response_size);

		if(is_all_read(pool))
		{
			// everything is read -> reinit
			LOG_INFO("%sreinitialize pool",COND_EVENT);
			init_pool(pool);
		}

		return bytes_to_read;
	}
	else
	{
		// no data to read
		LOG_INFO("%sno data to read in pool - return 0",COND_EVENT);
		return 0;
	}
}

size_t get_fd_set(mpsock_pool *pool, fd_set *con_set)
{
	if(USE_ASSERTS) assert(HASH_COUNT(pool->connection_table) > 0);

	size_t cnt = 0;

	// set sub connections
	mpsock_connection *current_con;
	for(current_con=pool->connection_table; current_con != NULL; current_con = current_con->hh.next)
	{
		FD_SET(current_con->sd,con_set);
		cnt++;
	}

	return cnt;
}

size_t pool_bytes_ready(mpsock_pool *pool)
{
	return pool->pos_buffer_save - pool->pos_buffer_read;
}

void advance_pool_save_pointer(mpsock_pool *pool)
{
	// TODO: check if locking is really necessary here...
	/* LOCK START */
	lock_for_save(pool);

	LOG_DEBUG("%sadvance_save_pointer(), current=%zu",FUN_EVENT,pool->pos_buffer_save);
	if(USE_ASSERTS) assert(pool->pos_buffer_save <= pool->current_file_size + pool->current_response_size);

	if(is_all_stored(pool))
	{
		// already at EOF
		LOG_DEBUG("%spos_buffer_save already reached EOF",RESULT_EVENT);

		/* LOCK END */
		unlock_for_save(pool);
		return;
	}

	
	// TODO: better...
	mpsock_chunk *current_chunk = pool->current_chunk_save;
	if(USE_ASSERTS) assert(current_chunk != NULL);
	LOG_DEBUG("%sstart_byte=%zu, pos_save=%zu",RESULT_EVENT,current_chunk->start_byte,current_chunk->pos_save);

	// check if we already need to advance
	if(current_chunk->start_byte + current_chunk->pos_save < pool->pos_buffer_save - pool->current_response_size)
	{
		if(current_chunk->next != NULL /*&& !is_all_stored(pool)*/)
		{
			// advance to next chunk
			current_chunk = current_chunk->next;
			if(USE_ASSERTS) assert(pool->current_chunk_save->start_byte < current_chunk->start_byte);

			pool->current_chunk_save = pool->current_chunk_save->next;
			if(USE_ASSERTS) assert(current_chunk == pool->current_chunk_save);

			LOG_DEBUG("%snext chunk to save start_byte=%zu",COND_EVENT,pool->current_chunk_save->start_byte);
		}
	}

	while(current_chunk->start_byte + current_chunk->pos_save > pool->pos_buffer_save - pool->current_response_size)
	{
		if(USE_ASSERTS) assert(current_chunk != NULL);

		// advance the pointer
		pool->pos_buffer_save = pool->current_response_size + current_chunk->start_byte + current_chunk->pos_save;
		LOG_DEBUG("%snew pos_buffer_save=%zu",RESULT_EVENT,pool->pos_buffer_save);

		// check if this chunk is complete
		if(current_chunk->pos_save == current_chunk->data_size)
		{
			if(current_chunk->next != NULL /*&& !is_all_stored(pool)*/)
			{
				// advance to next chunk
				current_chunk = current_chunk->next;
				if(USE_ASSERTS) assert(pool->current_chunk_save->start_byte < current_chunk->start_byte);

				pool->current_chunk_save = pool->current_chunk_save->next;
				if(USE_ASSERTS) assert(current_chunk == pool->current_chunk_save);

				LOG_DEBUG("%snext chunk to save start_byte=%zu",COND_EVENT,pool->current_chunk_save->start_byte);
			}
			else
			{
				// no next chunk yet -> end
				break;
			}
		}
	}

	LOG_DEBUG("%sfinished advance_save_pointer until %zu",RESULT_EVENT,pool->pos_buffer_save - pool->current_response_size);

	/* LOCK END */
	unlock_for_save(pool);
}

mpsock_connection *create_sub_connection(mpsock_pool *pool, mpsock_connection *connection)
{
	LOG_INFO("%stry using old sub connection",FUN_EVENT);
	// TODO: maybe get_best_conn?
	//lock_for_threading(pool);
	mpsock_connection *new_connection = get_random_free_connection(pool);

	if(new_connection == NULL)
	{
		LOG_INFO("%screating new sub connection",FUN_EVENT);

		// 1. create a new connection
		int sock = f_socket(AF_INET, SOCK_STREAM, 0);
		new_connection = create_connection(pool,sock);

		// get interface
		mpsock_interface *intf = scheduler_get_interface(new_connection->scheduler);

		// configure socket interface
		if(setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, intf->name, strlen(intf->name)) < 0)
		{
			// TODO: rescue procedure?
			perror("setsockopt error");
			exit(1);
		}

		// 2. connect to new server
		struct sockaddr_in pin;
		memset(&pin, 0, sizeof(pin));
		pin.sin_family = PF_INET;
		//pin.sin_addr.s_addr = scheduler_get_ip(connection->scheduler, pool->mpsocket->hostname);
		pin.sin_addr.s_addr = scheduler_get_ip(new_connection->scheduler, pool->mpsocket->hostname);
		pin.sin_port = htons(pool->mpsocket->port);

		//unlock_for_threading(pool);

		// TODO: more parallel...
		if(f_connect(sock, (struct sockaddr *) &pin, sizeof(struct sockaddr_in)) == -1)
		{
			// TODO: verify
			LOG_ERROR("%ssd#%d broke",ERROR_EVENT,new_connection->sd);
			new_connection->pool->breaks_in_row++;
			handle_bad_performance(new_connection,TRUE);
			stop_connection(new_connection);
			free_connection(new_connection);
			pthread_exit(NULL);
   		    //perror("Connection could not be established");
   		    //exit(1);
   		}
	}
	//else
	//{
	//	unlock_for_threading(pool);
	//}

	return new_connection;
}

struct mpsock_connection *get_best_connection(mpsock_pool *pool)
{
	if(USE_ASSERTS) assert(HASH_COUNT(pool->connection_table) > 0);

	// iterate over all connections and find the one we need
	mpsock_connection *current_con;
	mpsock_connection *best_conn = NULL;
	for(current_con=pool->connection_table; current_con != NULL; current_con = current_con->hh.next)
	{
		if(best_conn == NULL && current_con->is_used == FALSE)
		{
			best_conn = current_con;
		}
		// TODO: is bandwidth the best metric here?
		// TODO: use id of interface to distinguish -> then bandwidth
		//else if(current_con->scheduler->bandwidth > best_conn->scheduler->bandwidth)
		else if(current_con->interface->if_id < best_conn->interface->if_id && current_con->is_used == FALSE)
		{
			best_conn = current_con;
		}
	}

	if(USE_ASSERTS) assert(best_conn != NULL);
	if(USE_ASSERTS) assert(best_conn->is_used == FALSE);

	return best_conn;
}
