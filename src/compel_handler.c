#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "compel_handler.h"
#include "log.h"

static compel_handle cmpl_hdl = {0};

static void intialize_compel_handle(void);

#define COMPEL_LOG_LEVEL COMPEL_LOG_ERROR

static void print_vmsg(unsigned int lvl, const char *fmt, va_list parms)
{
	log_debug("\tLC%u: ", lvl);
	vprintf(fmt, parms);
}

static void intialize_compel_handle(void){
    compel_log_init(print_vmsg, COMPEL_LOG_LEVEL);
    cmpl_hdl.ctl = NULL;
    cmpl_hdl.status = -1;
    cmpl_hdl.victim_pid = -1;
    cmpl_hdl.is_intialized = false;
}

static int compel_setup(pid_t pid){
    int state;
    struct parasite_ctl *ctl;
    struct infect_ctx *ictx;

    if(!cmpl_hdl.is_intialized)
        intialize_compel_handle();

    cmpl_hdl.victim_pid = pid;
    log_info("Stoping the victim for compel code injection");
    state = compel_stop_task(pid);
    if(state < 0){
        log_error("Could not stop the victim for compel infection");
        return state;
    }
    cmpl_hdl.status = state;

    log_debug("Preparing compel's parasitic context");
    ctl = compel_prepare(pid);
    if(!ctl){
        log_error("Could not create compel context");
        goto fail_compel_setup;
    }
    cmpl_hdl.ctl = ctl;

    /*
     * First -- the infection context. Most of the stuff
     * is already filled by compel_prepare(), just set the
     * log descriptor for parasite side, library cannot
     * live w/o it.
     */
    ictx = compel_infect_ctx(cmpl_hdl.ctl);
    ictx->log_fd = STDERR_FILENO;
    
    log_debug("Preparing the parasite code header for injection");
    parasite_setup_c_header(cmpl_hdl.ctl);

    log_debug("Infecting the victim through code injection");
    if(compel_infect(cmpl_hdl.ctl, 1, sizeof(int))){
        log_error("Could not infect the victim");
        goto fail_compel_setup;
    }

    cmpl_hdl.is_intialized = true;

    return 0;

fail_compel_setup:
    return -1;
}

static int compel_destruct(void){
    struct infect_ctx *ictx;

    if(!cmpl_hdl.is_intialized){
        log_error("Compel is not intialized");
        goto fail_compel_destruct;
    }

    int pid = cmpl_hdl.victim_pid;
    int state = cmpl_hdl.status;

    log_debug("Curing the victim");
    if(compel_cure(cmpl_hdl.ctl)){
        log_error("Could not cure the victim");
        goto fail_compel_destruct;
    }

    log_debug("Resuming the victim for normal execution");
    if(compel_resume_task(pid, state, state)){
        log_error("Could not unseize the victim task");
        goto fail_compel_destruct;
    }

    /*
     *socket has to be closed in order for the consecutive
     *compel calls to work like compel_victim_madvise after compel_victim_stealFd
     */
    ictx = compel_infect_ctx(cmpl_hdl.ctl);
    close(ictx->sock);


    cmpl_hdl.is_intialized = false;
    cmpl_hdl.ctl = NULL;
    cmpl_hdl.victim_pid = -1;
    cmpl_hdl.status = -1;

    return 0;

fail_compel_destruct:
    return -1;
}

static int compel_stealFd(int cmd, int *stolen_fd){
    if(!cmpl_hdl.is_intialized){
        log_error("Compel is not intialized");
        goto fail_compel_stealFd;
    }

    log_debug("Stealing the %d fd from the victim pid %d", cmd, cmpl_hdl.victim_pid);
    if(compel_rpc_call(cmd, cmpl_hdl.ctl)){
        log_error("Cannot run the command %d", cmd);
        goto fail_compel_stealFd;
    }

    if(compel_util_recv_fd(cmpl_hdl.ctl, stolen_fd)){
        log_error("Could not receive the %d fd", cmd);
        goto fail_compel_stealFd;
    }

    if(compel_rpc_sync(cmd, cmpl_hdl.ctl)){
        log_error("Could not finalize the command %d", cmd);
        goto fail_compel_stealFd;
    }

    log_debug("Successfully stolen the fd %d", *stolen_fd);
    return 0;

fail_compel_stealFd:
    return -1;
}


int compel_victim_stealFd(pid_t victim_pid, int cmd, int *uffd){
    int ret = -1;

    ret = compel_setup(victim_pid);
    if(ret){
        log_error("Compel setup failed");
        goto fail_compel_victim_stealFd;
    }

    ret = compel_stealFd(cmd, uffd);
    if(ret){
        log_error("Stealing the fd from victim failed");
        goto fail_compel_victim_stealFd;
    }
    ret = compel_destruct();
    if(ret){
        log_error("Compel destruct failed");
        goto fail_compel_victim_stealFd;
    }

    return 0;

fail_compel_victim_stealFd:
    return ret;
}


int compel_victim_madvise(pid_t victim_pid, int cmd, uint64_t page_addr){
    int ret = -1;
    uint64_t *arg;

    ret = compel_setup(victim_pid);
    if(ret){
        log_error("Compel setup failed");
        goto fail_compel_victim_madvise;
    }

    /*
	 * Setting the page address for madvise
	 */
	arg = compel_parasite_args(cmpl_hdl.ctl, uint64_t);
    *arg = page_addr;
    
    log_debug("madvising the victim pid %d address %p with the command %d", victim_pid, page_addr, cmd);
    if(compel_rpc_call_sync(cmd, cmpl_hdl.ctl)){
        log_error("compel_rpc_call_sync failed");
        goto fail_compel_victim_madvise;
    }

    if(compel_rpc_call_sync(cmd, cmpl_hdl.ctl)){
        log_error("compel_rpc_call_sync failed");
        goto fail_compel_victim_madvise;
    }

    ret = compel_destruct();
    if(ret){
        log_error("Compel destruct failed");
        goto fail_compel_victim_madvise;
    }

fail_compel_victim_madvise:
    return ret;
}
