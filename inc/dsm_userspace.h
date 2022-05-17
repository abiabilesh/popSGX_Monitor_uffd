#ifndef __DSM_USERSPACE_H__
#define __DSM_USERSPACE_H__

#include "types.h"

typedef struct dsm_args{
    int host_port;
    char *remote_ip;
    int remote_port;
    int uffd;
    fault_region flt_reg;
}dsm_args;

int dsm_main(dsm_args args);
#endif
