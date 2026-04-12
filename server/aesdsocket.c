#include "aesdsocket.h"
#include "../aesd-char-driver/aesd_ioctl.h"


bool non_interrupted = true;
int fd;
int log_file;

#if USE_AESD_CHAR_DEVICE
#define LOG_FILE "/dev/aesdchar"
#else
#define LOG_FILE "/var/tmp/aesdsocketdata"
#endif


int sock_recv(int log_file, int client_fd){
    char buf[1024];
    memset(buf,0,1024);
    int num_bytes = 1024;
    //check if received data was the ioctl command
    //just use a posix regex for simplicity
    regex_t regex;
    //NOTE: this will technically match more than :X:Y,
    //but for the sake of this assignment it doesn't really matter
    int rc = regcomp(&regex, "AESDCHAR_IOCSEEKTO:*:*", REG_EXTENDED);
    if (rc != 0) {
        perror("regcomp");
        return -1;
    }

    while (num_bytes == 1024){
        int num_bytes = recv(client_fd, buf, 1024, MSG_DONTWAIT);
        if (num_bytes == -1 && errno == EAGAIN ){
            break;
        }
        else if (num_bytes == -1){
            perror("recv");
            syslog(LOG_DEBUG, "Closing log file\n");
            if (close(log_file) == -1){
                perror("close");
                syslog(LOG_ERR, "Could not close log file\n");
                return -1;
            }
            return -1;
        }
        if (num_bytes == 0){
            return 0;
        }
        syslog(LOG_DEBUG, "num_bytes: %d\n",num_bytes); 

        // char *pos = (char *)memchr(buf,'\n',sizeof(buf));
        // int delim = 0;
        // if (pos != NULL){ // \n in string, so do stuff
        //     delim = strcspn(buf,"\n");
        //     char enter = '\n';
        //     strncat(buf, &enter, sizeof(buf) - strlen(buf) - 1);
        // } else {
        //     delim = num_bytes -1; // 1023
        // }
        

        buf[num_bytes] = '\0';
        
        char *regbuf = strdup(buf);
        syslog(LOG_DEBUG, "buffer passed to regexec: %s\n", regbuf); 
        rc = regexec(&regex, regbuf, 0, NULL, 0);
        
        syslog(LOG_DEBUG, "regexec return value: %d\n", rc); 
        if (rc == 0){ // matches ioctl string
            syslog(LOG_DEBUG, "AESDCHAR_IOCSEEKTO:X:Y in received data!\n");
            // Do the ioctl command
            struct aesd_seekto seekto;
            //0123456789 123456789 1
            //AESDCHAR_IOCSEEKTO:X:Y
            //X=[19], Y=[21]
            seekto.write_cmd = regbuf[19] - '0'; //ascii magic to get the proper int
            seekto.write_cmd_offset = regbuf[21] - '0';
            syslog(LOG_DEBUG, "seekto X: %d, Y: %d\n", seekto.write_cmd, seekto.write_cmd_offset);
            rc = ioctl(log_file, AESDCHAR_IOCSEEKTO, &seekto);
            if (rc != 0){
                perror("ioctl");
                syslog(LOG_DEBUG, "Closing log file\n");
                if (close(log_file) == -1){
                    perror("close");
                    syslog(LOG_ERR, "Could not close log file\n");
                    free(regbuf);
                    return -1;
                }
            }
            free(regbuf);
            return 2; // flag that we need to do ioctl stuff
        } else if (rc == REG_NOMATCH){
            rc = write(log_file, buf, num_bytes);
            if (rc == -1){
                perror("write");
                syslog(LOG_DEBUG, "Closing log file\n");
                if (close(log_file) == -1){
                    perror("close");
                    syslog(LOG_ERR, "Could not close log file\n");
                    free(regbuf);
                    return -1;
                }
                free(regbuf);
                return -1;
            }
            syslog(LOG_DEBUG, "rc: %d, log_file: %d\n",rc, log_file);    
            syslog(LOG_DEBUG, "Wrote %d bytes to file.\n", num_bytes);
        } else { // errors, close log file and throw -1
            syslog(LOG_DEBUG, "Closing log file\n");
            if (close(log_file) == -1){
                perror("close");
                syslog(LOG_ERR, "Could not close log file\n");
                free(regbuf);
                return -1;
            }
            free(regbuf);
            return -1;
        }

    }
    return 0;
}

