#ifndef __COMPEL_HANDLER_H__
#define __COMPEL_HANDLER_H__

#include <compel/log.h>
#include <compel/infect-rpc.h>
#include <compel/infect-util.h>
#include <pthread.h>

#include "parasite.h"

/* --------------------------------------------------------------------
 * MACROS
 * -------------------------------------------------------------------*/
#define PARASITE_CMD_GET_STDIN_FD         PARASITE_USER_CMDS
#define PARASITE_CMD_GET_STDOUT_FD        PARASITE_USER_CMDS + 1
#define PARASITE_CMD_GET_STDERR_FD        PARASITE_USER_CMDS + 2
#define PARASITE_CMD_GET_STDUFLT_FD       PARASITE_USER_CMDS + 3
#define PARASITE_CMD_SET_MADVISE_NO_NEED  PARASITE_USER_CMDS + 4

/* --------------------------------------------------------------------
 * Structures & Required Datatypes
 * -------------------------------------------------------------------*/
typedef struct infect_handler_t{
   struct parasite_ctl *ctl;
   int state;
   pid_t pid;
}infect_handler;

typedef struct compel_handler_t{
   pthread_mutex_t compel_mutex;
   bool isInitialized;
   infect_handler infectHdl;
} compel_handler;


// Structures belonging to the arguments of compel handler
typedef enum{
   STEALFD,
   MADVISE
}compel_cmd;

typedef struct stealFd_args_t{
   int      fd_type;
   uint64_t shared_page_address;
   uint64_t no_of_pages;
   int      tracee_fd;
}stealFd_args;

typedef struct madvise_args_t{
      uint64_t page_address;
}madvise_args;

typedef union compel_args_t{
   stealFd_args fdArgs;
   madvise_args madvArgs;
}compel_args;

typedef struct compel_ioctl_arg{
   pid_t       tracee_pid;
   compel_cmd  cmd;
   compel_args cmd_args;
}compel_ioctl_arg;


/* --------------------------------------------------------------------
 * Local Functions
 * -------------------------------------------------------------------*/
static int __compel_stealFd(infect_handler *infectHdl, int cmd, int *traceeFd);
static int __compel_tracee_stealFd(infect_handler *infectHdl, stealFd_args *args);

static int __compel_prepare_infection(infect_handler *infectHdl, pid_t pid);
static int __compel_disinfection(infect_handler *infectHdl);


/* --------------------------------------------------------------------
 * Public Functions
 * -------------------------------------------------------------------*/
int compel_handler_init(compel_handler *cmpHdl);
int compel_ioctl(compel_handler *compelHandle, compel_ioctl_arg *args);

#endif
