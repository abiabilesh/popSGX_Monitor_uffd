#ifndef __POPSGX_MONITOR_H__
#define __POPSGX_MONITOR_H__

#include <pthread.h>
#include <sys/types.h>

/* --------------------------------------------------------------------
 * Structures & Required Datatypes
 * -------------------------------------------------------------------*/
typedef struct popsgx_child_app_t{
    pid_t c_pid;
    int   c_argc;
    char  *c_argv;
    char  *c_path; 
    int   uffd;
    pthread_mutex_t mutex; 
} popsgx_child;

enum app_mode{
    SERVER = 0,
    CLIENT
};

typedef struct popsgx_app_t{
    enum app_mode mode;
    popsgx_child child;
}popsgx_app;

#endif