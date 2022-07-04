#ifndef __DSM_HANDLER_H__
#define __DSM_HANDLER_H__

/* --------------------------------------------------------------------
 * Structures & Required Datatypes
 * -------------------------------------------------------------------*/
typedef struct remote_connection_t{
    char  *ip;
    int  port;
}remote_connection;

typedef struct host_connection_t{
    int port;
}host_connection;

enum connection_type{
    SERVER_MODE = 0,
    CLIENT_MODE,
    INVALID_MODE
};

typedef struct dsm_handler_t{
    int socket_fd;
    enum connection_type mode;
    host_connection host;
    remote_connection remote;
}dsm_handler;

 /* --------------------------------------------------------------------
 * Public Functions
 * -------------------------------------------------------------------*/
int dsm_handler_init(dsm_handler *dsm, char* remote_ip, int remote_port, int host_port);
void dsm_handler_destroy(dsm_handler *dsm);
int establish_communication(dsm_handler *dsm, long int *address, int *no_pages);
#endif