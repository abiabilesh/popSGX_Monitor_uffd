#define _GNU_SOURCE
#include <sys/types.h>
#include <stdio.h>
#include <linux/userfaultfd.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/types.h>
#include <arpa/inet.h>
#include <stdbool.h>

#include "dsm_userspace.h"
#include "messages.h"
#include "types.h"
#include "bus_functions.h"
#include "userfault_handler.h"
#include "msi_statemachine.h"

#define TOTAL_NUM_ARGS		(4)

char* msi_strings[NUM_TAGS] = {"INVALID", "MODIFIED", "SHARED"};

/* Global page array */
struct msi_page pages[MAX_PAGES];
unsigned long g_pages_mapped;

/*Victim process id */
pid_t victim_pid;

static void initialize_msi_pages()
{
	int i;
	for(i = 0; i < MAX_PAGES; ++i){
		pages[i].tag = INVALID;
		pthread_mutex_init(&pages[i].mutex, NULL);
		pages[i].in_use = false;
		pages[i].start_address = NULL;
	}
}

static void address_msi_pages(uint64_t mmap_addr)
{
	int i;
	uint64_t page_addr = mmap_addr;
	int page_size = sysconf(_SC_PAGE_SIZE);
	for(i = 0; i < MAX_PAGES; ++i, page_addr+=page_size){
		pages[i].start_address = (void*)page_addr;
	}
}

int dsm_main(dsm_args args){
	int socket_fd;
	char fgets_buffer[100];

	/* Bus thread related */
	struct bus_thread_args bus_args;
	pthread_t bus_thread;
	int bus_thread_ret;

	/* User fault thread related */
	struct mmap_args shared_mapping;
	pthread_t userfaultfd_thread;

	/* Message */
	struct msi_message msg;

	victim_pid = args.victim_pid;
	initialize_msi_pages();

	/* Create client first and try to connect. If other server doesn't
	 * exist, then we are the first node. Else, we are the second node. */
	socket_fd = try_connect_client(args.remote_port, args.remote_ip, &bus_args,
				       &shared_mapping);
	if (socket_fd > 0){
		/* We are the client node*/
		mmap(shared_mapping.memory_address, shared_mapping.len,
		     PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS |
		     MAP_FIXED, -1, 0);
		memset(shared_mapping.memory_address, 0, shared_mapping.len);
	}else{
		/* There is no server to connect to so we set up ourselves as the server*/
		socket_fd = setup_server(args.host_port, &bus_args,
					 &shared_mapping, args.flt_reg);
	}

	bus_thread_ret = pthread_create(&bus_thread, NULL,
					     bus_thread_handler,
					     (void *) &bus_args);
	if (bus_thread_ret != 0) {
		errno = bus_thread_ret;
		errExit("pthread_create");
	}

    address_msi_pages((uint64_t)shared_mapping.memory_address);

	setup_userfaultfd_region(shared_mapping.memory_address,
					shared_mapping.len, &userfaultfd_thread,
					&fault_handler_thread, socket_fd, args.uffd, args.victim_pid);

	for(;;);

	return 0;
}
