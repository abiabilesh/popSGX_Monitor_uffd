#define _POSIX_C_SOURCE 200112L

/* C standard library */
#include <errno.h>		// errno
#include <stdio.h>		// pid_t
#include <stddef.h>
#include <stdlib.h>		// EXIT_FAILURE
#include <string.h>

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

/* Time measuring. */
struct timespec tstart={0,0}, tend={0,0};

/**
 * Main function for the simple ptrace monitor
 * Use: ./monitor <executable> <args>
 * */
int main(int argc, char **argv)
{
	if (argc <= 1)
	    log_error("too few arguments: %d", argc);

	clock_gettime(CLOCK_MONOTONIC, &tstart);
	pid_t pid = fork();
	switch (pid) {
		case -1: /* error */
			log_error("%s. pid -1", strerror(errno));
		case 0:  /* child, executing the tracee */
			ptrace(PTRACE_TRACEME, 0, 0, 0);
			execvp(argv[1], argv + 1);
			log_error("%s. child", strerror(errno));
	}

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
	clock_gettime(CLOCK_MONOTONIC, &tend);
	log_info("Finish main loop! %.5f seconds.", ((double)tend.tv_sec + 1.0e-9*tend.tv_nsec) - 
           ((double)tstart.tv_sec + 1.0e-9*tstart.tv_nsec));
	return 0;
}
