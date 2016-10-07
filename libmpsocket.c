#define _GNU_SOURCE  /* to enable gcc magic in dlfcn.h for shared libs */

#include "libmpsocket.h"
#include "mpsock_socket.h"
#include "mpsock_connection.h"
#include "mpsock_dns.h"
#include "mpsock_interface.h"
#include "mpsock_buffer.h"

/*
 * Global variables
 */
// user input
static int initial_chunk_size;
static int conns;
static char *intf_list;
static char *ip_list;
static int max_req_con;
static int max_req_serv;
static int max_req_mpsocket;
static int initial_alpha;
int version = SCHEDULER_VERSION_DEFAULT;
int alpha_max = ALPHA_MAX_DEFAULT;
int processing_skips = PROCESSING_SKIPS_DEFAULT;
int use_initial_second_path = USE_INITIAL_SECOND_PATH_DEFAULT;
int use_random_path = USE_RANDOM_INTERFACE;
int log_decisions = DO_LOG_SYNC;
int log_traffic = DO_LOG_TRAFFIC;
int log_metrics = DO_LOG_METRICS;
int scheduler_algo = SCHEDULER_ALGO_DEFAULT;

// TODO: verify if static would be better
// hash tables
mpsock_socket *mpsock_socket_table = NULL;	// all mpsockets are stored here
mpsock_interface *mpsock_interface_table = NULL;	// all interfaces are stored here -> a pointer to this pointer is given to each socket
mpsock_addrs *mpsock_address_table = NULL;	// all dns resolvings are stored here -> a pointer of this pointer is given to each socket

// buffer
mpsock_buffer *mpsock_data_buffer = NULL;	// our buffer for payload data

/*
 * Renamed hooked function pointers
 */
// sys/socket.h interface
static int (*o_socket)(int, int, int);
static int (*o_connect)(int, const struct sockaddr *, socklen_t);
static int (*o_send)(int, const void *, size_t, int);
static int (*o_recv)(int, void *, size_t, int);
static int (*o_bind)(int, const struct sockaddr *, socklen_t);
static int (*o_getpeername)(int, struct sockaddr *, socklen_t *);
static int (*o_shutdown)(int, int);
static int (*o_setsockopt)(int, int, int, const void *, socklen_t);
static int (*o_getsockopt)(int, int, int, void *, socklen_t *);
static int (*o_accept)(int, struct sockaddr *, socklen_t *);
static int (*o_getsockname)(int, struct sockaddr *, socklen_t *);
static int (*o_listen)(int, int);
static int (*o_socketpair)(int, int, int, int socket_vector[2]);
static ssize_t (*o_recvfrom)(int, void *, size_t, int, struct sockaddr *, socklen_t *);
static ssize_t (*o_recvmsg)(int, struct msghdr *, int);
static ssize_t (*o_sendmsg)(int, const struct msghdr *, int);
static ssize_t (*o_sendto)(int, const void *, size_t, int, const struct sockaddr *, socklen_t);

// netdb.h interface
static struct hostent *(*o_gethostbyname)(const char *);
static int (*o_getaddrinfo)(const char *, const char *, const struct addrinfo *, struct addrinfo **);

// unistd.h interface
static size_t (*o_write)(int, const void *, size_t);
static ssize_t (*o_read)(int, void *, size_t);
static int (*o_close)(int);
static int (*o_select)(int, fd_set *, fd_set *, fd_set *, struct timeval *);
static int (*o_pselect)(int, fd_set *, fd_set *, fd_set *, const struct timespec *, const sigset_t *);

/*
 * hooked function pass through wrappers
 */
// sys/socket.h interface
int f_socket(int domain, int type, int protocol)
{
	return o_socket(domain,type,protocol);
}

int f_connect(int sd, const struct sockaddr *serv_addr, socklen_t addrlen)
{
	return o_connect(sd,serv_addr,addrlen);
}

int f_send(int s, const void *msg, size_t len, int flags)
{
	return o_send(s,msg,len,flags);
}

int f_recv(int fd, void *buf, size_t count, int flags)
{
	return o_recv(fd,buf,count,flags);
}

int f_bind(int fd, const struct sockaddr *address, socklen_t len)
{
	return o_bind(fd,address,len);
}

int f_getpeername(int fd, struct sockaddr *address, socklen_t *len)
{
	return o_getpeername(fd,address,len);
}

int f_shutdown(int fd, int how)
{
	return o_shutdown(fd,how);
}

int f_setsockopt(int fd, int level, int option_name, const void *option_value, socklen_t option_len)
{
	return o_setsockopt(fd,level,option_name,option_value,option_len);
}

int f_getsockopt(int fd, int level, int option_name, void *option_value, socklen_t *option_len)
{
	return o_getsockopt(fd,level,option_name,option_value,option_len);
}

int f_accept(int fd, struct sockaddr *address, socklen_t *address_len)
{
	return o_accept(fd,address,address_len);
}

int f_getsockname(int fd, struct sockaddr *address, socklen_t *address_len)
{
	return o_getsockname(fd,address,address_len);
}

int f_listen(int fd, int backlog)
{
	return o_listen(fd,backlog);
}

int f_socketpair(int domain, int type, int protocol, int socket_vector[2])
{
	return o_socketpair(domain,type,protocol,socket_vector);
}

ssize_t f_recvfrom(int fd, void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_len)
{
	return o_recvfrom(fd,buffer,length,flags,address,address_len);
}

