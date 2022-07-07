#include <stdio.h>
#include <pthread.h>
#include <sys/mman.h>

#include "../inc/log.h"
#include "../inc/popsgx_page_handler.h"
#include "../inc/msi_handler.h"
#include "../inc/compel_handler.h"

/* --------------------------------------------------------------------
 * Public Functions
 * -------------------------------------------------------------------*/
void msi_request_page(msi_handler *msi, int sk, char* page, void* fault_addr, unsigned int rw)
{
    int ret;
    struct msi_message msg; 

    pthread_mutex_lock(&msi->bus_lock);

    popsgx_page *page_to_transition = popsgx_find_page(msi->pgHdl, (void *)fault_addr);
    if(!page_to_transition){
        ret = -1;
        log_error("Could not find the relevant page with address %p", fault_addr);
        goto msi_request_page_fail;
    }

    pthread_mutex_lock(&page_to_transition->mutex_lock);

    msg.message_type = INVALID_STATE_READ;
    msg.payload.request_page.address = (uint64_t) fault_addr;
    msg.payload.request_page.size = PAGE_SIZE;
   
    ret = write(sk, &msg, sizeof(msg));
    if(ret <=0)
        goto msi_request_write_fail;
    
    memset(&msi->data_buffer, 0, PAGE_SIZE);
    msi->wait_for_reply = 1;
    while(msi->wait_for_reply == 1){
        pthread_cond_wait(&msi->page_reply_cond, &msi->bus_lock);
    }

    log_debug("page memcpy");
    memcpy(page, &msi->data_buffer, PAGE_SIZE);
    ret = popsgx_set_page_tag(msi->pgHdl, page_to_transition, SHARED);
    if(ret){
        log_error("Setting the tag failed for the page address %p", page_to_transition);
    }

msi_request_write_fail:
    pthread_mutex_unlock(&msi->bus_lock);
msi_request_page_fail:
    pthread_mutex_unlock(&page_to_transition->mutex_lock);
    return ret;
}

void msi_handle_page_request(msi_handler *msi ,int sk, struct msi_message *in_msg){
    int ret;
    struct msi_message msg_out;

    popsgx_page *page_to_transition = popsgx_find_page(msi->pgHdl, (void*)in_msg->payload.request_page.address);
    if(!page_to_transition){
        log_error("Could not find the relevant page with address %p", in_msg->payload.request_page.address);
        ret = -1;
        goto msi_page_request_fail;
    }

    msg_out.message_type = PAGE_REPLY;

    /*If I'm invalid too, then I'll give you an empty page */
    if(page_to_transition->tag == INVALID){
        memset(msg_out.payload.page_data, '0', PAGE_SIZE);
    }else{
        /* Else I'll give you my local memory storage, won't trigger
		 * pagefault since it's already been edited anyway */
        memcpy(msg_out.payload.page_data, page_to_transition->trace_physical_address, PAGE_SIZE);
    }

    pthread_mutex_lock(&page_to_transition->mutex_lock);
    ret = write(sk, &msg_out, sizeof(msg_out));
    if(ret <= 0){
        goto msi_page_write_fail;
    }

    ret = popsgx_set_page_tag(msi->pgHdl, page_to_transition, SHARED);
    if(ret){
        log_error("Setting the tag failed for the page address %p", page_to_transition);
    }

msi_page_write_fail:
    pthread_mutex_unlock(&page_to_transition->mutex_lock);
msi_page_request_fail:
    return;
}

