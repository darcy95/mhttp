#include "mpsock_interface.h"
#include "mpsock_connection.h"

void collect_interfaces(int sock) 
{
	LOG_INFO("collect_interfaces(%d)\n",sock);
    struct ifconf ifconf;
    struct ifreq ifreq[MAX_INTERFACES];
    int interfaces;
    int i;

    if(sock < 0)
	{
        perror("Socket error");
        exit(1);
    }

    // Point ifconf's ifc_buf to our array of interface ifreqs.
    ifconf.ifc_buf = (char *) ifreq;

    // Set ifconf's ifc_len to the length of our array of interface ifreqs.
    ifconf.ifc_len = sizeof ifreq;
    
    //  Populate ifconf.ifc_buf (ifreq) with a list of interface names and addresses.
    if(ioctl(sock, SIOCGIFCONF, &ifconf) == -1)
	{
        perror("ioctl error");
        exit(1);
    }
        
    // Divide the length of the interface list by the size of each entry.
    // This gives us the number of interfaces on the system.
    interfaces = ifconf.ifc_len / sizeof(ifreq[0]);
    
    for(i = 0 ; i < interfaces ; i++)
	{
        char ip[INET_ADDRSTRLEN];
        struct sockaddr_in *address = (struct sockaddr_in *) &ifreq[i].ifr_addr;
        
        // Convert the binary IP address into a readable string.
        if(!inet_ntop(AF_INET, &address->sin_addr, ip, sizeof(ip)))
		{
            perror("inet_ntop error");
            exit(1);
        } 

        mpsock_interface *intf = (mpsock_interface*) malloc(sizeof(mpsock_interface));
        memset(intf, 0, sizeof(mpsock_interface));

        intf->if_id = i;
        strcpy(intf->name, ifreq[i].ifr_name);

        // need better way to filter out unuseable interfaces such as "virbr0", "lo", or "vmnet".
        if(strstr(intf->name, "wlan") != NULL || strstr(intf->name, "eth") != NULL || strstr(intf->name, "lte") != NULL)
		{
            //intf->in_use = FALSE;
            //intf->used_by = NULL;
            intf->num_used = 0;
			HASH_ADD_INT(mpsock_interface_table, if_id, intf);
        } 
    }
}

void insert_interfaces(const char* intf_list)
{
	LOG_INFO("%sinsert_interfaces(%s)",FUN_EVENT,intf_list);
    int i = 0;
    int len = strlen(intf_list);
    char tmp[len];
    memset(tmp, 0, len);
    strcat(tmp, intf_list);

    char* token = strtok(tmp, ",");

    while(token)
	{
        mpsock_interface *intf = (mpsock_interface*) malloc(sizeof(mpsock_interface));
        memset(intf, 0, sizeof(mpsock_interface));
        strcpy(intf->name, token);
        intf->if_id = i++;
        //intf->in_use = FALSE;
        //intf->used_by = NULL;
		intf->num_used = 0;
        HASH_ADD_INT(mpsock_interface_table, if_id, intf);

		LOG_DEBUG("%sinserted interface#%d: %s", RESULT_EVENT, intf->if_id, intf->name);

        token = strtok(NULL, ",");
    }
}

void reset_interfaces()
{
	// TODO: verify
	/*
	mpsock_interface *e;
	for(e = mpsock_interface_table ; e != NULL ; e = e->hh.next)
	{
		e->in_use = FALSE;
		e->used_by = NULL;
	}
	*/
}

/*
mpsock_interface *get_interface(struct mpsock_connection *connection) 
{
	mpsock_interface *first = NULL;
    mpsock_interface *e;

    for(e = mpsock_interface_table ; e != NULL ; e = e->hh.next)
	{
		if(first == NULL)
		{
			first = e;
		}

        if(e->in_use == FALSE)
		{
			LOG_DEBUG("%swill use interface#%d: %s", COND_EVENT, e->if_id, e->name);
            e->in_use = TRUE;
            e->used_by = connection;
            return e;
        }
    }

    // In case that all interfaces are in use. Return the first interface again.
    LOG_DEBUG("%sall interfaces are in use. Thus, mHTTP establishes a new connection over %s, again", RESULT_EVENT,first->name);
    return first;
}
*/