ssize_t f_recvmsg(int fd, struct msghdr *message, int flags)
{
	return o_recvmsg(fd,message,flags);
}

ssize_t f_sendmsg(int fd, const struct msghdr *message, int flags)
{
	return o_sendmsg(fd,message,flags);
}

ssize_t f_sendto(int fd, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len)
{
	return o_sendto(fd,message,length,flags,dest_addr,dest_len);
}

// netdb.h interface
struct hostent *f_gethostbyname(const char *name)
{
	return o_gethostbyname(name);
}

int f_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
	return o_getaddrinfo(node,service,hints,res);
}

// unistd.h interface
size_t f_write(int fd, const void* buf, size_t count)
{
	return o_write(fd,buf,count);
}

ssize_t f_read(int fd, void* buf, size_t count)
{
	return o_read(fd,buf,count);
}

int f_close(int fd)
{
	return o_close(fd);
}

int f_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
	return o_select(nfds,readfds,writefds,exceptfds,timeout);
}

int f_pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout, const sigset_t *sigmask)
{
	return o_pselect(nfds, readfds, writefds, exceptfds, timeout, sigmask);
}

/* Subtract the `struct timeval' values X and Y,
   storing the result in RESULT.
   Return 1 if the difference is negative, otherwise 0.  */
int timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y)
{
	/* Perform the carry for the later subtraction by updating y. */
	if(x->tv_usec < y->tv_usec)
	{
		int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
		y->tv_usec -= 1000000 * nsec;
		y->tv_sec += nsec;
	}
	if(x->tv_usec - y->tv_usec > 1000000)
	{
		int nsec = (x->tv_usec - y->tv_usec) / 1000000;
		y->tv_usec += 1000000 * nsec;
		y->tv_sec -= nsec;
	}

	/* Compute the time remaining to wait.
	tv_usec is certainly positive. */
	result->tv_sec = x->tv_sec - y->tv_sec;
	result->tv_usec = x->tv_usec - y->tv_usec;

	/* Return 1 if result is negative. */
	return x->tv_sec < y->tv_sec;
}

//void delete_parser(http_parsers *p) {
//	LOG_INFO("delete_parser: sd=%d fd=%d\n", p->sd, p->fd);
//    HASH_DEL(parsers, p);
//    free(p->parser);
//    p->parser = NULL;
//}

//#include "./mpsock_misc.h"
//#include "./mpsock_tcp.h"
//#include "./mpsock_interface.h"
//#include "./mpsock_http.h"

void signalhandler(int signum)
{
	LOG_FATAL("%sSIGSEV Error Occurred",FATAL_EVENT);
	signal(signum,SIG_DFL);
	kill(getpid(),signum);
}

void signalhandlerAsserts(int signum)
{
	LOG_FATAL("%sSIGABRT (Assertion) Error Occurred",FATAL_EVENT);
	signal(signum,SIG_DFL);
	kill(getpid(),signum);
}

