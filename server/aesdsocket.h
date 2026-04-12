#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <syslog.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <pthread.h>
#include <regex.h>
#include "queue.h"
#include "time_functions_shared.h"
#define _GNU_SOURCE

#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1
#endif


struct addrinfo hints;
struct addrinfo *servinfo;

struct entry {
    pthread_t thread_id; /* ID returned by pthread_create() */
    pthread_mutex_t *file_lock; // aassign the same mutex to each entry as they're created
    int client_fd; // assign per thread
    int log_file; // always the same
    int complete; // modify in thread
    SLIST_ENTRY(entry) entries;
};