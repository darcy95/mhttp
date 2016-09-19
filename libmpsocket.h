#ifndef __LIBMPSOCKET_H__
#define __LIBMPSOCKET_H__

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/netdevice.h>
#include <signal.h>
//#include <netinet/tcp.h>
//#include <unistd.h>
#include <assert.h>
#include <pthread.h>

#ifdef __FreeBSD__
#include <sys/syslimits.h>
#else
#include <limits.h>
#endif

// constants
#include "mpsock_def.h"

// hash table
#include "uthash.h"

// start time
struct timeval global_start_ts;

/*
 * Renamed hooked function wrappers
 */
// sys/socket.h interface
int f_socket(int, int, int);
int f_connect(int, const struct sockaddr *, socklen_t);
int f_send(int, const void *, size_t, int);
int f_recv(int, void *, size_t, int);
int f_bind(int, const struct sockaddr *, socklen_t);
int f_getpeername(int, struct sockaddr *, socklen_t *);
int f_shutdown(int, int);
int f_setsockopt(int, int, int, const void *, socklen_t);
int f_getsockopt(int, int, int, void *, socklen_t *);
int f_accept(int, struct sockaddr *, socklen_t *);
int f_getsockname(int, struct sockaddr *, socklen_t *);
int f_listen(int, int);
int f_socketpair(int, int, int, int socket_vector[2]);
ssize_t f_recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *);
ssize_t f_recvmsg(int, struct msghdr *, int);
ssize_t f_sendmsg(int, const struct msghdr *, int);
ssize_t f_sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t);

// netdb.h interface
struct hostent *f_gethostbyname(const char *);
int f_getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **);

// unistd.h interface
size_t f_write(int, const void *, size_t);
ssize_t f_read(int, void *, size_t);
int f_close(int);
int f_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
int f_pselect(int, fd_set *, fd_set *, fd_set *, const struct timespec *, const sigset_t *);

#endif