static void __attribute__ ((constructor))
init(void)
{
	LOG_INFO("%sinitialize libmpsocket",FUN_EVENT);
	srand(time(NULL));
	//signal(SIGSEGV,signalhandler);
	//signal(SIGABRT,signalhandlerAsserts);
	// Parse input environment variables
    const char *s;

	s = getenv("SCHEDULER_VERSION");
    
    if(s)
	{
        version = atoi(s);
    }

	if(version == -1)
	{
		version = SCHEDULER_VERSION_DEFAULT;
	}

	if(USE_ASSERTS) assert(version >= 0 && version < 3);

	//printf("[mHTTP]\tusing scheduler version %d"version);

    s = getenv("INITIAL_CHUNK_SIZE_IN_KB");
    
    if(s)
	{
        initial_chunk_size = atoi(s) * 1024;
    }
	else
	{
		initial_chunk_size = INITIAL_CHUNK_SIZE;
    }

	printf("set initial_chunk_size = %d kB\n",initial_chunk_size/1024);

	s = getenv("MAX_REQ_CON");
    
    if(s)
	{
        max_req_con = atoi(s);
    }
	else
	{
		max_req_con = MAX_REQ_CON;
    }

	LOG_INFO("%sset maximum requests per connection to %d",COND_EVENT,max_req_con);

	// //
    // // The choice of a scheduler algorithm is disabled (jkim)
    // // 
    //
    //  s = getenv("SCHEDULER_ALGO");    
    //  if(s)
    //  {
    //      scheduler_algo = atoi(s);
    //  }
    //
	//  printf("using scheduler algorithm %d\n",scheduler_algo);

	s = getenv("MAX_REQ_SERV");
    
    if(s)
	{
        max_req_serv = atoi(s);
    }
	else
	{
		max_req_serv = MAX_REQ_SERV;
    }

	LOG_DEBUG("%sset maximum requests per server to %d",COND_EVENT,max_req_serv);

	s = getenv("INITIAL_SECOND_PATH");
    
    if(s)
	{
        use_initial_second_path = atoi(s);
    }

	LOG_INFO("%sintial second path set to %d",COND_EVENT,use_initial_second_path);

	s = getenv("RANDOM_PATH");
    
    if(s)
	{
        use_random_path = atoi(s);
    }

	LOG_INFO("%srandom path set to %d",COND_EVENT,use_random_path);

	s = getenv("LOG_DECISIONS");
    
    if(s)
	{
        log_decisions = atoi(s);
    }

	s = getenv("LOG_TRAFFIC");
    
    if(s)
	{
        log_traffic = atoi(s);
    }

	s = getenv("LOG_METRICS");
    
    if(s)
	{
        log_metrics = atoi(s);
    }

	s = getenv("MAX_REQ_MPSOCKET");
    
    if(s)
	{
        max_req_mpsocket = atoi(s);
    }
	else
	{
		max_req_mpsocket = MAX_REQ_MPSOCKET;
    }

	LOG_INFO("%sset maximum requests per mpsocket to %d",COND_EVENT,max_req_mpsocket);

	s = getenv("INITIAL_ALPHA");
    
    if(s)
	{
        initial_alpha = atoi(s);
    }
	else
	{
		initial_alpha = INITIAL_ALPHA;
    }

	printf("set initial_alpha to %d\n",initial_alpha);

	s = getenv("ALPHA_MAX");
    
    if(s)
	{
        alpha_max = atoi(s);
    }

	if(alpha_max == -1)
	{
		alpha_max = ALPHA_MAX_DEFAULT;
	}

	printf("set alpha_max to %d\n",alpha_max);

	s = getenv("PROCESSING_SKIPS");
    
    if(s)
	{
        processing_skips = atoi(s);
    }

	if(processing_skips = -1)
	{
		processing_skips = PROCESSING_SKIPS_DEFAULT;
	}

	LOG_INFO("%sset processing_skips to %d",COND_EVENT,processing_skips);

    const char *p;
    p = getenv("CONNECTIONS");

    if(p)
	{
        conns = atoi(p);
    }
	else
	{
        usage();
        exit(0);
    }

	// create the ringbuffer
	mpsock_data_buffer = create_buffer(MPSOCK_BUFFER_SIZE);

    // For the manual configuration of IP and interface list
    intf_list = getenv("INTERFACES");
    ip_list = getenv("IPADDRS");

    if(strcmp(intf_list, "0") == 0) intf_list = NULL;
    if(strcmp(ip_list, "0") == 0) ip_list = NULL;

	// set global start ts
	gettimeofday(&global_start_ts,NULL);

	// set/verify hooks
#define GET_SYM(sym)							\
    o_##sym = dlsym(RTLD_NEXT, #sym);			\
    if (o_##sym == NULL) {						\
        fprintf(stderr, "mn:" "dlsym(%s) failed: %s\n", #sym, dlerror()); \
    }

    GET_SYM(socket);
    GET_SYM(connect);
    GET_SYM(write);
    GET_SYM(read); 
    GET_SYM(send);
    GET_SYM(recv);
    GET_SYM(close);
    GET_SYM(gethostbyname);
    GET_SYM(getaddrinfo);
	GET_SYM(bind);
	GET_SYM(getpeername);
	GET_SYM(shutdown);
	GET_SYM(setsockopt);
	GET_SYM(getsockopt);
	GET_SYM(accept);
	GET_SYM(getsockname);
	GET_SYM(listen);
	GET_SYM(socketpair);
	GET_SYM(recvfrom);
	GET_SYM(recvmsg);
	GET_SYM(sendmsg);
	GET_SYM(sendto);
	GET_SYM(select);
	GET_SYM(pselect);

#undef GET_SYM
}

int socket(int domain, int type, int protocol)
{
	if(PASS_THROUGH)
	{
		LOG_INFO("%ssocket(domain=%d, type=%d, protocol=%d)",FUN_EVENT,domain,type,protocol);
		return f_socket(domain,type,protocol);
	}

	// ==================================================================
   	// we return a fake descriptor to the application. Meaning, the 
   	// application believes that it gets a socket descriptor, but what it
   	// really gets is the file descriptor made by our wrapper function.
   	// ==================================================================

	int sd = f_socket(domain, type, protocol);
   	int rt = sd; 

	// check if we got a tcp socket
	if(type == SOCK_STREAM)
	{
		LOG_INFO("%shooked socket(domain=%d, type=%d, protocol=%d)",FUN_EVENT,domain,type,protocol);
		// create fake socket
		mpsock_socket *sock = create_mpsocket(sd, initial_chunk_size, conns, mpsock_data_buffer, max_req_con, max_req_serv, max_req_mpsocket, initial_alpha, version, alpha_max, processing_skips, use_initial_second_path, use_random_path, log_decisions, log_traffic, log_metrics, scheduler_algo);
   	    rt = sock->m_sd;

		LOG_INFO("%screated mhttp-socket: %d",RESULT_EVENT,rt);
	
		// setup the available interfaces
        if(intf_list == NULL)
		{
			collect_interfaces(sd); // collect available network interfaces
		}
		else
		{
			insert_interfaces(intf_list); // create the list of interface by the user
		}      
   	}
	else
	{
		LOG_INFO("%snormal socket(domain=%d, type=%d, protocol=%d)",FUN_EVENT,domain,type,protocol);
		LOG_INFO("%screated normal-socket: %d",RESULT_EVENT,rt);
	}

    return rt;
}

int connect(int sd, const struct sockaddr *serv_addr, socklen_t addrlen)
{
	if(PASS_THROUGH)
	{
		LOG_INFO("%sconnect(sd=%d)",FUN_EVENT,sd);
		return f_connect(sd,serv_addr,addrlen);
	}

	if(!is_mpsocket(sd))
	{
		LOG_INFO("%snormal connect(m_sd=%d)",FUN_EVENT,sd);
		return f_connect(sd,serv_addr,addrlen);
	}

	LOG_INFO("%shooked connect(m_sd=%d)",FUN_EVENT,sd);

	mpsock_socket *mpsock = find_mpsocket(sd);
	if(USE_ASSERTS) assert(mpsock != NULL);
	mpsock_connection *conn = NULL;
	struct sockaddr_in *sa = (struct sockaddr_in *) serv_addr;

	// check if we got a http connection request
	if(htons(sa->sin_port) == 80 || htons(sa->sin_port) == 8080)
	{
		// set the port
		set_port(mpsock,htons(sa->sin_port));

		conn = get_random_free_connection(mpsock->pool);

		// Force to connect the first IP address of the list
        //if(ip_list)
		//{
		//	// TODO: verify
		//	LOG_DEBUG("%sForce IP address",COND_EVENT);
		//	sa->sin_addr.s_addr = scheduler_get_first_ip(conn->scheduler,sa->sin_addr.s_addr);
        //}

		if(USE_ASSERTS) assert(conn != NULL);

        mpsock_interface *intf = scheduler_get_interface(conn->scheduler);

        if(setsockopt(conn->sd, SOL_SOCKET, SO_BINDTODEVICE, intf->name, strlen(intf->name)) < 0)
		{
            perror("setsockopt error");
            exit(1);
        }

		set_index_and_ip(conn,(unsigned long)sa->sin_addr.s_addr);
	}
	else if(htons(sa->sin_port) == 443 || htons(sa->sin_port) == 8443)
	{
		// https not supported yet -> strip mpsocket wrapper from socket descriptor
		//LOG_INFO("%shttps connection detected -> strip mpsock wrapper",COND_EVENT);
		if(PRINT_DEFAULT_OUT)
		{
			printf("\n[mHTTP]\tSSL Connection");
		}
		strip_mpsock_from_socket(mpsock);
	}
	else
	{
		LOG_FATAL("%sunsupported port detected %d",FATAL_EVENT,htons(sa->sin_port));
		perror("shutting down due to unsupported port");
		exit(1);
	}

	if(conn != NULL)
	{
		int i=0;
		for(i=0; i<conn->adrs->count; i++)
		{
			if(conn->adrs->ipset[i].ip == (unsigned long)sa->sin_addr.s_addr)
			{
				conn->adrs->ipset[i].inuse = TRUE;
				break;
			}
		}

		if(USE_ASSERTS) assert(conn->adrs->ipset[i].ip == (unsigned long)sa->sin_addr.s_addr);
		if(USE_ASSERTS) assert(conn->adrs->ipset[i].inuse == TRUE);
		LOG_INFO("%ssd#%d using host %zu",RESULT_EVENT,conn->sd,(unsigned long)sa->sin_addr.s_addr);
	}

	return f_connect(sd, serv_addr, addrlen);
	//return (is_http_connection) ? f_connect(conn->sd, serv_addr, addrlen) : f_connect(sd, serv_addr, addrlen);
/*
	else
	{
    	int is_http = FALSE;
    	http_connection *item;

    	struct sockaddr_in *sa = (struct sockaddr_in *) serv_addr;

		// TODO: verify
    	if(htons(sa->sin_port) == 80 || htons(sa->sin_port) == 8080 || htons(sa->sin_port) == 443 || htons(sa->sin_port) == 8443)
		{
			LOG_DEBUG("%sis http connection",COND_EVENT);
	
	        is_http = TRUE;
	
	        item = find_connection_by_fd(sd);
	
	        if(item == NULL)
			{
				LOG_FATAL("Could not find parser for fd=%d",sd);
	            exit(0);
	        }

	        //  Force to connect the first IP address of the list
	        if(ip_list)
			{
				LOG_DEBUG("%sForce IP address",COND_EVENT);
	            sa->sin_addr.s_addr = get_first_ip(sa->sin_addr.s_addr);
	        }

	        mpsock_interface *intf = get_interface(item->sd);
	    
	        if(setsockopt(item->sd, SOL_SOCKET, SO_BINDTODEVICE, intf->name, strlen(intf->name)) < 0)
			{
	            perror("setsockopt error");
	            exit(1);
	        } 

	        // disabling the nagle algorithm; changed nothing;
	        //int tcp = 1;
	        //if (setsockopt(item->sd, IPPROTO_TCP, TCP_NODELAY, (char*)&tcp, sizeof(int))) {
	        //    perror("nagle's algorithm error");
	        //    exit(1);
	        //}

	        http_parser *parser = (http_parser*) malloc(sizeof(http_parser));
	        parser->fd = item->fd; // renamed from master_fd
	        parser->sd = item->sd; // renamed from fd
			parser->chunk_size = initial_chunk_size;
	        init_mhttp(parser);
	        parser->parser_status = PARSER_INIT;
			parser->used_port = htons(sa->sin_port);

	        // Registering the parser 
	        item->parser = parser;

	        mpsock_data_pool *pool = create_data_pool(parser);

			// TODO: verify
	        //int err;
			//if((err = pthread_create(&(pool->pid), NULL, &run_thread, (void*) pool)) != 0)
			//{
	        //    perror("pthread_create() error");
	        //    exit(0);
	        //}
			//LOG_INFO("%spid=%d",RESULT_EVENT,pool->pid);
	
			//pool->is_thread_running = TRUE;
	    }
		else
		{
			LOG_INFO("%sconnect(fd=%d) --> NO mhttp",FUN_EVENT,sd);
		}
	
	    return (is_http) ? f_connect(item->sd, serv_addr, addrlen) : f_connect(sd, serv_addr, addrlen);
	}
	*/
}

size_t write(int fd, const char* buf, size_t count)
{
	// TODO: what if this is not http?
	if(PASS_THROUGH)
	{
		LOG_INFO("%swrite(fd=%d)",FUN_EVENT,fd);
		return f_write(fd,buf,count);
	}

	if(!is_mpsocket(fd))
	{
		// not an mpsocket -> normal write
		LOG_DEBUG("%snormal write(fd=%d)",FUN_EVENT,fd);
		return f_write(fd,buf,count);
	}

	LOG_DEBUG("%shooked write(fd=%d)",FUN_EVENT,fd);

	// TODO: verify
	// extract request
	size_t req_size = count-2;
   	char *request = (char*)malloc(req_size);
   	memcpy(request, buf, req_size);

	// find pool and set request
   	mpsock_pool *pool = find_mpsocket(fd)->pool;

	if(USE_ASSERTS) assert(pool->pos_buffer_read == 0);
	if(USE_ASSERTS) assert(pool->pos_buffer_save == 0);
	if(USE_ASSERTS) assert(pool->current_response_size == 0);
	if(USE_ASSERTS) assert(!pool->is_response_created);
	if(USE_ASSERTS) assert(pool->next_start_byte == 0);
	if(USE_ASSERTS) assert(pool->req_start.tv_sec == 0);

	// set request
	pool->current_request = request;
	pool->current_request_size = req_size;

	// TODO: verify
	//mpsock_connection *conn = get_first_connection(pool);
	mpsock_connection *conn = get_best_connection(pool);

	if(USE_ASSERTS) assert(conn->is_used == FALSE);
	if(USE_ASSERTS) assert(conn->pool == pool);

	// start first connection
	gettimeofday(&(pool->req_start),NULL);
	start_connection(conn);

	// TODO: verify
	return count;
    //return f_write(p->sd, tmp, strlen(tmp)); // returning bytes must be the size of the original data
	/*
	else
	{
		if(no_connection(fd))
		{
			// no mpsock -> pass through
			LOG_INFO("%sno connection for write request fd=%d -> use normal write instead",COND_EVENT,fd);
			return f_write(fd, buf, count);
		}

		// Assuming this is an HTTP request from the application.
    	//char tmp[HTTP_MAX_HEADER_SIZE];
		//size_t req_size = strlen(buf);
		//LOG_INFO("%scount=%d, req_size=%d",FUN_EVENT,count,req_size);

		// TODO: better
		size_t req_size = count-2;
    	char *request = (char*)malloc(req_size);
		//memset(tmp, 0, HTTP_MAX_HEADER_SIZE);
    	memcpy(request, buf, req_size);

		// find parser and create request
    	http_parser *p = find_connection_by_fd(fd)->parser;
		p->original_request = request;
		p->original_request_size = req_size;

		LOG_DEBUG("%s==================================\n%s",FUN_EVENT,request);

		// convert to mhttp request
		char *tmp = (char*)malloc(HTTP_MAX_HEADER_SIZE);
		memset(tmp,0,HTTP_MAX_HEADER_SIZE);
		memcpy(tmp,buf,count);
    	parse_message(p, tmp, count);

		// if collector thread is not running (because this is a follow up request) -> start it again
		mpsock_data_pool *pool = find_data_pool(fd);
		if(!pool->is_thread_running)
		{
			int err;
			if((err = pthread_create(&(pool->pid), NULL, &run_thread, (void*) pool)) != 0)
			{
    	        perror("pthread_create() error");
    	        exit(0);
    	    }
			pool->is_thread_running = TRUE;
		}
		//LOG_INFO("%swrite request with parser->sd=%d, parser->status=%d:\n===================================================\n%s",RESULT_EVENT,p->sd,p->parser_status,request);
	
	    return f_write(p->sd, tmp, strlen(tmp)); // returning bytes must be the size of the original data
	}
	*/
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
	LOG_DEBUG("%sselect(nfds)",FUN_EVENT);
	if(PASS_THROUGH)
	{
		return f_select(nfds,readfds,writefds,exceptfds,timeout);
	}

	// check if there are fake descriptors to take care of
	if(HASH_COUNT(mpsock_socket_table) == 0)
	{
		// no fake sockets -> return normal function
		return f_select(nfds,readfds,writefds,exceptfds,timeout);
	}

	LOG_DEBUG("%shooked select(nfds)",FUN_EVENT);

	// keep track of passed time
	struct timeval start_exec, cur_time, diff_time;
	gettimeofday(&start_exec,NULL);

	// check if one of our fake descriptors is ready to read -> and if not remove it from given fd_set
	int num_fake_sockets_not_ready = 0;
	mpsock_socket *tracked_sockets[HASH_COUNT(mpsock_socket_table)];
	mpsock_socket *current_sock;

	// TODO: why is HASH_ITER not working here????????????
	for(current_sock = mpsock_socket_table; current_sock != NULL; current_sock = current_sock->hh.next)
	{
		// TODO: also check other fd_set lists!
		// check if main socket is in this list
		if(readfds != NULL && current_sock->m_sd < nfds)
		{
			if(FD_ISSET(current_sock->m_sd,readfds))
			{
				// unset our main socket descriptor from the set
				FD_CLR(current_sock->m_sd,readfds);
				tracked_sockets[num_fake_sockets_not_ready] = current_sock;
				num_fake_sockets_not_ready++;
			}
		}
	}

	int ret = 0;

	if(num_fake_sockets_not_ready)
	{
		// we modified the set_list -> now we have to keep track of changes ourselves
		struct timeval sleep, time_result;
		sleep.tv_sec = 0;
		sleep.tv_usec = 1; //1000; // TODO: find optimal value
		gettimeofday(&cur_time,NULL);
		timeval_subtract(&diff_time,&cur_time,&start_exec);
		int i;
		int timeout_not_passed = 1;

		while(ret == 0 && timeout_not_passed)
		{
			// check normal sockets
			// TODO: weird behaviour here
			ret = f_select(nfds,readfds,writefds,exceptfds,&sleep);

			// check fake sockets
			for(i = 0; i<num_fake_sockets_not_ready; i++)
			{
				if(!DO_THREAD_ADVANCE_POINTER)
				{
					// advance save pointers of this socket
					advance_socket_save_pointer(tracked_sockets[i]);
				}

				if(socket_bytes_ready(tracked_sockets[i]))
				{
					// this pool has something to read now!
					ret++;
					FD_SET(tracked_sockets[i]->m_sd,readfds);
				}
			}

			// update times
			gettimeofday(&cur_time,NULL);
			timeval_subtract(&diff_time,&cur_time,&start_exec);

			// get difference between timout and passed time
			timeout_not_passed = timeval_subtract(&time_result,&diff_time,timeout);
			LOG_DEBUG("%sdiff_time=%zu s",RESULT_EVENT,diff_time.tv_sec);
		}
	}
	else
	{
		// we did not fiddle with anything -> make normal call
		ret = f_select(nfds,readfds,writefds,exceptfds,timeout);
	}

	LOG_DEBUG("%sselect() returned: %d",FUN_EVENT,ret);
	return ret;
	/*
	if(PASS_THROUGH)
	{
		int ret = f_select(nfds,readfds,writefds,exceptfds,timeout);
		LOG_DEBUG("%sselect() returned: %d",FUN_EVENT,ret);
		return ret;
	}

	// keep track of passed time
	struct timeval start_exec, cur_time, diff_time;
	gettimeofday(&start_exec,NULL);

	// check if one of our fake descriptors is ready to read -> and if not remove it from given fd_set
	int num_fake_sockets_not_ready = 0;
	mpsock_data_pool *tracked_pools[HASH_COUNT(data_pools)];
	mpsock_data_pool *pool;
	for(pool=data_pools; pool != NULL; pool=pool->hh.next)
	{
		// TODO: also check other fd_set lists!
		if(readfds != NULL && pool->fd < nfds)
		{
			if(FD_ISSET(pool->fd,readfds))
			{
				// check if our fake descriptor is ready to read
				if(pool->pos_buffer_save == pool->pos_buffer_read)
				{
					// no new data -> unset fd
					FD_CLR(pool->fd,readfds);
					tracked_pools[num_fake_sockets_not_ready] = pool;
					num_fake_sockets_not_ready++;
				}
			}
		}
	}

	int ret = 0;

	if(num_fake_sockets_not_ready)
	{
		// we modified the set_list -> now we have to keep track of changes ourselves
		struct timeval sleep, time_result;
		sleep.tv_sec = 0;
		sleep.tv_usec = 1000;//1000; // TODO: find optimal value
		gettimeofday(&cur_time,NULL);
		timeval_subtract(&diff_time,&cur_time,&start_exec);
		int i;
		int timeout_not_passed = 1;

		while(ret == 0 && timeout_not_passed)
		{
			ret = f_select(nfds,readfds,writefds,exceptfds,&sleep);
			for(i = 0; i<num_fake_sockets_not_ready; i++)
			{
				if(tracked_pools[i]->pos_buffer_save > tracked_pools[i]->pos_buffer_read)
				{
					// this pool has something to read now!
					ret++;
					FD_SET(tracked_pools[i]->fd,readfds);
				}
			}

			// update times
			gettimeofday(&cur_time,NULL);
			timeval_subtract(&diff_time,&cur_time,&start_exec);

			// get difference between timout and passed time
			timeout_not_passed = timeval_subtract(&time_result,&diff_time,timeout);
			LOG_DEBUG("%sdiff_time=%d s",RESULT_EVENT,diff_time.tv_sec);
		}
	}
	else
	{
		// we did not fiddle with anything -> make normal call
		ret = f_select(nfds,readfds,writefds,exceptfds,timeout);
	}

	LOG_DEBUG("%sselect() returned: %d",FUN_EVENT,ret);
	return ret;
	*/
}

int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout, const sigset_t *sigmask)
{
	LOG_INFO("%spselect()",FUN_EVENT);
	if(PASS_THROUGH)
	{
		return f_pselect(nfds, readfds, writefds, exceptfds, timeout, sigmask);
	}

	return f_pselect(nfds, readfds, writefds, exceptfds, timeout, sigmask);
}