int sock_send(int log_file, int client_fd, int seek_arg){
    char buf[1024];
    memset(buf,0,1024);
    int read_count = 1024;
    lseek(log_file,0,seek_arg);
    while (read_count > 0){
        int read_count = read(log_file, buf, sizeof(buf)-1);
        syslog(LOG_DEBUG, "read_count: %d, log_file: %d, client_fd: %d\n",read_count, log_file, client_fd);
        if (read_count == -1){
            perror("read");
            if (close(log_file) == -1){
                perror("close");
                syslog(LOG_ERR, "Could not close log file\n");
                return -1;
            }
            return -1;
        } 
        if (read_count == 0) {
            return 0;
        }
        syslog(LOG_DEBUG, "Read %d bytes from log file.\n", read_count);

        int sent_bytes = send(client_fd, buf, read_count,0);
        if (sent_bytes == -1){
            perror("send");
            syslog(LOG_DEBUG, "Closing log file\n");
            if (close(log_file) == -1){
                perror("close");
                syslog(LOG_ERR, "Could not close log file\n");
                return -1;
            }
            return -1;
        }
        syslog(LOG_DEBUG, "Sent %d bytes back.\n", sent_bytes);
    }
    return 0;
}

void signal_handler(int signal_number){
    if (signal_number == SIGINT) {
        syslog(LOG_DEBUG, "Caught signal, exiting");
        non_interrupted = false;
        #if !USE_AESD_CHAR_DEVICE
        shutdown(fd, SHUT_RDWR);
        if (unlink(LOG_FILE) == -1){
            syslog(LOG_ERR, "Could not unlink log file\n");
        } else {
            syslog(LOG_DEBUG, "Unlinked log file\n");
        }
        #endif
        syslog(LOG_DEBUG, "Closing log file\n");
        if (close(log_file) == -1){
            syslog(LOG_ERR, "Could not close log file\n");
        }
    } else if (signal_number == SIGTERM) {
        syslog(LOG_DEBUG, "Caught signal, exiting");
        non_interrupted = false;
        #if !USE_AESD_CHAR_DEVICE
        shutdown(fd, SHUT_RDWR);
        if (unlink(LOG_FILE) == -1){
            syslog(LOG_ERR, "Could not unlink log file\n");
        } else {
            syslog(LOG_DEBUG, "Unlinked log file\n");
        }
        #endif
        syslog(LOG_DEBUG, "Closing log file\n");
        if (close(log_file) == -1){
            syslog(LOG_ERR, "Could not close log file\n");
        }
    }
}


int setup_timer(int clock_id, timer_t timerid, 
                 unsigned int timer_period_ms, struct timespec *start_time){
    // This function and the time_functions_shared file are from the lecture 9 examples.
    {
        int success = 1;
        if ( clock_gettime(clock_id,start_time) != 0 ) {
            printf("Error %d (%s) getting clock %d time\n",errno,strerror(errno),clock_id);
        } else {
            struct itimerspec itimerspec;
            memset(&itimerspec, 0, sizeof(struct itimerspec));
            itimerspec.it_value.tv_sec = 10;
            itimerspec.it_value.tv_nsec = timer_period_ms * 0;
            itimerspec.it_interval.tv_sec = itimerspec.it_value.tv_sec;
            itimerspec.it_interval.tv_nsec = itimerspec.it_value.tv_nsec;
            if( timer_settime(timerid, 0, &itimerspec, NULL ) != 0 ) {
                printf("Error %d (%s) setting timer\n",errno,strerror(errno));
            } else {
                success = 0;
            }
        }
        return success;
    }
}


void timer_thread(union sigval sigval){
    syslog(LOG_DEBUG, "Timer thread: %ld\n", pthread_self());
    struct entry *td = (struct entry*) sigval.sival_ptr;
    int rc = pthread_mutex_lock(td->file_lock);
    if (rc != 0 ){
        perror("pthread_mutex_lock");
        syslog(LOG_DEBUG,"rc %d \n",rc);
        syslog(LOG_DEBUG,"Error %d (%s) locking mutex\n",errno,strerror(errno));
    } else {
        syslog(LOG_DEBUG, "Thread %ld has lock\n", pthread_self());
        char outstr[37];
        time_t t;
        struct tm *tmp;
        t = time(NULL);
        tmp = localtime(&t);
        if (tmp == NULL){
            perror("localtime");
        }
        syslog(LOG_DEBUG, "Getting current time."); 
        if (strftime(outstr,sizeof(outstr),"%a, %d %b %Y %T %z",tmp) == 0) {
            perror("strftime"); 
        } else {
            char timestamp[43] = "timestamp:";
            strncat(timestamp, outstr, sizeof(timestamp) - strlen(timestamp) - 1);
            char enter = '\n';
            strncat(timestamp, &enter, sizeof(timestamp) - strlen(timestamp) - 1);
            int rc = write(log_file, timestamp, sizeof(timestamp)-1);
            if (rc == -1){
                perror("write");
            }
            syslog(LOG_DEBUG, "Wrote time %s to log_file: %d\n",outstr, log_file);
        }

        rc = pthread_mutex_unlock(td->file_lock);
        if (rc != 0 ){
            syslog(LOG_DEBUG,"rc %d \n",rc);
            perror("pthread_mutex_unlock");
            syslog(LOG_DEBUG,"Error %d (%s) unlocking mutex\n",errno,strerror(errno));
        }
        syslog(LOG_DEBUG, "Thread %ld dropped lock\n", pthread_self());
    }
    
}


