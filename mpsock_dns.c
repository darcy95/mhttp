#include "mpsock_dns.h"

int count_all_ips()
{
    return (mpsock_address_table)->count;
}

int count_unused_ips()
{
	int i, t = 0;

	for(i = 0 ; i < mpsock_address_table->count ; i++)
	{
		if(mpsock_address_table->ipset[i].inuse == 0 && mpsock_address_table->ipset[i].ip > 0)
		{
			t++;
		}
	}

	return t;
}

mpsock_addrs *lookup_by_name(const char *name)
{
	LOG_DEBUG("%slookup_by_name(%s)",FUN_EVENT,name);
    mpsock_addrs *e;
	HASH_FIND_STR(mpsock_address_table, name, e);
	if(USE_ASSERTS) assert(e != NULL);
    return e;
}

mpsock_addrs *lookup_by_keyip(unsigned long ip)
{
    mpsock_addrs *e;

    for(e = mpsock_address_table; e != NULL ; e = e->hh.next)
	{
        if(e->ip == ip)
		{
            return e;
        }
    }

    return NULL;
}

void set_index_and_ip(mpsock_connection *connection, unsigned long ip)
{
	connection->adrs_index = -1;
	mpsock_addrs *e = NULL;

    for(e = mpsock_address_table; e != NULL ; e = e->hh.next)
	{
		int i;
		for(i=0; i<e->count; i++)
		{
			if(e->ipset[i].ip == ip)
			{
				LOG_DEBUG("%sIndex %d",RESULT_EVENT,i);
				connection->ip = ip;
				connection->adrs = e;
				connection->adrs_index = i;
				e->ipset[i].inuse = TRUE;
				break;
			}
		}
    }

	if(USE_ASSERTS) assert(connection->adrs_index >= 0);
	if(USE_ASSERTS) assert(connection->adrs_index < connection->adrs->count);
}

/*
unsigned long get_first_ip(unsigned long first_ip) 
{
    //int i;
    mpsock_addrs *e;
    e = lookup_by_keyip(first_ip);

    if(e == NULL) perror("Can't find the IP entry.");

    e->ipset[0].inuse = 1;
 
    return e->ipset[0].ip;
}
*/

/*
unsigned long get_ip(const char *host)
{
    int i;
    mpsock_addrs *e;
    e = lookup_by_name(host);

	//mp_addrs *tmp;
	//for(tmp = address_map; tmp != NULL; tmp=tmp->hh.next)
	//{
	//	LOG_INFO("%sMAPPING: %s",RESULT_EVENT,tmp->name);
	//}

	if(USE_ASSERTS)
	{
		assert(e != NULL);
	}

    if(e)
	{
        for(i = 0 ; i < e->count ; i++)
		{
            if(e->ipset[i].inuse == 0)
			{
                e->ipset[i].inuse = 1;
                return e->ipset[i].ip;
            }
        }
    } // error handling? if (e == NULL)

    // In case that all IP addresses are in use or there is only one IP address assigned to the host
    //printf("\n\n[FYI] all IP addresses are in use. Thus, mHTTP establishes a new connection using %s, again.\n\n", long_to_dotted_ip(e->ipset[0].ip));

    return e->ipset[0].ip;
}
*/

/*
 * insert an address inside the given table
 */
void insert_addr(char *ip_list, const char *host, unsigned long key_ip) 
{
    int i = 0;
    int len = strlen(ip_list);
    char tmp[len];
    memset(tmp, 0, len);
    strcat(tmp, ip_list);
    char* token = strtok(tmp, ",");

    mpsock_addrs *e;
    e = (mpsock_addrs*) malloc(sizeof(mpsock_addrs));
    memset(e, 0, sizeof(mpsock_addrs));

	LOG_DEBUG("%sHOST: %s",RESULT_EVENT,host);

	// TODO: check
	char *b_host = (char*)malloc(strlen(host));
	//memset(b_host,'\0',strlen(host));
	strcpy(b_host,host);
	

	LOG_DEBUG("%sb_host: %s, b_host->size=%zu, host->size=%zu",RESULT_EVENT,b_host,strlen(b_host),strlen(host));

    //e->name = host;
	e->name = b_host;
    e->ip = key_ip; // The first IP address returned from the DNS resolver.

	// TODO: fix
	//e->name[strlen(host)] = '\0';

    while(token)
	{
        e->ipset[i].ip = (unsigned long) inet_addr(token);
        e->ipset[i].inuse = 0;
        e->ipset[i].priority = 100;
        
        token = strtok(NULL, ",");
        i++;
    }

    e->count = i;
    e->ttl = TTL;
	LOG_INFO("%sfinished create_address %s",COND_EVENT,e->name);
    HASH_ADD_KEYPTR(hh, mpsock_address_table, e->name, strlen(e->name), e);
}

