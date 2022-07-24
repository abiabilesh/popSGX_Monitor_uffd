#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/mman.h>

#include "../inc/ptrace.h"
#include "../inc/log.h"
#include "../inc/popsgx_monitor.h"
#include "../inc/compel_handler.h"
#include "../inc/dsm_handler.h"
#include "../inc/uffd_handler.h"

extern char* __progname;

// Required number of arguments for the application
#define OPT_MANDATORY_COUNT 7

/**
 * @brief Function declarations
 * 
 */
static void usage(void);
static int execute_tracee_app(popsgx_child *tracee);

/**
 * @brief Printing the help message
 * 
 */
static void usage(void)
{
    log_info("\n"
             "usage: %s [-m mode | -v victim | -r remote-node-ip | -p remote-node-port | -t host-port | -s shared_mem | -n no_pages]"
             "\n"
             "options:\n"
             "\t-m mode of the popsgx_monitor application either server or client\n"
             "\t-v victim process to serve page-faults & ditributed-memory-sharing\n"
             "\t-r remote node's ip-address for dsm\n"
             "\t-p remote node's port-number for dsm\n"
             "\t-t host's port-number\n"
             "\t-s address of the memory region to be shared\n"
             "\t-n number of pages to be shared\n"
             "\t-h help"
             "\n",
             __progname);
    exit(EXIT_SUCCESS);
}

/**
 * @brief Execute the tracee application 
 * 
 * @param tracee 
 * @return int 
 */