ssize_t read(int fd, char* buf, ssize_t count)
{
	LOG_DEBUG("%sread(fd=%d, count=%zu)",FUN_EVENT,fd,count);
	
	if(PASS_THROUGH)
	{
		LOG_DEBUG("%sread(fd=%d, count=%zu)",FUN_EVENT,fd,count);
		return f_read(fd,buf,count);
	}

	if(!is_mpsocket(fd))
	{
		// not an mpsocket -> normal read
		LOG_DEBUG("%snormal read(fd=%d)",FUN_EVENT,fd);
		return f_read(fd,buf,count);
	}

	mpsock_socket *sock = find_mpsocket(fd);
	if(USE_ASSERTS) assert(sock != NULL);

	if(sock->pool->is_broken) return -1;

	if(!DO_THREAD_ADVANCE_POINTER)
	{
		// advance save pointers of this socket
		advance_socket_save_pointer(sock);
	}

	//if(USE_ASSERTS) assert(sock->pool->pos_buffer_read < sock->pool->pos_buffer_save);
	//if(USE_ASSERTS) assert(sock->pool->pos_buffer_save > 0);

	// get data from pool
	ssize_t ret = read_data_from_socket(sock,buf,count,0);
	LOG_DEBUG("%shooked read returned: %zu",RESULT_EVENT,ret);
	return ret;
}

