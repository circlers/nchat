#include "common.h"
#include <netdb.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define CONNS 64
#define NICKLEN 64

enum role {
	R_RECEIVE = 1 << 0,
	R_SEND    = 1 << 1,
};

struct conn {
	int sock;
	struct sockaddr addr;
	socklen_t addr_len;
	char nickname[NICKLEN];
	enum role roles; // bitmap
};

struct server {
	int sock;
	int epfd;
	int count; // number of connections
	struct conn *conns[CONNS];
};

enum cmd {
	C_MSG, // No command at all or /msg
	C_LISTEN, // /listen – allow for incoming messages
	C_NICK, // /nick NICK – set or change nickname, allow for outgoing messages

	C_MAX
};

char *cmd_str[] = {
	[C_MSG] = "msg",
	[C_LISTEN] = "listen",
	[C_NICK] = "nick",

	[C_MAX] = "",
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
		EPOLLIN | EPOLLOUT | EPOLLET, // TODO ?
		{ .ptr = server }
	};
	if (epoll_ctl(server->epfd, EPOLL_CTL_ADD, server->sock, &ev) != 0)
		error(true, "could not subscribe to new connection events", NULL, true);

	return server;
}

// Might return NULL in case of failure.
struct conn *newconn(struct server *server) {
	if (server->count == CONNS) {
		error(false, "connection limit exhausted", NULL, false);
		return NULL;
	}

	// Accept a new connection

	struct conn pconn = {0};
	pconn.sock = accept(server->sock, &pconn.addr, &pconn.addr_len);
	if (pconn.sock < 0) {
		error(false, "could not accept incoming connection", NULL, true);
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
		EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLET, // TODO ?
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

void unicast(struct server *server, struct conn *conn, struct msg *msg) {
	printf("Unicast: %s\n", msg->buf); // TODO: details
	if (msg->buf[msg->len - 1] != '\n')
		append_msg(msg, "\n");
	send_data(conn->sock, msg);
}

// Send message to all clinets with at least the specified roles.
void broadcast(struct server *server, enum role roles, struct msg *msg) {
	printf("Broadcast: %s\n", msg->buf);
	if (msg->buf[msg->len - 1] != '\n')
		append_msg(msg, "\n");
	for (int i = 0; i < server->count; i++) {
		if (server->conns[i]->roles & roles) {
			send_data(server->conns[i]->sock, msg);
		}
	}
}

void usrmsg(struct server *server, const char *nick, const char *str) {
	struct msg *msg = init_msg(nick);
	append_msg(msg, "> ");
	append_msg(msg, str);
	broadcast(server, R_RECEIVE, msg);
	free_msg(msg);
}

void sysmsg(struct server *server, const char *str) {
	struct msg *msg = init_msg("[ ");
	append_msg(msg, str);
	append_msg(msg, " ]");
	broadcast(server, R_RECEIVE, msg);
	free_msg(msg);
}

// Sets *text to point to the beginning of the first argument.
enum cmd detect_command(struct msg *msg, char **text) {
	*text = msg->buf;
	if (msg->len < 2 || msg->buf[0] != '/')
		return C_MSG;
	for (enum cmd i = 0; i < C_MAX; i++) {
		int len = strlen(cmd_str[i]);
		if (strncmp(cmd_str[i], &msg->buf[1], len) == 0) {
			*text = &msg->buf[1 + len];
			while (**text == ' ') (*text)++;
			return i;
		}
	}
	return C_MSG;
}

// Process an incoming message.
void process(struct server *server, struct conn *conn, struct msg *msg) {
	char *text;
	enum cmd cmd = detect_command(msg, &text);
	switch (cmd) {
	case C_LISTEN:
		conn->roles |= R_RECEIVE;
		// TODO
		break;
	case C_NICK:
		for (int i = 0; text[i]; i++) {
			if (text[i] == ' ') {
				text[i] = '\0';
				break;
			}
		}
		char announcement[BUFSZ];
		snprintf(announcement, BUFSZ, "'%s' is now '%s'.", conn->nickname, text);
		if (text[0] != '\0') {
			*stpncpy(conn->nickname, text, NICKLEN - 1) = '\0';
			if (!(conn->roles & R_SEND)) {
				snprintf(announcement, BUFSZ, "'%s' has joined the chat.", conn->nickname);
			}
			sysmsg(server, announcement);
			conn->roles |= R_SEND;

			struct msg *msg = init_msg("Connected.");
			unicast(server, conn, msg);
			free_msg(msg);
		} else {
			struct msg *msg = init_msg("Error: empty nickname specified. Use '/nick NICK' again.");
			unicast(server, conn, msg);
			free_msg(msg);
		}
		break;
	case C_MSG:
	default:
		if (conn->roles & R_SEND) {
			usrmsg(server, conn->nickname, text);
		} else {
			struct msg *msg = init_msg("Error: use '/nick NICK' before messaging.");
			unicast(server, conn, msg);
			free_msg(msg);
		}
		break;
	}
}

void delconn(struct server *server, struct conn *conn) {
	if (conn->roles & R_SEND) {
		char smsg[BUFSZ];
		snprintf(smsg, BUFSZ, "'%s' has left the chat.", conn->nickname);
		sysmsg(server, smsg);
		conn->roles |= R_SEND;
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
		msg_stripnl(data);
		process(server, conn, data);
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
	if (argc != 1 && argc != 2) {
		printf("run a relay\nusage: %s PORT\n", argv[0]);
		return 1;
	}

	int port = 4620;
	if (argc == 2) {
		errno = 0;
		port = strtol(argv[1], NULL, 10);
		if (errno != 0)
			error(true, "could not parse port number", NULL, true);
	}

	struct server *server = init(port);
	loop(server);
}
