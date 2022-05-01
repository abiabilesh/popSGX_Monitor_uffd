
/* C standard library */
#include <errno.h>		// errno
#include <stdio.h>		// pid_t
#include <stddef.h>
#include <stdlib.h>		// EXIT_FAILURE
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

/* POSIX */
#include <unistd.h>
#include <signal.h>		// SIGTRAP
#include <sys/user.h>	// struct user_regs_struct
#include <sys/wait.h>
#include <time.h>		// struct timespec, clock_gettime()

/* Linux */
#include <syscall.h>
#include <sys/ptrace.h>

#include <linux/ptrace.h>
#include <sys/reg.h>	// ORIG_RAX

#include "ptrace.h"
#include "log.h"		// log printing
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

/* Time measuring. */
struct timespec tstart={0,0}, tend={0,0};


static void ufFd_fault_handler_func(long arg){
    long ufFd;
    static char *page = NULL;
    static struct uffd_msg msg;
    static int fault_cnt = 0;
    struct uffdio_copy muffdio_copy;

    printf("Hello world\n");

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

static int check_pipe_ends(int wfd, int rfd)
{
        struct stat r, w;

        printf("Check pipe ends are at hands\n");
        if (fstat(wfd, &w) < 0) {
                perror("Can't stat wfd");
                return 0;
        }

        if (fstat(rfd, &r) < 0) {
                perror("Can't stat rfd");
                return 0;
        }

        if (w.st_dev != r.st_dev || w.st_ino != r.st_ino) {
                perror("Pipe's not the same");
                return 0;
        }

        printf("Check pipe ends are connected\n");

        //char aux[2048] = "";
        int read_bytes = 0;
	int v;
        while((read_bytes = read(rfd, &v, sizeof(v))) > 0){
            printf("monitor: %x",v);
            if(read_bytes != 2048){
                break;
            }
        }

        return 1;
}


/**
 * Main function for the simple ptrace monitor
 * Use: ./monitor <executable> <args>
 * */
int main(int argc, char **argv)
{

        int p_out[2], pass = 1, stolen_fd = -1, stolen_stdout;

        if (pipe(p_out)) {
                perror("Can't make pipe");
                return -1;
        }

	if (argc <= 1)
	    log_error("too few arguments: %d", argc);

	clock_gettime(CLOCK_MONOTONIC, &tstart);
	pid_t pid = fork();
	switch (pid) {
		case -1: /* error */
			log_error("%s. pid -1", strerror(errno));
		case 0:  /* child, executing the tracee */
                        //close(p_out[0]);
                        //dup2(p_out[1], 1);
                        //close(p_out[1]);
                        execl("./victim", "victim", NULL);
                        exit(1);
			/*
                        ptrace(PTRACE_TRACEME, 0, 0, 0);
			execvp(argv[1], argv + 1);
			log_error("%s. child", strerror(errno));
                        */
	}

        //close(p_out[1]);
        printf("Infecting the victim\n");
        compel_setup(pid);
        //steal_fd(PARASITE_CMD_GET_STDOUT_FD, &stolen_stdout);
        steal_fd(PARASITE_CMD_GET_STDOUT_FD, &stolen_fd);
        printf("The fd that is stolen is %d\n", stolen_fd);
        compel_destruct(pid);
        ufFd_fault_handler_func(stolen_fd);

        //check_pipe_ends(p_out[1], p_out[0]);

        #if 1
	waitpid(pid, 0, 0);	// sync with PTRACE_TRACEME
	ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_EXITKILL);

	int terminate = 0;
	int status = 0;
	while (!terminate) {
		//uint64_t pc = get_pc(pid);
		//log_info("pc: 0x%lx", pc);
		//status = 0;
		ptrace(PTRACE_CONT, pid, 0L, 0L);
		if (waitpid(pid, &status, 0) == -1) {
			log_info("status %d", status);
			if (WIFEXITED(status))
				log_info("Child terminated normally.");
			break;
		}
	}
        #endif 
        
	clock_gettime(CLOCK_MONOTONIC, &tend);
	log_info("Finish main loop! %.5f seconds.", ((double)tend.tv_sec + 1.0e-9*tend.tv_nsec) - 
           ((double)tstart.tv_sec + 1.0e-9*tstart.tv_nsec));
	return 0;
}
