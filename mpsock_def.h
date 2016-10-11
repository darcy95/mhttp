/**
 * Multipath Multi-source HTTP (mHTTP)
 *
 * Developers: Juhoon Kim (kimjuhoon@gmail.com), Karl Fischer
 *
 */

#ifndef __MPSOCK_DEF_H__
#define __MPSOCK_DEF_H__

//#include <stdio.h>
//#include <pthread.h>
//#include <sys/mman.h>
//#include "http_parser.h"
//#include "uthash.h"

#define MAX_LENGTH_DOMAIN 256 /* RFC 1034 */
#define MAX_LENGTH_URL 256
#define MAX_ADDRESSES 64

#define PASS_THROUGH 0

//#define HTTP_HDR_MAX_NUM_LINES 25
//#define HTTP_HDR_MAX_CHARS 100

#define TTL 255

#define PARSER_INIT 0
#define PARSER_FIRST_REQ 1
#define PARSER_FIRST_RES 2
#define PARSER_MATURE 3
#define PARSER_NEW_REQUEST 4
//#define PARSER_NOMORE_REQ 5

#define READ_POOL_COMPLETE 0
#define READ_POOL_NOTREADY -1
#define READ_POOL_SUCCESS -2
#define READ_POOL_NOCHUNK -3
#define READ_POOL_RSTZERO -4

#define HT_FIELD_GRAB 0 // get this value
#define HT_FIELD_HOST 1 // "Host: "
#define HT_FIELD_LENG 2 // "Content-Length: " with status code 200
#define HT_FIELD_ELEN 3 // "Content-Length: " with any status code except 200 and 206
#define HT_FIELD_CODE 4 // "Transer-Encoding: " with any status code
#define HT_FIELD_RANG 5 // "Content-Range: " with the status code 206
#define HT_FIELD_SKIP 6 // skip this value when parsing

#define TRUE                1
#define FALSE               0
#define CR                  '\r'
#define LF                  '\n'
#define LOWER(c)            (unsigned char)(c | 0x20)
#define IS_ALPHA(c)         (LOWER(c) >= 'a' && LOWER(c) <= 'z')
#define IS_NUM(c)           ((c) >= '0' && (c) <= '9')
#define IS_ALPHANUM(c)      (IS_ALPHA(c) || IS_NUM(c))
#define IS_HEX(c)           (IS_NUM(c) || (LOWER(c) >= 'a' && LOWER(c) <= 'f'))
#define IS_MARK(c)          ((c) == '-' || (c) == '_' || (c) == '.' || \
  (c) == '!' || (c) == '~' || (c) == '*' || (c) == '\'' || (c) == '(' || \
  (c) == ')')
#define IS_USERINFO_CHAR(c) (IS_ALPHANUM(c) || IS_MARK(c) || (c) == '%' || \
  (c) == ';' || (c) == ':' || (c) == '&' || (c) == '=' || (c) == '+' || \
  (c) == '$' || (c) == ',')

// possible logging levels
#define FINE_LOG_LVL	6
#define DEBUG_LOG_LVL	5
#define INFO_LOG_LVL	4
#define WARN_LOG_LVL	3
#define ERROR_LOG_LVL	2
#define FATAL_LOG_LVL	1
#define NO_LOGGING		0

// current logging level
#ifdef LOG_LEVEL
#define LOG_LVL LOG_LEVEL
#else
#define LOG_LVL WARN_LOG_LVL
#endif

// tags for logging
#define FUN_EVENT			"\n:: [CALL]\t"
#define COND_EVENT			"\n:: [COND]\t"
#define RESULT_EVENT		"\n:: [RESULT]\t"
#define NEW_THREAD_EVENT	"\n:: [THREAD]\t"
#define END_THREAD_EVENT	"\n:: [THREAD]\t"
#define WARN_EVENT			"\n:: [WARN]\t"
#define FATAL_EVENT			"\n:: [FATAL]\t"
#define ERROR_EVENT			"\n:: [ERROR]\t"

// use asserts
#define USE_ASSERTS 1

// log synch time
#define DO_LOG_SYNC 0
#define DO_LOG_METRICS 0
#define DO_LOG_TRAFFIC 0

// general logging functions 
#define LOG_FINE(...)	(void)(LOG_LVL >= FINE_LOG_LVL ? printf(__VA_ARGS__) : 0)
#define LOG_DEBUG(...)	(void)(LOG_LVL >= DEBUG_LOG_LVL ? printf(__VA_ARGS__) : 0)
#define LOG_INFO(...)	(void)(LOG_LVL >= INFO_LOG_LVL ? printf(__VA_ARGS__) : 0)
#define LOG_WARN(...)	(void)(LOG_LVL >= WARN_LOG_LVL ? printf(__VA_ARGS__) : 0)
#define LOG_ERROR(...)	(void)(LOG_LVL >= ERROR_LOG_LVL ? printf(__VA_ARGS__) : 0)
#define LOG_FATAL(...)	(void)(LOG_LVL >= FATAL_LOG_LVL ? printf(__VA_ARGS__) : 0)
//#define LOG_SYNC(...)   (void)(DO_LOG_SYNC == TRUE ? printf(__VA_ARGS__) : 0)
//#define LOG_METRICS(...)   (void)(DO_LOG_METRICS == TRUE ? printf(__VA_ARGS__) : 0)

