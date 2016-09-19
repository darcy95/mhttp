/*
 * This structure represents a data bucket where one socket can store its data
 */
typedef struct 
{
    size_t start_byte;          // Key: the first byte of this chunk within the range of the file_size
                                // (i.e., 0 ~ file_size, see mpsock_data_pool). 
                                // Once set, this value will not change.

    size_t size;      			// the size of the payload followed by one HTTP response message

    size_t pos_delivered;       // the cursor that points out the last byte of the already delivered data (to application)

    size_t pos_stored;          // the cursor that points out the last byte of the data chunk that is currently stored.

    void *data;                 // data buffer

	UT_hash_handle hh;          // makes this structure hashable
} mpsock_data;

/*
 * This (mpsock_data_pool) is per-file data unit. (e.g., jpeg, txt, html)
 * Each per-file data unit consists of multiple per-req/rep data units (i.e., mpsock_data).
 */
typedef struct
{
    int fd; // Key (= parser->fd = parsers->parser->fd)

    size_t file_size; // the total length of the requested file (response header EXCLUDED)
	size_t response_header_length;

    size_t pos_read;  // the start-byte of the chunk to be READ. (see find_chunk(2))
                      // This will be used to lookup the data chunk to be read.
                      // (range: 0 ~ file_size)

	int finished_parsers; // number of parsers with no more requests // TODO: make better!!!

	int response_header_delivered; // flag to determine whether first response was already delivered to application

    mpsock_data *chunks;	// payload data chunks

	size_t next_start_byte;	// the next start byte to be requested (range (start_byte) ~ (start_byte+chunk->size))

    void *buffer;        // the pointer to the buffer in which the ready-to-be-read data is stored.
    //int pos_buffer_read; // the position of the read cursor. Data is delivered to the application until this position.
    size_t pos_buffer_save; // the position of the save cursor. Data is stored until this position.
	size_t pos_buffer_read; // The difference between pos_buffer_read and pos_buffer_save can be seen as data to be read.
	
	// temporary buffer - here socket data is transfered to before it is parsed
	size_t volatile_buffer_size;
    char *volatile_buffer;

	int highest_fd;	// highest file descriptor in fd_list

	// keep track of how many connections are used for this pool
	int open_connections;
	
	int max_connections; // maximum number of allowed parallel connections for this pool

	// collector thread worker related stuff (necessary for monitoring tcp buffers)
	pthread_t pid;			// collector thread id
	int is_thread_running;	// flag telling us whether collector thread for this pool is running
	fd_set fd_list;			// socket descriptor list -> tcp buffers to monitor
	fd_set fd_working;		// working set for pselect()

	UT_hash_handle hh;   // makes this structure hashable
} mpsock_data_pool;

/*
 * structure helpers
 */
size_t sum_delivered(mpsock_data_pool *pool)
{
	// TODO: verify
	return pool->pos_read;
}

size_t chunk_buffer_size(mpsock_data *chunk)
{
	return chunk->size;
}

size_t chunk_stored_size(mpsock_data *chunk)
{
	return chunk->pos_stored;
}

size_t chunk_read_size(mpsock_data *chunk)
{
	return chunk->pos_delivered;
}

void add_file_descriptor(mpsock_data_pool *pool, http_parser *parser)
{
	FD_SET(parser->sd,&(pool->fd_list));
	if(parser->sd > pool->highest_fd)
	{
		pool->highest_fd = parser->sd;
	}
}

// TODO: part of the scheduler
void determine_chunk_size(mpsock_data_pool *pool, http_parser *parser)
{
	// raise chunk size as far as possible
	while(pool->file_size - pool->max_connections * parser->chunk_size > 0 && parser->chunk_size < MAXIMUM_CHUNK_SIZE)
	{
		parser->chunk_size *= 2;
	}

	// adjust in case that we are close to EOF
	if(parser->chunk_size > pool->file_size-pool->pos_read)
	{
		parser->chunk_size = pool->file_size-pool->pos_read;
	}
}

/* 
 * global variables
 */
mpsock_data_pool *data_pools; // data_pool hash-table

// TODO: make thread_safe operations on this variables
int total_open_connections = 0; // number of open connections

static http_parser_settings settings_null =
{  .on_message_begin = 0
  ,.on_header_field = 0
  ,.on_header_value = 0
  ,.on_url = 0
  ,.on_body = 0
  ,.on_headers_complete = 0
  ,.on_message_complete = 0
};

http_parser_settings settings;

/* 
 * function declarations
 */
void *run_thread(void*);
void sub_conn(http_parser*,mpsock_data_pool*);
void init_mhttp(http_parser*);

/*
 * creation methods
 */
mpsock_data *create_chunk(mpsock_data_pool *pool, size_t start_byte, http_parser *parser)
{
	LOG_INFO("%screate_chunk(start_byte=%d, size=%d)",FUN_EVENT,start_byte,parser->chunk_size);
    mpsock_data *chunk = (mpsock_data *) malloc(sizeof(mpsock_data));
    chunk->start_byte = start_byte;
	chunk->size = parser->chunk_size;
    chunk->pos_delivered = 0;
    chunk->pos_stored = 0;

	size_t chunk_length = chunk_buffer_size(chunk);
	if(chunk_length > 0)
	{
		chunk->data = (void *) malloc(chunk_length);
	}

    HASH_ADD(hh, pool->chunks, start_byte, sizeof(size_t), chunk);

	pool->next_start_byte += parser->chunk_size;

	LOG_DEBUG("%sdone!",RESULT_EVENT);
	return chunk;
}

