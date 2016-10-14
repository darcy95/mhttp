/**
 * Multipath Multi-source HTTP (mHTTP)
 *
 * Developers: Juhoon Kim (kimjuhoon@gmail.com), Karl Fischer
 *
 */

#ifndef __MPSOCK_TCP_H__
#define __MPSOCK_TCP_H__

#include "libmpsocket.h"

typedef struct
{
	int fd;
    unsigned long ip;
	unsigned short port;
    char host[MAX_LENGTH_DOMAIN];
} tcpsock;

int tcpconnect(tcpsock *sock);
void tcpclose(tcpsock *sock);
void freesock(tcpsock *sock);
tcpsock *newtcp(const char *host, unsigned long ip, unsigned short port);
int tcpsend(tcpsock *sock, void *data, size_t len);
int tcprecv(tcpsock *sock, void *buffer, size_t len);
int tcprecv_next(tcpsock *sock, void *buffer, size_t len);
tcpsock * tcpaccept(tcpsock *listener);

#endif
