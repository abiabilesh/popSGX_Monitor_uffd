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

static int page_size;

static void* ufFd_fault_handler_func(void *arg){
    long ufFd;
    static char *page = NULL;
    static struct uffd_msg msg;
    static int fault_cnt = 0;
    struct uffdio_copy muffdio_copy;

    ufFd = (long) arg;

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

int main(int argc, char **argv){
    u_int64_t noPages;
    u_int64_t memorySize;
    char* addr;
    int ret;
    
    //userfaultfd stuffs
    long ufFd;
    struct uffdio_api ufFd_api;
    struct uffdio_register ufFd_register;

    //thread
    pthread_t ufFd_handler;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s num-pages\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    page_size = sysconf(_SC_PAGESIZE);
    noPages = strtoul(argv[1], NULL, 0);
    memorySize = noPages * page_size;
    
    ufFd = syscall(__NR_userfaultfd, O_CLOEXEC| O_NONBLOCK);
    if(ufFd == -1){
        errExit("SYS_userfaultfd failed");
    }

    //UFFDIO_API ioctl
    ufFd_api.api = UFFD_API;
    ufFd_api.features = 0;

    if(ioctl(ufFd, UFFDIO_API, &ufFd_api) == -1){
        errExit("UFFDIO_API_ioctl failed");
    }

    addr = mmap(NULL, memorySize, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
    if(addr == MAP_FAILED){
        errExit("Memory map failed");
    }

    printf("The address is %p\n", addr);
    printf("The memory size is %ld\n", memorySize);

    //UFFDIO_REGISTER ioctl
    ufFd_register.range.start = (unsigned long long)addr;
    ufFd_register.range.len   = memorySize;
    ufFd_register.mode = UFFDIO_REGISTER_MODE_MISSING;

    if(ioctl(ufFd, UFFDIO_REGISTER, &ufFd_register) == -1){
        errExit("UFFDIO_REGISTER_ioctl failed");
    }

    ret = pthread_create(&ufFd_handler, NULL, &ufFd_fault_handler_func, (void*)ufFd);
    if(ret != 0){
        errno = ret;
        errExit("pthread_create failed");
    }

    int l;
    l = 0xf;

    while(l < memorySize){
        printf("Reading %d\n", l);
        char c = addr[l];
        printf("Read address %p in main(): ", addr+l);
        printf("%c\n", c);
        l += 1024;
        //usleep(1000000);
    }

    return 0;
}