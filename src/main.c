#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <errno.h>
#include <poll.h>
#include <getopt.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <linux/userfaultfd.h>  /* Definition of UFFD* constants */
#include <sys/ioctl.h>

#include "log.h"
#include "compel_handler.h"
#include "dsm_userspace.h"

void print_usage(void){
    log_info("Usage: ./uffd -v [filename]");
    exit(EXIT_FAILURE);
}

int fork_child(char *victim){
    int pid = fork();
    switch (pid)
    {
        case -1:
            log_error("%s. pid -1", strerror(errno));
        case 0:
            execl(victim, victim, NULL);
            exit(EXIT_FAILURE);
    }

    return pid;
}


int steal_fd_using_compel(int pid, int cmd){
    int fd;
    if(!(setup_compel(pid)));
        if(!steal_fd(cmd, &fd));
            if(!cleanup_compel());
                return fd;
    return -1;
}

int main(int argc, char *argv[]){
    int opt, ret, child_pid;
    char *victim;
    dsm_args d_args;

    const char *short_opt = "v:r:p:t:";
    struct option   long_opt[] =
    {
        {     "victim", required_argument, NULL, 'v'},
        {  "remote_ip", required_argument, NULL, 'r'},
        {  "host_port", required_argument, NULL, 'p'},
        {"remote_port", required_argument, NULL, 't'},
        {         NULL,                 0, NULL,  0 }
    };

    while((opt = getopt_long(argc, argv, short_opt, long_opt, NULL)) != -1){
        switch (opt)
        {
        case 'v':
            victim = strdup(optarg);
            break;
        
        case 'r':
            d_args.remote_ip = strdup(optarg);
            break;

        case 'p':
            d_args.host_port = atoi(optarg);
            break;

        case 't':
            d_args.remote_port = atoi(optarg);
            break;

        case '?':
        default:
            print_usage();
            break;
        }
    }
    
    child_pid = fork_child(victim);
    d_args.uffd = steal_fd_using_compel(child_pid, PARASITE_CMD_GET_STDUFLT_FD);
    if(d_args.uffd == -1){
        log_error("Error in Compel");
        exit(EXIT_FAILURE);
    }

    d_args.flt_reg.fault_addr = 0x10000;
    d_args.flt_reg.num_pages  = 25;

    dsm_main(d_args);
    return 0;
}
