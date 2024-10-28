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
#define QUEUE_DEPTH 1024
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

// To be set as user_data for accept requests.
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

	/* Create a TCP server socket */
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		perror("socket");
		exit(-1);
	}

	int optval = 1;
	/* SO_REUSEADDR to quickly restart the server */
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval,
		       sizeof(optval)) == -1) {
		perror("setsockopt");
		exit(-1);
	}

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(port);

	if (bind(server_fd, (struct sockaddr *)&server_addr,
		 sizeof(server_addr)) == -1) {
		perror("bind");
		exit(-1);
	}

	if (listen(server_fd, NUM_PENDING_CONNECTIONS) == -1) {
		perror("listen");
		exit(-1);
	}
	return server_fd;
}

struct client_data *alloc_client_data_or_die()
{
	struct client_data *data =
		(struct client_data *)malloc(sizeof(struct client_data));
	if (!data) {
		perror("malloc");
		exit(1);
	}

	return data;
}

void add_accept_request(struct io_uring *ring, struct client_data *client_data,
			struct accept_data *accept_data)
{
	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	io_uring_prep_accept(sqe, accept_data->server_fd,
			     (struct sockaddr *)&client_data->client_addr,
			     &client_data->client_addr_len, 0);
	io_uring_sqe_set_data(sqe, accept_data);
}

void add_write_request(struct io_uring *ring, struct write_data *wd)
{
	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	io_uring_prep_write(sqe, wd->fd, hello_response, strlen(hello_response),
			    0);
	io_uring_sqe_set_data(sqe, wd);
}

void add_read_request(struct io_uring *ring, int client_fd)
{
	struct read_data *read_data =
		(struct read_data *)malloc(sizeof(struct read_data));

	read_data->type = OP_READ;
	read_data->fd = client_fd;

	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	io_uring_prep_read(sqe, client_fd, read_data->buffer,
			   sizeof(read_data->buffer), 0);
	io_uring_sqe_set_data(sqe, read_data);
}

// Here we are not saving the clients connected in anyways.
int main(int argc, char **argv)
{
	// These data structures are static, we avoid allocating
	// them again and again.
	struct client_data client_data;
	struct write_data write_data;
	struct accept_data accept_data;
	struct io_uring ring;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <port>\n", argv[0]);
		return 1;
	}

	accept_data.type = OP_ACCEPT;
	accept_data.server_fd = setup_server_socket_or_die(atoi(argv[1]));
	client_data.client_addr_len = sizeof(client_data.client_addr);

	write_data.type = OP_WRITE;

	if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
		perror("io_uring_queue_init");
		exit(1);
	}

	add_accept_request(&ring, &client_data, &accept_data);

	while (1) {
		io_uring_submit(&ring);

		struct io_uring_cqe *cqe;
		if (io_uring_wait_cqe(&ring, &cqe) < 0)
			continue;

		if (cqe->res == -ENOBUFS) {
			fprintf(stderr, "io_ring queue is full");
		} else {
			struct read_data *rd;
			void *data = io_uring_cqe_get_data(cqe);
			enum op_type type = *(enum op_type *)data;

			switch (type) {
			case OP_ACCEPT:
				if (cqe->res < 0) {
					perror("error on accept request");
					break;
				}
				int client_fd = cqe->res;
				printf("%d is the client fd\n", client_fd);
				add_accept_request(&ring, &client_data, data);
				add_read_request(&ring, client_fd);
				break;
			case OP_READ:
				rd = data;
				if (cqe->res > 0) {
					printf("read %d bytes as %s\n",
					       cqe->res, rd->buffer);
					write_data.fd = rd->fd;
					add_write_request(&ring, &write_data);
					break;
				}
				close(rd->fd);
				free(rd);
				break;
			case OP_WRITE:
				if (cqe->res < 0)
					perror("failed to write\n");
				break;
			default:
				perror("illegal op on the ring");
				exit(1);
			};
		}
		io_uring_cqe_seen(&ring, cqe);
	}

	io_uring_queue_exit(&ring);
	close(accept_data.server_fd);
	return 0;
}
