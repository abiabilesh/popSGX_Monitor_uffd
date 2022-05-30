#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <poll.h>
#include <sys/types.h>
#include <stdio.h>
#include <linux/userfaultfd.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ptrace.h>

#include "dsm_userspace.h"
#include "userfault_handler.h"
#include "types.h"
#include "msi_statemachine.h"
#include "compel_handler.h"

#define NO_NEW_PAGEFAULT 			0xFF
#define NEW_PAGEFAULT_READ  		0x00
#define NEW_PAGEFAULT_WRITE 		0x01 // UFFD_PAGEFAULT_FLAG_WRITE
#define PAGEFAULT_WRITE_PROTECTION 	0x03 // UFFD_PAGEFAULT_FLAG_WRITE | UFFDIO_REGISTER_MODE_WP 

static uint8_t handle_rw_pagefault(long uffd, struct uffd_msg msg, char *page, int sk);
static int handle_wprotect_pagefaults(long uffd, struct uffd_msg msg, pid_t victim_pid, char *page);
static int retrieve_victim_page_postwrite(pid_t victim_pid, __u64 address, char *page);

 /**
  * @brief It peeks the page using ptrace
  * 
  * @param victim_pid 
  * @param address 
  * @param page 
  * @return int 
  */
static int retrieve_victim_page_postwrite(pid_t victim_pid, __u64 address, char *page){
	ptrace(PTRACE_ATTACH, victim_pid, NULL, NULL);
   	wait(NULL);
   	int err;

   	err = get_child_data(victim_pid, page, (void*)address, (sysconf(_SC_PAGE_SIZE)));
   	if(err)
   	    return err;

   	printf("The stolen value from %p is %c\n", address, *((char*)(page+0xf)));
   	ptrace(PTRACE_DETACH, victim_pid, NULL, NULL);
   	printf("Setting the WP\n");
	return 0;
}

 /**
  * @brief It handles the pagefaults that are caused by a write operation and 
  *        make the page write unprotected and take a snapshot of the page after 10ms,
  *        then makes the page write protected again.
  * 
  * @param uffd 
  * @param msg 
  * @param victim_pid 
  * @param page 
  * @return int 
  */
