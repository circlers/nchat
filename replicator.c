#include "common.h"
#include <netdb.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define CONNS 64

struct conn {
	int sock;
	struct sockaddr addr;
	socklen_t addr_len;
};

struct server {
	int sock;
	int epfd;
	int count; // number of connections
	struct conn *conns[CONNS];
};

struct server *init(int port) {
	// Object

	struct server *server = malloc(sizeof(struct server));
	if (server == NULL)
		error(true, "error allocating memory", NULL, true);
	server->count = 0;

	// Networking

	server->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server->sock < 0)
		error(true, "could not create a server socket", NULL, true);

	if (fcntl(server->sock, F_SETFL, fcntl(server->sock, F_GETFL, 0) | O_NONBLOCK) == -1)
		error(true, "could not put socket in non-blocking mode", NULL, true);

	const struct sockaddr_in addr = {
		AF_INET,
		htons(port),
		{ INADDR_ANY },
		{ 0 }
	};

	if (bind(server->sock, (const struct sockaddr *)&addr, sizeof(addr)))
		error(true, "could not bind to socket", NULL, true);

	if (listen(server->sock, CONNS) != 0)
		error(true, "could not mark socket as accepting connections", NULL, true);

	// Events

	server->epfd = epoll_create(1);
	if (server->epfd < 0)
		error(true, "could not create epoll instance", NULL, true);

	struct epoll_event ev = {
		EPOLLIN | EPOLLOUT, // TODO ?
		{ .ptr = server }
	};
	if (epoll_ctl(server->epfd, EPOLL_CTL_ADD, server->sock, &ev) != 0)
		error(true, "could not subscribe to new connection events", NULL, true);

	return server;
}

// Might return NULL in case of failure.
struct conn *newconn(struct server *server) {
	// Accept a new connection

	struct conn pconn = {0};
	pconn.sock = accept(server->sock, &pconn.addr, &pconn.addr_len);
	if (pconn.sock < 0) {
		error(false, "could not accept incoming connection", NULL, true);
		return NULL;
	}

	char buf[128];
	if (inet_ntop(AF_INET, &pconn.addr, buf, sizeof(buf)/sizeof(buf[0])) == NULL) {
		error(false, "failed to print the ip address", NULL, true);
	} else {
		printf("Connected from %s\n", buf);
	}

	if (server->count == CONNS) {
		error(false, "connection limit exhausted", NULL, false);
		close(pconn.sock);
		return NULL;
	}

	struct conn *conn = malloc(sizeof(struct conn));
	if (conn == NULL)
		error(true, "error allocating memory", NULL, true);
	*conn = pconn;

	if (fcntl(conn->sock, F_SETFL, fcntl(conn->sock, F_GETFL, 0) | O_NONBLOCK) == -1)
		error(true, "could not put socket in non-blocking mode", NULL, true);

	// Hook up the events

	struct epoll_event ev = {
		EPOLLIN | EPOLLRDHUP | EPOLLHUP, // TODO ?
		{ .ptr = conn }
	};
	if (epoll_ctl(server->epfd, EPOLL_CTL_ADD, conn->sock, &ev) != 0) {
		error(false, "could not subscribe to events related with a new connection", NULL, true);
		close(conn->sock);
		free(conn);
		return NULL;
	}

	// Add to list

	server->conns[server->count++] = conn;

	return conn;
}

void forward(struct server *server, struct conn *from, struct msg *msg) {
	char buf[128];
	if (inet_ntop(AF_INET, &from->addr, buf, sizeof(buf)/sizeof(buf[0])) == NULL) {
		error(false, "failed to print the ip address", NULL, true);
	} else {
		printf("Forwarding a message from %s (%d B)\n", buf, msg->len);
	}

	for (int i = 0; i < server->count; i++) {
		if (server->conns[i] != from) {
			send_data(server->conns[i]->sock, msg);
		}
	}
}

void delconn(struct server *server, struct conn *conn) {
	char buf[128];
	if (inet_ntop(AF_INET, &conn->addr, buf, sizeof(buf)/sizeof(buf[0])) == NULL) {
		error(false, "failed to print the ip address", NULL, true);
	} else {
		printf("Disconnected from %s\n", buf);
	}

	close(conn->sock);
	free(conn);

	for (int i = 0; i < server->count; i++) {
		if (server->conns[i] == conn) {
			server->conns[i] = server->conns[--server->count];
			break;
		}
	}
}

void connevent(struct server *server, const struct epoll_event *ev) {
	struct conn *conn = ev->data.ptr;
	if (ev->events & (EPOLLHUP|EPOLLRDHUP)) {
		delconn(server, conn);
	} else if (ev->events & EPOLLIN) {
		struct msg *data = recv_data(conn->sock);
		forward(server, conn, data);
		free_msg(data);
	}
}

void loop(struct server *server) {
	struct epoll_event events[CONNS * 4];
	while (1) {
		int count = epoll_wait(server->epfd, events, sizeof(events)/sizeof(events[0]), -1);
		if (count < 0)
			error(true, "could not get events", NULL, true);
		for (int i = 0; i < count; i++) {
			if (events[i].data.ptr == server) {
				newconn(server);
			} else {
				connevent(server, &events[i]);
			}
		}
	}
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		printf("forward incoming data to all connected clients\nusage: %s PORT\n", argv[0]);
		return 1;
	}

	int port;
	errno = 0;
	port = strtol(argv[1], NULL, 10);
	if (errno != 0)
		error(true, "could not parse port number", NULL, true);

	struct server *server = init(port);
	printf("Listening on port %d\n", port);
	loop(server);
}
