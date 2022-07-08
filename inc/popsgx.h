#ifndef __POPSGX_H__
#define __POPSGX_H__

#include "../inc/compel_handler.h"
#include "../inc/popsgx_page_handler.h"
#include "../inc/dsm_handler.h"
#include "../inc/msi_handler.h"

/* --------------------------------------------------------------------
 * Structures & Required Datatypes
 * -------------------------------------------------------------------*/
typedef struct popsgx_child_app_t{
    pid_t c_pid;
    int   c_argc;
    char  *c_argv;
    char  *c_path;
} popsgx_child;

typedef struct popsgx_app_t{
    popsgx_child child;
    compel_handler cmplHandler;
    popsgx_page_handler pgHandler;  
    dsm_handler dsmHandler;
    msi_handler msiHandler;
    pthread_t userfaultfd_thread;
} popsgx_app;

#endif
