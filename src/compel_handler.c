#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "compel_handler.h"
#include "log.h"

compel_handle compel_hdl;

int steal_fd(int cmd, int *stolen_fd){
    struct infect_ctx *ctl = compel_hdl.ctl;
    int pid = compel_hdl.victim_pid;

    log_info("Infecting the victim");
    if (compel_infect(ctl, 1, sizeof(int))){
        log_error("Can't infect the victim\n");
        goto out_bad;
    }
    log_info("Stealing fd\n");
    if (compel_rpc_call(cmd, ctl)){
        log_error("Can't run cmd %d", cmd);
        goto out_bad;
    }
    if (compel_util_recv_fd(ctl, stolen_fd)){
        log_error("Can't recv fd");
        goto out_bad;
    }
    if (compel_rpc_sync(cmd, ctl)){
        log_error("Can't finalize cmd\n");
        goto out_bad;
    }

    //log_info("Stole %d fd\n", *stolen_fd);
    return 0;

out_bad:
    return -1;
}

int setup_compel(int victim_pid){    
    int state;
    struct infect_ctx *ictx;
    struct infect_ctx *ctl;

    log_info("Stopping the victim %d", victim_pid);
    state = compel_stop_task(victim_pid);
    if (state < 0){
        log_error("Can't stop task", strerror(errno));
        goto out_bad;
    }
    log_info("Preparing parasite ctl");
    ctl = compel_prepare(victim_pid);
    if (!ctl){
        log_error("Can't prepare for infection\n");
        goto out_bad;
    }

    /*
     * First -- the infection context. Most of the stuff
     * is already filled by compel_prepare(), just set the
     * log descriptor for parasite side, library cannot
     * live w/o it.
     */
    ictx = compel_infect_ctx(ctl);
    ictx->log_fd = STDERR_FILENO;
    parasite_setup_c_header(ctl);
    compel_hdl.status = state;
    compel_hdl.ctl = ctl;
    compel_hdl.victim_pid = victim_pid;
    log_info("Compel setup done");
    return 0;

out_bad:
    return -1;
}

int cleanup_compel(void){
    int state = compel_hdl.status;
    int pid = compel_hdl.victim_pid;
    struct infect_ctx *ctl = compel_hdl.ctl;
    
    log_info("Curing the victim");
    if (compel_cure(ctl)){
        log_error("Can't cure the victim %d", pid);
        goto out_bad;  
    }

    if (compel_resume_task(pid, state, state)){
        log_error("Can't unseize the victim %d", pid);
        goto out_bad;  
    }
    log_info("Compel cleanup done");
    return 0;

out_bad:
    return -1;
}
