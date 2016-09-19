#include <unistd.h>
#include "mpsock_socket.h"
#include "mpsock_collector.h"
#include "mpsock_connection.h"
#include "mpsock_pool.h"
#include "http_parser.h"

/*
 * string extraction helpers
 */
size_t extract_file_size(char *buf)
{
	char *tmp = strchr(buf, '/');
	if(tmp != NULL)
	{                                                                                                                                                                                                                                                                  
		tmp++; // eliminating '/'                                                                                                                                                                                                                                                       
	}
       
	return atoi(tmp);
}

int enough_resources_for_sub_connection(mpsock_connection *connection);

/*
 * create a connection and start it
 */
void* create_and_start_con(void *obj)
{
	http_parser *parser = (http_parser*)obj;

	lock_for_con(parser->connection->pool);
	
	if(USE_ASSERTS) assert(parser->connection->num_opening < parser->connection->pool->mpsocket->max_connections);

	if(enough_resources_for_sub_connection(parser->connection))
	{
		//lock_for_threading(parser->connection->pool);

		// TODO: lock for threading?
		// create a new connection...
		lock_for_threading(parser->connection->pool);
		mpsock_connection *new_conn = create_sub_connection(parser->connection->pool,parser->connection);
	
		// ...and start it
		start_connection(new_conn);
		unlock_for_threading(parser->connection->pool);
	}
	
	parser->connection->num_opening--;

	unlock_for_con(parser->connection->pool);

	pthread_exit(NULL);
	/* exit thread */
}

/* TODO: needed?
size_t extract_first_byte(char *buf)
{
	char *tmp = strtok(buf, "-");
	tmp = strchr(tmp, ' ');
	if(tmp != NULL)
	{
		tmp++; // trimming a left white space
	}

	return atoi(tmp);                                                                                                                                                                                                                                                                   
}
*/

/*
 * creates an error message for this request
 */
