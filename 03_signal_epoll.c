#include <stdio.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/epoll.h>
#include <errno.h>

#define handle_error(msg) \
           do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define MAX_EVENTS          10

struct epoll_event ev, events[MAX_EVENTS];
int listen_sock, conn_sock, nfds, epollfd;

/*
 * Our signal handler simply prints a message and returns.
 * */
void sigalrm_handler(int signo) {
    printf("Received SIGALRM\n");
}

/*
 * This function is responsible for setting up the
 * listening socket for the socket-based echo
 * functionality. Pretty standard stuff.
 * */

int setup_listening_socket(int port) {
    int sock;
    struct sockaddr_in srv_addr;

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        handle_error("socket()");

    int enable = 1;
    if (setsockopt(sock,
                   SOL_SOCKET, SO_REUSEADDR,
                   &enable, sizeof(int)) < 0)
        handle_error("setsockopt(SO_REUSEADDR)");


    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(port);
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    /* We bind to a port and turn this socket into a listening
     * socket.
     * */
    if (bind(sock,
             (const struct sockaddr *)&srv_addr,
             sizeof(srv_addr)) < 0)
        handle_error("bind()");

    if (listen(sock, 10) < 0)
        handle_error("listen()");

    return (sock);
}

/*
 * Reads 1kb from the "in" file descriptor and
 * writes it to the "out" file descriptor.
 */

void copy_fd(int in, int out) {
    char buff[1024];
    int bytes_read;

    bzero(buff, sizeof(buff));
    bytes_read = read(in, buff, sizeof(buff)-1 );
    if (bytes_read == -1)
        handle_error("read");
    else if (bytes_read == 0)
        return;
    write(out, buff, strlen(buff));
}

/*
 * If a parsable number is passed as the 1st argument to the
 * program, sets up SIGALRM to be sent to self the specified
 * seconds later.
 * */

void setup_signals(int argc, char **argv) {
    struct sigaction sa;

    /* No argument passed, and so no signal will be delivered */
    if (argc < 2) {
        printf("No alarm set. Will not be interrupted.\n");
        return;
    }

    /*  if a proper number is passed, we setup the alarm,
     * else we return without setting one up.
     * */
    errno = 0;
    long alarm_after = strtol(argv[1], NULL, 10);
    if (errno)
        handle_error("strtol()");
    if (alarm_after)
        printf("Alarm after %ld seconds.\n", alarm_after);
    else {
        printf("No alarm set. Will not be interrupted.\n");
        return;
    }

    /*
     * Setup a signal handler for the SIGALRM signal
     * */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = sigalrm_handler;
    if (sigaction(SIGALRM, &sa, NULL) == -1)
        handle_error("sigaction");

    /*
     * Let's send ourselves a SIGALRM signal specified
     * seconds later.
     * */
    alarm(alarm_after);
}

/**
 * Helper function to setup epoll
 */

void setup_epoll() {
    epollfd = epoll_create1(0);
    if (epollfd == -1)
        handle_error("epoll_create1()");
}

/**
 * Adds the file descriptor passed to be monitored by epoll
 * */
void add_fd_to_epoll(int fd) {
    /* Add fd to be monitored by epoll */
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1)
        handle_error("epoll_ctl");
}

int main(int argc, char *argv[]) {
    /* Setup sigalrm if a number is passed as the first argument */
    setup_signals(argc, argv);
    /* Let's setup epoll */
    setup_epoll();
    /* Add stdin-based echo server to epoll's monitoring list */
    add_fd_to_epoll(STDIN_FILENO);
    /* Setup a socket to listen on port 5000 */
    listen_sock = setup_listening_socket(5000);
    /* Add socket-based echo server to epoll's monitoring list */
    add_fd_to_epoll(listen_sock);

    while(1) {
        /* Let's wait for some activity on either stdin or on the socket */
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1)
            handle_error("epoll_wait()");

        /*
         * For each of the file descriptors epoll says are ready,
         * check which one it is the echo read data back.
         * */
        for (int n = 0; n < nfds; n++) {
            if ( events[n].data.fd == STDIN_FILENO ) {
                printf("stdin ready..\n");
                copy_fd(STDIN_FILENO, STDOUT_FILENO);
            } else if ( events[n].data.fd == conn_sock ) {
                printf("socket data ready..\n");
                copy_fd(conn_sock, conn_sock);
            } else if (events[n].data.fd == listen_sock) {
                /* Listening socket is ready, meaning
                 * there's a new client connection */
                printf("new connection ready..\n");
                conn_sock = accept(listen_sock, NULL, NULL);
                if (conn_sock == -1 )
                    handle_error("accept()");
                /* Add the connected client to epoll's monitored FDs list */
                add_fd_to_epoll(conn_sock);
            }
        }
    }
    return 0;
}