ssize_t send(int s, const void *msg, size_t len, int flags)
{
	LOG_DEBUG("%ssend(sd=%d, flags=%d)",FUN_EVENT,s,flags);

	if(PASS_THROUGH)
	{
		return f_send(s, msg, len, flags);
	}

	if(!is_mpsocket(s))
	{
		// not an mpsocket
		return f_send(s, msg, len, flags); 
	}

	// TODO: support other flags
	if(USE_ASSERTS) assert(flags == 0);

    return write(s, msg, len);
}

ssize_t recv(int fd, void *buf, size_t count, int flags)
{
	// TODO: handle non-blocking sockets!
	LOG_DEBUG("%srecv(fd=%d, count=%zu, flags=%d)",FUN_EVENT,fd,count,flags);
	
	if(PASS_THROUGH)
	{
		return f_recv(fd,buf,count,flags);
	}

	if(!is_mpsocket(fd))
	{
		// not an mpsocket
		return f_recv(fd,buf,count,flags); 
	}

	mpsock_socket *sock = find_mpsocket(fd);

	// blocking socket -> wait for data in pool
	fd_set sock_set;
	FD_ZERO(&sock_set);
	FD_SET(fd,&sock_set);
	struct timeval tv;
	tv.tv_usec = 0;
	tv.tv_sec = 0;

	// call our hook, to handle fake socket
	select(fd+1,&sock_set,NULL,NULL,&tv);

	// TODO: verify
	return read_data_from_socket(sock,buf,count,flags);

	/*
	if((flags & MSG_PEEK) == MSG_PEEK)
	{
		LOG_DEBUG("%sMSG_PEEK detected",COND_EVENT);
        
        int gap = 0;

        while(gap == 0)
		{
            mpsock_data_pool *pool = find_data_pool(fd);

            if(pool == NULL)
			{
                perror("pool is null");
                exit(0);
            }   

            gap = pool->pos_buffer_save - pool->pos_buffer_read;
			LOG_DEBUG("%sgap = %d",FUN_EVENT,gap);

            if(gap > 0)
			{
                gap = (count >= gap) ? gap : count;
                memcpy(buf, pool->buffer+pool->pos_buffer_read, gap);
            }
        } 

        return gap;
    }
	else
	{
		LOG_INFO("%sNo MSG_PEEK -> call normal read()",COND_EVENT);
        return read(fd, buf, count);    
    }
	*/

	/*
    if((flags & MSG_PEEK) == MSG_PEEK)
	{
		LOG_DEBUG("%sMSG_PEEK detected",COND_EVENT);
		if(no_connection(fd))
		{
            return f_read(fd, buf, count);
        }
		
        
        int gap = 0;

        while(gap == 0)
		{
            mpsock_data_pool *pool = find_data_pool(fd);

            if(pool == NULL)
			{
                perror("pool is null");
                exit(0);
            }   

            gap = pool->pos_buffer_save - pool->pos_buffer_read;
			LOG_DEBUG("%sgap = %d",FUN_EVENT,gap);

            if(gap > 0)
			{
                gap = (count >= gap) ? gap : count;
                memcpy(buf, pool->buffer+pool->pos_buffer_read, gap);
            }
        } 

        return gap;
    }
	else
	{
		LOG_INFO("%sNo MSG_PEEK -> call normal read()",COND_EVENT);
        return read(fd, buf, count);    
    }
	*/
}

