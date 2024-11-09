#include "common.h"

#include <netdb.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void error(bool fatal, const char *msg, const char *cause, bool use_errno) {
	fprintf(stderr, "error: %s\n", msg);
	if (cause)
		fprintf(stderr, "cause: %s\n", cause);
	if (use_errno && errno != 0)
		perror("cause");
	if (fatal)
		exit(10);
}

struct addrinfo *resolve(const char *host, const char *port) {
	const struct addrinfo hints = {
		0,
		AF_INET,
		SOCK_STREAM,
		0,
		0,
		NULL,
		NULL,
		NULL
	};

	struct addrinfo *addr = NULL;
	int status = getaddrinfo(host, port, &hints, &addr);
	if (status != 0)
		error(true, "could not resolve host name", gai_strerror(status), false);
	return addr;
}

int setup_client(const struct addrinfo *ai) {
	int sock = socket(ai->ai_family, SOCK_STREAM, 0);
	if (sock < 0)
		error(true, "could not create a client socket", NULL, true);

	if (connect(sock, ai->ai_addr, ai->ai_addrlen) != 0)
		error(true, "could not connect to server", NULL, true);

	return sock;
}

struct msg *init_empty_msg() {
	struct msg *msg = malloc(sizeof(struct msg));
	if (msg == NULL)
		error(true, "error allocating memory", NULL, true);
	msg->buf[0] = '\0';
	msg->len = 0;
	return msg;
}

void append_msg(struct msg *msg, const char *str) {
	int maxlen = sizeof(msg->buf) - 1 - msg->len;
	if (maxlen <= 0) {
		error(false, "message too long, will be truncated", NULL, false);
		return;
	}
	int len = strnlen(str, maxlen);
	if (len == maxlen)
		error(false, "message too long, will be truncated", NULL, false);
	strncpy(&msg->buf[msg->len], str, len);
	msg->len += len;
	msg->buf[msg->len] = '\0';
}

struct msg *init_msg(const char *str) {
	struct msg *msg = init_empty_msg();
	append_msg(msg, str);
	return msg;
}

void read_msg(struct msg *msg, FILE *fd) {
	char buf[BUFSZ];
	if (fgets(buf, sizeof(buf), fd) == NULL) {
		error(false, "could not read message", NULL, true);
		return;
	}
	append_msg(msg, buf);
}

void msg_stripnl(struct msg *msg) {
	if (msg->buf[msg->len - 1] == '\n')
		msg->buf[--msg->len] = '\0';
}

void free_msg(struct msg *msg) {
	free(msg);
}

bool send_data(int sock, const struct msg *msg) {
	ssize_t sent = send(sock, msg->buf, msg->len, 0);
	if (sent < msg->len) {
		error(false, "could not send data", NULL, true);
		return false;
	} else {
		return true;
	}
}

struct msg *recv_data(int sock) {
	struct msg *msg = init_empty_msg();
	ssize_t len = recv(sock, msg->buf, sizeof(msg->buf) - 1, 0);
	if (len < 0) {
		error(false, "error receiving data", NULL, true);
		free_msg(msg);
		return NULL;
	} else {
		msg->len = len;
		return msg;
	}
}
