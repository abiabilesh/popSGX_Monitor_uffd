#ifndef __POPSGX_PAGES_H__
#define __POPSGX_PAGES_H__

#include <stdbool.h>
#include <pthread.h>

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
    popsgx_page *buffer_pages;
}popsgx_page_handler;

/* --------------------------------------------------------------------
 * Public Functions
 * -------------------------------------------------------------------*/
int popsgx_pgHandler_init(popsgx_page_handler *pgHandler, int no_pages);
void popsgx_pgHandler_destroy(popsgx_page_handler *pgHandler);
#endif