static int handle_wprotect_pagefaults(long uffd, struct uffd_msg msg, pid_t victim_pid, char *page)
{
	struct uffdio_writeprotect uffdio_wp;
	int ret = 0;

	uffdio_wp.range.start = msg.arg.pagefault.address;
 	uffdio_wp.range.len = sysconf(_SC_PAGE_SIZE);
 	uffdio_wp.mode = 0;
 	if (ioctl(uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1){
		printf("UFFDIO_WRITEPROTECT failed\n");
		goto fail_handle_wprotect_pagefaults;
	} 

	usleep(10000);

	if(retrieve_victim_page_postwrite(victim_pid, msg.arg.pagefault.address, page))
	{
		printf("retrieve_victim_page_postwrite failed\n");
		goto fail_handle_wprotect_pagefaults; 
	}

	uffdio_wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;
 	if (ioctl(uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1)
	{ 
   		printf("ioctl-UFFDIO_WRITEPROTECT");
		goto fail_handle_wprotect_pagefaults;
	}

	return 0;

fail_handle_wprotect_pagefaults:
	return -1;
}

/**
 * @brief Handles new read & write pagefaults
 * 
 * @param uffd 
 * @param msg 
 * @param page 
 * @param sk 
 * @return uint8_t 
 */
static uint8_t handle_rw_pagefault(long uffd, struct uffd_msg msg, char *page, int sk)
{
	struct uffdio_copy uffdio_copy;
	uint8_t new_pagefault_type = NO_NEW_PAGEFAULT;

	//New pagefault due to read
	if(msg.event == UFFD_EVENT_PAGEFAULT && 
	  (msg.arg.pagefault.flags == NEW_PAGEFAULT_READ))
	{
		printf("Entering into the read zone!!\n");
		msi_request_page(sk, page,
			 (void*)msg.arg.pagefault.address,
			 msg.arg.pagefault.flags);
		
		new_pagefault_type = NEW_PAGEFAULT_READ;
	}
	//New pagefault due to write
	else if(msg.event == UFFD_EVENT_PAGEFAULT && 
		   (msg.arg.pagefault.flags == NEW_PAGEFAULT_WRITE))
	{
		new_pagefault_type = NEW_PAGEFAULT_WRITE;
	}

	//Second write page fault
	else if(msg.event == UFFD_EVENT_PAGEFAULT && 
		   (msg.arg.pagefault.flags == PAGEFAULT_WRITE_PROTECTION))
	{
		new_pagefault_type = PAGEFAULT_WRITE_PROTECTION;
	}

	if((new_pagefault_type == NEW_PAGEFAULT_READ) || (new_pagefault_type == NEW_PAGEFAULT_WRITE)){
		uffdio_copy.src = (unsigned long) page;
		uffdio_copy.dst = (unsigned long) msg.arg.pagefault.address &
			~(sysconf(_SC_PAGE_SIZE)- 1);
		uffdio_copy.len = sysconf(_SC_PAGE_SIZE);
		uffdio_copy.mode = UFFDIO_COPY_MODE_WP;
		uffdio_copy.copy = 0;
		if (ioctl(uffd, UFFDIO_COPY, &uffdio_copy) == -1)
			errExit("ioctl-UFFDIO_COPY");
	}
	
	return new_pagefault_type;
}

/**
 * @brief Fault handler thread
 *
 * @param arg
 *
 * @return
 */
void *
fault_handler_thread(void *arg)
{
	static struct uffd_msg msg;   /* Data read from userfaultfd */
	struct userfaultfd_thread_args* handler_arg = (struct
						userfaultfd_thread_args*)arg;
	long uffd;                    /* userfaultfd file descriptor */
	//char *page = (char*)handler_arg->physical_address;
	char *page = NULL;
	struct uffdio_copy uffdio_copy;
	ssize_t nread;

	uffd = (long)handler_arg->uffd;
	printf("UFFD descriptor is %ld\n", uffd);

	page = mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
    if(page == MAP_FAILED){
        errExit("Memory map failed");
    }

	for (;;) {
		struct pollfd pollfd;
		int nready;
		pollfd.fd = uffd;
		pollfd.events = POLLIN;
		nready = poll(&pollfd, 1, -1);
		if (nready == -1)
			errExit("poll");

		nread = read(uffd, &msg, sizeof(msg));
		if (nread == 0) {
			printf("EOF on userfaultfd!\n");
			exit(EXIT_FAILURE);
		}

		if (nread == -1)
			errExit("read");

		if (msg.event != UFFD_EVENT_PAGEFAULT) {
			fprintf(stderr, "Unexpected event on userfaultfd\n");
			exit(EXIT_FAILURE);
		}

		printf("    UFFD_EVENT_PAGEFAULT event: ");
		printf("flags = %llx; ", msg.arg.pagefault.flags);
		printf("address = %llx\n", msg.arg.pagefault.address);

		//Check if we need to handle new page-faults
		uint8_t pagefault_type = handle_rw_pagefault(uffd, msg, page, handler_arg->sk);
		if(pagefault_type != NO_NEW_PAGEFAULT){
			printf("New pagefault type is %d\n", pagefault_type);
			printf("Handled new pagefault\n");
		}

		if(pagefault_type == NEW_PAGEFAULT_WRITE || pagefault_type == PAGEFAULT_WRITE_PROTECTION){
			volatile void *t = alloca(sysconf(_SC_PAGE_SIZE));
			if(handle_wprotect_pagefaults(uffd, msg, handler_arg->victim_pid, t)){
				printf("Erros in handling write-protect pagefaults\n");
			}

			handle_write_command(handler_arg->sk, msg.arg.pagefault.address, t, sysconf(_SC_PAGE_SIZE));

			printf("\n[%p]PAGEFAULT\n", (void *)msg.arg.pagefault.address);
		}
	}
}

/**
 * @brief Sets up the pthread and user fault region
 *
 * @param start_region
 * @param length
 * @param thr
 * @param handler
 *
 * @return user fault fd
 */
long setup_userfaultfd_region(void* start_region, uint64_t length,
			     pthread_t* thr, void* (*handler)(void*), int sk, int uffd, pid_t victim_pid)
{
	
	int pthread_ret;
	struct userfaultfd_thread_args* args =
		(struct userfaultfd_thread_args*)malloc(sizeof(struct
						userfaultfd_thread_args));

	args->sk = sk;
	args->uffd = uffd;
	args->victim_pid = victim_pid;
	
	pthread_ret = pthread_create(thr, NULL, handler, (void *) args);
	if (pthread_ret != 0) {
		errno = pthread_ret;
		errExit("pthread_create");
	}

	return uffd;
}


