#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#define handle_error(msg) \
           do { perror(msg); exit(EXIT_FAILURE); } while (0)
#define ALARM_AFTER_SECS    5
/*
 * Our signal handler simply prints a message and returns.
 * */
void sigalrm_handler(int signo) {
    printf("Received SIGALRM\n");
}

int main() {
    struct sigaction sa;
    char buff[1024];

    /*
     * Setup a signal handler for the SIGALRM signal
     * */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = sigalrm_handler;
    if (sigaction(SIGALRM, &sa, NULL) == -1)
        handle_error("sigaction");

    /*
     * Let's send ourselves a SIGALRM signal a few
     * seconds later.
     * */
    alarm(ALARM_AFTER_SECS);

    /*
     * Infinite loop where we read from stdin and print
     * what we read back to stdout.
     * */
    while(1) {
        bzero(buff, sizeof(buff));
        if( read(STDIN_FILENO, buff, sizeof(buff)-1 ) == -1 )
            handle_error("read");
        if( write(STDOUT_FILENO, buff, sizeof(buff) ) == -1 )
            handle_error("write");
    }

    return 0;
}
