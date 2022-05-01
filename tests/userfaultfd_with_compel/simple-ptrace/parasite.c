#include <errno.h>
#include <unistd.h>

#include <compel/infect-rpc.h>
#include <compel/plugins/plugin-fds.h>

#include <compel/plugins/std.h>
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


/*
 * Stubs for std compel plugin.
 */
int compel_main(void *arg_p, unsigned int arg_s)
{
	return 0;
}
int parasite_trap_cmd(int cmd, void *args)
{
	return 0;
}
void parasite_cleanup(void)
{
}

#define PARASITE_CMD_GET_STDIN_FD PARASITE_USER_CMDS
#define PARASITE_CMD_GET_STDOUT_FD PARASITE_USER_CMDS + 1
#define PARASITE_CMD_GET_STDERR_FD PARASITE_USER_CMDS + 2
#define PARASITE_CMD_GET_STDUFLT_FD PARASITE_USER_CMDS + 3


int parasite_daemon_cmd(int cmd, void *args)
{
	if (cmd == PARASITE_CMD_GET_STDIN_FD)
		return (fds_send_fd(STDIN_FILENO) < 0);
	//if (cmd == PARASITE_CMD_GET_STDOUT_FD)
	//	return (fds_send_fd(STDOUT_FILENO) < 0);
	if (cmd == PARASITE_CMD_GET_STDERR_FD)
		return (fds_send_fd(STDERR_FILENO) < 0);

	if (cmd == PARASITE_CMD_GET_STDOUT_FD){
//		return (fds_send_fd(STDOUT_FILENO) < 0);
	
	int page_size;
	u_int64_t noPages;
    	u_int64_t memorySize;
    	char* addr;
    	int ret;
	long ufFd = -1;
	
    	//userfaultfd stuffs
    	struct uffdio_api ufFd_api;
    	struct uffdio_register ufFd_register;

    	//thread
    	//pthread_t ufFd_handler;

    	page_size = 4096;
    	noPages = 26;
    	memorySize = noPages * page_size;
	
    	//ufFd = syscall(__NR_userfaultfd, O_CLOEXEC| O_NONBLOCK);
    	ufFd = sys_userfaultfd(O_CLOEXEC| O_NONBLOCK);
    	if(ufFd == -1){
		return -1;
    	}

    	//UFFDIO_API ioctl
    	ufFd_api.api = UFFD_API;
    	ufFd_api.features = 0;

    	if(sys_ioctl(ufFd, UFFDIO_API, &ufFd_api) == -1){
		return -1;
    	}
    	
	addr = sys_mmap(0x10000, memorySize, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
    	if(addr == MAP_FAILED){
    	    //errExit("Memory map failed");
		return -1;
    	}

//    	sys_write(STDOUT_FILENO,'c',sizeof(char));
    	//printf("The address is %p\n", addr);
    	//printf("The memory size is %ld\n", memorySize);
//    	sys_write(1, &memorySize, sizeof(memorySize));

    	//UFFDIO_REGISTER ioctl
    	ufFd_register.range.start = (unsigned long long)addr;
    	ufFd_register.range.len   = memorySize;
    	ufFd_register.mode = UFFDIO_REGISTER_MODE_MISSING;

    	if(sys_ioctl(ufFd, UFFDIO_REGISTER, &ufFd_register) == -1){
		return -1;
    	    //errExit("UFFDIO_REGISTER_ioctl failed");
    	}

	if(fds_send_fd(ufFd) < 0){
		return -1;
	}

#if 0
	ret = pthread_create(&ufFd_handler, NULL, &ufFd_fault_handler_func, (void*)ufFd);
    	if(ret != 0){
        	errno = ret;
        	errExit("pthread_create failed");
    	}
#endif
/*
	int l;
    	l = 0xf;
    	while(l < memorySize){
    	    //printf("Reading %d\n", l);
    	    //char c = addr[l];
	    sys_write(STDOUT_FILENO, addr[l], sizeof(char));
    	    //fprintf(stdout,"Read address %p in main(): ", addr+l);
    	    //fprintf(stdout, "%c\n", c);
    	    l += 1024;
    	}
*/	
	}

	return 0;
}	
