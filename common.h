#pragma once

#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>

#define BUFSZ 10240
struct msg {
	int len; // excluding '\0', ie. it is the index of the trailing null byte
	char buf[BUFSZ];
};

struct msg *init_empty_msg();
struct msg *init_msg(const char *str);
void append_msg(struct msg *msg, const char *str);
// Append line from file to msg.
void read_msg(struct msg *msg, FILE *fd);
void msg_stripnl(struct msg *msg);
void free_msg(struct msg *msg);

void error(bool fatal, const char *msg, const char *cause, bool use_errno);

// Returned value must be freed using freeaddrinfo.
struct addrinfo *resolve(const char *host, const char *port);
// Returns the socket file descriptor, which should be closed manually.
int setup_client(const struct addrinfo *ai);
// Returns true on success.
bool send_data(int sock, const struct msg *msg);
// Returns NULL on failure.
struct msg *recv_data(int sock);
