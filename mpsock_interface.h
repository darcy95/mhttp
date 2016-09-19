#ifndef __MPSOCK_INTERFACE_H__
#define __MPSOCK_INTERFACE_H__

#include "libmpsocket.h"

typedef struct mpsock_interface
{
    int if_id;              // key, start from 0 and incremental identifier
    char name[IFNAMSIZ];    // Interface name. E.g., "eth0", "wlan0"
    //int in_use;             // TRUE or FALSE
    //struct mpsock_connection *used_by;	// connection that uses this interface
    int num_used;
	UT_hash_handle hh;
} mpsock_interface;

// hashtable from libmpsocket.c
extern mpsock_interface *mpsock_interface_table;

/*
 * get available interfaces through given socket and store them in hash_table
 */
void collect_interfaces(int sock);

/*
 * insert given list of interfaces into hash_table
 */
void insert_interfaces(const char* intf_list);

/*
 * reset the used flags of all interfaces
 */
void reset_interfaces();

/*
 * get an interface for the given connection
 */
//mpsock_interface *get_interface(struct mpsock_connection *connection);

/*
mpsock_interface *intfs = NULL;

void collect_interfaces(int sock) {
	LOG_DEBUG("collect_interfaces(%d)\n",sock);
    struct ifconf ifconf;
    struct ifreq ifreq[MAXINTERFACES];
    int interfaces;
    int i;

    if (sock < 0) {
        perror("Socket error");
        exit(1);
    }

    // Point ifconf's ifc_buf to our array of interface ifreqs.
    ifconf.ifc_buf = (char *) ifreq;

    // Set ifconf's ifc_len to the length of our array of interface ifreqs.
    ifconf.ifc_len = sizeof ifreq;
    
    //  Populate ifconf.ifc_buf (ifreq) with a list of interface names and addresses.
    if (ioctl(sock, SIOCGIFCONF, &ifconf) == -1) {
        perror("ioctl error");
        exit(1);
    }
        
    // Divide the length of the interface list by the size of each entry.
    // This gives us the number of interfaces on the system.
    interfaces = ifconf.ifc_len / sizeof(ifreq[0]);
    
    for (i = 0 ; i < interfaces ; i++) {
        char ip[INET_ADDRSTRLEN];
        struct sockaddr_in *address = (struct sockaddr_in *) &ifreq[i].ifr_addr;
        
        // Convert the binary IP address into a readable string.
        if (!inet_ntop(AF_INET, &address->sin_addr, ip, sizeof(ip))) {
            perror("inet_ntop error");
            exit(1);
        } 

        mpsock_interface *intf = (mpsock_interface*) malloc(sizeof(mpsock_interface));
        memset(intf, 0, sizeof(mpsock_interface));

        intf->if_id = i;
        strcpy(intf->name, ifreq[i].ifr_name);

        // need better way to filter out unuseable interfaces such as "virbr0", "lo", or "vmnet".
        if (strstr(intf->name, "wlan") != NULL || strstr(intf->name, "eth") != NULL || strstr(intf->name, "lte") != NULL) {
            intf->in_use = FALSE;
            intf->used_by = -1;
            HASH_ADD_INT(intfs, if_id, intf);
        } 
    }
}

void insert_interfaces(const char* intf_list) {
	LOG_DEBUG("%sinsert_interfaces(%s)",FUN_EVENT,intf_list);
    int i = 0;
    int len = strlen(intf_list);
    char tmp[len];
    memset(tmp, 0, len);
    strcat(tmp, intf_list);

    char* token = strtok(tmp, ",");

    while (token) {
        mpsock_interface *intf = (mpsock_interface*) malloc(sizeof(mpsock_interface));
        memset(intf, 0, sizeof(mpsock_interface));
        strcpy(intf->name, token);
        intf->if_id = i++;
        intf->in_use = FALSE;
        intf->used_by = -1;
        HASH_ADD_INT(intfs, if_id, intf);

		LOG_DEBUG("%sinterface#%d: %s", RESULT_EVENT, intf->if_id, intf->name);

        token = strtok(NULL, ",");
    }
}

mpsock_interface *get_interface(int sock) {
    mpsock_interface *e;

    for (e = intfs ; e != NULL ; e = e->hh.next) {
        if (e->in_use == FALSE) {
			LOG_DEBUG("%swill use interface#%d: %s", COND_EVENT, e->if_id, e->name);
            e->in_use = TRUE;
            e->used_by = sock;
            return e;
        }
    }

    // In case that all interfaces are in use. Return the first interface again.
    LOG_DEBUG("\n\n[FYI] all interfaces are in use. Thus, mHTTP establishes a new connection over %s, again.\n\n", intfs->name);
    return intfs;
}
*/

#endif