// initial chunk size (for first request)
#define INITIAL_CHUNK_SIZE 64*1024 // 12k

// maximum allowed chunk size
//#define MAXIMUM_CHUNK_SIZE 500*1024
#define MAXIMUM_CHUNK_SIZE 8*1024*1024 // 8MB
#define MINIMUM_CHUNK_SIZE 4*1024 // 4kB
//#define MAXIMUM_CHUNK_SIZE 256*1024 // 256 kB

// maximum header size (for server response header)
#define MAXIMUM_HEADER_SIZE 2*1024 // 2kB

// size of volatile buffer
#define VOLATILE_BUFFER_SIZE 10000
//#define VOLATILE_BUFFER_SIZE 1000000

// maximum number of interfaces to use
#define MAX_INTERFACES 10

// maximum requests per connection for one object (0: no limit)
#define MAX_REQ_CON 0	// per connection
#define MAX_REQ_SERV 0	// per object 
#define MAX_REQ_MPSOCKET 0 // per mpsocket - maybe later needed???

// define whether save pointer advances are done by application or by collector threads
#define DO_THREAD_ADVANCE_POINTER 1

// use the ringbuffer
#define USE_RINGBUFFER 1

// use wait for reuse
#define USE_WAIT_FOR_REUSE 0

/* SCHEDULER */
// use scheduler
#define USE_SCHEDULER 1

// initial alpha value
//#define INITIAL_ALPHA 100
#define INITIAL_ALPHA 20

// smoothing factor
#define DIST_FACTOR 0.875

// optimal bandwidth range difference
#define OPTIMAL_RANGE_DIFF 0.01 // 1%

// alpha raise factors
#define ALPHA_RAISE_BIG 2
#define ALPHA_RAISE_SMALL 1.1
#define ALPHA_LINEAR_INC 2
#define ALPHA_DECREASE 0.9

// f_select timeout in collector thread in nsec
#define WAIT_TIMEOUT 500000000 // 50ms
#define WAIT_TIMEOUT_SMALL 250000000 // 25ms

// max timeout to wait for data in sec
#define MAX_DATA_WAIT 3 // 3s

// max number of bad_performance_handling in a row
#define MAX_BREAKS_IN_ROW 50

// return value for no more requests
#define NO_MORE_REQ_FLAG -666666

// bad performance flags
#define BAD_PERF_CLOSE 10
#define BAD_PERF_KEEP 20

// read skips
#define READ_SKIPS 3

// minimum filesize to use scheduler
#define MIN_SIZE_FOR_SCHEDULER 128*1024 // 128 kB
#define MIN_SIZE_FOR_MULTIPATH 128*1024 // 128 kB
//#define MIN_SIZE_FOR_SCHEDULER 512*1024 // 512 kB
//#define MIN_SIZE_FOR_MULTIPATH 750*1024 // 750 kB

// ringbuffer size in bytes
#define MPSOCK_BUFFER_SIZE 512*1024*1024 // 512MB

// max number of allowed bytes left at EOF
#define MAX_BYTES_LEFT 512

// firefox
#define USE_FF 1

// use minimum throughput assurance
#define MINIMUM_THROUGHPUT_ASSURANCE 1

// parser hack
#define ZERO_IN_A_ROW 6

// which scheduler algorithm to run
#define DYNAMIC_ALPHA_ALGORITHM 0
#define TIME_CHUNK_ALGORITHM 1
#define BASELINE_SCHEDULER 0

/*
* 0: baseline scheduler
* 1: dynamic alpha
* 2: time slices
*/
#define SCHEDULER_ALGO_DEFAULT 1

// use max(T,T_min) (cases 1.3 and 2.3) in time chunk scheduler algorithm
#define FLAG_MAX_ALGO 0

// use min(T_1,max, T_2,max)
#define USE_MIN_T_MAX 0

// seconds to add to compare minimum assurance
#define ASSURANCE_PENALTY 0.05 // in s

// alpha_max, alpha_min
//#define ALPHA_MAX 30
#define ALPHA_MIN 10

// create a second path simultaneously to first path
#define USE_INITIAL_SECOND_PATH_DEFAULT 0

// define whether to use random interface or not
#define USE_RANDOM_INTERFACE 0

// minimum download time for multipath
#define MIN_DOWNLOAD_TIME 0 // 0.5???

// use harmonic mean
#define USE_HARMONIC_MEAN 1

// Default values
#define SCHEDULER_VERSION_DEFAULT 2 // Only relevant to time_chunk_algorithm
#define ALPHA_MAX_DEFAULT 20        // Only relevant to time_chunk_algorithm
#define PROCESSING_SKIPS_DEFAULT 0

// print default output
#define PRINT_DEFAULT_OUT 1

#endif
