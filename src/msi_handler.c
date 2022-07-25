#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <stdbool.h>

#include "../inc/messages.h"
#include "../inc/pages.h"
#include "../inc/log.h"
#include "../inc/msi_handler.h"

/* --------------------------------------------------------------------
 * Public Functions defintions
 * -------------------------------------------------------------------*/
int msi_request_page(msi_handler *msi, int sk, char* page, void* fault_addr, unsigned int rw)
{
    int ret;
    struct msi_message msg; 

    pthread_mutex_lock(&msi->mutex);

    popsgx_page *page_to_transition = find_page(&msi->buffer, (void *)fault_addr);
    if(!page_to_transition){
        ret = -1;
        log_error("Could not find the relevant page with address %p", fault_addr);
        goto msi_request_page_fail;
    }

    pthread_mutex_lock(&page_to_transition->mutex);

    msg.message_type = INVALID_STATE_READ;
    msg.payload.request_page.address = (uint64_t) fault_addr;
    msg.payload.request_page.size = PAGE_SIZE;
   
    ret = write(sk, &msg, sizeof(msg));
    if(ret <=0)
        goto msi_request_write_fail;
    
    memset(&msi->tmp_buffer, 0, PAGE_SIZE);
    msi->wait_for_reply = 1;
    while(msi->wait_for_reply == 1){
        pthread_cond_wait(&msi->page_reply_cond, &msi->mutex);
    }

    memcpy(page, &msi->tmp_buffer, PAGE_SIZE);
    page_to_transition->tag = SHARED;

msi_request_write_fail:
    pthread_mutex_unlock(&msi->mutex);
msi_request_page_fail:
    pthread_mutex_unlock(&page_to_transition->mutex);
    return ret;
}

int msi_request_proc_reg(msi_handler *msi, int sk,  struct user_regs_struct *regs){
    int ret;
    struct msi_message msg;
    pthread_mutex_lock(&msi->mutex);

    msg.message_type = REMOTE_REGS;

    ret = write(sk, &msg, sizeof(msg));
    if(ret <= 0)
        goto msi_request_proc_reg_fail;
    
    memset(&msi->regs, 0, sizeof(struct user_regs_struct));
     msi->wait_for_reply = 1;
    while(msi->wait_for_reply == 1){
        pthread_cond_wait(&msi->page_reply_cond, &msi->mutex);
    }

    memcpy(regs, &msi->regs, sizeof(struct user_regs_struct));

msi_request_proc_reg_fail:
    pthread_mutex_unlock(&msi->mutex);
    return ret;
}

int msi_handle_reg_request(msi_handler *msi, int sk){
    int ret;
    struct msi_message msg_out;

    msg_out.message_type = REMOTE_REGS_REPLY;
    memcpy(&msg_out.payload.regs_message, &msi->regs, sizeof(struct user_regs_struct));

    log_debug("Hello xip: %p", msg_out.payload.regs_message.regs.rip);
    pthread_mutex_lock(&msi->mutex);
    ret = write(sk, &msg_out, sizeof(msg_out));
    if(ret <= 0){
        goto msi_handle_reg_request_fail;
    }

msi_handle_reg_request_fail:
    pthread_mutex_unlock(&msi->mutex);
    return ret;
}

int msi_handle_page_request(msi_handler *msi ,int sk, struct msi_message *in_msg){
    int ret;
    struct msi_message msg_out;

    popsgx_page *page_to_transition = find_page(&msi->buffer, (void*)in_msg->payload.request_page.address);
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
        memcpy(msg_out.payload.page_data, page_to_transition->popsgx_address, PAGE_SIZE);
    }

    pthread_mutex_lock(&page_to_transition->mutex);
    ret = write(sk, &msg_out, sizeof(msg_out));
    if(ret <= 0){
        goto msi_page_write_fail;
    }

    page_to_transition->tag = SHARED;

msi_page_write_fail:
    pthread_mutex_unlock(&page_to_transition->mutex);
msi_page_request_fail:
    return ret;
}

