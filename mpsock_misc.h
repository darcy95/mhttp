/**
 * Multipath Multi-source HTTP (mHTTP)
 *
 * Developers: Juhoon Kim (kimjuhoon@gmail.com), Karl Fischer
 *
 */

void print_time(const char* txt) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    printf("\n[%s] %ld.%06ld\n", txt, tv.tv_sec, tv.tv_usec);
}

void print_msg(const char* pre, int socknum, const char* buf, size_t size)
{
    printf("\n\n+-----------------------------------------------+\n");
    printf("%s Socknum: %d, Length: %d\n", pre, socknum, (int) size);
    printf("+-----------------------------------------------+\n");

    int i;
    char* tmp = (char*) buf;
    
    for (i = 0 ; i < size ; i++)
        printf("%c", tmp[i]); 

    printf("\n+-----------------------------------------------+\n\n");
}

void print_addrinfo(const struct addrinfo *ai)
{
    const struct addrinfo *ptr;
    int i, cnt;
    cnt = 0;

    for (ptr = ai; ptr != NULL ; ptr = ptr->ai_next) {
        if (ptr->ai_family == AF_INET) {
            ++cnt;
        }
        //if (cnt == 0) { return NULL; }
    }

    for (ptr = ai, i = 0 ; ptr != NULL; ptr = ptr->ai_next, i++) {
        if (ptr->ai_family == AF_INET) {
            const struct sockaddr_in *sin = (const struct sockaddr_in *) ptr->ai_addr;
            printf("%d: %lu\n", i, (unsigned long) sin->sin_addr.s_addr);
        }
    }
}                                                                                                          

void usage() {
    printf("\n");
    printf("Usage: LD_PRELOAD=./libmpsocket.so CHUNK_SIZE_IN_KB=<number> CONNECTIONS=<number>  <application> <application specific options>\n");
    printf("LD_PRELOAD=./libmpsocket.so CHUNK_SIZE_IN_KB=1024 CONNECTIONS=2 wget http://www.google.com/index.html\n\n\n");
}

char *long_to_dotted_ip(unsigned long ip)
{
    struct in_addr addr;
    addr.s_addr = ip;
    char *dot_ip = inet_ntoa(addr);
    return dot_ip;
}

