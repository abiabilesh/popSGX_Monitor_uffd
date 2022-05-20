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

#define errExit(msg) do{ perror(msg); exit(EXIT_FAILURE);}while(0)

static void ufFd_fault_handler_func(long arg){
    long ufFd;
    static char *page = NULL;
    static struct uffd_msg msg;
    static int fault_cnt = 0;
    struct uffdio_copy muffdio_copy;

    //printf("Hello world\n");

    ufFd = (long) arg;
    int page_size;
    page_size = sysconf(_SC_PAGESIZE);

    page = mmap(NULL, page_size, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
    if(page == MAP_FAILED){
        errExit("Memory map failed");
    }
    
    for(;;){
        int ret;
        static struct pollfd mpollFd;

        mpollFd.fd = ufFd;
        mpollFd.events = POLLIN;

        ret = poll(&mpollFd, 1, -1);
        if(ret == -1){
            errExit("Polling failed");
        }

        printf("\nfault_handler_thread():\n");
        printf("poll() returns: nready = %d; "
                "POLLIN = %d; POLLERR = %d\n", ret, 
                (mpollFd.revents & POLLIN) != 0,
                (mpollFd.revents & POLLERR) != 0);

        ret = read(ufFd, &msg, sizeof(msg));
        if(ret == 0){
            printf("EOF on userfaultfd!!\n");
            exit(EXIT_FAILURE);
        }

        if(msg.event != UFFD_EVENT_PAGEFAULT){
            fprintf(stderr, "Unexpected event on page fault error\n");
            exit(EXIT_FAILURE);
        }

        printf("UFFD_EVENT_PAGEFAULT event: ");
        printf("flags = %lld; ", msg.arg.pagefault.flags);
        printf("address = %p \n", (void*)msg.arg.pagefault.address);

        memset(page, 'A' + fault_cnt % 26, page_size);
        fault_cnt++;
        
        muffdio_copy.src = (unsigned long) page;
        muffdio_copy.dst = (unsigned long) msg.arg.pagefault.address &
                                                  ~(page_size - 1);
        muffdio_copy.len = page_size;
        muffdio_copy.mode = 0;
        muffdio_copy.copy = 0;
        if (ioctl(ufFd, UFFDIO_COPY, &muffdio_copy) == -1)
            errExit("ioctl-UFFDIO_COPY");

        printf("(uffdio_copy.copy returned %lld\n",muffdio_copy.copy);
    }
}


void print_usage(void){
    log_info("Usage: ./uffd -v [filename]");
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]){
    int opt, ret;
    pid_t child_pid;
    char victim[2000];
    dsm_args d_args;
    int uffd;
    
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
            snprintf(victim, BUFSIZE, "%s", optarg);
            break;
        case 'r':
            snprintf(d_args.remote_ip, BUFSIZE, "%s", optarg);
            break;

        case 'p':
            d_args.host_port = atoi(optarg);
            break;

        case 't':
            d_args.remote_port = atoi(optarg);
            break;
        default:
            print_usage();
            break;
        }
    }

    child_pid = fork();
    switch(child_pid){
        case -1:
                 log_error("%s. pid -1", strerror(errno));
        case  0:
                 execl("./victim", "victim", NULL);
                 exit(1);
    }
   
    setup_compel(child_pid);
    steal_fd(PARASITE_CMD_GET_STDUFLT_FD, &uffd);
    d_args.uffd = uffd;
    cleanup_compel();    
    
    d_args.flt_reg.fault_addr = 0x10000;
    d_args.flt_reg.num_pages  = 26;
   
    dsm_main(d_args);
    //ufFd_fault_handler_func(uffd);

    return 0;
}
