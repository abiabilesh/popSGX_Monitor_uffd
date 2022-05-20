#ifndef __DSM_USERSPACE_H__
#define __DSM_USERSPACE_H__

#include "types.h"

#define BUFSIZE 2000

typedef struct dsm_args{
    int host_port;
    char remote_ip[BUFSIZE + 1];
    int remote_port;
    int uffd;
    fault_region flt_reg;
}dsm_args;

int dsm_main(dsm_args args);
static void address_msi_pages(uint64_t mmap_addr, uint64_t phy_addr);
#endif
