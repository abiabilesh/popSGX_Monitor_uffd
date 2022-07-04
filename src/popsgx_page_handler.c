#include <stdio.h>
#include <stdlib.h>

#include "../inc/popsgx_page_handler.h"
#include "../inc/log.h"


/* --------------------------------------------------------------------
 * Local Functions declarations
 * -------------------------------------------------------------------*/
static void __initialize_page(popsgx_page* page);
static int __initialize_pages(popsgx_page** pages, int no_pages);

static void __destroy_page(popsgx_page* page);
static void __destroy_pages(popsgx_page **pages, int no_pages);

/* --------------------------------------------------------------------
 * Local Functions
 * -------------------------------------------------------------------*/
static void __initialize_page(popsgx_page* page){
    pthread_mutex_init(&page->mutex_lock, NULL);
    page->tag = INVALID;
    page->trace_physical_address = NULL;
    page->monitor_virtual_address = NULL;
    page->in_use = false;
}

static int __initialize_pages(popsgx_page** pages, int no_pages){
    int ret = 0;

    *pages = (popsgx_page*)malloc(no_pages * (sizeof(popsgx_page)));
    if(pages == NULL){
        log_error("Couldn't create enough pages, it failed with error %s", strerror(errno));
        ret = -1;
        goto initialize_pages_fail;
    }

    log_debug("Successfully initialized %d pages", no_pages);

    for(int iter = 0; iter < no_pages; iter++){
        __initialize_page((*pages)+iter);
    }

initialize_pages_fail:
    return ret;
}

static void __destroy_page(popsgx_page* page){
    pthread_mutex_destroy(&page->mutex_lock);
}

static void __destroy_pages(popsgx_page **pages, int no_pages){

    for(int iter = 0; iter < no_pages; iter++){
        __destroy_page((*pages)+iter);
    }

    free(*pages);
}

/* --------------------------------------------------------------------
 * Public Functions
 * -------------------------------------------------------------------*/
int popsgx_pgHandler_init(popsgx_page_handler *pgHandler, int no_pages){
    int ret = 0;

    if(pgHandler == NULL){
        log_error("page handler is NULL");
        return -1;
    }

    ret = __initialize_pages(&pgHandler->buffer_pages, no_pages);
    if(ret){
        log_error("Initialization of popsgx pages failed");        
        goto popsgx_pgHandler_init_fail;
    }

    pgHandler->no_pages = no_pages;
    pgHandler->is_initialized = true;

    return ret;

popsgx_pgHandler_init_fail:
    free(pgHandler->buffer_pages);
    return ret;
}

void popsgx_pgHandler_destroy(popsgx_page_handler *pgHandler){
    __destroy_pages(&pgHandler->buffer_pages, pgHandler->no_pages);
}