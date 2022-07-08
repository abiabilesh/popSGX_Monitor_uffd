#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "../inc/messages.h"
#include "../inc/log.h"
#include "../inc/dsm_handler.h"
#include "../inc/msi_handler.h"

typedef struct bus_handler_args{
    dsm_handler *dsm;
    msi_handler *msi;
}bus_handler_args;

bus_handler_args bus_args;

 /* --------------------------------------------------------------------
 * Local Functions declarations
 * -------------------------------------------------------------------*/
static int __connect_as_client(remote_connection remote, int *socket_fd);
static int __connect_as_server(host_connection host, int *socket_fd);


/* --------------------------------------------------------------------
 * Local Functions
 * -------------------------------------------------------------------*/
static int __connect_as_client(remote_connection remote, int *socket_fd){
    int ret = 0;
    int sk = 0;
    
    struct sockaddr_in addr;
    sk = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0) {
        log_error("Failed on creating a socket");
        ret = sk;
		goto out_socket_err;
	}

    log_info("Connecting as a client to %s:%d", remote.ip, remote.port);

    memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;

    ret = inet_aton(remote.ip, &addr.sin_addr);
	if (ret < 0) {
		goto out_close_socket;
	}

    addr.sin_port = htons(remote.port);

    ret = connect(sk, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		goto out_close_socket;
	}

    log_info("Connection established with the server");

    *socket_fd = sk;

    return ret;

out_close_socket:
	close(sk);
out_socket_err:
	return ret;
}

static int __connect_as_server(host_connection host, int *socket_fd){
    int ret = 0;
    int sk, ask;
    struct sockaddr_in addr;
    
    sk = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0) {
		log_error("Failed on creating a socket");
        ret = sk;
        goto connect_as_server_failed;
	}

    memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(host.port);

    log_info("Establishing the node as server at port %d", host.port);
    ret = bind(sk, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		log_error("Binding to the port failed");
        goto connect_as_server_bind_failed;
	}

    ret = listen(sk, 16);
	if (ret < 0) {
		log_error("Listening on the port failed");
        goto connect_as_server_bind_failed;
	}

    log_info("Server waiting for connections");

    ask = accept(sk, NULL, NULL);
	if (ask < 0) {
        ret = ask;
		log_error("Server accept failed");
        goto connect_as_server_bind_failed;
	}

    log_info("Connection established with client");

    *socket_fd = ask;

    close(sk);
    return ret;

connect_as_server_bind_failed:
    close(sk);
connect_as_server_failed:
    return ret;
}

static void bus_thread_cleanup_handler(void* arg)
{
	/* Close out the sockets so we don't have loose ends */
	int sk = *(int*)arg;
	/* Ensure it's not stdin/out/err */
	log_debug("Cleanup handler called: %d", sk);
	if (sk >= 2)
		close(sk);
}

void* bus_handler_func(void *arg){
    int rd;
    struct msi_message msg;
    bus_handler_args* bus_args = (bus_handler_args*)arg;
    dsm_handler *dsm = bus_args->dsm;
    msi_handler *msi = bus_args->msi;

    //log_debug("bus handler msi %p", msi->pgHdl);

    if(!msi){
        log_error("The bus handler argument is NULL");
    }

    pthread_cleanup_push(bus_thread_cleanup_handler, &dsm->socket_fd);

    for(;;){
        rd = read(dsm->socket_fd, &msg, sizeof(msg));
        if(rd < 0){
            log_error("Read error in dsm");
            return NULL;
        }

        switch(msg.message_type){
            case DISCONNECT:
                close(dsm->socket_fd);
                return NULL;
            case INVALID_STATE_READ:
                log_debug("INVALID_STATE_READ_MSG_RECEIVED");
                msi_handle_page_request(msi, dsm->socket_fd, &msg);
                break;
            case INVALIDATE:
                log_debug("INVALIDATE RECEIVED");
                msi_handle_page_invalidate(msi, dsm->socket_fd, &msg);
                break;
            case PAGE_REPLY:
                log_debug("PAGE_REPLY_RECEIVED");
                msi_handle_page_reply(msi, dsm->socket_fd, &msg);
                break;
            case INVALIDATE_ACK:
                log_debug("INVALIDATE_ACK");
                break;
            default:
                log_error("Unhandled bus request, %d", msg.message_type);
                break;
        }
    }

    pthread_cleanup_pop(0);
    return NULL;
}

