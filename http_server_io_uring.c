#include <alloca.h>
#include <ctype.h>
#include <fcntl.h>
#include <liburing/io_uring.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <liburing.h>

#define BUFFER_SIZE 4096
#define QUEUE_DEPTH 4096
#define NUM_PENDING_CONNECTIONS 32

const char *hello_response = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/html\r\n"
                             "Content-Length: 13\r\n"
                             "\r\n"
                             "<h1>Hello</h1>";

enum op_type {
    OP_ACCEPT,
    OP_READ,
    OP_WRITE,
};

struct client_data {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
};

struct accept_data {
    enum op_type type;
    int server_fd;
};

struct read_data {
    enum op_type type;
    int fd;
    char buffer[BUFFER_SIZE];
};

struct write_data {
    enum op_type type;
    int fd;
};

int setup_server_socket_or_die(int port)
{
    struct sockaddr_in server_addr;
    int server_fd;

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
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        exit(-1);
    }

    if (listen(server_fd, NUM_PENDING_CONNECTIONS) == -1) {
        perror("listen");
        exit(-1);
    }
    return server_fd;
}

void add_accept_request(struct io_uring *ring, int server_fd)
{
    struct accept_data *accept_data = malloc(sizeof(struct accept_data));
    accept_data->type = OP_ACCEPT;
    accept_data->server_fd = server_fd;

    struct client_data *client_data = malloc(sizeof(struct client_data));
    client_data->client_addr_len = sizeof(client_data->client_addr);

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_accept(sqe, server_fd, (struct sockaddr *)&client_data->client_addr,
                         &client_data->client_addr_len, 0);
    io_uring_sqe_set_data(sqe, accept_data);
}

void add_write_request(struct io_uring *ring, int client_fd)
{
    struct write_data *wd = malloc(sizeof(struct write_data));
    wd->type = OP_WRITE;
    wd->fd = client_fd;

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_write(sqe, client_fd, hello_response, strlen(hello_response), 0);
    io_uring_sqe_set_data(sqe, wd);
}

void add_read_request(struct io_uring *ring, int client_fd)
{
    struct read_data *rd = malloc(sizeof(struct read_data));
    rd->type = OP_READ;
    rd->fd = client_fd;

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_read(sqe, client_fd, rd->buffer, sizeof(rd->buffer), 0);
    io_uring_sqe_set_data(sqe, rd);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int server_fd = setup_server_socket_or_die(atoi(argv[1]));
    struct io_uring ring;

    if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
        perror("io_uring_queue_init");
        exit(1);
    }

    add_accept_request(&ring, server_fd);

    while (1) {
        io_uring_submit(&ring);

        struct io_uring_cqe *cqe;
        if (io_uring_wait_cqe(&ring, &cqe) < 0)
            continue;

        void *data = io_uring_cqe_get_data(cqe);
        enum op_type type = *(enum op_type *)data;

        switch (type) {
            case OP_ACCEPT:
                add_accept_request(&ring, server_fd);
                if (cqe->res < 0) {
                    perror("error on accept request");
                } else {
                    int client_fd = cqe->res;
                    add_read_request(&ring, client_fd);
                }
                free(data);
                break;

            case OP_READ: {
                struct read_data *rd = data;
                if (cqe->res > 0) {
                    add_write_request(&ring, rd->fd);
                } else {
                    close(rd->fd);
                }
                free(rd);
                break;
            }

            case OP_WRITE: {
                struct write_data *wd = data;
                if (cqe->res < 0) {
                    perror("failed to write");
                }
                close(wd->fd);
                free(wd);
                break;
            }

            default:
                fprintf(stderr, "Unexpected operation\n");
                exit(1);
        }

        io_uring_cqe_seen(&ring, cqe);
    }

    io_uring_queue_exit(&ring);
    close(server_fd);

    return 0;
}