void msi_handle_page_invalidate(msi_handler *msi, int sk, struct msi_message *in_msg){
    int ret;
    struct msi_message msg;

    popsgx_page *page_to_transition = popsgx_find_page(msi->pgHdl, (void*)in_msg->payload.request_page.address);
    if(!page_to_transition){
        log_error("Could not find the relevant page with address %p", in_msg->payload.request_page.address);
        ret = -1;
        goto msi_handle_page_fail;
    }

    pthread_mutex_lock(&page_to_transition->mutex_lock);
    ret = popsgx_set_page_tag(msi->pgHdl, page_to_transition, INVALID);
    if(ret){
        log_error("Setting the tag failed for the page address %p", page_to_transition);
        goto msi_post_lock_fail;
    }

    if (ret = madvise(page_to_transition->trace_physical_address, PAGE_SIZE, MADV_DONTNEED)){
		log_error("fail to madvise");
        goto msi_post_lock_fail;
	}

    compel_ioctl_arg compelArgs;
    compelArgs.cmd = MADVISE;
    compelArgs.tracee_pid = msi->tracee_pid;
    compelArgs.cmd_args.madvArgs.page_address = page_to_transition->trace_physical_address;
    ret = compel_ioctl(&msi->cmpHdl, &compelArgs);
    if(ret){
        log_error("Could not retrieve the uffd");
        goto msi_post_lock_fail;
    }

    msg.message_type = INVALIDATE_ACK;
    ret = write(sk, &msg, sizeof(msg));
    if(ret <= 0){
        log_error("Could not invalidate the page");
    }   

msi_post_lock_fail:
    pthread_mutex_unlock(&page_to_transition->mutex_lock);
msi_handle_page_fail:
    return ret;
}

void msi_handle_page_reply(msi_handler *msi, int sk, struct msi_message *in_msg){
    pthread_mutex_lock(&msi->bus_lock);
    memcpy(&msi->data_buffer, in_msg->payload.page_data, PAGE_SIZE);

    msi->wait_for_reply = 0;
    pthread_cond_signal(&msi->page_reply_cond);
    pthread_mutex_unlock(&msi->bus_lock);
}

void msi_handle_write_command(msi_handler *msi, int sk, void *addr, void *data, size_t data_size){
    char write_buffer[100] = {0};
	unsigned long page_num = 0;
	struct msi_message msg;
	int ret;  

    popsgx_page *page_to_transition = popsgx_find_page(msi->pgHdl, (void*)addr);
    if(!page_to_transition){
        log_error("Could not find the relevant page with address %p", addr);
        ret = -1;
        goto msi_handle_write_fail;
    }

    int vdata = *(int*)(data+0xf);
    if(page_to_transition){
        log_debug("Copying %c to address %p", vdata, page_to_transition->trace_physical_address);
        memcpy(page_to_transition->trace_physical_address, data, data_size);
        ret = popsgx_set_page_tag(msi->pgHdl, page_to_transition, INVALID);
        if(ret){
            log_error("Setting the tag failed for the page address %p", page_to_transition);
            goto msi_handle_write_fail;
        }

        page_to_transition->tag = MODIFIED;
        msg.message_type = INVALIDATE;
        msg.payload.invalidate_page.address = (uint64_t)page_to_transition->trace_physical_address;
        ret = write(sk, &msg, sizeof(msg));
        if(ret <= 0){
            log_error("Bad write in MSI");
        }
    }

msi_handle_write_fail:
    return ret;
}

int msi_handler_init(msi_handler *msi, pid_t tracee_pid, popsgx_page_handler *pgHdl, compel_handler *cmpHdl){
    int ret = 0;

    if(msi == NULL){
        log_error("MSI handler is NULL");
        ret = -1;
        goto msi_handler_fail;
    }

    pthread_cond_t p = PTHREAD_COND_INITIALIZER;
    msi->page_reply_cond = p;

    if(pgHdl != NULL){
        msi->pgHdl = pgHdl;
    }
    else{
        log_error("Page handler is NULL");
        ret = -1;
        goto msi_handler_fail;
    }

    if(cmpHdl != NULL){
        msi->cmpHdl = cmpHdl;
    }else{
        log_error("Compel handler is NULL");
        ret = -1;
        goto msi_handler_fail;
    }

    *(msi->data_buffer) = 0;
    msi->wait_for_reply = 0;
    msi->tracee_pid = tracee_pid;

msi_handler_fail:
    return ret;
}



