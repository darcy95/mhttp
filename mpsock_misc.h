/**
 * Multipath Multi-source HTTP (mHTTP)
 *
 * Developers: Juhoon Kim (kimjuhoon@gmail.com), Karl Fischer
 *
 */

#ifndef __MPSOCK_MISC_H__
#define __MPSOCK_MISC_H__

#include "libmpsocket.h"

void print_time(const char* txt); 
void print_msg(const char* pre, int socknum, const char* buf, size_t size);
void print_addrinfo(const struct addrinfo *ai);
void usage();
char *long_to_dotted_ip(unsigned long ip);

#endif