int close(int fd)
{
	LOG_INFO("%sclose(fd=%d)",FUN_EVENT,fd);
	if(PASS_THROUGH)
	{
		return f_close(fd);
	}

	if(!is_mpsocket(fd))
	{
		// not an mpsocket
		return f_close(fd);
	}

	// TODO
	mpsock_socket *sock = find_mpsocket(fd);
	free_socket(sock);

	return 0;

	/*
	// TODO: verify
	LOG_INFO("%sclose(fd=%d)",FUN_EVENT,fd);
	if(PASS_THROUGH)
	{
		return f_close(fd);
	}

	mpsock_data_pool *pool = find_data_pool(fd);
	if(pool != NULL)
	{
		// wait until pool thread finished
		while(pool->is_thread_running) LOG_FINE("%swait",COND_EVENT);
		free_data_pool(pool);
		LOG_INFO("%sFreed pool",RESULT_EVENT);
	}
    return f_close(fd);
	*/
}

struct hostent *gethostbyname(const char *name)
{
	LOG_INFO("%sgethostbyname(%s)",FUN_EVENT,name);
	if(PASS_THROUGH)
	{
		return f_gethostbyname(name);
	}

    struct hostent *hp = f_gethostbyname(name);

    int i;
    unsigned long u_addr_list[hp->h_length];
    memset(&u_addr_list, 0, sizeof(unsigned long) * hp->h_length);

