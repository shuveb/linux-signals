#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/time.h>
#include <errno.h>
#include <syscall.h>
#include <linux/sched.h>

#define handle_error(msg) \
           do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define MAX_EVENTS          10

static int pidfd_send_signal(int pidfd, int sig, siginfo_t *info,
                             unsigned int flags)
{
    return (int) syscall(__NR_pidfd_send_signal, pidfd, sig, info,
    flags);
}

static pid_t sys_clone3(struct clone_args *args)
{
    return syscall(__NR_clone3, args, sizeof(struct clone_args));
}

#define ptr_to_u64(ptr) ((__u64)((uintptr_t)(ptr)))

struct epoll_event ev, events[MAX_EVENTS];
int nfds, epollfd, sfd, pidfd;

/**
 * If a parsable number is passed as the 1st argument to the
 * program, sets up SIGALRM to be sent to self the specified
 * seconds later.
 * */

void setup_signals(int argc, char **argv) {
    sigset_t mask;

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
     * Setup SIGALRM to be delivered via SignalFD
     * */
    sigemptyset(&mask);
    sigaddset(&mask, SIGALRM);
    sigaddset(&mask, SIGQUIT);

    /*
     * Block these signals so that they are not handled
     * in the usual way. We want them to be handled via
     * SignalFD.
     * */
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
        handle_error("sigprocmask");

    sfd = signalfd(-1, &mask, 0);
    if (sfd == -1)
        handle_error("signalfd");

    /*
     * Let's send ourselves a SIGALRM signal every specified
     * seconds continuously.
     * */
    struct itimerval itv;
    itv.it_interval.tv_sec = alarm_after;
    itv.it_interval.tv_usec = 0;
    itv.it_value = itv.it_interval;
    if (setitimer(ITIMER_REAL, &itv, NULL) == -1)
        handle_error("setitimer()");
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
/**
 * This isn't really a signal handler in the traditional sense,
 * which is called asynchronously. This function is called
 * synchronously from epoll's event loop.
 */
void handle_signals() {
    struct signalfd_siginfo sfd_si;
    if (read(sfd, &sfd_si, sizeof(sfd_si)) == -1)
        handle_error("read()");

    if (sfd_si.ssi_signo == SIGALRM)
        printf("Got SIGALRM via SignalFD.\n");
    else if (sfd_si.ssi_signo == SIGQUIT) {
        printf("Got SIGQUIT. Will exit.\n");
        pidfd_send_signal(pidfd, SIGINT, NULL, 0);
        exit(0);
    }
    else
        printf("Got unexpected signal!\n");
}

/* This is the child's SIGINT handler. The child is setup
 * to get a regular SIGINT when the parent receives a
 * SIGQUIT (for which you press Ctrl+\
 * */
void childs_sigint_handler(int signo) {
    printf("Child got SIGINT. Quitting.\n");
}

/*
 * Create a child with the clone3() system call so we get
 * a PIDFD which we can use to monitor the child's exit
 * and also use to sent it a signal in a race-free manner.
 * */

void create_child() {
    pid_t pid = -1;
    struct clone_args args = {
            /* CLONE_PIDFD */
            .pidfd = ptr_to_u64(&pidfd),
            .flags = CLONE_PIDFD,
            .exit_signal = SIGCHLD,
    };

    pid = sys_clone3(&args);
    if (pid < 0)
        handle_error("clone3");

    if (pid == 0) {
        /* Child */
        signal(SIGINT, childs_sigint_handler);
        printf("In child process. Sleeping..\n");
        sleep(5);
        printf("Exiting child process.\n");
        exit(0);
    } else {
        /* Parent */
        printf("Adding child PID %d with pidfd %d"
               "to be monitored by epoll.\n",
               pid, pidfd);
        /* We have the pidfd returned by clone3().
         * Add it to epoll's monitoring list. */
        add_fd_to_epoll(pidfd);
    }
}

int main(int argc, char *argv[]) {
    /* Let's setup epoll */
    setup_epoll();
    /* Setup sigalrm+sigquit if a number is passed as the first argument */
    setup_signals(argc, argv);
    /* Add the SignalFD file descriptor to epoll's monitoring list */
    add_fd_to_epoll(sfd);

    create_child();

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
            if ( events[n].data.fd == pidfd ) {
                printf("Child exited, creating new child..\n");
                /* We're done using the pidfd that pointed to the child
                 * process that just exited */
                close(pidfd);
                create_child();
            } else if (events[n].data.fd == sfd) {
                /* Looks like we got some other signal, let's handle it */
                handle_signals();
            }
        }
    }
    return 0;
}
