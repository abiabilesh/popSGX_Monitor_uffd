#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include<fcntl.h>

#include "compel_extension.h"

#include <stdio.h>
#include <stdlib.h>
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

#define errExit(msg) do{ perror(msg); exit(EXIT_FAILURE);}while(0)

int state;
struct infect_ctx *ctl;


static void* ufFd_fault_handler_func(void *arg){
    long ufFd;
    static char *page = NULL;
    static struct uffd_msg msg;
    static int fault_cnt = 0;
    struct uffdio_copy muffdio_copy;

    ufFd = (long) arg;

    int page_size = sysconf(_SC_PAGESIZE);
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

int steal_fd(int cmd, int *stolen_fd){
    printf("Infecting\n");
    
    if (compel_infect(ctl, 1, sizeof(int))){
        fprintf(stderr,"Can't infect victim\n");
        return -1;
    }

    printf("Stealing fd\n");
    if (compel_rpc_call(PARASITE_CMD_GET_STDOUT_FD, ctl)){
        fprintf(stderr,"Can't run cmd\n");
        return -1;
    }

    if (compel_util_recv_fd(ctl, stolen_fd)){
        fprintf(stderr,"Can't recv fd\n");
        return -1;
    }

    if (compel_rpc_sync(PARASITE_CMD_GET_STDOUT_FD, ctl)){
        fprintf(stderr,"Con't finalize cmd\n");
        return -1;
    }

    printf("Stole %d fd\n", *stolen_fd);
    return 0;
}

int compel_setup(int pid){
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

int compel_destruct(int pid){
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
