#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_EVENTS 1000
#define NUM_PENDING_CONNECTIONS 100
#define NUM_THREADS 4
#define BUFFER_SIZE 1024

struct worker_arg {
    int epoll_fd;
    int server_fd;
};

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }
    flags |= O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
}

void handle_client(int epoll_fd, int client_fd) {
    const char *response = "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\nHello World!";
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    // Read client data in a loop until EAGAIN or error
    while (1) {
        bytes_read = read(client_fd, buffer, BUFFER_SIZE);
        if (bytes_read > 0) {
            // TODO: Do something with the request data, e.g parse headers etc.
        } else if (bytes_read == 0) {
            // Client closed the connection
            close(client_fd);
            return;
        } else { // bytes_read == -1
            if (errno == EAGAIN) {
                // No more data available now
                break;
            } else {
                perror("read");
                return;
            }
        }
    }

    write(client_fd, response, strlen(response));

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    ev.data.fd = client_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev) == -1) {
        perror("epoll_ctl: EPOLL_CTL_MOD");
        close(client_fd);
    }
}

void *worker_thread(void *args) {
    struct worker_arg *wargs = (struct worker_arg *)args;

    while (1) {
        struct epoll_event events[MAX_EVENTS];
        int nfds = epoll_wait(wargs->epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            continue;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == wargs->server_fd) { // New connection
                struct sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
                int client_fd = accept(wargs->server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

                if (client_fd == -1) {
                    continue;
                }

                if (set_nonblocking(client_fd) == -1) {
                    continue;
                }

                // Add client_fd to epoll without EPOLLEXCLUSIVE
                struct epoll_event ev;
                ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                ev.data.fd = client_fd;

                if (epoll_ctl(wargs->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
                    perror("epoll_ctl - ADD");
                    close(client_fd);
                }
            } else {
                // Handle client requests
                handle_client(wargs->epoll_fd, events[i].data.fd);
            }
        }
    }
    return NULL;
}

int main(void) {
    int server_fd;
    struct sockaddr_in server_addr;
    int epoll_fd;

    // Create a TCP server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        exit(-1);
    }

    int optval = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        perror("setsockopt");
        exit(-1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        exit(-1);
    }

    if (listen(server_fd, NUM_PENDING_CONNECTIONS) == -1) {
        perror("listen");
        exit(-1);
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(-1);
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLEXCLUSIVE;
    ev.data.fd = server_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl - server");
        exit(-1);
    }

    // Create threads for handling connections
    pthread_t threads[NUM_THREADS];
    struct worker_arg wargs = {
        .epoll_fd = epoll_fd,
        .server_fd = server_fd,
    };

    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, worker_thread, &wargs) != 0) {
            perror("pthread_create");
            exit(1);
        }
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            perror("pthread_join");
        }
    }

    close(server_fd);
    close(epoll_fd);

    return 0;
}
