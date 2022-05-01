#include <compel/log.h>
#include <compel/infect-rpc.h>
#include <compel/infect-util.h>

#include "parasite.h"

#define PARASITE_CMD_GET_STDIN_FD PARASITE_USER_CMDS
#define PARASITE_CMD_GET_STDOUT_FD PARASITE_USER_CMDS + 1
#define PARASITE_CMD_GET_STDERR_FD PARASITE_USER_CMDS + 2
#define PARASITE_CMD_GET_STDUFLT_FD PARASITE_USER_CMDS + 3


int compel_setup(int pid);
int compel_destruct(int pid);
int steal_fd(int cmd, int *stolen_fd);
