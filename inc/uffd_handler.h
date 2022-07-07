#ifndef __USERFAULT_HANDLER_H__
#define __USERFAULT_HANDLER_H__
#include <stdint.h>
#include "../inc/msi_handler.h"

struct userfaultfd_thread_args
{
	int sk;
	long uffd;
	pid_t victim_pid;
    msi_handler *msi;
};


void* fault_handler_thread(void *arg);
long setup_userfaultfd_region(msi_handler* msi, pthread_t* thr, void* (*handler)(void*), int sk, int uffd, pid_t pid);

#endif
