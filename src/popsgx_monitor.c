#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#include "../inc/log.h"
#include "../inc/popsgx_monitor.h"
#include "../inc/compel_handler.h"

extern char* __progname;

// Required number of arguments for the application
#define OPT_MANDATORY_COUNT 7

/**
 * @brief Function declarations
 * 
 */
static void usage(void);
static int execute_tracee_app(popsgx_child *tracee);

/**
 * @brief Printing the help message
 * 
 */
static void usage(void)
{
    log_info("\n"
             "usage: %s [-m mode | -v victim | -r remote-node-ip | -p remote-node-port | -t host-port | -s shared_mem | -n no_pages]"
             "\n"
             "options:\n"
             "\t-m mode of the popsgx_monitor application either server or client\n"
             "\t-v victim process to serve page-faults & ditributed-memory-sharing\n"
             "\t-r remote node's ip-address for dsm\n"
             "\t-p remote node's port-number for dsm\n"
             "\t-t host's port-number\n"
             "\t-s address of the memory region to be shared\n"
             "\t-n number of pages to be shared\n"
             "\t-h help"
             "\n",
             __progname);
    exit(EXIT_SUCCESS);
}

/**
 * @brief Execute the tracee application 
 * 
 * @param tracee 
 * @return int 
 */
static int execute_tracee_app(popsgx_child *tracee){
    int rc = 0;
    pid_t tracee_pid;

    tracee_pid = fork();
    if(tracee_pid < 0){
        log_error("Forking failed with error %s", strerror(errno));
        return -1;
    }else if(tracee_pid == 0){
        // We are into the child process now
        execl(tracee->c_path, tracee->c_path, NULL);
        // Should not execute the below line
        log_error("Failed on execl of the tracee with error %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    tracee->c_pid = tracee_pid;
    
    log_info("Successfully forked the tracee as a child process %d", tracee_pid);
    return rc;
}

/**
 * @brief main function of the application
 * 
 * @param argc 
 * @param argv 
 * @return int 
 */

int main(int argc, char* argv[]){
    int rc = 0;
    int opt, opt_counter = 0;
    char *remote_ip = NULL;
    char *mode = NULL;
    int remote_port = -1;
    int host_port = -1;
    unsigned long shared_physical_address = -1;
    int no_of_shared_pages = -1;
    popsgx_app monitor_app;

    memset(&monitor_app, 0, sizeof(popsgx_app));

    /*
     *  Parse the arguments
     */
    struct option long_opt[] =
    {
        {       "mode", required_argument, NULL, 'm'},
        {     "victim", required_argument, NULL, 'v'},
        {  "remote_ip", required_argument, NULL, 'r'},
        {"remote_port", required_argument, NULL, 'p'},
        {  "host_port", required_argument, NULL, 't'},
        { "shared_mem", required_argument, NULL, 's'},
        {   "no_pages", required_argument, NULL, 'n'},
        {         NULL,                 0, NULL,  0 }
    };

    while((opt = getopt_long(argc, argv, "hv:r:p:t:m:n:s:", long_opt, NULL)) != -1)
    {
        switch (opt)
        {
        case 'v':
            monitor_app.child.c_path = strdup(optarg);
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
        
        case 's':
            shared_physical_address = strtoul(optarg, NULL, 16);
            break;
        
        case 'n':
            no_of_shared_pages = atoi(optarg);
            break;
        
        case 'm':
            mode = strdup(optarg);
            if(!strcmp("server", mode)){
                monitor_app.mode = SERVER;
            }else if(!strcmp("client", mode)){
                monitor_app.mode = CLIENT;
            }else{
                usage();
            }
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

    rc = execute_tracee_app(&monitor_app.child);
    if(rc){
        log_error("failed to execute the tracee app");
        goto out_fail; 
    }

    rc = compel_steal_uffd(&monitor_app.child,          \
                           &monitor_app.child.uffd,     \
                           shared_physical_address,     \
                           no_of_shared_pages);
    if(rc){
        log_error("failed to steal uffd");
        goto out_fail;
    }

    log_debug("the stolen uffd is %d", monitor_app.child.uffd);

    while(1);

out_fail:
    return rc;
}