void create_addr(char *ip_list, const char *host, unsigned long *ips, int cnt)
{
	LOG_INFO("%screate_addr(%s)",FUN_EVENT,host);
    if(ip_list != NULL)
	{
		LOG_INFO("%sip_list != NULL",COND_EVENT);
        insert_addr(ip_list, host, ips[0]);
        return;
    }

	mpsock_addrs *e;

	// HASH_FIND(hh, address_map, &ips[0], sizeof(unsigned long), e);
    HASH_FIND_STR(mpsock_address_table, host, e);

	if(e == NULL)
	{
        //int i;
		e = (mpsock_addrs*) malloc(sizeof(mpsock_addrs));
		// TODO: check
		memset(e, '\0', sizeof(mpsock_addrs));
		char *tmp = (char*)malloc(strlen(host));
		memcpy(tmp,host,strlen(host));

        // Inserting the hostname
		//e->name = host;
        e->name = tmp;

        // Inserting the main IP address 	
        //e->ip = ips[0];

        // The number of IP addresses in the entry
        //e->count = cnt;
    
        // The rest will be stored for the later use.
		//for(i = 0 ; i < cnt ; i++)
		//{
		//	e->ipset[i].ip = ips[i];
        //    e->ipset[i].inuse = (i == 0) ? 1 : 0;
        //   e->ipset[i].priority = 100;
		//	LOG_INFO("%sadd host %zu to list",RESULT_EVENT,e->ipset[i].ip);
        //}

        // The lifetime of the address mapping. Not decided how
        // how to use this field.
		//e->ttl = TTL;

        // HASH_ADD(hh, address_map, ip, sizeof(unsigned long), e);
		LOG_DEBUG("%sadd address e->name=%s",FUN_EVENT,e->name);
		LOG_DEBUG("%sadd address strlen(e->name)=%zu",FUN_EVENT,strlen(e->name));
		LOG_DEBUG("%sadd address strlen(host)=%zu",FUN_EVENT,strlen(host));

		e->name[strlen(host)] = '\0';
        HASH_ADD_KEYPTR(hh, mpsock_address_table, e->name, strlen(e->name), e);
	}
	else
	{
		LOG_INFO("%snew DNS query -> remove old hosts",RESULT_EVENT);
	}

	// Inserting the main IP address 	
    e->ip = ips[0];

    // The number of IP addresses in the entry
    e->count = cnt;
    
    // The rest will be stored for the later use.
	int i;
	for(i = 0 ; i < cnt ; i++)
	{
		e->ipset[i].ip = ips[i];
        e->ipset[i].inuse = (i == 0) ? 1 : 0;
        e->ipset[i].priority = 100;
		LOG_INFO("%sadd host %zu to list",RESULT_EVENT,e->ipset[i].ip);
    }

    // The lifetime of the address mapping. Not decided how
    // how to use this field.
	e->ttl = TTL;	
}

void create_addr_from_addrinfo(char *ip_list, const char *host, const struct addrinfo *ai)
{
	LOG_INFO("%screate_addr_from_addrinfo(%s)",FUN_EVENT,host);
    const struct addrinfo *ptr;
    int i, cnt = 0;

    for(ptr = ai; ptr != NULL ; ptr = ptr->ai_next)
	{
        if(ptr->ai_family == AF_INET)
		{
            ++cnt;
        }
    }

    unsigned long u_addr_list[cnt];
    memset(&u_addr_list, 0, sizeof(unsigned long) * cnt);

    for(ptr = ai, i = 0 ; ptr != NULL; ptr = ptr->ai_next, i++)
	{
        if(ptr->ai_family == AF_INET)
		{
            const struct sockaddr_in *sin = (const struct sockaddr_in *) ptr->ai_addr;
            u_addr_list[i] = (unsigned long) sin->sin_addr.s_addr;
        }
    }

    create_addr(ip_list, host, u_addr_list, cnt);
}

void remove_addr(mpsock_addrs *e)
{
    HASH_DEL(mpsock_address_table, e);
    free(e);
}
