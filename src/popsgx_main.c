#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include "../inc/popsgx.h"
#include "../inc/compel_handler.h"
#include "../inc/popsgx_page_handler.h"
#include "../inc/dsm_handler.h"
#include "../inc/log.h"

extern char* __progname;

// Required number of arguments for the application
#define OPT_MANDATORY_COUNT 6

static void usage(void)
{
    log_info("\n"
             "usage: %s [-v victim | -r remote-node-ip | -p remote-node-port | -t host-port |-m shared_mem | -n no_pages]"
             "\n"
             "options:\n"
             "\t-v victim process to serve page-faults & ditributed-memory-sharing\n"
             "\t-r remote node's ip-address for dsm\n"
             "\t-p remote node's port-number for dsm\n"
             "\t-t host's port-number\n"
             "\t-m address of the memory region to be shared\n"
             "\t-n number of pages to be shared\n"
             "\t-h help"
             "\n",
             __progname);
    exit(EXIT_SUCCESS);
}

static int execute_popsgx_tracee(popsgx_child *child){
    int ret = -1;
    pid_t pid;

    pid = fork();
    if(pid < 0){
        log_error("Forking failed with error %s", strerror(errno));
        return ret;
    }else if(pid == 0){
        // We are into the child process now
        execl(child->c_path, child->c_path, NULL);
        // Should not execute the below line
        log_error("Failed on execl of the tracee with error %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    child->c_pid = pid;
    
    log_info("Successfully forked the tracee as a child process %d", pid);
    return 0;
}

static int popsgx_setup_memory(popsgx_app* popsgx, long int address, int no_pages){
    int ret = 0;
    dsm_handler *dsm = &popsgx->dsmHandler;

    ret = dsm_establish_communication(dsm, &address, &no_pages);
    if(ret){
        log_error("dsm_establish_communication failed");
        goto popsgx_setup_memory_fail;
    }

popsgx_setup_memory_fail:
    return ret;
}

int main(int argc, char* argv[]){
    
    int ret = 0;
    int opt, opt_counter = 0;
    char *remote_ip = NULL;
    int remote_port = -1;
    int host_port = -1;
    long int shared_physical_address = -1;
    int no_of_shared_pages = -1;
    popsgx_app *popsgx = NULL;

    popsgx = (popsgx_app*)calloc(1, sizeof(popsgx_app));
    if(popsgx == NULL){
        log_error("Popsgx memory allocation failed %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

     /*
     *  Parse the arguments
     */
    struct option long_opt[] =
    {
        {     "victim", required_argument, NULL, 'v'},
        {  "remote_ip", required_argument, NULL, 'r'},
        {"remote_port", required_argument, NULL, 'p'},
        {  "host_port", required_argument, NULL, 't'},
        { "shared_mem", required_argument, NULL, 'm'},
        {   "no_pages", required_argument, NULL, 'n'},
        {         NULL,                 0, NULL,  0 }
    };

    while((opt = getopt_long(argc, argv, "hv:r:p:t:m:n:", long_opt, NULL)) != -1)
    {
        switch (opt)
        {
        case 'v':
            popsgx->child.c_path = strdup(optarg);
            break;
        
        case 'r':
            remote_ip = strdup(optarg);
            break;

        case 'p':
            remote_port = atoi(optarg);
            break;

        case 't':
            host_port = atoi(optarg);
            break;
        
        case 'm':
            shared_physical_address = strtol(optarg, NULL, 16);
            break;
        
        case 'n':
            no_of_shared_pages = atoi(optarg);
            break;

        case 'h':
        default:
            usage();
            break;
        }
        opt_counter++;
    }
    if (optind < argc || opt_counter != OPT_MANDATORY_COUNT)
	{
		usage();
	}

    ret = execute_popsgx_tracee(&popsgx->child);
    if(ret){
        log_error("Could not execute the tracee for popsgx");
        goto popsgx_fail;
    }

    ret = compel_handler_init(&popsgx->cmplHandler);
    if(ret){
        log_error("Could not retrieve the uffd");
        goto popsgx_tracee_fail;
    }

    compel_ioctl_arg compelArgs;
    compelArgs.cmd = STEALFD;
    compelArgs.tracee_pid = popsgx->child.c_pid;
    compelArgs.cmd_args.fdArgs.fd_type = PARASITE_CMD_GET_STDUFLT_FD;
    compelArgs.cmd_args.fdArgs.shared_page_address = 0x10000;
    compelArgs.cmd_args.fdArgs.no_of_pages = 26;
    compelArgs.cmd_args.fdArgs.tracee_fd = -1;

    ret = compel_ioctl(&popsgx->cmplHandler, &compelArgs);
    if(ret){
        log_error("Could not retrieve the uffd");
        goto popsgx_tracee_fail;
    }

    log_debug("The stolen uffd is %d", compelArgs.cmd_args.fdArgs.tracee_fd);

    ret = popsgx_pgHandler_init(&popsgx->pgHandler, no_of_shared_pages);
    if(ret){
        log_error("Could not initialize page handler");
        goto popsgx_pgHandler_fail;
    }

    ret = dsm_handler_init(&popsgx->dsmHandler, \
                            remote_ip,          \
                            remote_port,        \
                            host_port );
    if(ret){
        log_error("DSM Handler initialization failed");
        goto popsgx_pgHandler_fail;
    }

    ret = popsgx_setup_memory(popsgx,                     \
                              shared_physical_address,    \
                              no_of_shared_pages);
    if(ret){
        log_error("Couldn't setup buffers across nodes");
        goto popsgx_dsm_fail;
    }

    while(1);

    return 0;

popsgx_dsm_fail:
    dsm_handler_destroy(&popsgx->dsmHandler);
popsgx_pgHandler_fail:
    popsgx_pgHandler_destroy(&popsgx->pgHandler);
popsgx_tracee_fail:
    //Have to implement killing the tracee process
popsgx_fail:
    free(popsgx);
    exit(EXIT_FAILURE);
}