/* --------------------------------------------------------------------
 * Public Functions
 * -------------------------------------------------------------------*/
int dsm_handler_init(dsm_handler *dsm, char* remote_ip, int remote_port, int host_port){
    int ret = 0;

    if(dsm == NULL){
        log_error("dsm is NULL");
        ret = -1;
        goto dsm_handler_init_failed;
    }

    dsm->socket_fd = -1;
    dsm->host.port = host_port;
    dsm->remote.ip = remote_ip;
    dsm->remote.port = remote_port;
    dsm->mode = INVALID_MODE;

dsm_handler_init_failed:
    return ret;
}

void dsm_handler_destroy(dsm_handler *dsm){
    close(dsm->socket_fd);
}

int dsm_establish_communication(dsm_handler *dsm, long int *pg_address, int *no_pgs){
    int ret = 0;
    struct msi_message msg;
    long int host_pg_address = *pg_address;
    int host_no_pgs = *no_pgs;

    ret =  __connect_as_client(dsm->remote, &dsm->socket_fd);
    if(ret){
        log_info("Setting up the node as server as we are the only one");
        
        ret = __connect_as_server(dsm->host, &dsm->socket_fd);
        if(ret){
            log_error("Couldn't establish communication as server or client");
            return ret;
        }
        dsm->mode = SERVER_MODE;

        int nret;
        msg.message_type = CONNECTION_ESTABLISHED;
        msg.payload.memory_pair.address = (uint64_t) *pg_address;
        msg.payload.memory_pair.size = *no_pgs;
        nret = write(dsm->socket_fd, &msg, sizeof(msg));
        if(nret <= 0){
            ret = nret;
            log_error("Couldn't send message to the client side");
        }

        log_info("Sent the shared memory address information to client");

        nret = read(dsm->socket_fd, &msg, sizeof(msg));
        if(nret < 0){
            ret = nret;
            log_error("Couldn't receieve message from the server side");
            goto dsm_establish_communication_fail;
        }

        if(msg.message_type == CONNECTION_ESTABLISHED){
            log_info("Pairing Request Received: Addr: 0x%lx, Length: %lu",                  \
		                                            msg.payload.memory_pair.address,        \
		                                            msg.payload.memory_pair.size);
        }else{
            log_error("Received a wrong message %d from the client", msg.message_type);
            ret = -1;
            goto dsm_establish_communication_fail;
        }

        *pg_address = msg.payload.memory_pair.address;
        *no_pgs = msg.payload.memory_pair.size;

    }else{
        dsm->mode = CLIENT_MODE;
        int nret;
        nret = read(dsm->socket_fd, &msg, sizeof(msg));
        if(nret < 0){
            ret = nret;
            log_error("Couldn't receieve message from the server side");
            goto dsm_establish_communication_fail;
        }

        if(msg.message_type == CONNECTION_ESTABLISHED){
            log_info("Pairing Request Received: Addr: 0x%lx, Length: %lu",                  \
		                                            msg.payload.memory_pair.address,        \
		                                            msg.payload.memory_pair.size);
        }else{
            log_error("Received a wrong message %d from the server", msg.message_type);
            ret = -1;
            goto dsm_establish_communication_fail;
        }

        *pg_address = msg.payload.memory_pair.address;
        *no_pgs = msg.payload.memory_pair.size;

        msg.message_type = CONNECTION_ESTABLISHED;
        msg.payload.memory_pair.address = (uint64_t) host_pg_address;
        msg.payload.memory_pair.size = host_no_pgs;
        nret = write(dsm->socket_fd, &msg, sizeof(msg));
        if(nret <= 0){
            ret = nret;
            log_error("Couldn't send message to the server side");
        }

        log_info("Sent the shared memory address information to server");

    }

dsm_establish_communication_fail:
    return ret;
}

int dsm_handle_bus_messages(dsm_handler *dsm, msi_handler *msi){
    int ret = 0;


    bus_args.dsm = dsm;
    bus_args.msi = msi;

    ret = pthread_create(&dsm->bus_handler_thread, NULL, bus_handler_func, (void *)&bus_args);
    if(ret != 0){
        log_error("Could not establish bus handler thread");
    }

    return ret;
}