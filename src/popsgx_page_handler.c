#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

#include "../inc/popsgx_page_handler.h"
#include "../inc/log.h"

/* --------------------------------------------------------------------
 * Local Functions declarations
 * -------------------------------------------------------------------*/
static void __initialize_page(popsgx_page* page);
static int __initialize_pages(popsgx_page** pages, int no_pages);
static void __map_address_to_virtualAddress(popsgx_page** pages, uint64_t mmap_addr, int no_pages);
static void __map_address_to_physicalAddress(popsgx_page** pages, uint64_t mmap_addr, int no_pages);

static void __destroy_page(popsgx_page* page);
static void __destroy_pages(popsgx_page **pages, int no_pages);

/* --------------------------------------------------------------------
 * Local Functions
 * -------------------------------------------------------------------*/
static void __map_address_to_virtualAddress(popsgx_page** pages, uint64_t mmap_addr, int no_pages){
    popsgx_page *buffer_pages = *pages;
    uint64_t page_addr = mmap_addr;
    for(int i = 0; i < no_pages; i++, page_addr+=PAGE_SIZE){
        buffer_pages[i].monitor_virtual_address = (void*)page_addr;
    }
}

static void __map_address_to_physicalAddress(popsgx_page** pages, uint64_t mmap_addr, int no_pages){
    popsgx_page *buffer_pages = *pages;
    uint64_t page_addr = mmap_addr;
    for(int i = 0; i < no_pages; i++, page_addr+=PAGE_SIZE){
        buffer_pages[i].trace_physical_address = (void*)page_addr;
    }
}

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

int pgHandler_setup_memory(popsgx_page_handler *pgHandler, long int address){
    int adr, ret = 0;
    int no_pages = pgHandler->no_pages;

    if(pgHandler->is_initialized){
        
        adr = mmap(address, no_pages * PAGE_SIZE,                                      \
                 PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED,           \
                 -1, 0);                                                               \

        if((void *)adr == -1){
            ret = adr;
            log_error("Creating a mmap failed with error %s", strerror(errno));
            goto pgHandler_setup_memory_failed;
        }
        
        memset(address, 0, no_pages * PAGE_SIZE);

        //Map the address for the page handler
        __map_address_to_physicalAddress(&pgHandler->buffer_pages, adr, pgHandler->no_pages);
        __map_address_to_virtualAddress(&pgHandler->buffer_pages, adr, pgHandler->no_pages);

    }else{
        log_error("Page handler is not initialized");
        return -1;
    }

pgHandler_setup_memory_failed:
    return ret;
}

popsgx_page* popsgx_find_page(popsgx_page_handler *pgHandler, void* fault_address){
    popsgx_page *page = NULL;
    popsgx_page *buffer_pages = pgHandler->buffer_pages;
    uint64_t addr_val = (uint64_t)fault_address;
    
    for(int iter = 0; iter < pgHandler->no_pages; iter++){
        if((uint64_t)buffer_pages[iter].trace_physical_address + PAGE_SIZE > fault_address){
            page = &buffer_pages[iter];
            break;
        }
    }

    log_debug("The page address is %p", page->trace_physical_address);

    return page;
}

int popsgx_set_page_tag(popsgx_page_handler *pgHandler, popsgx_page* page, enum page_tag tag){
    int ret = 0;
    popsgx_page *m_page = NULL;
    popsgx_page *buffer_page = pgHandler->buffer_pages;

    for(int iter = 0; iter < pgHandler->no_pages; iter++){
        if(&buffer_page[iter] == page){
            m_page = &buffer_page[iter];
        }
    }

    if(m_page == NULL){
        log_error("Could not find the page");
        ret = -1;
        goto popsgx_set_page_tag_fail;
    }

    m_page->tag = tag;

popsgx_set_page_tag_fail:
    return ret;
}