void shrink_chunk_buffer_size(mpsock_data *chunk, size_t size)
{
	if(chunk->size <= size) return;

	// set new size
	chunk->size = size;
}

mpsock_data_pool *create_data_pool(http_parser *p)
{
    // ==================================================================
    // Creating a mpsock_data_pool
    // ==================================================================
    LOG_INFO("%screate_data_pool(fd=%d)",FUN_EVENT,p->fd);
	mpsock_data_pool *pool = (mpsock_data_pool*) malloc(sizeof(mpsock_data_pool));
    pool->fd = p->fd;
	pool->file_size = 0;
	pool->response_header_length = 0;
	pool->finished_parsers = 0;
    pool->pos_read = 0; 
	pool->response_header_delivered = FALSE;
	pool->chunks = NULL;
    pool->buffer = NULL;
	pool->highest_fd = 0;
    pool->pos_buffer_read = 0;
    pool->pos_buffer_save = 0;
	pool->open_connections = 0;
	pool->max_connections = conns;
	pool->next_start_byte = 0;
	total_open_connections++;
	pool->volatile_buffer_size = VOLATILE_BUFFER_SIZE;
	pool->volatile_buffer = (char*)malloc(VOLATILE_BUFFER_SIZE);

    HASH_ADD_INT(data_pools, fd, pool);
	
	// add current socket descriptor to pool fd_list
	add_file_descriptor(pool,p);

    return pool;
}

/*
* cleanup methods
*/
// parser -> this method also closes the assigned socket!
void free_http_connection(http_connection *connection)
{
	// TODO: fre properly
	LOG_INFO("%sfree_http_connection(connection->sd=%d)",FUN_EVENT,connection->sd);
	// close the socket first
	f_close(connection->sd);

	// remove parser from hashtable
	HASH_DEL(connections,connection);
	free(connection->parser);
	free(connection);
}

// free chunk
void free_data_chunk(mpsock_data_pool *pool, mpsock_data *chunk)
{
	// TODO: free properly
	LOG_INFO("%sfree_data_chunk(chunk->start=%d)",FUN_EVENT,chunk->start_byte);
	HASH_DEL(pool->chunks, chunk);
	free(chunk->data);
	free(chunk);
}

// data chunks
void free_data_chunks(mpsock_data_pool *pool)
{
	// TODO: free properly
	LOG_INFO("%sfree_data_chunks()",FUN_EVENT);
	mpsock_data *current_data, *tmp;
	HASH_ITER(hh, pool->chunks, current_data, tmp)
	{
    	HASH_DEL(pool->chunks, current_data);
		free(current_data->data);
		free(current_data);
	}

	free(pool->chunks);
}

// data pools
void free_data_pool(mpsock_data_pool *pool)
{
	// TODO: free properly
	LOG_INFO("%sfree_data_pool()",FUN_EVENT);
	HASH_DEL(data_pools,pool);

	// close sockets and parsers
	http_connection *p;
	for(p=connections; p!=NULL; p=p->hh.next)
	{
		if(p->fd == pool->fd)
		{
			// this parser belongs to this pool -> remove it!
			FD_CLR(p->sd, &(pool->fd_list));
			free_http_connection(p);
		}
	}

	// adjust open connections
	total_open_connections -= pool->open_connections;

	// clear fd lists
	FD_ZERO(&(pool->fd_list));
	FD_ZERO(&(pool->fd_working));

	free(pool->volatile_buffer);
	
	// free chunks
	free_data_chunks(pool);

	// remove pool
	free(pool);
}

/*
 * resets
 */
void reset_data_pool(mpsock_data_pool* pool)
{
	// unmap buffer space
	munmap(pool->buffer,pool->file_size+pool->response_header_length);

	// rewind file pointers
	lseek(pool->fd,0,SEEK_SET);

	// clear chunks
	HASH_CLEAR(hh,pool->chunks);

	// reset flags and values
	// TODO: free buffer???
	pool->file_size = 0;
	pool->response_header_delivered = FALSE;
	pool->finished_parsers = 0;
	pool->buffer = NULL;
	pool->next_start_byte = 0;
	pool->open_connections = 0;
	pool->pos_read = 0;
	pool->pos_buffer_save = 0;
	pool->pos_buffer_read = 0;
	// TODO: take care of fd_sets!!!!
	LOG_INFO("%sreset_data_pool(pool->fd=%d)",RESULT_EVENT,pool->fd);
}

/*
 * find methods
 */
mpsock_data *find_chunk(mpsock_data *mdata, size_t start_byte)
{
    mpsock_data *data;
    HASH_FIND(hh, mdata, &start_byte, sizeof(size_t), data);
    return data;
}

mpsock_data_pool *find_data_pool(fd)
{
    mpsock_data_pool *pool;
    HASH_FIND_INT(data_pools, &fd, pool);
    return pool;
}

/*
 * header generation methods
 */
//size_t generate_http_response_header(http_parser *parser, char *hdr)
//{
    //sprintf(hdr, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nLast-Modified: %s\r\nContent-Length: %"PRIu64"\r\nConnection: keep-alive\r\n\r\n", parser->res_content_type, parser->res_last_modified, parser->file_size);
//
    //return strlen(hdr);
//}

