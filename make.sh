#!/bin/bash

mkdir -p bin
gcc -Wall -Wextra -g common.c listener.c -o bin/listener
gcc -Wall -Wextra -g common.c sender.c -o bin/sender
gcc -Wall -Wextra -g common.c relay.c -o bin/relay
gcc -Wall -Wextra -g common.c replicator.c -o bin/replicator
