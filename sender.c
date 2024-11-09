#include "common.h"

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>

void sender(const struct addrinfo *ai) {
	int sock = setup_client(ai);
	while (1) {
		struct msg *msg = init_empty_msg();
		read_msg(msg, stdin);
		msg_stripnl(msg);
		send_data(sock, msg);
		free_msg(msg);
	}
}

int main(int argc, char *argv[]) {
	if (argc != 2 && argc != 3) {
		printf("connect to a relay as a sender\nusage: %s HOST [PORT]\n", argv[0]);
		return 1;
	}

	const char *port = "4621";
	if (argc == 3) {
		port = argv[2];
	}

	struct addrinfo *ai = resolve(argv[1], port);

	sender(ai);

	freeaddrinfo(ai);
	return 0;
}
