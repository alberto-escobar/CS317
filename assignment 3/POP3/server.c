/* server.c
 * Handles the creation of a server socket and data sending.
 * Author  : Jonatan Schroeder
 * Modified: Nov 6, 2021
 *
 * Modified by: Norm Hutchinson
 * Modified: Mar 5, 2022
 *
 * Notes: You will find useful examples in Beej's Guide to Network
 * Programming (http://beej.us/guide/bgnet/).
 */
//x
#include "server.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <signal.h>
#include <pthread.h>


/* TODO: Fill in the server code. You are required to listen on all interfaces for connections. For each connection,
 * invoke the handler on a new thread. */

struct thread_args {
    int client_socket;
    void (*handler)(void *);
};

void *thread_handler(void *arg) {
    struct thread_args *args = (struct thread_args *)arg;
    int client_socket = args->client_socket;
    void (*handler)(void *) = args->handler;
    free(arg);
    pthread_detach(pthread_self());
    handler((void *)&client_socket);
    close(client_socket);
    return NULL;
}

void run_server(const char *port, void (*handler)(void *)) {
    // TODO: Implement this function
    int server_fd, new_socket;
    struct addrinfo hints, *res, *p;
    struct sockaddr_storage client_addr;
    socklen_t addr_size;
    int yes = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, port, &hints, &res) != 0) exit(1);

    for (p = res; p != NULL; p = p->ai_next) {
        server_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (server_fd == -1) continue;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            close(server_fd);
            exit(1);
        }
        if (bind(server_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(server_fd);
            continue;
        }
        break; 
    }

    freeaddrinfo(res);

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(server_fd, 10) == -1) exit(1);

    printf("Server is listening on port %s...\n", port);

    while (1) {
        addr_size = sizeof client_addr;
        new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_size);
        if (new_socket == -1) continue;
        struct thread_args *args = malloc(sizeof(struct thread_args));
        if (args == NULL) continue;
        args->client_socket = new_socket;
        args->handler = handler;
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, thread_handler, args) != 0) {
            close(new_socket);
            free(args);
            continue;
        }
    }
    close(server_fd); 
}