size_t generate_http_request_header(http_parser *parser, char *new_header, size_t len, int range_from, int range_to)
{
	LOG_INFO("%sgenerate_http_request_header(parser->sd=%d, from=%d, to=%d)",FUN_EVENT,parser->sd,range_from,range_to);
	char line[512];

	memset(new_header, 0, len);
	memset(line, '\0', 512);
	sprintf(line, "Range: bytes=%d-%d\r\n\r\n", range_from, range_to);
	strncpy(new_header, parser->original_request, parser->original_request_size);
	strncat(new_header, line, strlen(line));
	return strlen(new_header);

	/**
    int i;
    int found_range = FALSE;
    size_t size = 0;
    char line[512];

    memset(new_header, 0, len);
    memset(line, '\0', 512);
    sprintf(line, "GET %s HTTP/1.1\r\n", parser->url);
    strncpy(new_header, line, strlen(line));

    for (i = 0 ; i < parser->req_header_lines ; i++) {
        memset(line, '\0', 512);

        if (strcmp(parser->req_field[i], "Range") == 0) {
            sprintf(line, "Range: bytes=%d-%d\r\n", range_from, range_to);
            found_range = TRUE;
        } else {
            sprintf(line, "%s: %s\r\n", parser->req_field[i], parser->req_value[i]);
        }

        strncat(new_header, line, strlen(line));
    }

    if (found_range == FALSE) {
        memset(line, '\0', 512);
        sprintf(line, "Range: bytes=%d-%d\r\n", range_from, range_to);
        strncat(new_header, line, strlen(line));               
    }

    strcat(new_header,"\r\n");

    return strlen(new_header);
	**/
}

/*
 * parser helpers
 */
size_t extract_file_size(char *buf)
{
    char *tmp = strchr(buf, '/'); 

    if (tmp != NULL) {
        tmp++; // eliminating '/'
    }

    return atoi(tmp);
}

size_t extract_first_byte(char *buf)
{
    char *tmp = strtok(buf, "-");
    tmp = strchr(tmp, ' ');

    if (tmp != NULL) {
        tmp++; // trimming a left white space
    }

    return atoi(tmp);
}

/*
 * mhttp logic
 */