	for (i = 0 ; i <= hp->h_length ; ++i) {
        u_addr_list[i] = ntohl(((struct in_addr *)(hp->h_addr_list[i]))->s_addr);
    }

    create_addr(ip_list, name, u_addr_list, hp->h_length);

    return hp;
}

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
	LOG_INFO("%sgetaddrinfo()",FUN_EVENT);
	if(PASS_THROUGH)
	{
		return f_getaddrinfo(node, service, hints, res);
	}
    int ret = f_getaddrinfo(node, service, hints, res);
    create_addr_from_addrinfo(ip_list, node, res[0]);

    return ret;
}

int bind(int fd, const struct sockaddr *address, socklen_t len)
{
	LOG_INFO("%sbind(fd=%d)",FUN_EVENT,fd);
	if(PASS_THROUGH)
	{
		return f_bind(fd, address, len);
	}

	return f_bind(fd, address, len);
}

int getpeername(int fd, struct sockaddr *address, socklen_t *len)
{
	return f_getpeername(fd, address, len);

	/*
	LOG_INFO("%sgetpeername(fd=%d)",FUN_EVENT,fd);
	if(PASS_THROUGH)
	{
		return f_getpeername(fd, address, len);
	}

	if(no_connection(fd))
	{
		return f_getpeername(fd, address, len); 
	}

	// TODO: handle properly
	http_connection *con = find_connection_by_fd(fd);

	return f_getpeername(con->sd, address, len);
	*/
}

