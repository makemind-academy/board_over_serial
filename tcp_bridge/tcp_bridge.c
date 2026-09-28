/*
 * tcp_bridge — the same job as serial_bridge, over a socket instead of a wire.
 *
 *     mcp_client  ──stdio──▶  tcp_bridge  ──TCP──▶  board on the wifi
 *
 * Put this next to serial_bridge.c and read them together: that is the whole
 * point of the article. The board on the desk and the board across the room
 * are reached by two different programs, and **the client above them is byte
 * for byte the same**. Choosing a transport is choosing which of these to
 * launch.
 *
 * The board advertises itself over mDNS as `_mcp._tcp` with a TXT record that
 * says `proto=ndjson` — newline-delimited JSON-RPC, exactly what came down the
 * UART. So once the socket is open there is nothing left to translate.
 *
 * Usage:  tcp_bridge <host> <port>
 * Build:  cc -O2 -o tcp_bridge tcp_bridge.c
 */

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        return 2;
    }

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(argv[1], argv[2], &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "tcp_bridge: cannot resolve %s:%s: %s\n",
                argv[1], argv[2], gai_strerror(rc));
        return 1;
    }

    int fd = -1;
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        fprintf(stderr, "tcp_bridge: cannot connect to %s:%s: %s\n",
                argv[1], argv[2], strerror(errno));
        return 1;
    }

    /* A request is one short line and the answer matters immediately. Letting
     * Nagle hold it back to fill a segment adds latency for no gain here. */
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    /* Same rule as the serial side: only JSON lines are protocol. A board is
     * free to print notes to the same stream and a client must not choke. */
    char up[8192];
    size_t up_len = 0;

    for (;;) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(0, &rd);
        FD_SET(fd, &rd);

        struct timeval tv = {0, 20000};
        int n = select(fd + 1, &rd, NULL, NULL, &tv);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (FD_ISSET(0, &rd)) {
            char buf[4096];
            ssize_t r = read(0, buf, sizeof(buf));
            if (r == 0) break;
            if (r > 0) {
                ssize_t off = 0;
                while (off < r) {
                    ssize_t w = write(fd, buf + off, (size_t)(r - off));
                    if (w <= 0) break;
                    off += w;
                }
            }
        }

        if (FD_ISSET(fd, &rd)) {
            char buf[4096];
            ssize_t r = read(fd, buf, sizeof(buf));
            if (r == 0) break;      /* board closed the socket */
            for (ssize_t k = 0; k < r; k++) {
                char c = buf[k];
                if (c == '\r') continue;
                if (c != '\n') {
                    if (up_len + 1 < sizeof(up)) up[up_len++] = c;
                    continue;
                }
                up[up_len] = '\0';
                if (up_len > 0) {
                    if (up[0] == '{') {
                        printf("%s\n", up);
                        fflush(stdout);
                    } else {
                        fprintf(stderr, "[board] %s\n", up);
                    }
                }
                up_len = 0;
            }
        }
    }

    close(fd);
    return 0;
}
