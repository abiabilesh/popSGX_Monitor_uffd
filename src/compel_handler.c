#include <stdio.h>
#include <stdlib.h> 
#include <unistd.h>

#include "../inc/compel_handler.h"
#include "../inc/log.h"

/* --------------------------------------------------------------------
 * Defines
 * -------------------------------------------------------------------*/
#define COMPEL_LOG_LEVEL COMPEL_LOG_ERROR

/* --------------------------------------------------------------------
 * Local Functions declarations
 * -------------------------------------------------------------------*/
static int __compel_stealFd(infect_handler *infectHdl, int cmd, int *traceeFd);
static int __compel_tracee_stealFd(infect_handler *infectHdl, stealFd_args *args);

static int __compel_prepare_infection(infect_handler *infectHdl, pid_t pid);
static int __compel_disinfection(infect_handler *infectHdl);


/* --------------------------------------------------------------------
 * Local Functions
 * -------------------------------------------------------------------*/
static void print_vmsg(unsigned int lvl, const char *fmt, va_list parms)
{
	log_debug("\tLC%u: ", lvl);
	vprintf(fmt, parms);
}

/* --------------------------------------------------------------------
 * Functions executed with locks
 * -------------------------------------------------------------------*/

static int __compel_stealFd(infect_handler *infectHdl, int cmd, int *traceeFd){
    int ret  = 0;

    log_debug("Stealing the %d fd from the victim pid %d", cmd, infectHdl->pid);
    if(!compel_rpc_call(cmd, infectHdl->ctl)){
        if(!compel_util_recv_fd(infectHdl->ctl, traceeFd)){
            if(compel_rpc_sync(cmd, infectHdl->ctl)){
                log_error("Couldn't finalize the command %d", cmd);
                goto fail_compel_stealFd;
            }
        }else{
            log_error("Could not receive the %d fd", cmd);
            goto fail_compel_stealFd;
        }
    }else{
        log_error("Cannot run the command %d", cmd);
        goto fail_compel_stealFd;
    }

return ret;

fail_compel_stealFd:
    return -1;
}

static int __compel_tracee_stealFd(infect_handler *infectHdl, stealFd_args *args){
    int ret = 0;
    uint64_t *compel_arg;
    
    if(args->fd_type == PARASITE_CMD_GET_STDUFLT_FD){
        //Preparing arguments for the compel only for the case of UFFD
        compel_arg = compel_parasite_args(infectHdl->ctl,                                                                   \
                                  sizeof(args->shared_page_address) + sizeof(args->no_of_pages));
        compel_arg[0] = args->shared_page_address;
        compel_arg[1] = args->no_of_pages;
    }

    ret = __compel_stealFd(infectHdl, args->fd_type, &args->tracee_fd);
    if(ret){
        args->tracee_fd = -1;
        log_error("Stealing the fd from tracee failed");
        goto compel_stealFd_fail;
    }

compel_stealFd_fail:
    return ret;
}


static int __compel_tracee_madvise(infect_handler *infectHdl, madvise_args *args){
    int ret = 0;
    uint64_t *compel_arg;
    int cmd = PARASITE_CMD_SET_MADVISE_NO_NEED;

    compel_arg = compel_parasite_args(infectHdl->ctl, uint64_t);
    *compel_arg = args->page_address;

    log_debug("madvising the victim pid %d address %p with the command %d",                                                 \
                                        infectHdl->pid, args->page_address, cmd);
    
    if(ret = compel_rpc_call_sync(cmd, infectHdl->ctl)){
        log_error("compel_rpc_call_sync failed");
        goto fail_compel_victim_madvise;
    }

    if(ret = compel_rpc_call_sync(cmd, infectHdl->ctl)){
        log_error("compel_rpc_call_sync failed");
        goto fail_compel_victim_madvise;
    }

fail_compel_victim_madvise:
    return ret;
}


