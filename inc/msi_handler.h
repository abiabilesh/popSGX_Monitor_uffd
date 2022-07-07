#ifndef __MSI_HANDLER_H__
#define __MSI_HANDLER_H__

#include <stdio.h>
#include <pthread.h>

#include "../inc/messages.h"
#include "../inc/compel_handler.h"
#include "../inc/popsgx_page_handler.h"

/* --------------------------------------------------------------------
 * Structures & Required Datatypes
 * -------------------------------------------------------------------*/

typedef struct msi_handler_t
{
    int wait_for_reply;
    pthread_mutex_t bus_lock;
    pthread_cond_t page_reply_cond;
    pid_t tracee_pid;
    popsgx_page_handler *pgHdl;
    compel_handler *cmpHdl;
    char data_buffer[4096];
}msi_handler;

/* --------------------------------------------------------------------
 * Public Functions
 * -------------------------------------------------------------------*/
int msi_handler_init(msi_handler* msi, pid_t tracee_pid, popsgx_page_handler *pgHdl, compel_handler *cmpHdl);

#endif