// check if data chunk is complete
int is_data_chunk_complete(mpsock_data *chunk)
{
	if(chunk_stored_size(chunk) >= chunk_buffer_size(chunk))
	{
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

// copy data to chunk and adjust buffer pointers
void transfer_chunk_data(mpsock_data *chunk, void *data, size_t length)
{
	if(length > 0)
	{
		memcpy(chunk->data + chunk_stored_size(chunk), data, length);
		chunk->pos_stored += length;
	}
}

// method called to initialize the pool -> this is called after the first "test" request returns with the necessary file-size and response header
void init_data_pool(mpsock_data_pool *pool, http_parser *parser)
{
	//mp_addrs *tmp;
	//for(tmp = address_map; tmp != NULL; tmp=tmp->hh.next)
	//{
	//	LOG_INFO("%sDOMAIN %s",FUN_EVENT,tmp->name);
	//}

	LOG_INFO("%sinit_data_pool(pool->fd=%d, parser->sd=%d, file_size=%d) --- initial_chunk_size=%d",FUN_EVENT,pool->fd,parser->sd,parser->file_size,parser->chunk_size);
    pool->file_size = parser->file_size;

	// generate repsonse header for application
    //char header[HTTP_MAX_HEADER_SIZE];
    //size_t header_size = generate_http_response_header(parser, header);
	pool->response_header_length = parser->response_header_size;

    // ==================================================================
    // Mapping the master file descriptor to the memory file descriptor
    // ==================================================================
    int pool_buffer_size = pool->file_size + parser->response_header_size;

	// update applications file buffer size
    if(ftruncate(pool->fd, pool_buffer_size) < 0)
	{
        perror("ftruncate() error");
    }

	LOG_DEBUG("%smapping %d bytes",RESULT_EVENT,pool_buffer_size);
    pool->buffer = mmap((caddr_t) 0, pool_buffer_size, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_SHARED, pool->fd, 0);

    if(pool->buffer == MAP_FAILED)
	{
        perror("mmap failed");
        exit(0);
    }

	// copy header to application
	LOG_DEBUG("%sapplication response header size=%d:\n============================\n%s",RESULT_EVENT,parser->response_header_size,parser->response_header_buffer);
	memcpy(pool->buffer, parser->response_header_buffer, parser->response_header_size);
	pool->pos_buffer_save+=parser->response_header_size;
	parser->response_header_delivered = TRUE;
	pool->response_header_delivered=TRUE;

	// TODO: verify
	create_chunk(pool,0,parser);

	// determine new chunk size
	determine_chunk_size(pool,parser);
	
	//sub_conn(parser,pool);
	//http_parsers * parser_wrap = find_parser_by_sd(parser->sd);
	//free_http_parser(parser_wrap);
	//init_mhttp(parser);
	//request_more(parser);
}

ssize_t find_next_start_byte_and_reserve(mpsock_data_pool *pool, http_parser *parser) 
{
	// TODO: verify
	if(pool->next_start_byte >= pool->file_size)
	{
		// the file is complete -> no more chunks
		return -1;
	}

	int next_start_byte = pool->next_start_byte;

	LOG_INFO("%snext start byte: %d for pool->fd=%d",RESULT_EVENT, next_start_byte, pool->fd);

	determine_chunk_size(pool,parser);
    create_chunk(pool, next_start_byte, parser);

    return next_start_byte;
}

int request_more(http_parser *parser)
{
    // 1. find the start byte of the next data chunk to be requested (size_t)
	if(USE_ASSERTS)
	{
		assert(parser!=NULL);
	}

	LOG_INFO("%srequest_more(parser->sd=%d)",FUN_EVENT,parser->sd);

    /*
    // ====================================================================== //
    //                                                                        //
    //  PATH MANAGEMENT: examine the connection when it needs to send every   //
    //                   X(e.g., 5)th chunk whether it is Y (e.g., 3) times   //
    //                   slower than the highest rate connection. If so, the  //
    //                   multiHTTP must mark the connection as the disuse     //
    //                   connection and establish a new connection. Which     //
    //                   IP address to be used for the new connection is      //
    //                   entirely upto the multiDNS.                          //
    // ---------------------------------------------------------------------- // 
    // Every X(5) requests
    if (p->count_request_sent > 0 && (p->count_request_sent % 5) == 0) {
        // if conditions match (meaning that the connection is Y(3) times
        // slower than the best connection)
        double highest_rate = get_highest_rate();

        if ((highest_rate > 0 && p->avg_throughput > 0) && highest_rate >= (3 * p->avg_throughput)) {
            // this connection is removed from the fd_list.
            FD_CLR(p->sd, &fd_list); 
            
            // will close() make a delay? Leave it if so.
            // close(p->sd); 
            
            // mark the parser as a disuse parser
            p->parser_status = PARSER_NOMORE_REQ; 

            // open a new connection
            sub_conn(p); 

            return FALSE;
       }
    }
    // ---------------------------------------------------------------------- // 
    //  THE END OF PATH MANAGEMENT                                            //
    // ====================================================================== //
    */
    mpsock_data_pool *pool = find_data_pool(parser->fd);

    // ====================================================================== //
    //                                                                        //
    //  SCHEDULING: every time when the connection needs to request another   //
    //              chunk, it must choose the next chunk. get_skip_count(p)   //
    //              calculates how many (next) chunks the current connection  //
    //              must skip according to the difference between the rate of //
    //              the current connection and that of the best connection.   //
    //              Skipped chunks will be downloaded by faster connections.  //
    //                                                                        //
    // ---------------------------------------------------------------------- // 
    ssize_t next_start_byte = find_next_start_byte_and_reserve(pool,parser);
    //size_t next_start_byte = find_next_start_byte(pool, p, 0, get_skip_count(p));
    // ---------------------------------------------------------------------- //
    //  THE END OF SCHEDULING                                                 //
    // ====================================================================== //
    if(next_start_byte < 0) 
	{
		LOG_INFO("%snext start byte < 0 for parser->sd=%d --> parser reinitializes",RESULT_EVENT,parser->sd);
		// TODO: verify
        //parser->parser_status = PARSER_NOMORE_REQ;
		// TODO: reinit parser
		parser->chunk_size = initial_chunk_size;
		parser->parser_status = PARSER_INIT;
		parser->response_header_delivered = FALSE;
		//memset(parser->response_header_buffer,'\0',parser->response_header_size);
		parser->response_header_size = 0;
		pool->finished_parsers++;
        return FALSE;
    }

    parser->parser_status = PARSER_MATURE;
    http_parser_soft_init(parser, HTTP_BOTH);

    // 2. generate_http_request_header() using the range found in 1
	char req[HTTP_MAX_HEADER_SIZE];
	// TODO: recalculate new chunk size for this parser, based on performance
    size_t req_len = generate_http_request_header(parser, req, HTTP_MAX_HEADER_SIZE, next_start_byte, ((next_start_byte + parser->chunk_size - 1) <= pool->file_size) ? next_start_byte + parser->chunk_size - 1 : pool->file_size);
    
    // 3. send out the request message made in 2
	LOG_INFO("%swrite request to parser->sd=%d",FUN_EVENT,parser->sd);
    if(f_write(parser->sd, req, req_len) < 0)
	{
		LOG_ERROR("%sError writing request",COND_EVENT);
        return FALSE;
    }
	else
	{
		LOG_DEBUG("%srequest_more() -> success",RESULT_EVENT);
	}

    parser->start_byte_of_recent_request_sent = next_start_byte;

    return TRUE;
}

int condition_for_new_conn_match(mpsock_data_pool *pool, http_parser *parser)
{
	// TODO: make better
	return (pool != NULL && HASH_COUNT(pool->chunks) >= 1 && pool->open_connections < pool->max_connections && pool->open_connections*parser->chunk_size < pool->file_size);
}

void parse_message(http_parser *parser, char *buf, size_t len)
{
	LOG_DEBUG("%sCHUNK_SIZE=%d",FUN_EVENT,parser->chunk_size);
	LOG_DEBUG("%sparse_message(parser->fd=%d, parser->sd=%d)",FUN_EVENT,parser->fd,parser->sd);
    http_parser_execute(parser, &settings, buf, len);

    mpsock_data_pool *pool = find_data_pool(parser->fd);

	if(parser->parser_status == PARSER_FIRST_REQ)
	{
		// this is a test request to verify if 'Range' atribute is supported by the server
		LOG_INFO("%sfirst http request: parser->sd=%d",COND_EVENT,parser->sd);
		pool->open_connections++;
        generate_http_request_header(parser, buf, len, 0, parser->chunk_size-1);
    }
    else
	{
		while(condition_for_new_conn_match(pool,parser))
		{
			// Create a new connection
			LOG_INFO("%senough resources for new connection",COND_EVENT);
        	sub_conn(parser,pool);
			LOG_DEBUG("%ssub_conn() --> success",COND_EVENT);
		}
    }
}

int move_available_data_to_application(mpsock_data_pool *pool)
{
	LOG_DEBUG("%smove_available_data_to_application(pool->fd=%d)",FUN_EVENT,pool->fd);
    int len = 0;
	// find next chunk for application
    mpsock_data *chunk = find_chunk(pool->chunks, pool->pos_read);

    if(chunk == NULL)
	{
		LOG_DEBUG("%smove_data -> chunk is NULL",COND_EVENT);
        return len;
    }

	len = chunk->pos_stored - chunk->pos_delivered;

	if(len>0)
	{
		LOG_DEBUG("%sfinished read data from application: %d ~ %d",RESULT_EVENT,0,chunk->pos_delivered);
		memcpy(pool->buffer + pool->pos_buffer_save, chunk->data + chunk->pos_delivered, len);
		chunk->pos_delivered += len;
		pool->pos_buffer_save += len;
		LOG_DEBUG("%spos_buffer_save = %d / %d",RESULT_EVENT,pool->pos_buffer_save,pool->file_size+pool->response_header_length);
		LOG_DEBUG("%spos_buffer_read = %d / %d",RESULT_EVENT,pool->pos_buffer_read,pool->file_size+pool->response_header_length);

		if(chunk->pos_delivered == chunk_buffer_size(chunk))
		{
			// everything is copied -> we point to next chunk and free this one
			LOG_INFO("%schunk %d ~ %d complete",RESULT_EVENT,chunk->start_byte,chunk->start_byte + chunk_buffer_size(chunk));
			pool->pos_read += chunk->size;

			if(pool->pos_read+1 < pool->file_size)
			{
				// TODO: ???
				//free_data_chunk(pool,chunk);
			}
    	} 
	}

    return len;
}

void *run_thread(void *mdp)
{
    mpsock_data_pool *pool = (mpsock_data_pool*) mdp;
	LOG_INFO("%srun thread: pool->fd=%d",NEW_THREAD_EVENT,pool->fd);

	if(USE_ASSERTS)
	{
		assert(pool != NULL);
	}

    sigset_t sigs;
    sigfillset(&sigs);

	struct timespec tv;
	tv.tv_sec = 1;
	tv.tv_nsec = 0;

    int cnt;
    http_connection *con;
    int loop = TRUE;

    while(loop)
	{
		// update file descriptor list
		FD_ZERO(&(pool->fd_working));
		pool->fd_working = pool->fd_list;

		// TODO: possible bottleneck: when another fd is added while pselect() blocks and current fds might not trigger pselect()
		// TODO: maybe problem: parallel threads reading from same TCP buffers -> might lead to trouble
		LOG_DEBUG("%spselect() - block",FUN_EVENT);
        if(pselect(pool->highest_fd+1, &(pool->fd_working), NULL, NULL, &tv, &sigs) == -1) 
		{
            perror("pselect error");
            exit(0);
        }

		LOG_DEBUG("%spselect() - unblock",FUN_EVENT);

		// TODO: maybe problem: going through all parsers and reading all atached buffers -> also the ones that theoretically belong to other threads
        for(con = connections; con != NULL; con = con->hh.next)
		{
			LOG_DEBUG("%sFOR LOOP",COND_EVENT);

			if(FD_ISSET(con->sd, &(pool->fd_working))) 
			{
				LOG_DEBUG("%sISSET sd=%d",RESULT_EVENT,con->sd);
                cnt = f_read(con->sd, pool->volatile_buffer, pool->volatile_buffer_size);
				LOG_DEBUG("%sread into volatile buffer",RESULT_EVENT);

                if(cnt < 0)
				{
					// TODO: use free_data_pool
					FD_CLR(con->sd, &(pool->fd_list));
                    close(con->sd);
                    perror("Connection read error");
                    exit(0);
                }
				else if(cnt == 0)
				{
                    LOG_FATAL("%sread from the connection fd#%d returns 0\n",FATAL_EVENT,con->sd);
					FD_CLR(con->sd, &(pool->fd_list));
                    close(con->sd);
                    exit(0);
                }
				else
				{
					// parse data in volatile buffer -> use callbacks to transfer payload to pool buckets (*chunks)
                    parse_message(con->parser, pool->volatile_buffer, cnt);
					LOG_DEBUG("%sparse_message() --> success",RESULT_EVENT);
                }
            }

            if(con->parser->parser_status == PARSER_NEW_REQUEST)
			{
				// create new request
                request_more(con->parser);
            }
        }

		LOG_DEBUG("%smove data to application",RESULT_EVENT);
		// move coherent data from pool buckets to pool queue
        move_available_data_to_application(pool);

		if(pool->finished_parsers >= pool->open_connections)
		{
			loop = FALSE;
		}

		// TODO: remove this
        //for(parser = parsers; parser != NULL; parser = parser->hh.next)
		//{
        //    if(parser->fd == pool->fd && parser->parser->parser_status != PARSER_NOMORE_REQ)
		//	{
        //        loop = TRUE;
		//		//LOG_INFO("%sparser->sd=%d REQ!!!!",RESULT_EVENT,parser->sd);
        //    }
		//	else
		//	{
		//		//LOG_INFO("%sparser->sd=%d NOMORE_REQ!!!!",RESULT_EVENT,parser->sd);
		//	}
        //}
    }

    while(sum_delivered(pool) < pool->file_size)
	{
        move_available_data_to_application(pool);
    }

	LOG_INFO("%sterminate thread for pool->fd=%d",END_THREAD_EVENT,pool->fd);
	pool->is_thread_running = FALSE;
}

void sub_conn(http_parser *current_parser, mpsock_data_pool *pool)
{	
	// TODO: check everything here!!!!!!!!!!
	LOG_INFO("%ssub_conn() clone parser->sd=%d in pool->fd=%d",FUN_EVENT,current_parser->sd,pool->fd);
    // 1. get socket
	// TODO: more efficient
	// find belonging parsers that might have been created before
	http_parser *parser;
	http_connection *connection;
	int no_open_connection = TRUE;
	for(connection = connections; connection != NULL; connection = connection->hh.next)
	{
		if(connection->fd == pool->fd && connection->parser->parser_status == PARSER_INIT)
		{
			no_open_connection = FALSE;
			parser = connection->parser;
			break;
		}
	}

	if(no_open_connection)
	{
		// we need to create a new connection to a server
    	int s = f_socket(AF_INET, SOCK_STREAM, 0);

		LOG_DEBUG("%sNEW SOCKET: %d",FUN_EVENT,s);
	
	    mpsock_interface *intf = get_interface(s);

    	if(setsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, intf->name, strlen(intf->name)) < 0)
		{
    	    perror("setsockopt error");
    	    exit(1);
    	} 

		// 2. Connect to a new server
		struct sockaddr_in pin;
		memset(&pin, 0, sizeof(pin));

		// TODO: get connection parameters from datastructure
		pin.sin_family = PF_INET;
		pin.sin_addr.s_addr = get_ip(current_parser->host);
		LOG_DEBUG("%sgot new ip!",RESULT_EVENT);
		//pin.sin_port = htons(80);
		pin.sin_port = htons(current_parser->used_port);

		if(f_connect(s, (struct sockaddr *) &pin, sizeof(struct sockaddr_in)) == -1)
		{
    	    perror("Connection failed");
    	    return;
    	}

    	// 3. Create a new HTTP parser
    	parser = (http_parser*) malloc(sizeof(http_parser));

    	parser->fd = current_parser->fd;
    	parser->sd = s;
    	init_mhttp(parser);

		http_connection *item = (http_connection*) malloc(sizeof(http_connection));
    	item->sd = parser->sd;
    	item->fd = parser->fd;
    	item->parser = parser;
    	add_connection(item);
	}
    
    // 4. Copying 
    //parser->req_header_lines = current_parser->req_header_lines;
   	parser->ht_field = HT_FIELD_INIT;
   	parser->is_payload = FALSE;
	parser->chunk_size = current_parser->chunk_size;
   	parser->file_size = current_parser->file_size;
    parser->start_byte_of_currently_collecting_chunk = 0;
    strncpy(parser->host, current_parser->host, MAX_LENGTH_DOMAIN);
    //strncpy(parser->url, current_parser->url, MAX_LENGTH_URL);
    //strncpy(parser->res_content_type, current_parser->res_content_type, HTTP_HDR_MAX_CHARS);
    //strncpy(parser->res_last_modified, current_parser->res_last_modified, HTTP_HDR_MAX_CHARS);

    parser->parser_status = PARSER_MATURE;

	// TODO: verify if pointer setting is enough
	parser->original_request = current_parser->original_request;
	parser->original_request_size = current_parser->original_request_size;
    //int i, j;
    //for (i = 0; i < HTTP_HDR_MAX_NUM_LINES; i++) {
    //    for (j = 0; j < HTTP_HDR_MAX_CHARS; j++) {
    //        parser->req_field[i][j] = current_parser->req_field[i][j];
    //        parser->req_value[i][j] = current_parser->req_value[i][j];
    //    }
    //}

	// set new socket descriptor in pool's fd_list
	add_file_descriptor(pool,parser);

	// adjust counters
	pool->open_connections++;
	total_open_connections++;

	// make a request
    request_more(parser);
}

/*
 * parser callbacks
 */
int message_begin_cb(http_parser *parser)
{
    parser->is_payload = FALSE;

    return 0;
}

int body_cb(http_parser *parser, const char *buf, size_t len)
{
    if(parser->is_payload == TRUE)
	{
		LOG_DEBUG("%sbody_cb(parser->sd=%d)",COND_EVENT,parser->sd);
        mpsock_data_pool *pool = find_data_pool(parser->fd);

		// security checks to avoid segs
		if(USE_ASSERTS)
		{
    		assert(pool!=NULL);
		}

		mpsock_data *chunk = find_chunk(pool->chunks, parser->start_byte_of_currently_collecting_chunk);

		// security checks to avoid segs
		if(USE_ASSERTS)
		{
    		assert(chunk!=NULL);
			assert(chunk_stored_size(chunk)+len <= chunk_buffer_size(chunk));
		}

		// check if chunk size is appropriate
		size_t size = chunk->size;
		if(pool->file_size - chunk->start_byte < chunk->size)
		{
			size = pool->file_size - chunk->start_byte;
			LOG_INFO("%supdate chunk size to %d",COND_EVENT,size);
		}

		shrink_chunk_buffer_size(chunk,size);

		// transfer parsed body data to chunk buffer
		transfer_chunk_data(chunk,(void*)buf,len);

        // ========================================================
        // if the current writing chunk is complete but the file is
        // not yet complete, a new request message must be created
        // and sent via the same conneciton.
        // ========================================================
		if(is_data_chunk_complete(chunk))
		{
            parser->parser_status = PARSER_NEW_REQUEST;
        }
    }

    return 0;
}

int header_field_cb(http_parser *parser, const char *buf, size_t len)
{
	if(parser->type == HTTP_REQUEST)
	{
		// TODO: more efficient
		char name[len];
    	name[len] = '\0';
    	strncpy(name, buf, len);
        
		if(strcmp(name, "Host") == 0)
		{
            parser->ht_field = HT_FIELD_HOST;
        }
		else
		{
            parser->ht_field = HT_FIELD_INIT;
        }

        //strncpy(parser->req_field[parser->req_header_lines], buf, len);
    }
	else
	{
		if(!parser->response_header_delivered)
		{
			if(parser->response_header_size == 0)
			{
				// set response code etc
				unsigned short response_code = parser->status_code;
				if(response_code == 206)
				{
					// application should not see that its 206
					response_code = 200;
				}

				// TODO: verify
				// TODO: add OK, Partial Content etc to end of this line!
				size_t first_size = 14;
				char first_line[first_size];
				sprintf(first_line, "HTTP/%d.%d %d\r\n", parser->http_major, parser->http_minor, response_code);

				memcpy(parser->response_header_buffer+parser->response_header_size,first_line,first_size);
				parser->response_header_size += first_size;
				LOG_DEBUG("%sSet first line of header to:\n===============\n%s",RESULT_EVENT,first_line);
			}

			size_t size = len+2;
			char field[size];
			field[size-2] = ':';
			field[size-1] = ' ';
			field[size] = '\0';
    		strncpy(field, buf, len);

			// set flags to determine file-size
			if((parser->status_code == 200) && (strcmp(field, "Content-Length: ") == 0))
			{
            	parser->ht_field = HT_FIELD_LENG;
        	}
			else if((parser->status_code == 206) && (strcmp(field, "Content-Range: ") == 0))
			{
            	parser->ht_field = HT_FIELD_RANG;
        	}
			else if(strcmp(field, "Content-Length: ") == 0)
			{
				parser->ht_field = HT_FIELD_SKIP;
			}
			else
			{
				parser->ht_field = HT_FIELD_INIT;
			}

			if((parser->ht_field != HT_FIELD_RANG && parser->ht_field != HT_FIELD_LENG && parser->ht_field != HT_FIELD_SKIP))
			{
				// copy field to response header buffer
				memcpy(parser->response_header_buffer+parser->response_header_size,field,size);
				parser->response_header_size += size;
			}
			
			LOG_DEBUG("%sFOUND FIELD: %s",RESULT_EVENT,field);
		}
	}

	/**
    char name[len];
    name[len] = '\0';
    strncpy(name, buf, len);


    if (parser->type == HTTP_REQUEST) {
        if (strcmp(name, "Host") == 0) {
            parser->ht_field = HT_FIELD_HOST;
        } else {
            parser->ht_field = HT_FIELD_INIT;
        }

        strncpy(parser->req_field[parser->req_header_lines], buf, len);
    } else if (parser->type == HTTP_RESPONSE) {
        if ((parser->status_code == 200) && (strcmp(name, "Content-Length") == 0)) {
            parser->ht_field = HT_FIELD_LENG;
        } else if ((parser->status_code == 206) && (strcmp(name, "Content-Range") == 0)) {
            parser->ht_field = HT_FIELD_RANG;
        } else if (strcmp(name, "Content-Type") == 0) {
            parser->ht_field = HT_FIELD_TYPE;
        } else if (strcmp(name, "Last-Modified") == 0) {
            parser->ht_field = HT_FIELD_LAST;
        } else {
            parser->ht_field = HT_FIELD_INIT;
        }
    }
	**/

    return 0;
}

int header_value_cb(http_parser *parser, const char *buf, size_t len)
{
	if(parser->type == HTTP_REQUEST)
	{
        if(parser->ht_field == HT_FIELD_HOST)
		{
			// TODO: better
			//memset(parser->host,0,MAX_LENGTH_DOMAIN);
            parser->host[len] = '\0';
            strncpy(parser->host, buf, len);
        }         

        //strncpy(parser->req_value[parser->req_header_lines], buf, len);
        //parser->req_header_lines++;
    }
	else
	{
		if(!parser->response_header_delivered)
		{
			size_t size = len+2;
			char value[size];
			value[size-2] = '\r';
			value[size-1] = '\n';
			value[size] = '\0';
    		strncpy(value, buf, len);

			// we do not want the Range attribute to appear for the application - length added later
			if(parser->ht_field != HT_FIELD_RANG && parser->ht_field != HT_FIELD_LENG && parser->ht_field != HT_FIELD_SKIP)
			{
				// copy field to response header buffer
				memcpy(parser->response_header_buffer+parser->response_header_size,value,size);
				parser->response_header_size += size;
				LOG_DEBUG("%sHEADER at VALUE:%s\n==================================\n%s",FUN_EVENT,value,parser->response_header_buffer);
			}

			LOG_DEBUG("%sVALUE: %s",RESULT_EVENT,value);

			// reformat string for extraction
			value[len] = '\0';
			

			// extract file-size
			if(parser->ht_field == HT_FIELD_RANG)
			{
            	parser->file_size = extract_file_size(value);
            	parser->start_byte_of_currently_collecting_chunk = extract_first_byte(value);
				LOG_DEBUG("%sfound size=%d, first_byte=%d",COND_EVENT,parser->file_size,parser->start_byte_of_currently_collecting_chunk);
        	}
			else if(parser->ht_field == HT_FIELD_LENG)
			{
				// TODO: verify!
				LOG_INFO("%sDO MAGIC",COND_EVENT);
            	parser->file_size = atoi(buf);
        	}
		}
	}

	/**
    char name[len];
    name[len] = '\0';
    strncpy(name, buf, len);

    if (parser->type == HTTP_REQUEST) {
        if (parser->ht_field == HT_FIELD_HOST) {
            parser->host[len] = '\0';
            strncpy(parser->host, buf, len);
        }         

        strncpy(parser->req_value[parser->req_header_lines], buf, len);
        parser->req_header_lines++;
    } else if (parser->type == HTTP_RESPONSE) {
        if (parser->ht_field == HT_FIELD_RANG) {
            parser->file_size = extract_file_size(name);
            parser->start_byte_of_currently_collecting_chunk = extract_first_byte(name);
        } else if (parser->ht_field == HT_FIELD_LENG) {
            parser->file_size = atoi(buf);
        } else if (parser->ht_field == HT_FIELD_TYPE) {
            strncpy(parser->res_content_type, name, len);
        } else if (parser->ht_field == HT_FIELD_LAST) {
            strncpy(parser->res_last_modified, name, len);
        }
    }

    parser->ht_field = HT_FIELD_INIT;
	**/

    return 0;
}

int url_cb(http_parser *parser, const char *buf, size_t len)
{
	// TODO: necessary?
    //strncpy(parser->url, buf, len);

    return 0;
}

int headers_complete_cb(http_parser *parser)
{
    mpsock_data_pool *pool = find_data_pool(parser->fd);
	LOG_DEBUG("%sheaders_complete_cb",COND_EVENT);

    if(parser->type == HTTP_REQUEST)
	{
		// check if this is the first request for this data pool
        if(pool->file_size == 0 && pool->buffer == NULL)
		{
            parser->parser_status = PARSER_FIRST_REQ;
        }
		else
		{
			LOG_INFO("%spool already got file_size=%d",COND_EVENT,pool->file_size);
		}
    } 
	else if(parser->type == HTTP_RESPONSE)
	{
		if(pool->response_header_delivered)
		{
			LOG_INFO("%sheader already delivered",COND_EVENT);
			// response handling of follow-up requests
			if(parser->status_code != 200 && parser->status_code != 206)
			{
				// TODO: handle trouble in follow-up requests
				LOG_ERROR("%sbad response code: %d",COND_EVENT,parser->status_code);
			}
			else
			{
				parser->is_payload = TRUE;
			}
		}
		else
		{
			LOG_INFO("%sheader not yet delivered",COND_EVENT);
			
			// append Content-Length to header
			char line[512];
			memset(line, '\0', 512);
			sprintf(line,"Content-Length: %d\r\n\r\n",parser->file_size);
			size_t length = strlen(line);
			memcpy(parser->response_header_buffer+parser->response_header_size,line,length);
			parser->response_header_size += length;
			LOG_DEBUG("%sheaders_complete_cb(parser->sd=%d) -- send to application:\n===========================================================================\n%s",COND_EVENT,parser->sd,parser->response_header_buffer);
			
			// handle first response
        	switch(parser->status_code) 
			{
				// TODO: transfer header to application if first request status code is different
        		case 200:
					// TODO: verify!! Some servers respond with 200 instead of with 206, even though they are chunk capable!!!
					LOG_INFO("%sparser found file_size=%d",COND_EVENT,parser->file_size);
        	        init_data_pool(pool, parser);
        	        parser->parser_status = PARSER_FIRST_RES;
					parser->is_payload = TRUE;
	    	        break;
        		case 206:
					LOG_INFO("%sparser found file_size=%d",COND_EVENT,parser->file_size);
        	        init_data_pool(pool, parser);
        	        parser->parser_status = PARSER_FIRST_RES;
					parser->is_payload = TRUE;
	    	        break;

					/**
        	    	// Update data pool according to the information obtained from the first HTTP response.
        	    	if (pool->file_size == 0 && pool->buffer == NULL) 
					{
						LOG_INFO("%sparser found file_size=%d",COND_EVENT,parser->file_size);
        	    	    init_data_pool(pool, parser);
        	    	    parser->parser_status = PARSER_FIRST_RES;
        	    	}
					else
					{
						// TODO: verify
        	    	    // This reponse is from follow-up requests.
        	    	    // This data chunk doesn't contain the HTTP header.
						//mpsock_data *chunk = find_chunk(pool->chunks,parser->start_byte_of_currently_collecting_chunk);
						//size_t size = chunk->size;
						//if(pool->file_size - chunk->start_byte < chunk->size)
						//{
						//	size = pool->file_size - chunk->start_byte;
						//	LOG_INFO("%supdate chunk size to %d",COND_EVENT,size);
						//}
        	    	    //update_chunk_buffer_size(chunk, size);
        	    	}
		
		            parser->is_payload = TRUE;
	    	        break;
					**/
				default:
					
					break;
	        }
		}
    }    

    return 0;
}

int message_complete_cb(http_parser *parser)
{
	// TODO: verify
    http_parser_soft_init(parser, HTTP_BOTH);

    return 0;
}

void init_mhttp(http_parser *parser)
{
	LOG_INFO("%sinit_mhttp(parser->sd=%d)\n",FUN_EVENT,parser->sd);
    settings = settings_null;
    settings.on_message_begin    = message_begin_cb;
    settings.on_header_field     = header_field_cb;
    settings.on_header_value     = header_value_cb;
    settings.on_url              = url_cb;
    settings.on_body             = body_cb;
    settings.on_headers_complete = headers_complete_cb;
    settings.on_message_complete = message_complete_cb;

    http_parser_init(parser, HTTP_BOTH);
}
