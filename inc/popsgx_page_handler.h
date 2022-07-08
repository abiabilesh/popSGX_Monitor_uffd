#ifndef __POPSGX_PAGES_H__
#define __POPSGX_PAGES_H__

#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>

/* --------------------------------------------------------------------
 * Macros
 * -------------------------------------------------------------------*/
#define PAGE_SIZE sysconf(_SC_PAGE_SIZE)

/* --------------------------------------------------------------------
 * Structures & Required Datatypes
 * -------------------------------------------------------------------*/
enum page_tag{
    INVALID = 0,
    MODIFIED,
    SHARED,
    NUM_TAGS
};

typedef struct popsgx_page_t{
    enum page_tag tag;
    bool in_use;
    pthread_mutex_t mutex_lock;
    void* trace_physical_address;
    void* monitor_virtual_address;
}popsgx_page;

typedef struct popsgx_page_handler_t{
    bool is_initialized;
    int no_pages;
    long int host_tracee_start_address;
    long int remote_tracee_start_address;
    long int monitor_start_address;
    popsgx_page *buffer_pages;
}popsgx_page_handler;

/* --------------------------------------------------------------------
 * Public Functions
 * -------------------------------------------------------------------*/
int popsgx_pgHandler_init(popsgx_page_handler *pgHandler, int no_pages);
void popsgx_pgHandler_destroy(popsgx_page_handler *pgHandler);
int pgHandler_setup_memory(popsgx_page_handler *pgHandler, long int host_address, long int remote_address);
popsgx_page* popsgx_find_page(popsgx_page_handler *pgHandler, void* fault_address);
int popsgx_set_page_tag(popsgx_page_handler *pgHandler, popsgx_page* page, enum page_tag tag);
long int popsgx_host_to_remote_address(popsgx_page_handler *pgHandler, long int host_address);
long int popsgx_remote_to_host_address(popsgx_page_handler *pgHandler, long int remote_address);
long int popsgx_remote_to_monitor_address(popsgx_page_handler *pgHandler, long int remote_address);
long int popsgx_host_to_monitor_address(popsgx_page_handler *pgHandler, long int host_address);
long int popsgx_monitor_to_host_address(popsgx_page_handler *pgHandler, long int monitor_address);
long int popsgx_monitor_to_remote_address(popsgx_page_handler *pgHandler, long int monitor_address);
#endif