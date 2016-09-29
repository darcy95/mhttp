/**
 * Multipath Multi-source HTTP (mHTTP)
 *
 * Developers: Juhoon Kim (kimjuhoon@gmail.com), Karl Fischer
 *
 */

#ifndef SSOCK_LISTEN_BACKLOG
#define SSOCK_LISTEN_BACKLOG SOMAXCONN
#endif

typedef struct
{
	int fd;
    unsigned long ip;
	unsigned short port;
    char host[MAX_LENGTH_DOMAIN];
} tcpsock;

int tcpconnect(tcpsock *sock)
{
    struct sockaddr_in pin;
    struct in_addr addr;

   	memset(&pin, 0, sizeof(pin));

    pin.sin_family = PF_INET;
    pin.sin_addr.s_addr = sock->ip;
    pin.sin_port = htons(80);

	return f_connect(sock->fd, (struct sockaddr *) &pin, sizeof(struct sockaddr_in));
}

void tcpclose(tcpsock *sock)
{
	close(sock->fd);
}

void freesock(tcpsock *sock)
{
	free(sock);
}

tcpsock *newtcp(const char *host, unsigned long ip, unsigned short port)
{
	tcpsock *s = malloc(sizeof(tcpsock));
    memset(s, 0, sizeof(tcpsock));

	s->fd = f_socket(AF_INET, SOCK_STREAM, 0);
    s->ip = ip;
    s->port = port;
    strncpy(s->host, host, sizeof(host));

	return s;
}

int tcpsend(tcpsock *sock, void *data, size_t len)
{
	size_t sent = 0;
	size_t cnt;
	uint8_t *seek;

	while (sent < len) {
		seek = (uint8_t *) data + sent;
        cnt = f_write(sock->fd, seek, sizeof(uint8_t) * (len - sent));
		sent += cnt;

		if (cnt < 0)
			return cnt;
	}

	return sent;
}

//For receiving information which you already know the size of.
int tcprecv(tcpsock *sock, void *buffer, size_t len)
{
	int recvd;
	size_t total = 0;

	while (total < len) {
        recvd = read(sock->fd, (uint8_t *) buffer + total, len - total);

		if (recvd == -1)
			return -1;
		else
			total += recvd;
	}

	return total;
}

//Takes whatever information is waiting on your socket (up to 'sz' bytes)
//and puts it into the memory pointed to by 'buffer'. As the code shows,
//it works just like the 'recv(//)' system call
int tcprecv_next(tcpsock *sock, void *buffer, size_t len)
{
    return f_read(sock->fd, (uint8_t *) buffer, len);
}

tcpsock * tcpaccept(tcpsock *listener)
{
	socklen_t len;
	struct sockaddr_storage sockaddr_str;
	len = sizeof(struct sockaddr_storage);
	tcpsock *s = malloc(sizeof(tcpsock));
	int accepted = accept(listener->fd, (struct sockaddr *)&sockaddr_str, &len);

	if (accepted == -1)
		return NULL;

	s->fd = accepted;

	return s;
}