static int execute_tracee_app(popsgx_child *tracee){
    int rc = 0;
    pid_t tracee_pid;

    tracee_pid = fork();
    if(tracee_pid < 0){
        log_error("Forking failed with error %s", strerror(errno));
        return -1;
    }else if(tracee_pid == 0){
        char *user_args[] = {"../host/helloworldhost", "../enclave/helloworldenc.signed", "--simulate", NULL};
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);
        // We are into the child process now
        //execl(tracee->c_path, tracee->c_path, NULL);
        execve(user_args[0], user_args, NULL);
        // Should not execute the below line
        log_error("Failed on execl of the tracee with error %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    tracee->c_pid = tracee_pid;
    
    log_info("Successfully forked the tracee as a child process %d", tracee_pid);
    return rc;
}

/**
 * @brief Get the stack frame address
 * 
 * @param tracee_pid 
 * @param stack_start_address 
 * @return int (size of the stack frame)
 */
static int get_virtual_address_frame(pid_t tracee_pid, unsigned long *stack_start_address, char *frame){
    int ret = 0;
    char *sret;
    FILE *fp;
    char file_name[50];
    char line[128];
    unsigned long stack_end_address;

    ret = snprintf(file_name, 50, "/proc/%d/maps", tracee_pid);
    if(ret < 0){
        log_error("failed in finding the maps file for the process %d", tracee_pid);
        goto get_stack_frame_fail;
    }

    fp = fopen(file_name, "r");
    if(!fp)
        goto get_stack_frame_fail;
    
    while(fgets(line, sizeof(line), fp)){
        sret = strstr(line, frame);
        if(sret){
            char *ptr;
            *stack_start_address = strtoul(line, &ptr, 16);            
            stack_end_address = strtoul(ptr+1, NULL, 16);
            log_debug("The starting address of the pid %d stack : %lx", tracee_pid, *stack_start_address);
            log_debug("The ending address of the pid %d stack : %lx", tracee_pid, stack_end_address);
            ret = stack_end_address - *stack_start_address;
            break;
        }
    }

    if(!sret){
        log_debug("Could not find the stack frame in the tracee proc map");
        ret = -1;
    }
    
    fclose(fp);

get_stack_frame_fail:
    return ret;
}

/**
 * @brief main function of the application
 * 
 * @param argc 
 * @param argv 
 * @return int 
 */

int main(int argc, char* argv[]){
    int rc = 0;
    int status;
    int opt, opt_counter = 0;
    char *mode = NULL;
    unsigned long long shared_physical_address = -1;
    int no_of_shared_pages = -1;
    unsigned long long shared_heap_address = -1;
    int no_of_heap_pages = -1;
    popsgx_app monitor_app;

    memset(&monitor_app, 0, sizeof(popsgx_app));

    /*
     *  Parse the arguments
     */
    struct option long_opt[] =
    {
        {       "mode", required_argument, NULL, 'm'},
        {     "victim", required_argument, NULL, 'v'},
        {  "remote_ip", required_argument, NULL, 'r'},
        {"remote_port", required_argument, NULL, 'p'},
        {  "host_port", required_argument, NULL, 't'},
        { "shared_mem", required_argument, NULL, 's'},
        {   "no_pages", required_argument, NULL, 'n'},
        {         NULL,                 0, NULL,  0 }
    };

    while((opt = getopt_long(argc, argv, "hv:r:p:t:m:n:s:", long_opt, NULL)) != -1)
    {
        switch (opt)
        {
        case 'v':
            monitor_app.dsm.child.c_path = strdup(optarg);
            break;
        
        case 'r':
            monitor_app.dsm.remote_ip = strdup(optarg);
            break;

        case 'p':
            monitor_app.dsm.remote_port = atoi(optarg);
            break;

        case 't':
            monitor_app.dsm.host_port = atoi(optarg);
            break;
        
        case 's':
            shared_physical_address = strtoul(optarg, NULL, 16);
            break;
        
        case 'n':
            no_of_shared_pages = atoi(optarg);
            break;
        
        case 'm':
            mode = strdup(optarg);
            if(!strcmp("server", mode)){
                monitor_app.mode = SERVER;
            }else if(!strcmp("client", mode)){
                monitor_app.mode = CLIENT;
            }else{
                usage();
            }
            break;

        case 'h':
        default:
            usage();
            break;
        }
        opt_counter++;
    }
    if (optind < argc || opt_counter != OPT_MANDATORY_COUNT)
	{
		usage();
	}

    rc = execute_tracee_app(&monitor_app.dsm.child);
    if(rc){
        log_error("failed to execute the tracee app");
        goto out_fail; 
    }

    // if(monitor_app.mode == CLIENT || monitor_app.mode == SERVER){
    //     //unsigned long int expected_pc =  monitor_app.mode == CLIENT ? 0x43c292 : 0x43c154;
    //     unsigned long int expected_pc =  monitor_app.mode == CLIENT ? 0x43c252 : 0x43c252;
    //     while(1){
    //         unsigned long int pc = get_pc(monitor_app.dsm.child.c_pid);
    //         log_debug("PC:%lx", pc);
    //         if(pc == expected_pc){
    //             log_debug("Got the add call");
    //             break;
    //         }
    //         ptrace(PTRACE_SINGLESTEP, monitor_app.dsm.child.c_pid, NULL, NULL);
    //         waitpid(monitor_app.dsm.child.c_pid, &status, 0);
    //         if(WIFEXITED(status)) break;
    //     }
    // }

    if(monitor_app.mode == CLIENT || monitor_app.mode == SERVER){
        unsigned long int expected_inst_address = 0x43c40c;
        wait(&status);
        long old_data = set_breakpoint(monitor_app.dsm.child.c_pid, expected_inst_address);
        ptrace(PTRACE_CONT, monitor_app.dsm.child.c_pid, NULL, NULL);
        wait(&status);
        log_debug("Client hit a breakpoint\n");
        clear_breakpoint(monitor_app.dsm.child.c_pid, expected_inst_address, old_data);
    }

    rc = get_virtual_address_frame(monitor_app.dsm.child.c_pid, &shared_physical_address, "[stack]");
    if(rc < 0){
        log_error("Couldn't find the stack frame");
        goto out_fail;
    }

    no_of_shared_pages = rc / 4096;


    if(monitor_app.mode == CLIENT || monitor_app.mode == SERVER){
        rc = get_virtual_address_frame(monitor_app.dsm.child.c_pid, &shared_heap_address, "[heap]");
        if(rc < 0){
            log_error("Couldn;t find the heap frame");
            goto out_fail;
        }

        no_of_heap_pages = rc / 4096;
    }

    mmap(0x10000, (no_of_heap_pages + no_of_shared_pages) * 4096,                                     \
		     PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS |                                    \
		     MAP_FIXED, -1, 0);
	memset(0x10000, 0, (no_of_heap_pages + no_of_shared_pages) * 4096);
    rc = create_msi_pages(&monitor_app.dsm.msi, 0x10000, (no_of_heap_pages + no_of_shared_pages));
    if(rc){
        log_error("Couldn't create msi pages");
        goto out_msi_fail;
    }

    log_debug("The overall size is %x", 0x10000 + ((no_of_heap_pages + no_of_shared_pages) * 4096));

    rc = dsm_main(&monitor_app.dsm, monitor_app.mode);
    if(rc){
        log_error("failed to start dsm");
        goto out_dsm_fail;
    }

    if(monitor_app.mode == CLIENT){
        for(int i = 0; i < no_of_shared_pages; i++){
            char page[4096] = "";
            //if(i != no_of_shared_pages - 1){
                log_debug("Retrieving the page : %p to %p", shared_physical_address + (i * 4096), 0x10000 + (i * 4096));
                get_child_data(monitor_app.dsm.child.c_pid, page, shared_physical_address + (i * 4096), 4096);
                msi_handle_write_command(&monitor_app.dsm.msi, monitor_app.dsm.socket_fd, 0x10000 + (i * 4096), &page, 4096);
            // }else{
            //     log_debug("Retrieving the page : %p to %p", shared_physical_address + (i * 4096), 0x10000 + (i * 4096));
            //     get_child_data(monitor_app.dsm.child.c_pid, page, shared_physical_address + (i * 4096), 4096);
            //     msi_handle_write_command(&monitor_app.dsm.msi, monitor_app.dsm.socket_fd, 0x10000 + (i * 4096), &page, 4096);
            // }
        }

        for(int i = no_of_shared_pages, j = 0 ; i < (no_of_shared_pages + no_of_heap_pages); i++, j++){
            char page[4096] = "";
            //if(i != (no_of_shared_pages + no_of_heap_pages - 1)){
                log_debug("Retrieving the page : %p to %p", shared_heap_address + (j * 4096), 0x10000 + (i * 4096));
                get_child_data(monitor_app.dsm.child.c_pid, page, shared_heap_address + (j * 4096), 4096);
                msi_handle_write_command(&monitor_app.dsm.msi, monitor_app.dsm.socket_fd, 0x10000 + (i * 4096), &page, 4096);
            // }else{
            //     log_debug("Retrieving the page : %p to %p", shared_heap_address + (j * 4096), 0x10000 + (i * 4096));
            //     get_child_data(monitor_app.dsm.child.c_pid, page, shared_heap_address + (j * 4096), 4096);
            //     msi_handle_write_command(&monitor_app.dsm.msi, monitor_app.dsm.socket_fd, 0x10000 + (i * 4096), &page, 4096);
            // }
        }

        int as[100];
        get_regs_args(monitor_app.dsm.child.c_pid, &monitor_app.dsm.msi.regs, &as);
        log_debug("XIP: %lx", monitor_app.dsm.msi.regs.rip);
        msi_request_remote_execute(&monitor_app.dsm.msi, monitor_app.dsm.socket_fd);

        struct user_regs_struct usr_reg;
        while(!monitor_app.dsm.msi._can_request){
            sleep(0.1);
        }

        monitor_app.dsm.msi._can_request = false;

        for(int i = 0; i < (no_of_heap_pages + no_of_shared_pages); i++){
            msi_request_page(&monitor_app.dsm.msi, monitor_app.dsm.socket_fd, 0x10000 + (i * 4096), 0x10000 + (i * 4096), 0x00);
            //break;
        }

        msi_request_proc_reg(&monitor_app.dsm.msi, monitor_app.dsm.socket_fd, &usr_reg);
        //log_debug("XIP: %lx", usr_reg.rip);

        for(int i = 0; i < no_of_shared_pages; i++){
            //char page[4096] = "";
            //if(i != no_of_shared_pages - 1){
                log_debug("Retrieving the page : %p to %p", shared_physical_address + (i * 4096), 0x10000 + (i * 4096));
                update_child_data(monitor_app.dsm.child.c_pid, shared_physical_address + (i * 4096), 0x10000 + (i * 4096), 4096);
            // }else{
            //     log_debug("Retrieving the page : %p to %p", shared_physical_address + (i * 4096), 0x10000 + (i * 4096));
            //     update_child_data(monitor_app.dsm.child.c_pid, shared_physical_address + (i * 4096), 0x10000 + (i * 4096), 4096);
            // }
        }

        for(int i = no_of_shared_pages, j = 0; i < (no_of_shared_pages + no_of_heap_pages); i++, j++){
            //char page[4096] = "";
            // if(i != (no_of_heap_pages + no_of_shared_pages - 1)){
                log_debug("Retrieving the page : %p to %p", shared_heap_address + (j * 4096), 0x10000 + (i * 4096));
                update_child_data(monitor_app.dsm.child.c_pid, shared_heap_address + (j * 4096), 0x10000 + (i * 4096), 4096);
            // }else{  
            //     log_debug("Retrieving the page : %p to %p", shared_heap_address + (j * 4096), 0x10000 + (i * 4096));
            //     update_child_data(monitor_app.dsm.child.c_pid, shared_heap_address + (j * 4096), 0x10000 + (i * 4096), 4096);
            // }
        }

        //ptrace(PTRACE_SETREGS, monitor_app.dsm.child.c_pid, NULL, &usr_reg);
        ptrace(PTRACE_SETREGS, monitor_app.dsm.child.c_pid, NULL, &usr_reg);
        // while(1){
        //     unsigned long int pc = get_pc(monitor_app.dsm.child.c_pid);
        //     log_info("PC:%lx", pc);
        //     // if(pc == expected_pc){
        //     //     log_debug("Got the add call");
        //     //     break;
        //     // }
        //     ptrace(PTRACE_SINGLESTEP, monitor_app.dsm.child.c_pid, NULL, NULL);
        //     waitpid(monitor_app.dsm.child.c_pid, &status, 0);
        //     if(WIFEXITED(status)) break;
        // }
        ptrace(PTRACE_DETACH, monitor_app.dsm.child.c_pid, NULL, NULL);

    }else{
        struct user_regs_struct usr_reg;
        while(!monitor_app.dsm.msi._can_request){
            sleep(0.1);
        }

        monitor_app.dsm.msi._can_request = false;

        for(int i = 0; i < (no_of_heap_pages + no_of_shared_pages); i++){
            msi_request_page(&monitor_app.dsm.msi, monitor_app.dsm.socket_fd, 0x10000 + (i * 4096), 0x10000 + (i * 4096), 0x00);
            //break;
        }


        monitor_app.dsm.msi._can_request = false;
        msi_request_proc_reg(&monitor_app.dsm.msi, monitor_app.dsm.socket_fd, &usr_reg);
        //log_debug("XIP: %lx", usr_reg.rip);

        for(int i = 0; i < no_of_shared_pages; i++){
            //char page[4096] = "";
            // if(i != no_of_shared_pages - 1){
                log_debug("Retrieving the page : %p to %p", shared_physical_address + (i * 4096), 0x10000 + (i * 4096));
                update_child_data(monitor_app.dsm.child.c_pid, shared_physical_address + (i * 4096), 0x10000 + (i * 4096), 4096);
            // }else{
            //     log_debug("Retrieving the page : %p to %p", shared_physical_address + (i * 4096), 0x10000 + (i * 4096));
            //     update_child_data(monitor_app.dsm.child.c_pid, shared_physical_address + (i * 4096), 0x10000 + (i * 4096), 4096);
            // }
        }

        for(int i = no_of_shared_pages, j = 0; i < (no_of_shared_pages + no_of_heap_pages); i++, j++){
            //char page[4096] = "";
            // if(i != (no_of_heap_pages + no_of_shared_pages - 1)){
                log_debug("Retrieving the page : %p to %p", shared_heap_address + (j * 4096), 0x10000 + (i * 4096));
                update_child_data(monitor_app.dsm.child.c_pid, shared_heap_address + (j * 4096), 0x10000 + (i * 4096), 4096);
            // }else{  
            //     log_debug("Retrieving the page : %p to %p", shared_heap_address + (j * 4096), 0x10000 + (i * 4096));
            //     update_child_data(monitor_app.dsm.child.c_pid, shared_heap_address + (j * 4096), 0x10000 + (i * 4096), 4096);
            // }
        }

        ptrace(PTRACE_SETREGS, monitor_app.dsm.child.c_pid, NULL, &usr_reg);
        //ptrace(PTRACE_DETACH, monitor_app.dsm.child.c_pid, NULL, NULL);
        // ptrace(PTRACE_SINGLESTEP, monitor_app.dsm.child.c_pid, NULL, NULL);

        // waitpid(monitor_app.dsm.child.c_pid, &status, 0);

        // log_debug("xxx");
        // unsigned long int expected_pc = 0x43c257;
        // while(1){
        //     unsigned long int pc = get_pc(monitor_app.dsm.child.c_pid);
        //     //log_info("PC:%lx", pc);
        //     if(pc == expected_pc){
        //         log_debug("Got the add call");
        //         break;
        //     }
        //     ptrace(PTRACE_SINGLESTEP, monitor_app.dsm.child.c_pid, NULL, NULL);
        //     waitpid(monitor_app.dsm.child.c_pid, &status, 0);
        //     if(WIFEXITED(status)) break;
        // }

        unsigned long int expected_inst_address = 0x43c411;
        long old_data = set_breakpoint(monitor_app.dsm.child.c_pid, expected_inst_address);
        
        ptrace(PTRACE_CONT, monitor_app.dsm.child.c_pid, NULL, NULL);
        wait(&status);
        log_debug("Client hit a breakpoint\n");
        clear_breakpoint(monitor_app.dsm.child.c_pid, expected_inst_address, old_data);

        for(int i = 0; i < no_of_shared_pages; i++){
             char page[4096] = "";
            // if(i != no_of_shared_pages - 1){
                log_debug("Retrieving the page : %p to %p", shared_physical_address + (i * 4096), 0x10000 + (i * 4096));
                get_child_data(monitor_app.dsm.child.c_pid, page, shared_physical_address + (i * 4096), 4096);
                msi_handle_write_command(&monitor_app.dsm.msi, monitor_app.dsm.socket_fd, 0x10000 + (i * 4096), &page, 4096);
            // }else{
            //     log_debug("Retrieving the page : %p to %p", shared_physical_address + (i * 4096), 0x10000 + (i * 4096));
            //     get_child_data(monitor_app.dsm.child.c_pid, page, shared_physical_address + (i * 4096), 4096);
            //     msi_handle_write_command(&monitor_app.dsm.msi, monitor_app.dsm.socket_fd, 0x10000 + (i * 4096), &page, 4096);
            // }
        }

        for(int i = no_of_shared_pages, j = 0 ; i < (no_of_shared_pages + no_of_heap_pages); i++, j++){
            char page[4096] = "";
            // if(i != (no_of_shared_pages + no_of_heap_pages - 1)){
                log_debug("Retrieving the page : %p to %p", shared_heap_address + (j * 4096), 0x10000 + (i * 4096));
                get_child_data(monitor_app.dsm.child.c_pid, page, shared_heap_address + (j * 4096), 4096);
                msi_handle_write_command(&monitor_app.dsm.msi, monitor_app.dsm.socket_fd, 0x10000 + (i * 4096), &page, 4096);
            // }else{
            //     log_debug("Retrieving the page : %p to %p", shared_heap_address + (j * 4096), 0x10000 + (i * 4096));
            //     get_child_data(monitor_app.dsm.child.c_pid, page, shared_heap_address + (j * 4096), 4096);
            //     msi_handle_write_command(&monitor_app.dsm.msi, monitor_app.dsm.socket_fd, 0x10000 + (i * 4096), &page, 4096);
            // }
        }

        int as[100];
        get_regs_args(monitor_app.dsm.child.c_pid, &monitor_app.dsm.msi.regs, &as);
        log_debug("XIP: %lx", monitor_app.dsm.msi.regs.rip);
        msi_request_remote_execute(&monitor_app.dsm.msi, monitor_app.dsm.socket_fd);
    }

    // //return 0;



    // // rc = compel_steal_uffd(&monitor_app.dsm.child,          \
    // //                        &monitor_app.dsm.child.uffd,     \
    // //                        shared_physical_address,         \
    // //                        no_of_shared_pages);
    // // if(rc){
    // //     log_error("failed to steal uffd");
    // //     goto out_fail;
    // // }
    // // log_debug("the stolen uffd is %d", monitor_app.dsm.child.uffd);

   

    // rc = dsm_main(&monitor_app.dsm, monitor_app.mode);
    // if(rc){
    //     log_error("failed to start dsm");
    //     goto out_dsm_fail;
    // }

    // // monitor_app.uffd_hdl.args.child = &monitor_app.dsm.child;
    // // monitor_app.uffd_hdl.args.msi = &monitor_app.dsm.msi;
    // // monitor_app.uffd_hdl.args.sock_fd = monitor_app.dsm.socket_fd;
    // // rc = start_uffd_thread_handler(&monitor_app.uffd_hdl);
    // // if(rc){
    // //     log_error("failed to start uffd thread");
    // //     goto out_uffd_thread_fail;
    // // }

    while(1);
 
out_uffd_thread_fail:
    //Implement stopping the dsm & its threads
out_dsm_fail:
    //Delete the msi pages
out_msi_fail:
out_fail:
    //Implement killing the tracee process
    return rc;
}

