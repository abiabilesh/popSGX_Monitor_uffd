#ifndef _COMPEL_HANDLER_H
#define _COMPEL_HANDLER_H

#include <compel/log.h>
#include <compel/infect-rpc.h>
#include <compel/infect-util.h>

#include "parasite.h"

#define PARASITE_CMD_GET_STDIN_FD PARASITE_USER_CMDS
#define PARASITE_CMD_GET_STDOUT_FD PARASITE_USER_CMDS + 1
#define PARASITE_CMD_GET_STDERR_FD PARASITE_USER_CMDS + 2
#define PARASITE_CMD_GET_STDUFLT_FD PARASITE_USER_CMDS + 3

typedef struct compel_handle
{
    struct infect_ctx *ctl;
    int status;
    pid_t victim_pid;
    bool is_intialized;
}compel_handle;

//Compel Intializer
int compel_setup(pid_t pid);
//Compel Destructor
int compel_destruct(void);
//Compel functionalities
int compel_stealFd(int cmd, int *stolen_fd);

#endif