void create_error_response(mpsock_connection *connection)
{
	sprintf(connection->pool->current_response,"HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
	connection->pool->current_response_size = strlen(connection->pool->current_response);
	connection->pool->pos_buffer_save = connection->pool->current_response_size;
	connection->pool->current_file_size = 0;
	connection->pool->is_response_created = TRUE;
	LOG_ERROR("%sUnexpected connection shutdown -> skipped resource",ERROR_EVENT);
}

/*
 * determine whether we could start a sub connection
 */
int enough_resources_for_sub_connection(mpsock_connection *conn)
{
	return enough_res_for_conn(conn->scheduler);
	// TODO: better
	//if(HASH_COUNT(conn->pool->connection_table) < conn->pool->mpsocket->max_connections && conn->parser->status_code == 206)
	/*
	if(num_active_connections(conn->pool) < conn->pool->mpsocket->max_connections && conn->parser->status_code == 206)
	{
		if(conn->pool->current_file_size > conn->chunk_size)
		{
			return TRUE;
		}
		else
		{
			return FALSE;
		}
	}
	else
	{
		return FALSE;
	}
	*/
}

/*
 * send a normal request over given connection
 */
void send_normal_request(mpsock_connection *connection)
{
	LOG_WARN("%sreceived code %d --> resending with normal request",WARN_EVENT,connection->parser->status_code);

	// create request without Range attribute
	memcpy(connection->request_buffer,connection->pool->current_request,connection->pool->current_request_size);
	sprintf(connection->request_buffer+connection->pool->current_request_size,"\r\n");
	connection->request_len = strlen(connection->request_buffer);

	// start timer
	connection_send_event(connection);

	// send request
	int ret = f_write(connection->sd, connection->request_buffer, connection->request_len);

	LOG_DEBUG("%snormal request sent",COND_EVENT);

	if(USE_ASSERTS) assert(ret > 0);
}

/*
 * =========================== Parser Callbacks ======================
 */
int parser_status_complete_cb(http_parser *parser)
{
	mpsock_pool *pool = parser->connection->pool;

	// check if we got a response status and if response header is not yet created
	if(parser->type == HTTP_RESPONSE && !pool->is_response_created)
	{
		// initial flag
		parser->connection->is_corrupted = FALSE;

		// create the first line of the response header
		// TODO: get full first line -> 200 OK
		char line[25];
		int status = parser->status_code;

		// we do not want to let the application see 206
		if(status == 206)
		{
			LOG_DEBUG("%sresponse code 206",COND_EVENT);
			status = 200;
		}
		else if(status == 416)
		{
			if(parser->connection->pool->mpsocket->use_initial_second_path)
			{
				// TODO: delete this chunk because server cannot satisfy request
				parser->connection->out_of_bounds_request = TRUE;
			}
			else
			{
				// mark connection as corrupted
				parser->connection->is_corrupted = TRUE;
			}
		}

		if(!parser->connection->out_of_bounds_request)
		{
			// create first line
			sprintf(line,"HTTP/%d.%d %d\r\n",parser->http_major,parser->http_minor,status);
			size_t len = strlen(line);
			memcpy(pool->current_response,line,len);
			pool->current_response_size = len;
			pool->pos_buffer_save = len;
			LOG_DEBUG("%sset response status=%d",RESULT_EVENT,status);
		}

		pool->status_code = status;
	}

	if(pool->is_response_created && pool->mpsocket->use_initial_second_path && parser->status_code == 416)
	{
		parser->connection->out_of_bounds_request = TRUE;
	}

	return 0;
}

int parser_header_field_cb(http_parser *parser, const char *buf, size_t len)
{
	mpsock_pool *pool = parser->connection->pool;

	if(parser->type == HTTP_REQUEST && !pool->is_response_created && !parser->connection->out_of_bounds_request)
	{
		char field[len];
		field[len] = '\0';
		strncpy(field,buf,len);
		// extract host from request
		if(strcmp(field,"Host") == 0)
		{
			parser->ht_field = HT_FIELD_HOST;
		}
		else
		{
			parser->ht_field = HT_FIELD_SKIP;
		}
	}
	// check if we got a response status and if response header is not yet created
	else if(parser->type == HTTP_RESPONSE && !pool->is_response_created && !parser->connection->out_of_bounds_request)
	{
		
		// TODO: more efficient -> buffer is char[]
		// bring into correct format "xxx: "
		size_t size = len+2;
		char field[size];
		field[size-2] = ':';
		field[size-1] = ' ';
		field[size] = '\0';
		strncpy(field,buf,len);

		LOG_DEBUG("%sset field: %s",RESULT_EVENT,field);

		
		if(parser->status_code == 206 && (strcmp(field,"Content-Range: ") == 0))
		{
			// tell parser to get file length from Content-Range field
			parser->connection->pool->has_length_set = TRUE;
			parser->ht_field = HT_FIELD_RANG;
		}
		else if(parser->status_code == 200 && (strcmp(field,"Content-Length: ") == 0))
		{
			// tell parser to get file length from Content-Length field
			parser->connection->pool->has_length_set = TRUE;
			parser->ht_field = HT_FIELD_LENG;
		}
		else if(parser->status_code == 206 && (strcmp(field,"Content-Length: ") == 0))
		{
			// we don't want conent length parsed in a partial request
			parser->connection->pool->has_length_set = TRUE;
			parser->ht_field = HT_FIELD_SKIP;
		}
		else if(parser->status_code != 206 && parser->status_code != 200 &&(strcmp(field,"Content-Length: ") == 0))
		{
			// some other response code -> take file size from Content-Length
			parser->connection->pool->has_length_set = TRUE;
			parser->ht_field = HT_FIELD_ELEN;
		}
		else if(strcmp(field,"Transfer-Encoding: ") == 0)
		{
			// Transfer Encoding set
			parser->ht_field = HT_FIELD_CODE;
		}
		else
		{
			// something else -> save this value
			parser->ht_field = HT_FIELD_GRAB;
		}

		// check if we want to copy this field into our header
		if(parser->ht_field == HT_FIELD_GRAB)
		{
			// copy field to header
			memcpy(pool->current_response+pool->current_response_size, field, size);

			// update sizes
			pool->current_response_size += size;
			pool->pos_buffer_save += size;
		}
	}

	return 0;
}

int parser_header_value_cb(http_parser *parser, const char *buf, size_t len)
{
	mpsock_pool *pool = parser->connection->pool;

	if(parser->type == HTTP_REQUEST && !pool->is_response_created && !parser->connection->out_of_bounds_request)
	{
		// extract host from request
		if(parser->ht_field == HT_FIELD_HOST)
		{
			// set host for later sub connection requests
			set_hostname(parser->connection->pool->mpsocket,buf,len);
		}
	}
	// check if we got a response status and if response header is not yet created
	else if(parser->type == HTTP_RESPONSE && !pool->is_response_created && !parser->connection->out_of_bounds_request)
	{
		// TODO: more efficient -> buffer is char[]
		// format value
		size_t size = len+2;
		char value[size];
		value[size-2] = '\r';
		value[size-1] = '\n';
		value[size] = '\0';
		strncpy(value,buf,len);

		LOG_DEBUG("%sset value: %s",RESULT_EVENT,value);

		// check if we want to store this value
		if(parser->ht_field == HT_FIELD_GRAB)
		{
			// store in application response header
			memcpy(pool->current_response+pool->current_response_size,value,size);

			// update sizes
			pool->current_response_size += size;
			pool->pos_buffer_save += size;
		}

		// check if we want to extract the file size from Range response
		if(parser->ht_field == HT_FIELD_RANG)
		{
			value[len] = '\0';
			// set file size
			pool->current_file_size = extract_file_size(value);
		}

		// check if we want to extract from Content-Length response
		if(parser->ht_field == HT_FIELD_LENG || parser->ht_field == HT_FIELD_ELEN)
		{
			// set file size
			pool->current_file_size = atoi(buf);

			if(pool->current_file_size > parser->connection->current_chunk->buffer_size)
			{
				if(USE_ASSERTS) assert(parser->status_code == 200);

				// more data coming than requested -> mark corrupted
				parser->connection->is_normal_get = TRUE;
				LOG_WARN("%sGET responds with too big chunk -> error handling starts",WARN_EVENT);
			}
		}

		// check if transfer encoding is set
		if(parser->ht_field == HT_FIELD_CODE)
		{
			if(strncmp(value,"chunked",len) == 0)
			{
				// Transfer-Encoding: chunked
				parser->connection->is_chunked_encoding = TRUE;
				LOG_INFO("%sTransfer-Encoding: chunked detected -> using normal http",WARN_EVENT);
				// unsupported - set corrupted
				//parser->connection->is_normal_get = TRUE;
				//LOG_FATAL("%sTransfer-Encoding: chunked --> not yet supported!",FATAL_EVENT);
				//exit(1);
			}
		}
	}

	return 0;
}

int parser_headers_complete_cb(http_parser *parser)
{
	mpsock_pool *pool = parser->connection->pool;

	// check if we got a response status
	if(parser->type == HTTP_RESPONSE)
	{
		// check if respone header is already created
		if(!pool->is_response_created && !parser->connection->is_corrupted && !parser->connection->out_of_bounds_request && !parser->connection->is_chunked_encoding)
		{
			// set Content-Length in response
			char line[100];
			sprintf(line,"Content-Length: %zu\r\n\r\n",pool->current_file_size);
			size_t len = strlen(line);

			// TODO: verify if len+1 makes any trouble
			// len+1 in order to also copy termination '\0'
			memcpy(pool->current_response+pool->current_response_size,line,len+1);

			// update sizes
			pool->current_response_size += len;
			pool->pos_buffer_save += len;

			// adjust the first chunk's data size if necessary
			if(pool->current_file_size < parser->connection->chunk_size)
			{
				// adjust chunk size
				parser->connection->current_chunk->data_size = pool->current_file_size;
				parser->connection->chunk_size = pool->current_file_size;
			}

			// mark response header for application as created
			pool->is_response_created = TRUE;
			LOG_DEBUG("%screated application response header:\n=============================================\n%s",RESULT_EVENT,pool->current_response);

			if(USE_ASSERTS) assert(pool->pos_buffer_save == pool->current_response_size);

			LOG_DEBUG("%scurrent_file_size=%zu",RESULT_EVENT,pool->current_file_size);
		}
		
		if(!parser->connection->is_corrupted && !parser->connection->out_of_bounds_request && !parser->connection->is_normal_get && !parser->connection->is_chunked_encoding)
		{
			// since this is a response, the following will be payload to store in our pool
			parser->is_payload = TRUE;
	
			LOG_DEBUG("%shostname: %s",RESULT_EVENT,parser->connection->pool->mpsocket->hostname);
	
			// XXX: dirty hack....
			// check if we could make another sub connection
			//lock_for_threading(parser->connection->pool);
			//while(enough_resources_for_sub_connection(parser->connection))
			while(enough_resources_for_sub_connection(parser->connection) && parser->connection->num_opening < parser->connection->pool->mpsocket->max_connections-1)
			{
				// TODO: PARALLELIZE!!!!
				// create a new connection...
				//mpsock_connection *new_conn = create_sub_connection(parser->connection->pool,parser->connection);
	
				// ...and start it
				//if(!start_connection(new_conn));
				//{
					// TODO: verify if correct?!?!?
					// connection did not start -> most likely everything already requested
				//	unlock_for_threading(parser->connection->pool);
				//	break;
				//}

				// TODO: better parallelization
				parser->connection->num_opening++;
				int err;
				if((err = pthread_create(&(parser->connection->new_con_pid),NULL,&create_and_start_con,(void*)parser)) != 0)
				{
					perror("pthread_create() error");
					exit(0);
				}
			}

			//unlock_for_threading(parser->connection->pool);
		}
		else if(!parser->connection->is_normal_get && !parser->connection->is_chunked_encoding && !parser->connection->out_of_bounds_request)
		{
			// error handling
			parser->is_payload = FALSE;

			// ensure that this is the first request that failed
			if(USE_ASSERTS) assert(parser->connection->current_chunk->start_byte == 0);

			if(parser->connection->pool->current_response_size == 0)
			{
				// no body with response -> send normal request
				send_normal_request(parser->connection);
			}
		}
		else if(parser->connection->out_of_bounds_request)
		{
			parser->is_payload = FALSE;
		}
	}

	return 0;
}

int parser_body_cb(http_parser *parser, const char *buf, size_t len)
{
	// ========================== 206 ================================
	// check if we got payload to store
	if(parser->is_payload)
	{
		mpsock_chunk *chunk = parser->connection->current_chunk;
		
		if(parser->connection->pool->has_length_set)
		{
			if(USE_ASSERTS) assert(chunk->data_size <= parser->connection->pool->current_file_size);
			if(USE_ASSERTS) assert(chunk->data_size - chunk->pos_save >= len);

			// store data into chunk
			if(USE_RINGBUFFER)
			{
				store_in_buffer(parser->connection->pool->data_buffer,chunk->buffer,buf,chunk->pos_save,len);
			}
			else
			{
				memcpy(chunk->buffer+chunk->pos_save,buf,len);
			}
	
			// adjust pointers
			chunk->pos_save += len;
			LOG_DEBUG("%schunk start_byte=%zu stored %zu / %zu",RESULT_EVENT,chunk->start_byte,chunk->pos_save,chunk->data_size);
		}
		else
		{
			LOG_WARN("%sNo Content Length set when was actually expected -> discard parsed body data (data most likely unnecessary)",WARN_EVENT);
		}
	}
	// ========================================================================

	// error handling
	if(parser->type == HTTP_RESPONSE && !parser->is_payload)
	{
		// corrupted case
		if(parser->connection->is_corrupted)
		{
			// count how many bytes we already got
			parser->connection->corrupted_payload_read += len;
			
			// if we got the length -> check it
			if(parser->connection->corrupted_payload_read == parser->connection->pool->current_file_size)
			{
				// total response parsed -> send normal request now
				send_normal_request(parser->connection);
			}
		}

		// =========================== normal GET 200 ================================
		if(parser->connection->is_normal_get)
		{
			LOG_DEBUG("%snormal get!!!",COND_EVENT);
			// store in chunk until chunk is filled up, then allocate next one
			// TODO: implement
			mpsock_chunk *cur_chunk = parser->connection->current_chunk;
			size_t bytes_to_store = len;

			size_t f_bytes_to_store = 0;
			size_t total_bytes_stored = 0;

			while(cur_chunk->data_size - cur_chunk->pos_save < bytes_to_store)
			{
				// store whatever fits
				f_bytes_to_store = cur_chunk->data_size - cur_chunk->pos_save;

				if(USE_RINGBUFFER)
				{
					store_in_buffer(parser->connection->pool->data_buffer,cur_chunk->buffer,buf+total_bytes_stored,cur_chunk->pos_save,f_bytes_to_store);
				}
				else
				{
					memcpy(cur_chunk->buffer+cur_chunk->pos_save,buf+total_bytes_stored,f_bytes_to_store);
				}

				cur_chunk->pos_save += f_bytes_to_store;
				total_bytes_stored += f_bytes_to_store;

				if(USE_ASSERTS) assert(cur_chunk->pos_save == cur_chunk->data_size);

				// allocate a new chunk
				calculate_chunk_size(parser->connection);
				parser->connection->current_chunk = next_chunk_for_connection(parser->connection);

				cur_chunk = parser->connection->current_chunk;

				bytes_to_store = len - total_bytes_stored;
			}

			if(USE_ASSERTS) assert(len == bytes_to_store+total_bytes_stored);

			// store data in buffer
			if(USE_RINGBUFFER)
			{
				store_in_buffer(parser->connection->pool->data_buffer,cur_chunk->buffer,buf+total_bytes_stored,cur_chunk->pos_save,bytes_to_store);
			}
			else
			{
				memcpy(cur_chunk->buffer+cur_chunk->pos_save,buf+total_bytes_stored,bytes_to_store);
			}

			cur_chunk->pos_save += bytes_to_store;

			if(USE_ASSERTS) assert(cur_chunk->pos_save <= cur_chunk->data_size);

			// advance pointer for total payload of pool
			advance_pool_save_pointer(parser->connection->pool);
			//parser->connection->pool->pos_buffer_save += len;
		}
		// ===================================================================================

		// ========================= Transfer-Encoding: chunked ==============================
		if(parser->connection->is_chunked_encoding && !parser->connection->out_of_bounds_request)
		{
			if(parser->status_code != 200)
			{
				// we dont handle weird chunked stuff yet
				mpsock_pool *pool = parser->connection->pool;
				pool->current_file_size = 0;
				char line[100];
				sprintf(line,"Content-Length: %zu\r\n\r\n",pool->current_file_size);
				size_t len = strlen(line);

				// TODO: verify if len+1 makes any trouble
				// len+1 in order to also copy termination '\0'
				memcpy(pool->current_response+pool->current_response_size,line,len+1);

				// update sizes
				pool->current_response_size += len;
				pool->pos_buffer_save += len;

				// mark response header for application as created
				pool->is_response_created = TRUE;
				LOG_DEBUG("%screated application response header:\n=============================================\n%s",RESULT_EVENT,pool->current_response);

				if(USE_ASSERTS) assert(pool->pos_buffer_save == pool->current_response_size);

				// advance pointer for total payload of pool
				advance_pool_save_pointer(parser->connection->pool);

				if(USE_ASSERTS) assert(pool->pos_buffer_save == pool->current_file_size+pool->current_response_size);

				return 0;
			}

			parser->is_first_read_chunk_size = FALSE;
			parser->zero_in_a_row = 0;
			LOG_DEBUG("%schunk_size = %zu",COND_EVENT,parser->cur_chunk_length);
			
			if(parser->connection->pool->is_response_created) return 0;

			if(parser->cur_chunk_length == 0)
			{
				LOG_DEBUG("%sEOF",RESULT_EVENT);
				// we reached EOF
				// set Content-Length in response
				mpsock_pool *pool = parser->connection->pool;
				char line[100];
				sprintf(line,"Content-Length: %zu\r\n\r\n",pool->current_file_size);
				size_t len = strlen(line);

				// TODO: verify if len+1 makes any trouble
				// len+1 in order to also copy termination '\0'
				memcpy(pool->current_response+pool->current_response_size,line,len+1);

				// update sizes
				pool->current_response_size += len;
				pool->pos_buffer_save += len;

				// mark response header for application as created
				pool->is_response_created = TRUE;
				LOG_DEBUG("%screated application response header:\n=============================================\n%s",RESULT_EVENT,pool->current_response);

				if(USE_ASSERTS) assert(pool->pos_buffer_save == pool->current_response_size);

				// advance pointer for total payload of pool
				advance_pool_save_pointer(parser->connection->pool);

				if(USE_ASSERTS) assert(pool->pos_buffer_save == pool->current_file_size+pool->current_response_size);

				return 0;
			}

			if(parser->connection->pool->current_file_size == 0)
			{
				parser->connection->pool->current_file_size = parser->cur_chunk_length;
			}

			LOG_DEBUG("%scur_chunk = %zu / %zu , needed = %zu, len to save now = %zu",COND_EVENT,parser->connection->current_chunk->pos_save,parser->connection->current_chunk->data_size,parser->cur_chunk_length,len);
			if(parser->connection->current_chunk->data_size != parser->cur_chunk_length && parser->connection->current_chunk->pos_save == 0 && len > 0)
			{
				// renew chunk
				//free_chunk_in_buffer(parser->connection->pool->data_buffer,parser->connection->current_chunk);
				mpsock_pool *pool = parser->connection->pool;
				free_chunk(pool,parser->connection->current_chunk);
				pool->current_chunk_request = NULL;
				pool->current_request = NULL;
				pool->current_chunk_read = NULL;
				pool->current_chunk_save = NULL;
				pool->next_start_byte = 0;

				parser->connection->chunk_size = parser->cur_chunk_length;
				parser->connection->current_chunk = next_chunk_for_connection(parser->connection);
			}

			mpsock_chunk *cur_chunk = parser->connection->current_chunk;

			if(cur_chunk->pos_save == cur_chunk->data_size && len > 0)
			{
				if(parser->cur_chunk_length == 0)
				{
					// we reached EOF
					// set Content-Length in response
					mpsock_pool *pool = parser->connection->pool;
					char line[100];
					sprintf(line,"Content-Length: %zu\r\n\r\n",pool->current_file_size);
					size_t len = strlen(line);

					// TODO: verify if len+1 makes any trouble
					// len+1 in order to also copy termination '\0'
					memcpy(pool->current_response+pool->current_response_size,line,len+1);

					// update sizes
					pool->current_response_size += len;
					pool->pos_buffer_save += len;

					// mark response header for application as created
					pool->is_response_created = TRUE;
					LOG_DEBUG("%screated application response header:\n=============================================\n%s",RESULT_EVENT,pool->current_response);

					if(USE_ASSERTS) assert(pool->pos_buffer_save == pool->current_response_size);

					// advance pointer for total payload of pool
					advance_pool_save_pointer(parser->connection->pool);

					if(USE_ASSERTS) assert(pool->pos_buffer_save == pool->current_file_size+pool->current_response_size);

					return 0;
				}
				else
				{
					// update file size
					parser->connection->pool->current_file_size += parser->cur_chunk_length;

					// get a new chunk
					parser->connection->chunk_size = parser->cur_chunk_length;
					parser->connection->current_chunk = next_chunk_for_connection(parser->connection);
					cur_chunk = parser->connection->current_chunk;
					LOG_DEBUG("%scurrent_chunk - start_byte = %zu, size = %zu, parser-chunk = %zu",COND_EVENT,cur_chunk->start_byte,cur_chunk->data_size,parser->cur_chunk_length);
				}
			}

			if(len > 0)
			{
				if(USE_RINGBUFFER)
				{
					store_in_buffer(parser->connection->pool->data_buffer,cur_chunk->buffer,buf,cur_chunk->pos_save,len);
				}
				else
				{
					memcpy(cur_chunk->buffer+cur_chunk->pos_save,buf,len);
				}

				cur_chunk->pos_save += len;
			}

			if(USE_ASSERTS) assert(cur_chunk->pos_save <= cur_chunk->data_size);
		}
		// ===================================================================================
	}

	return 0;
}

int parser_url_cb(http_parser *parser, const char *buf, size_t len)
{
	return 0;
}

int parser_message_begin_cb(http_parser *parser)
{
	return 0;
}

int parser_message_complete_cb(http_parser *parser)
{
	// ============= chunked encoding ============
	if(parser->connection->is_chunked_encoding && !parser->connection->out_of_bounds_request)
	{
		if(!parser->connection->pool->is_response_created)
		{
			LOG_DEBUG("%sEOF",RESULT_EVENT);
			// we reached EOF
			// set Content-Length in response
			mpsock_pool *pool = parser->connection->pool;
			char line[100];
			sprintf(line,"Content-Length: %zu\r\n\r\n",pool->current_file_size);
			size_t len = strlen(line);

			// TODO: verify if len+1 makes any trouble
			// len+1 in order to also copy termination '\0'
			memcpy(pool->current_response+pool->current_response_size,line,len+1);

			// update sizes
			pool->current_response_size += len;
			pool->pos_buffer_save += len;

			// mark response header for application as created
			pool->is_response_created = TRUE;
			LOG_DEBUG("%screated application response header:\n=============================================\n%s",RESULT_EVENT,pool->current_response);

			if(USE_ASSERTS) assert(pool->pos_buffer_save == pool->current_response_size);
	
			// advance pointer for total payload of pool
			LOG_DEBUG("%ssave_pointer = %zu",COND_EVENT,pool->pos_buffer_save);
			advance_pool_save_pointer(parser->connection->pool);
			LOG_DEBUG("%ssave_pointer = %zu / %zu",COND_EVENT,pool->pos_buffer_save,pool->current_file_size+pool->current_response_size);

			if(USE_ASSERTS) assert(pool->pos_buffer_save == pool->current_file_size+pool->current_response_size);
		}
	}
	// ==========================================

	// TODO: verify
	http_parser_soft_init(parser,HTTP_BOTH);
	return 0;
}

/*
 * ====================================================================
 */

/*
 * callback settings for the parser
 */
static http_parser_settings parser_callbacks =
{
	.on_message_begin = parser_message_begin_cb
	,.on_header_field = parser_header_field_cb
	,.on_header_value = parser_header_value_cb
	,.on_status_complete = parser_status_complete_cb
	,.on_url = parser_url_cb
	,.on_body = parser_body_cb
	,.on_headers_complete = parser_headers_complete_cb
	,.on_message_complete = parser_message_complete_cb
};

/*
 * generates a request for this connection
 */
size_t generate_request(mpsock_connection *connection, size_t from, size_t to)
{
	// TODO: more efficient
	LOG_DEBUG("%sgenerate_request from %zu to %zu",FUN_EVENT,from,to);
	memcpy(connection->request_buffer,connection->pool->current_request,connection->pool->current_request_size);
	sprintf(connection->request_buffer+connection->pool->current_request_size,"Range: bytes=%zu-%zu\r\n\r\n",from,to);
	connection->request_len = strlen(connection->request_buffer);
	return connection->request_len;
}

/*
 * send request with specified range
 */
int send_request(mpsock_connection *connection)
{
	if(USE_ASSERTS) assert(connection->current_chunk->start_byte+connection->current_chunk->pos_save < connection->current_chunk->start_byte+connection->current_chunk->data_size-1);

	// prepare request
	size_t len = generate_request(connection,connection->current_chunk->start_byte+connection->current_chunk->pos_save,connection->current_chunk->start_byte+connection->current_chunk->data_size-1);

	if(!connection->pool->is_response_created)
	{
		// first request -> parse header for Host: part
		http_parser_execute(connection->parser,&parser_callbacks,connection->request_buffer,len);
	}

	LOG_INFO("%ssd#%d request bytes %zu - %zu",RESULT_EVENT,connection->sd,connection->current_chunk->start_byte+connection->current_chunk->pos_save,connection->current_chunk->start_byte+connection->current_chunk->data_size-1);

	if(connection->pool->mpsocket->log_decisions)
	{
		struct timeval tmp_ts;
		gettimeofday(&tmp_ts,NULL);
		double tmp_diff = tmp_ts.tv_sec - global_start_ts.tv_sec;
		tmp_diff += (double)(tmp_ts.tv_usec - global_start_ts.tv_usec)/1000000;
		//LOG_SYNC("%d %f %zu\n",connection->sd,tmp_diff,(connection->current_chunk->data_size - connection->current_chunk->pos_save)/1024);
		printf("Decision sd%d %f %zu %zu %f\n",connection->sd,tmp_diff,(connection->current_chunk->data_size - connection->current_chunk->pos_save)/1024, connection->scheduler->bandwidth/1024, connection->scheduler->rtt);
	}

	// start time counter
	connection_send_event(connection);

	// send request
	int ret = f_write(connection->sd, connection->request_buffer, len);

	if(USE_ASSERTS) assert(ret > 0);
	return ret;
}

int make_next_request(mpsock_connection *connection)
{
	LOG_DEBUG("%smake_next_request",FUN_EVENT);

	if(connection->pool->mpsocket->scheduler_algo == 0)
	{
		// baseline scheduling
		if(connection->scheduler->bandwidth > 0)
		{
			mpsock_connection *con_iter;
			
			size_t best_bw = 0;

			for(con_iter = connection->pool->connection_table; con_iter != NULL; con_iter = con_iter->hh.next)
			{
				if(con_iter->scheduler->bandwidth > best_bw)
				{
					best_bw = con_iter->scheduler->bandwidth;
				}
			}

			if(connection->scheduler->bandwidth < best_bw)
			{
				int chunk_skips = ((int) (best_bw / connection->scheduler->bandwidth)) - 1;
				if(chunk_skips*connection->pool->mpsocket->initial_chunk_size + connection->pool->next_start_byte >= connection->pool->current_file_size)
				{
					// stop this connection and finish with best
					connection->pool->block_new_conns = TRUE;
					return NO_MORE_REQ_FLAG;
				}
			}
		}
	}

	// TODO: if concurrent writes -> check if reinit or not!
	// check if there is really a request to make
	//if(!connection->pool->is_response_created && connection != connection->pool->main_connection)
	//{
	//	return -1;
	//}
	//if(!is_first_connection_and_first_request(connection) && connection->pool->next_start_byte == 0)
	//{
	//	// concurrent reason
	//	return -1;
	//}

	// calculate chunk size
	// TODO: fix chunk size calculation
	if(connection->pool->next_start_byte < connection->pool->current_file_size || !connection->pool->is_response_created/*connection->pool->next_start_byte == 0*/)
	{
		calculate_chunk_size(connection);
	}

	// get the next chunk (if necessary)
	if(connection->current_chunk == NULL)
	{
		// first chunk for connection
		connection->current_chunk = next_chunk_for_connection(connection);
	}
	else if(connection->current_chunk->pos_save == connection->current_chunk->data_size)
	{
		// chunk is full -> next chunk
		connection->current_chunk = next_chunk_for_connection(connection);
	}
	/*
	else
	{
		if(USE_ASSERTS) assert(connection->current_chunk->pos_save < connection->current_chunk->data_size);
	}
	*/

	// check if there is a chunk to send
	if(connection->current_chunk != NULL)
	{
		// send request
		return send_request(connection);
	}
	else
	{
		// no more requests to send
		LOG_INFO("%ssd#%d no more requests",COND_EVENT,connection->sd);
		return NO_MORE_REQ_FLAG;
	}
}

void handle_bad_performance(mpsock_connection *connection, int bad_perf)
{
	if(USE_ASSERTS) assert(connection->interface != NULL);

	LOG_INFO("%ssd#%d handle bad performance",FUN_EVENT,connection->sd);

	if(USE_ASSERTS) assert(connection->scheduler->bad_connection == FALSE);

	// mark connection as bad
	connection->scheduler->bad_connection = bad_perf;

	// connection will shutdown -> mark interface as free
	//connection->interface->in_use = FALSE;

	if(connection->current_chunk == NULL)
	{
		if(partial_req_over_new_con(connection->scheduler) && connection->pool->breaks_in_row < MAX_BREAKS_IN_ROW)
		{
			// create new connection for requests
			lock_for_threading(connection->pool);
			mpsock_connection *conn = create_sub_connection(connection->pool,connection);
			start_connection(conn);
			unlock_for_threading(connection->pool);
		}

		return;
	}

	lock_for_next_chunk(connection->pool);

	if(USE_ASSERTS) assert(connection->current_chunk->pos_save <= connection->current_chunk->data_size);

	connection->waiting_bytes = connection->current_chunk->data_size - connection->current_chunk->pos_save;

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

int start_connection(mpsock_connection *connection)
{
	//lock_for_threading(connection->pool);
	if(USE_ASSERTS) assert(connection->is_used == FALSE);

	// mark connection used
	connection->is_used = TRUE;

	//unlock_for_threading(connection->pool);

	// send the request
	int ret = make_next_request(connection);

	// check if there was a request made
	if(ret == NO_MORE_REQ_FLAG)
	{
		// no request made -> already everything requested
		connection->is_used = FALSE;
		return FALSE;
	}
	else if(ret < 0)
	{
		LOG_ERROR("%ssd#%d broke",ERROR_EVENT,connection->sd);
		if(USE_ASSERTS) assert(connection->current_chunk != NULL);
		connection->pool->breaks_in_row++;
		handle_bad_performance(connection,TRUE);
		stop_connection(connection);
		free_connection(connection);
		return FALSE;
	}

	// TODO: verify
	// create premature second path
	if(connection->pool->mpsocket->use_initial_second_path)
	{
		if(!connection->pool->is_response_created && num_active_connections(connection->pool) < 2)
		{
			// first request -> open premature second path
			mpsock_connection *second_path = create_sub_connection(connection->pool,connection);
			start_connection(second_path);
		}
	}

	// start collector thread for this connection
	int err;
	if((err = pthread_create(&(connection->pid),NULL,&collector_thread,(void*)connection)) != 0)
	{
		perror("pthread_create() error");
		exit(0);
	}

	return TRUE;
}

void stop_connection(mpsock_connection *connection)
{
	if(DO_THREAD_ADVANCE_POINTER)
	{
		advance_pool_save_pointer(connection->pool);
	}

	lock_for_next_chunk(connection->pool);

	if(!connection->waiting)
	{
		connection->is_used = FALSE;
	}

	if(connection->out_of_bounds_request)
	{
		free_chunk(connection->pool, connection->current_chunk);
	}

	struct timeval final_ts;
	gettimeofday(&final_ts,NULL);
	double tmp_diff = final_ts.tv_sec - global_start_ts.tv_sec;
	tmp_diff += (double)(final_ts.tv_usec - global_start_ts.tv_usec)/1000000;

	LOG_INFO("%ssd#%d at %fs finished collector thread",FUN_EVENT,connection->sd,tmp_diff);

	if(connection->pool->mpsocket->log_traffic)
	{
		printf("Traffic sd#%d %zu\n",connection->sd,connection->bytes_received);
	}

	if(connection->pool->mpsocket->log_decisions)
	{
		//LOG_SYNC("%d %f -1\n",connection->sd,tmp_diff);
		printf("Decision sd%d %f END %zu %f\n",connection->sd,tmp_diff,connection->scheduler->bandwidth/1024, connection->scheduler->rtt);
	}

	if(connection->pool->pos_buffer_save == connection->pool->current_file_size+connection->pool->current_response_size && connection->pool->is_response_created)
	{
		if(connection->pool->req_start.tv_sec > 0)
		{
			struct timeval tmp_ts;
			gettimeofday(&tmp_ts,NULL);
			double tmp_diff = tmp_ts.tv_sec - connection->pool->req_start.tv_sec;
			tmp_diff += (double)(tmp_ts.tv_usec - connection->pool->req_start.tv_usec)/1000000;
			//LOG_INFO("%sFinished Object Download (%zu kB) after %f s",RESULT_EVENT,(size_t)(connection->pool->current_file_size/1024),tmp_diff);
			if(PRINT_DEFAULT_OUT)
			{
				printf("Finished Object Download ( %zu KB ) after %f s\n",(size_t)(connection->pool->current_file_size)/1024,tmp_diff);
			}

			if(connection->pool->mpsocket->log_metrics || connection->pool->mpsocket->log_traffic || connection->pool->mpsocket->log_decisions)
			{
				//LOG_SYNC("\n");
				printf("\n");
			}

			connection->pool->req_start.tv_sec = 0;
		}
	}
	
	int num_active = num_active_connections(connection->pool);
	int num_waiting = num_waiting_connections(connection->pool);

	if((num_active == 0 || num_waiting == num_active) && HASH_COUNT(connection->pool->partial_chunk_table) > 0)
	{
		if(connection->pool->breaks_in_row >= MAX_BREAKS_IN_ROW)
		{
			if(connection->pool->pos_buffer_save == 0)
			{
				// TODO: is this needed? or simply set_broken
				// create an error response for the application
				create_error_response(connection);
			}
			else
			{
				// TODO: maybe wait a few msec before finally giving up?
				// set pool broken
				connection->pool->is_broken = TRUE;
			}
		}
		else
		{
			unlock_for_next_chunk(connection->pool);
			// there are no active connections that could take care of the chunk -> create one
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

void wait_for_reuse(mpsock_connection *connection)
{
	if(USE_ASSERTS) assert(connection->waiting == TRUE);
	if(USE_ASSERTS) assert(connection->waiting_bytes > 0);
	if(USE_ASSERTS) assert(connection->is_used == TRUE);

	// TODO: verify
	fd_set sock_set;
	sigset_t sigs;
	sigfillset(&sigs);
	struct timespec tv;
	tv.tv_sec = 0;
	tv.tv_nsec = WAIT_TIMEOUT_SMALL;

	size_t read_bytes = 0;

	while(connection->waiting_bytes - read_bytes > 0)
	{
		// set socket to monitor
		FD_ZERO(&sock_set);
		FD_SET(connection->sd,&sock_set);

		if(connection->waiting == FALSE)
		{
			pthread_exit(NULL);
		}

		// wait for socket to receive data
		int ret = f_pselect(connection->sd+1,&sock_set,NULL,NULL,&tv,&sigs);

		if(connection->waiting == FALSE)
		{
			pthread_exit(NULL);
		}

		if(ret < 0)
		{
			if(connection->waiting == FALSE)
			{
				pthread_exit(NULL);
			}
			LOG_ERROR("%ssd#%d broke",ERROR_EVENT,connection->sd);
			connection->waiting = FALSE;
			stop_connection(connection);
			free_connection(connection);
			pthread_exit(NULL);
			/* EXIT */
		}

		int cnt = 0;
		if(FD_ISSET(connection->sd,&sock_set))
		{
			cnt = f_read(connection->sd,connection->volatile_buffer,VOLATILE_BUFFER_SIZE);

			if(cnt <= 0)
			{
				if(connection->waiting == FALSE)
				{
					pthread_exit(NULL);
				}
				LOG_ERROR("%ssd#%d broke",ERROR_EVENT,connection->sd);
				connection->waiting = FALSE;
				stop_connection(connection);
				free_connection(connection);
				pthread_exit(NULL);
				/* EXIT */
			}

			read_bytes += cnt;
		}

		connection_read_event(connection,cnt);

		int perf_flag = performs_bad(connection->scheduler);
		if(perf_flag == BAD_PERF_CLOSE)
		{
			if(connection->waiting == FALSE)
			{
				pthread_exit(NULL);
			}
			connection->waiting = FALSE;
			stop_connection(connection);
			free_connection(connection);
			pthread_exit(NULL);
			/* EXIT */
		}
	}
	
	if(USE_ASSERTS) assert(connection->waiting_bytes - read_bytes == 0);

	LOG_INFO("%ssd#%d finished waiting for old data",RESULT_EVENT,connection->sd);

	connection->is_used = FALSE;
	connection->waiting = FALSE;
}

void* collector_thread(void *obj)
{
	mpsock_connection *connection = (mpsock_connection*)obj;
	LOG_INFO("%ssd#%d starting collector thread on interface %s",FUN_EVENT,connection->sd,connection->interface->name);

	// send the request
	//int ret = make_next_request(connection);

	// check if there was a request made
	//if(ret < 0)
	//{
	//	// no request made -> already everything requested
	//	connection->is_used = FALSE;
	//}

	// pselect relevant structures
	fd_set sock_set;
	sigset_t sigs;
	sigfillset(&sigs);
	struct timespec tv;
	tv.tv_sec = 0;
	tv.tv_nsec = WAIT_TIMEOUT;

	while(connection->is_used)
	{
		// set socket to monitor
		FD_ZERO(&sock_set);
		FD_SET(connection->sd,&sock_set);

		// wait for socket to receive data
		int ret = f_pselect(connection->sd+1,&sock_set,NULL,NULL,&tv,&sigs);
		LOG_DEBUG("%sselect",FUN_EVENT);

		if(ret < 0)
		{
			LOG_ERROR("%ssd#%d broke",ERROR_EVENT,connection->sd);
			connection->pool->breaks_in_row++;
			handle_bad_performance(connection,TRUE);
			stop_connection(connection);
			free_connection(connection);
			pthread_exit(NULL);
			/* EXIT */
		}

		// read data into connections volatile buffer
		int cnt = 0;
		if(FD_ISSET(connection->sd,&sock_set))
		{
			cnt = f_read(connection->sd,connection->volatile_buffer,VOLATILE_BUFFER_SIZE);

			if(cnt <= 0)
			{
				LOG_ERROR("%ssd#%d broke",ERROR_EVENT,connection->sd);
				connection->pool->breaks_in_row++;
				handle_bad_performance(connection,TRUE);
				stop_connection(connection);
				free_connection(connection);
				pthread_exit(NULL);
				/* EXIT */
			}
		}

		// notify scheduler
		if(!connection->is_corrupted && !connection->is_normal_get && !connection->is_chunked_encoding)
		{
			connection_read_event(connection,cnt);
		}

		if(cnt > 0)
		{
			// parse the volatile buffer and jump into callbacks on the way
			http_parser_execute(connection->parser,&parser_callbacks,connection->volatile_buffer,cnt);
		}

		if(!connection->is_corrupted && !connection->is_normal_get && !connection->is_chunked_encoding && !connection->out_of_bounds_request)
		{
			// check if a new request has to be made
			LOG_DEBUG("%sdata_size=%zu, pos_save=%zu",RESULT_EVENT,connection->current_chunk->data_size, connection->current_chunk->pos_save);
			if(connection->current_chunk->data_size == connection->current_chunk->pos_save)
			{
				// close if maximum requests reached
				if(reached_maximum(connection->scheduler))
				{
					stop_connection(connection);
					free_connection(connection);
					pthread_exit(NULL);
					/* EXIT */
					/*
					if(DO_THREAD_ADVANCE_POINTER)
					{
						advance_pool_save_pointer(connection->pool);
					}
					connection->is_used = FALSE;
					LOG_INFO("%sfinished collector for: sd#%d - request limit reached",FUN_EVENT,connection->sd);
					// TODO: remove from hash table!!!!
					free_connection(connection);
					pthread_exit(NULL);
					*/
				}

				// check if we need a new connection before we send the next request
				int needs_new_con = needs_new_connection(connection->scheduler);

				// make a new request
				if(connection->scheduler->num_sends < connection->pool->mpsocket->max_req_con || connection->pool->mpsocket->max_req_con == 0)
				{
					LOG_DEBUG("%smake new request",COND_EVENT);
					int cnt = make_next_request(connection);

					if(cnt == NO_MORE_REQ_FLAG)
					{
						// we stored everything - we do not need to use this connection anymore
						connection->is_used = FALSE;
					}
					else if(cnt < 0)
					{
						// some error occurred
						if(USE_ASSERTS) assert(connection->current_chunk != NULL);
						LOG_ERROR("%ssd#%d broke",ERROR_EVENT,connection->sd);
						connection->pool->breaks_in_row++;
						handle_bad_performance(connection,TRUE);
						stop_connection(connection);
						free_connection(connection);
						pthread_exit(NULL);
						/* EXIT */
					}
				}

				// create new connection if necessary to overcome request limit
				//if(needs_new_connection(connection->scheduler))
				if(needs_new_con)
				{
					lock_for_threading(connection->pool);
					mpsock_connection *conn = create_sub_connection(connection->pool,connection);
					start_connection(conn);
					unlock_for_threading(connection->pool);
				}
			}
			else
			{
				// check if we want to close the connection due to performance issues
				int perf_flag = performs_bad(connection->scheduler);
				//if(performs_bad(connection->scheduler))
				if(perf_flag == BAD_PERF_CLOSE || (perf_flag == BAD_PERF_KEEP && !USE_WAIT_FOR_REUSE))
				{
					// close down and free
					handle_bad_performance(connection,TRUE);
					stop_connection(connection);
					free_connection(connection);
					pthread_exit(NULL);
					/* EXIT */
				}
				else if(perf_flag == BAD_PERF_KEEP)
				{
					// reuse connection later on
					connection->waiting = TRUE;
					handle_bad_performance(connection,FALSE);
					//stop_connection(connection);
					//wait_for_reuse(connection);
					stop_connection(connection);
					wait_for_reuse(connection);
					reinit_connection(connection);
					pthread_exit(NULL);
					/* EXIT */
				}
			}

			if(DO_THREAD_ADVANCE_POINTER)
			{
				// try to advance the save pointer of the pool
				advance_pool_save_pointer(connection->pool);
			}
		}

		if(!connection->out_of_bounds_request)
		{
			if(USE_ASSERTS) assert(connection->pool->pos_buffer_save <= connection->pool->current_file_size+connection->pool->current_response_size);

			// TODO: verify
			if(connection->pool->pos_buffer_save == connection->pool->current_file_size+connection->pool->current_response_size && connection->pool->is_response_created)
			{
				connection->is_used = FALSE;
			}

			connection->pool->breaks_in_row = 0;
		}
		else
		{
			connection->is_used = FALSE;
		}
	}

	connection->pool->breaks_in_row = 0;

	stop_connection(connection);
	pthread_exit(NULL);
}
