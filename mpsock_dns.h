#ifndef __MPSOCK_DNS_H__
#define __MPSOCK_DNS_H__

#include "libmpsocket.h"
#include "mpsock_connection.h"

typedef struct mpsock_addr
{
    unsigned long ip;
    int inuse;
    int priority;
} mpsock_addr;

typedef struct mpsock_addrs
{
	// TODO: check if ok without const
    //const char *name;   // the key: a url or a hostname
	char *name;   		// the key: a url or a hostname
	unsigned long ip;	// the representative IP
	                    // address
	int ttl;    		// the life time of the
			        	// address mapping
    int count;          // the number of addresses                        
    mpsock_addr ipset[MAX_ADDRESSES];
	UT_hash_handle hh;	// makes this structure
				        // hashable
} mpsock_addrs;

// hashtable from libmpsocket.c
extern mpsock_addrs *mpsock_address_table;

/*
 * return number of ips in table
 */
int count_all_ips();

/*
 * return number of unused ips in table
 */
int count_unused_ips();

/*
 * lookup address of given name
 */
mpsock_addrs *lookup_by_name(const char *name);

/*
 * lookup address 
 */
mpsock_addrs *lookup_by_keyip(unsigned long ip);

/*
 * return first ip from table
 */
//unsigned long get_first_ip(unsigned long first_ip); 

/*
 * return an ip address for the given host
 */
//unsigned long get_ip(const char *host);

/*
 * create an address entry in given table
 */
void create_addr(char *ip_list, const char *host, unsigned long *ips, int cnt);

/*
 * create address...
 */
void create_addr_from_addrinfo(char *ip_list, const char *host, const struct addrinfo *ai);

/*
 * remove address...
 */
void remove_addr(mpsock_addrs *e);

/*
 * set index and ip to connection for given ip
 */
void set_index_and_ip(struct mpsock_connection *connection, unsigned long ip);

#endif
