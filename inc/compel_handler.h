#ifndef _COMPEL_HANDLER_H
#define _COMPEL_HANDLER_H

#include <compel/log.h>
#include <compel/infect-rpc.h>
#include <compel/infect-util.h>

#include "parasite.h"

#define PARASITE_CMD_GET_STDIN_FD         PARASITE_USER_CMDS
#define PARASITE_CMD_GET_STDOUT_FD        PARASITE_USER_CMDS + 1
#define PARASITE_CMD_GET_STDERR_FD        PARASITE_USER_CMDS + 2
#define PARASITE_CMD_GET_STDUFLT_FD       PARASITE_USER_CMDS + 3
#define PARASITE_CMD_SET_MADVISE_NO_NEED  PARASITE_USER_CMDS + 4

typedef struct compel_handle
{
    struct infect_ctx *ctl;
    int status;
    pid_t victim_pid;
    bool is_intialized;
}compel_handle;

//Compel Intializer
static int compel_setup(pid_t pid);
//Compel Destructor
static int compel_destruct(void);
//Compel functionalities
static int compel_stealFd(int cmd, int *stolen_fd);

//User functionalities
int compel_victim_stealFd(pid_t victim_pid, int cmd, int *fd, uint64_t shared_addr, uint64_t no_of_pages);
int compel_victim_madvise(pid_t victim_pid, int cmd, uint64_t page_addr);

#endif