int msi_handle_page_invalidate(msi_handler *msi, int sk, struct msi_message *in_msg){
    int ret = 0;
    struct msi_message msg;

    log_debug("msi_handle_page_invalidate");
    log_debug("page address requested was %x", in_msg->payload.request_page.address);
    popsgx_page *page_to_transition = find_page(&msi->buffer, (void*)in_msg->payload.request_page.address);
    if(!page_to_transition){
        log_error("Could not find the relevant page with address %p", in_msg->payload.request_page.address);
        ret = -1;
        goto msi_handle_page_fail;
    }

    page_to_transition->tag = INVALID;

    log_debug("msi_handle_page_invalidate");
    if (ret = madvise(page_to_transition->popsgx_address, PAGE_SIZE, MADV_DONTNEED)){
		log_error("fail to madvise");
        goto msi_post_lock_fail;
	}

    msg.message_type = INVALIDATE_ACK;
    ret = write(sk, &msg, sizeof(msg));
    if(ret <= 0){
        log_error("Could not invalidate the page");
    }   

    log_debug("msi_handle_page_invalidate");
msi_post_lock_fail:
    pthread_mutex_unlock(&page_to_transition->mutex);
msi_handle_page_fail:
    return ret;
}

void msi_handle_page_reply(msi_handler *msi, int sk, struct msi_message *in_msg){
    pthread_mutex_lock(&msi->mutex);
    memcpy(&msi->tmp_buffer, in_msg->payload.page_data, PAGE_SIZE);

    msi->wait_for_reply = 0;
    pthread_cond_signal(&msi->page_reply_cond);
    pthread_mutex_unlock(&msi->mutex);
}

void msi_handle_regs_reply(msi_handler *msi, int sk, struct msi_message *in_msg){
    pthread_mutex_lock(&msi->mutex);
    memcpy(&msi->regs, &in_msg->payload.regs_message.regs, sizeof(struct user_regs_struct));
    log_debug("hello from in xip: %lx", in_msg->payload.regs_message.regs.rip);
    log_debug("hello from in msi regs: %lx", msi->regs.rip);
    msi->wait_for_reply = 0;
    pthread_cond_signal(&msi->page_reply_cond);
    pthread_mutex_unlock(&msi->mutex);
}

void msi_request_remote_execute(msi_handler *msi, int sk){
    int ret = 0;
    struct msi_message msg;
    pthread_mutex_lock(&msi->mutex);
    msg.message_type = REMOTE_EXECUTE;
    msg.payload.memory_pair.address = 0x10000;
    msg.payload.memory_pair.size = 33;
    ret = write(sk, &msg, sizeof(msg));
    if(ret <= 0){
        log_error("Bad write in MSI");
    }
    pthread_mutex_unlock(&msi->mutex);
}

void msi_handle_remote_execution(msi_handler *msi, int sk, struct msi_message *in_msg){
    // int ret = 0;
    // char page[4096] = {0};
    // for(int i = 0; i < in_msg->payload.memory_pair.size; i++){
    //     //msi_request_page(msi, sk, &page, (void*)in_msg->payload.memory_pair.address + (i * 4096), 0x00);
    //     break;
    // }
    pthread_mutex_lock(&msi->mutex);
    msi->_can_request = true;
    pthread_mutex_unlock(&msi->mutex);
}

int msi_handle_write_command(msi_handler *msi, int sk, void *addr, void *data, size_t data_size){
    char write_buffer[100] = {0};
	unsigned long page_num = 0;
	struct msi_message msg;
	int ret;  

    popsgx_page *page_to_transition = find_page(&msi->buffer, (void*)addr);
    if(!page_to_transition){
        log_error("Could not find the relevant page with address %p", addr);
        ret = -1;
        goto msi_handle_write_fail;
    }

    if(page_to_transition){
        memcpy(page_to_transition->popsgx_address, data, data_size);
        page_to_transition->tag = MODIFIED;
        msg.message_type = INVALIDATE;
        msg.payload.invalidate_page.address = (uint64_t)addr;
        ret = write(sk, &msg, sizeof(msg));
        if(ret <= 0){
            log_error("Bad write in MSI");
        }
    }

msi_handle_write_fail:
    return ret;
}

int create_msi_pages(msi_handler *msi, uint64_t popsgx_address, int no_pages){
    int rc = 0;

    if(msi == NULL){
        log_error("msi handle is NULL");
        rc = -1;
        goto out_fail;
    }

    rc = create_pages(&msi->buffer, popsgx_address, no_pages);
    if(rc){
        log_error("Could not create enough pages for the msi");
        goto out_fail;
    }

    
    pthread_mutex_init(&msi->mutex, NULL);
    msi->is_initialized = true;


out_fail:
    return rc;
}