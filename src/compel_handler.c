#if 1
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include<fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <pthread.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <linux/userfaultfd.h>  /* Definition of UFFD* constants */
#include <sys/ioctl.h>

#include "compel_handler.h"

#define errExit(msg) do{ perror(msg); exit(EXIT_FAILURE);}while(0)

int state;
struct infect_ctx *ctl;
int child_pid;

int steal_fd(int cmd, int *stolen_fd){
    printf("Infecting\n");
    
    if (compel_infect(ctl, 1, sizeof(int))){
        fprintf(stderr,"Can't infect victim\n");
        return -1;
    }

    printf("Stealing fd\n");
    if (compel_rpc_call(cmd, ctl)){
        fprintf(stderr,"Can't run cmd\n");
        return -1;
    }

    if (compel_util_recv_fd(ctl, stolen_fd)){
        fprintf(stderr,"Can't recv fd\n");
        return -1;
    }

    if (compel_rpc_sync(cmd, ctl)){
        fprintf(stderr,"Con't finalize cmd\n");
        return -1;
    }

    printf("Stole %d fd\n", *stolen_fd);
    return 0;
}

int setup_compel(int pid){
    child_pid = pid;
    int *stolen_fd;
    struct infect_ctx *ictx;
    printf("Stopping task\n");
    state = compel_stop_task(pid);
    if (state < 0){
         fprintf(stderr,"Can't stop task\n");
         return -1;
    }
 
    printf("Preparing parasite ctl\n");
    ctl = compel_prepare(pid);
    if (!ctl){
         fprintf(stderr,"Can't prepare for infection\n");
         return -1;
    }

    printf("Configuring contexts\n");

    /*
     * First -- the infection context. Most of the stuff
     * is already filled by compel_prepare(), just set the
     * log descriptor for parasite side, library cannot
     * live w/o it.
     */
    ictx = compel_infect_ctx(ctl);
    ictx->log_fd = STDERR_FILENO;

    parasite_setup_c_header(ctl);
 
    return 0;

}

int cleanup_compel(void){
    int pid = child_pid;
    printf("Curing\n");
   
    if (compel_cure(ctl)){
          fprintf(stderr, "Can't cure victim");
          return -1;
    }

    if (compel_resume_task(pid, state, state)){
          fprintf(stderr, "Can't unseize task");
          return -1;
    }

    printf("Done\n");
    return 0;
}
#endif