void* thread_job(void* t_args) {
    syslog(LOG_DEBUG, "In thread now!\n");
    struct entry* args = (struct entry *)t_args;
    int client_fd = args->client_fd;
    syslog(LOG_DEBUG, "Thread sees client fd %d\n", client_fd);
    //lock mutex
    int rc = pthread_mutex_lock(args->file_lock);
    if (rc != 0 ){
        perror("pthread_mutex_lock");
        return (void *)NULL;
    }
    syslog(LOG_DEBUG, "Thread %ld has lock\n", pthread_self());
    
    // recv---------------------------------------------------------------
    rc = sock_recv(args->log_file, client_fd);
    syslog(LOG_DEBUG, "Thread wrote to fd %d\n", args->log_file);
    if (rc == -1){
        syslog(LOG_ERR, "Could not receive from %d\n", client_fd);
    } else if (rc == 2){
        //ioctl time
        rc = sock_send(args->log_file, client_fd, SEEK_CUR);
        if (rc == -1){
            syslog(LOG_ERR, "Could not send to %d\n", client_fd);
        }
    } else {
        // send---------------------------------------------------------------
        rc = sock_send(args->log_file, client_fd, SEEK_SET);
        if (rc == -1){
            syslog(LOG_ERR, "Could not send to %d\n", client_fd);
        }
        syslog(LOG_DEBUG, "Thread read from fd %d\n", args->log_file);
    }
    
    close(client_fd);
    //unlock mutex
    rc = pthread_mutex_unlock(args->file_lock);
    if (rc != 0 ){
        perror("pthread_mutex_unlock");
        return (void *)NULL;
    }
    syslog(LOG_DEBUG, "Thread %ld dropped lock\n", pthread_self());
    //raise flag
    args->complete = 0;
    return (void *)NULL;
}


