#include "common.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>

void listener(const struct addrinfo *ai) {
	int sock = setup_client(ai);


	// const struct sockaddr_in addr = {
	// 	AF_INET,
	// 	htons(port),
	// 	{ INADDR_ANY },
	// 	{ 0 }
	// };

	// if (bind(sock, (const struct sockaddr *)&addr, sizeof(addr)))
	// 	error("could not bind");

	while (1) {
		struct msg *msg = recv_data(sock);
		if (msg) {
			printf("Received: %s\n", msg->buf);
			free_msg(msg);
		}
		// char buf[BUFSZ];
		// ssize_t len = recvfrom(sock, &buf, sizeof(buf) - 1, 0, NULL, NULL); // TODO: Use the source address?
		// if (len < 0)
		// 	error("error receiving data");
		// if (len > 0)
		// 	printf("%.*s", (int)len, buf);
		// // TODO: does it queue messages when not listening? how to do that?
	}
}

int main(int argc, char *argv[]) {
	if (argc != 2 && argc != 3) {
		printf("connect to a relay as a listener\nusage: %s HOST [PORT]\n", argv[0]);
		return 1;
	}

	const char *port = "4621";
	if (argc == 3) {
		port = argv[2];
	}

	struct addrinfo *ai = resolve(argv[1], port);

	listener(ai);

	freeaddrinfo(ai);
	return 0;
}
