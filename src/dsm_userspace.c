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

#define TOTAL_NUM_ARGS		(4)

char* msi_strings[NUM_TAGS] = {"INVALID", "MODIFIED", "SHARED"};

/* Global page array */
struct msi_page pages[MAX_PAGES];
unsigned long g_pages_mapped;

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

static void address_msi_pages(uint64_t mmap_addr, uint64_t phy_addr)
{
	int i;
	uint64_t page_addr = mmap_addr;
	int page_size = sysconf(_SC_PAGE_SIZE);
	for(i = 0; i < MAX_PAGES; ++i, page_addr+=page_size, phy_addr+=page_size){
		pages[i].start_address = (void*)page_addr;
		pages[i].physical_address = (void*)phy_addr;
	}
}

static void handle_write_command(int sk)
{
	//char write_buffer[WRITE_BUF_LEN] = {0};
	char write_buffer[WRITE_BUF_LEN] = "Abilesh";
	unsigned long page_num;
	struct msi_message msg;
	int write_ret;

	#if 0
	printf("\nWhat would you like to write?:\n");
	if (!fgets(write_buffer, WRITE_BUF_LEN, stdin))
		errExit("fgets error");
	#endif

	page_num = 0;
	if (page_num < g_pages_mapped) {
		printf("\nCopying %s to address %p\n", write_buffer,
		       pages[page_num].start_address);
		memcpy(pages[page_num].start_address, write_buffer,
		       strlen(write_buffer));
		pages[page_num].tag = MODIFIED;
		msg.message_type = INVALIDATE;
		msg.payload.invalidate_page.address =
			(uint64_t)pages[page_num].start_address;
		write_ret = write(sk, &msg, sizeof(msg));
		if (write_ret <= 0) {
			errExit("Bad write");
		}
	}
}

static void handle_read_command()
{
	unsigned long page_num;
	char *probe;

	page_num = 0;
	if (page_num < g_pages_mapped) {
		probe = (char*)pages[page_num].start_address;
		if (*probe == (int)0){
			printf("Read String: \n");
		}
		else {
			printf("Read String: %s\n", probe);
		}
	}
}

static void handle_msi_status_command()
{
	unsigned long page_num;

	page_num = 0;
	if (page_num < g_pages_mapped) {
		printf("[*]Page %lu: %s \n", page_num,
			       msi_strings[pages[page_num].tag]);
	}
}

#if 0
int
main(int argc, char *argv[])
{
	int socket_fd;
	char fgets_buffer[100];

	/* Bus thread related */
	struct bus_thread_args bus_args;
	pthread_t bus_thread;
	int bus_thread_ret;

	/* User fault thread related */
	struct mmap_args shared_mapping;
	pthread_t userfaultfd_thread;
	void* physical_address;
	int exit_write_ret;

	/* Message */
	struct msi_message msg;

	if (argc != TOTAL_NUM_ARGS) {
		fprintf(stderr, "Usage: %s my_port remote_ip remote_port\n",
			argv[0]);
		exit(EXIT_FAILURE);
	}

	initialize_msi_pages();

	/* Create client first and try to connect. If other server doesn't
	 * exist, then we are the first node. Else, we are the second node. */
	socket_fd = try_connect_client(atoi(argv[3]), argv[2], &bus_args,
				       &shared_mapping);

	if (socket_fd > 0){
		/* We have successfully connected and have a socket fd*/
		mmap(shared_mapping.memory_address, shared_mapping.len,
		     PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS |
		     MAP_FIXED, -1, 0);
	} else {
	/* There is no server to connect to so we set up ourselves as the server*/
		socket_fd = setup_server(atoi(argv[1]), &bus_args,
					 &shared_mapping);
	}
	
	bus_thread_ret = pthread_create(&bus_thread, NULL,
					     bus_thread_handler,
					     (void *) &bus_args);
	if (bus_thread_ret != 0) {
		errno = bus_thread_ret;
		errExit("pthread_create");
	}

	setup_userfaultfd_region(shared_mapping.memory_address,
				 &physical_address,
				 shared_mapping.len, &userfaultfd_thread,
				 &fault_handler_thread, socket_fd);

	address_msi_pages((uint64_t)shared_mapping.memory_address,
			  (uint64_t)physical_address);

	/* Prompt User for Command */
	for(;;) {
		printf("\nWhat would you like to do? (r)ead/(w)rite/(v)iew msi/E(x)it?: ");
		if (!fgets(fgets_buffer, INPUT_CMD_LEN, stdin))
			errExit("fgets error");

		if (!strncmp(fgets_buffer, "x", 1)){
			pthread_cancel(bus_thread);
			msg.message_type = DISCONNECT;
			exit_write_ret = write(socket_fd, &msg, sizeof(msg));
			if (exit_write_ret <= 0) {
				errExit("Exit Write Error");
			}
			goto exit_success;
		}
		else if (!strncmp(fgets_buffer, "w", 1)){
			handle_write_command(socket_fd);
		}
		else if (!strncmp(fgets_buffer, "r", 1)){
			handle_read_command();
		}
		else if (!strncmp(fgets_buffer, "v", 1)){
			handle_msi_status_command();
		}
	}

exit_success:
	printf("EXITING");
	exit(EXIT_SUCCESS);
}

#endif

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
	char* physical_address;
	int exit_write_ret;
	bool is_server = 0;

	/* Message */
	struct msi_message msg;

	initialize_msi_pages();

	/* Create client first and try to connect. If other server doesn't
	 * exist, then we are the first node. Else, we are the second node. */
	socket_fd = try_connect_client(args.remote_port, args.remote_ip, &bus_args,
				       &shared_mapping);
	if (socket_fd <= 0){
	/* There is no server to connect to so we set up ourselves as the server*/
		socket_fd = setup_server(args.host_port, &bus_args,
					 &shared_mapping, args.flt_reg);
		is_server = 1;
	}

#if 1
	bus_thread_ret = pthread_create(&bus_thread, NULL,
					     bus_thread_handler,
					     (void *) &bus_args);
	if (bus_thread_ret != 0) {
		errno = bus_thread_ret;
		errExit("pthread_create");
	}
#endif

	physical_address = mmap(NULL, shared_mapping.len, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (*physical_address == MAP_FAILED)
		errExit("mmap");
	memset(physical_address, 0, shared_mapping.len);

        address_msi_pages((uint64_t)shared_mapping.memory_address,
			(uint64_t)physical_address);

	if(is_server)
	setup_userfaultfd_region(shared_mapping.memory_address,
					&physical_address,
					shared_mapping.len, &userfaultfd_thread,
					&fault_handler_thread, socket_fd, args.uffd);
	
	
	for(;;);

	return 0;
}