static int __compel_prepare_infection(infect_handler *infectHdl, pid_t pid){
    int ret = 0;
    int state;
    struct parasite_ctl *ctl;
    struct infect_ctx *ictx;  

    if(infectHdl == NULL){
        log_error("Infect Handle is NULL");
        return -1;
    }

    memset(infectHdl, 0, sizeof(infect_handler));
    infectHdl->pid = pid;
    
    log_info("Stoping the tracee for compel code injection");
    state = compel_stop_task(pid);
    if(state < 0){
        log_error("Could not stop the victim for compel infection");
        return state;
    }
    infectHdl->state = state;

    log_debug("Preparing compel's parasitic context");
    ctl = compel_prepare(pid);
    if(!ctl){
        log_error("Could not create compel context");
        goto fail_compel_create_context;
    }
    infectHdl->ctl = ctl;

    /*
     * First -- the infection context. Most of the stuff
     * is already filled by compel_prepare(), just set the
     * log descriptor for parasite side, library cannot
     * live w/o it.
     */
    ictx = compel_infect_ctx(infectHdl->ctl);
    ictx->log_fd = STDERR_FILENO;

    log_debug("Preparing the parasite code header for injection");
    parasite_setup_c_header(infectHdl->ctl);

    log_debug("Infecting the tracee through code injection");
    if(compel_infect(infectHdl->ctl, 1, sizeof(int))){
        log_error("Could not infect the tracee");
        goto fail_compel_prepare_infection;
    }

    return ret;

fail_compel_prepare_infection:
    log_info("Curing the victim");
    compel_cure(infectHdl->ctl);
fail_compel_create_context:
    log_info("Resuming the tracee");
    compel_resume_task(pid, infectHdl->state,infectHdl->state);
    infectHdl->state = -1;
    return -1;
}


static int __compel_disinfection(infect_handler *infectHdl){
    int ret = 0;
    struct infect_ctx *ictx;

    log_debug("Curing the victim");
    if(compel_cure(infectHdl->ctl)){
        ret = -1;
        log_error("Could not cure the victim");
    }

    log_debug("Resuming the victim for normal execution");
    if(compel_resume_task(infectHdl->pid, infectHdl->state, infectHdl->state)){
        ret = -1;
        log_error("Could not unseize the victim task");
    }

    /*
     *socket has to be closed in order for the consecutive
     *compel calls to work like compel_victim_madvise after compel_victim_stealFd
     */
    ictx = compel_infect_ctx(infectHdl->ctl);
    close(ictx->sock);
    
    memset(infectHdl, 0, sizeof(infect_handler));
    
    return ret;
}

/* --------------------------------------------------------------------
 * Public Functions
 * -------------------------------------------------------------------*/
/**
 * @brief It just initializes the compel handler
 * @param void 
 * @return int 
 */
int compel_handler_init(compel_handler *cmpHdl){
    int ret = 0;
  
    if(pthread_mutex_init(&cmpHdl->compel_mutex, NULL) != 0){
        log_error("Couldn't setup compel mutex lock");
        ret = -1;
        goto compel_mutex_failed;
    }
    compel_log_init(print_vmsg, COMPEL_LOG_LEVEL);
    cmpHdl->isInitialized = true;

    return ret;

compel_mutex_failed:
    return ret;
}


int compel_ioctl(compel_handler *compelHandle, compel_ioctl_arg *args){
    int ret = 0, sret = 0;

    if(compelHandle == NULL){
        log_error("compel handle is NULL");
        goto compel_ioctl_failed;
    }

    if(!compelHandle->isInitialized){
        ret = compel_handler_init(compelHandle);
        if(ret)
            goto compel_ioctl_failed;
    }

    pthread_mutex_lock(&compelHandle->compel_mutex);
    
    //Execution in mutex 
    ret = __compel_prepare_infection(&compelHandle->infectHdl, args->tracee_pid);
    if(ret){
        log_error("Compel infection of tracee failed");
        goto compel_ioctl_failed;
    }

    switch (args->cmd)
    {
    
    case STEALFD:
        sret = __compel_tracee_stealFd(&compelHandle->infectHdl, &(args->cmd_args.fdArgs));
        break;
    
    case MADVISE:
        sret = __compel_tracee_madvise(&compelHandle->infectHdl, &(args->cmd_args.madvArgs));
        break;

    default:
        log_error("compel_ioctl command is wrong!!");
        break;
    }

    ret = __compel_disinfection(&compelHandle->infectHdl);
    if(ret){
        log_error("Compel disinfection failed");
        goto compel_ioctl_failed;
    }


compel_ioctl_failed:
    pthread_mutex_unlock(&compelHandle->compel_mutex);
    return ret = ((sret != 0) || (ret != 0)) ? 1 : 0;
}