int perform_socket_actions(int log_file, int listen_backlog) {
    struct sockaddr_in client_addr;
    socklen_t client_len;
    client_len = sizeof(client_addr);
    //pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;    
    pthread_mutex_t mutex;
    if ( pthread_mutex_init(&mutex,NULL) != 0 ) {
        printf("Error %d (%s) initializing thread mutex!\n",errno,strerror(errno));
    }
    SLIST_HEAD(slisthead, entry);
    struct slisthead head;
    SLIST_INIT(&head);

    #if !USE_AESD_CHAR_DEVICE
    //create timer thread with posix timer + event handler
    timer_t timerid;
    struct sigevent sev;
    int clock_id = CLOCK_MONOTONIC;
    memset(&sev,0,sizeof(struct sigevent));
    struct entry* t_node = (struct entry *)malloc(sizeof(struct entry));
    t_node->client_fd = 0; // don't care for timer
    t_node->log_file = log_file;
    t_node->file_lock = &mutex; // all nodes point to the same mutex address
    t_node->complete = 1; // all nodes start at 1
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_value.sival_ptr = t_node;
    //SLIST_INSERT_HEAD(&head, t_node, entries);
    sev.sigev_notify_function = timer_thread;
    if (timer_create(clock_id,&sev,&timerid) != 0) {
        perror("timer_create");
        syslog(LOG_ERR, "Error creating timer: %d %s\n", errno, strerror(errno));
    } else {
        syslog(LOG_DEBUG, "timer ID is %#jx\n", (uint64_t) timerid);
        struct timespec start_time;
        int rc = setup_timer(clock_id, timerid, 10000, &start_time);
        if (rc != 0) {
            syslog(LOG_ERR, "Error starting timer %#jx\n", (uint64_t) timerid);
        }
    }
    #endif

    while (non_interrupted == true){
        if (listen(fd,listen_backlog) == -1){
            perror("listen");
            syslog(LOG_ERR, "Could not listen on socket %d!\n", fd);
            close(fd);
            return -1;
        } else {
            syslog(LOG_DEBUG, "Listening on socket %d\n", fd);
        }

        int client_fd = accept(fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)){
            continue;
        } else if (client_fd == -1 && errno == EINTR){
            close(fd);
            break;
        }else if (client_fd == -1){
            close(fd);
            break;
        } else {
            syslog(LOG_DEBUG, "Accepted connection from %d\n", client_fd);
        }
        log_file = open(LOG_FILE, O_CREAT | O_RDWR | O_NONBLOCK, 0777);
        if (log_file == -1) {
            perror("open");
            syslog(LOG_ERR, "Could not access %s\n", LOG_FILE);
            return -1;
        }
    
        struct entry* node = (struct entry *)malloc(sizeof(struct entry));
        node->client_fd = client_fd;
        node->log_file = log_file;
        node->file_lock = &mutex; // all nodes point to the same mutex address
        node->complete = 1; // all nodes start at 1

        //make a new node on linked list
        if (SLIST_EMPTY(&head)) {
            SLIST_INSERT_HEAD(&head, node, entries);
            syslog(LOG_DEBUG, "Creating linked list head.\n");
        } else {
            struct entry* cur_node = SLIST_FIRST(&head);
            while (SLIST_NEXT(cur_node, entries) != NULL) {
                cur_node = SLIST_NEXT(cur_node, entries);
            }
            syslog(LOG_DEBUG, "Adding element to list.\n");
            SLIST_INSERT_AFTER(cur_node, node, entries);
        }

        //create thread on the new node
        int rc = pthread_create(&node->thread_id,NULL,thread_job,node);
        if (rc  != 0){
            perror("pthread_create");
            free(node);
            return -1;
        }

        syslog(LOG_DEBUG, "Creating thread with ID %lu\n", node->thread_id);
        struct entry* np;
        struct entry* temp_var;
        SLIST_FOREACH_SAFE(np, &head, entries, temp_var) {
            if (np->complete == 0) {
                syslog(LOG_DEBUG, "Joining thread with ID %lu\n", np->thread_id);
                pthread_join(np->thread_id,NULL);
                SLIST_REMOVE(&head, np, entry, entries);
                free(np);
            }
        }
    }
    #if !USE_AESD_CHAR_DEVICE
    if (timer_delete(timerid) != 0) {
        perror("timer delete");
        syslog(LOG_ERR, "Error deleting timer: %d %#jx\n", errno, (uint64_t) timerid);
    }
    free(t_node);
    #endif
    return 0; 
}


int main(int argc, char *argv[]){
    openlog(NULL,0,LOG_USER);
    int listen_backlog = 100;

    struct sigaction new_action;
    memset(&new_action,0,sizeof(struct sigaction));
    new_action.sa_handler = signal_handler;

    if (sigaction(SIGTERM, &new_action, NULL) == -1) {
            perror("sigaction");
            return -1;
    }

    if (sigaction(SIGINT, &new_action, NULL) == -1) {
            perror("sigaction");
            return -1;
    }


    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; 

    fd = socket(AF_INET, SOCK_STREAM, 0); 
    if (fd == -1) {
        syslog(LOG_ERR, "Could not create socket!\n");
        return -1;
    }
    int val = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int)) == -1 ){
        perror("setsockopt");
    }


    int status = getaddrinfo(NULL, "9000", &hints, &servinfo);
    if (status != 0){
        perror("getaddrinfo");
        syslog(LOG_ERR, "Could not get address info!\n");

        return -1;
    }
    
    int rc = bind(fd, servinfo->ai_addr, servinfo->ai_addrlen);
    if (rc == -1){
        perror("bind");
        syslog(LOG_ERR, "Could not bind socket address!\n");
        close(fd);
        return -1;
    }

    freeaddrinfo(servinfo);

    pid_t pid;
    if (argc == 2){
        if (strcmp(argv[1], "-d") == 0){
            syslog(LOG_DEBUG, "Deamon flag set, forking");
            pid = fork();
            switch (pid) {
            case -1:
                perror("fork");
                return -1;
            case 0:
                return perform_socket_actions(log_file, listen_backlog);
            default:
                return 0; // parent does nothing in this case.
            }
        } 
    }
    else {
        return perform_socket_actions(log_file, listen_backlog);
    }

}