int shutdown(int fd, int how)
{
	LOG_INFO("%sshutdown(fd=%d)",FUN_EVENT,fd);
	if(PASS_THROUGH)
	{
		return f_shutdown(fd, how);
	}

	return f_shutdown(fd, how);
}

int setsockopt(int fd, int level, int option_name, const void *option_value, socklen_t option_len)
{
	LOG_DEBUG("%ssetsockopt(fd=%d, option=%d)",FUN_EVENT,fd,option_name);
	if(PASS_THROUGH)
	{
		return f_setsockopt(fd,level,option_name,option_value,option_len);
	}

	return f_setsockopt(fd,level,option_name,option_value,option_len);
}

int getsockopt(int fd, int level, int option_name, void *option_value, socklen_t *option_len)
{
	LOG_INFO("%sgetsockopt(fd=%d)",FUN_EVENT,fd);
	if(PASS_THROUGH)
	{
		return f_getsockopt(fd,level,option_name,option_value,option_len);
	}

	return f_getsockopt(fd,level,option_name,option_value,option_len);
}

int accept(int fd, struct sockaddr *address, socklen_t *address_len)
{
	LOG_INFO("%saccept(fd=%d)",FUN_EVENT,fd);
	if(PASS_THROUGH)
	{
		return f_accept(fd,address,address_len);
	}

	return f_accept(fd,address,address_len);
}

int getsockname(int fd, struct sockaddr *address, socklen_t *address_len)
{
	LOG_INFO("%sgetsockname(fd=%d)",FUN_EVENT,fd);
	if(PASS_THROUGH)
	{
		return f_getsockname(fd,address,address_len);
	}

	return f_getsockname(fd,address,address_len);
}

int listen(int fd, int backlog)
{
	LOG_INFO("%slisten(fd=%d)",FUN_EVENT,fd);
	if(PASS_THROUGH)
	{
		return f_listen(fd,backlog);
	}

	return f_listen(fd,backlog);
}

int socketpair(int domain, int type, int protocol, int socket_vector[2])
{
	LOG_INFO("%ssocketpair(domain=%d, type=%d)",FUN_EVENT,domain,type);
	if(PASS_THROUGH)
	{
		return f_socketpair(domain,type,protocol,socket_vector);
	}

	return f_socketpair(domain,type,protocol,socket_vector);
}

ssize_t recvfrom(int fd, void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_len)
{
	LOG_INFO("%srecvfrom(fd=%d)",FUN_EVENT,fd);
	if(PASS_THROUGH)
	{
		return f_recvfrom(fd,buffer,length,flags,address,address_len);
	}

	return f_recvfrom(fd,buffer,length,flags,address,address_len);
}

ssize_t recvmsg(int fd, struct msghdr *message, int flags)
{
	LOG_INFO("%srecvmsg(fd=%d)",FUN_EVENT,fd);
	if(PASS_THROUGH)
	{
		return f_recvmsg(fd,message,flags);
	}

	return f_recvmsg(fd,message,flags);
}

ssize_t sendmsg(int fd, const struct msghdr *message, int flags)
{
	LOG_INFO("%ssendmsg(fd=%d)",FUN_EVENT,fd);
	if(PASS_THROUGH)
	{
		return f_sendmsg(fd,message,flags);
	}

	return f_sendmsg(fd,message,flags);
}

ssize_t sendto(int fd, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len)
{
	LOG_INFO("%ssendto(fd=%d)",FUN_EVENT,fd);
	if(PASS_THROUGH)
	{
		return f_sendto(fd,message,length,flags,dest_addr,dest_len);
	}

	return f_sendto(fd,message,length,flags,dest_addr,dest_len);
}
