#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include "log.h"
#include "dsm_userspace.h"
#include "compel_handler.h"

#define OPT_MANDATORY_COUNT 6
extern char* __progname;
static pid_t execute_victim(char *victim);

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

static pid_t execute_victim(char *victim)
{
    pid_t pid;

    pid = fork();
    if(pid < 0){
        log_error("Failed forking the victim with error %s", strerror(errno));
        exit(EXIT_FAILURE);
    }else if(pid == 0){
        execl(victim, victim, NULL);
        //should not execute the below line
        exit(EXIT_FAILURE);
    }

    log_debug("Succesfully forked the victim as a child process");
    return pid;
}

int main(int argc, char *argv[]){
    int ret;
    int opt, opt_counter = 0;
    dsm_args d_args = {0};
    char *victim = NULL;
    pid_t child_pid = -1;

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
            victim = strdup(optarg);
            break;
        
        case 'r':
            d_args.remote_ip = strdup(optarg);
            break;

        case 'p':
            d_args.remote_port = atoi(optarg);
            break;

        case 't':
            d_args.host_port = atoi(optarg);
            break;
        
        case 'm':
            d_args.flt_reg.fault_addr = strtol(optarg, NULL, 16);
            break;
        
        case 'n':
            d_args.flt_reg.num_pages = atoi(optarg);
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

    child_pid = execute_victim(victim);

    ret = compel_victim_stealFd(child_pid,                      \
                                PARASITE_CMD_GET_STDUFLT_FD,    \
                                &d_args.uffd,                   \
                                d_args.flt_reg.fault_addr,      \
                                d_args.flt_reg.num_pages);
    if(ret){
        log_error("Stealing victim's uffd failed");
        exit(EXIT_FAILURE);
    }

    d_args.victim_pid = child_pid;

    ret = dsm_main(d_args);

    return 0;
}