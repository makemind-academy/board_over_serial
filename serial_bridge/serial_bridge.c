/*
 * serial_bridge — put a serial port on stdio, so an MCP client can talk to a
 * board without knowing anything about UARTs.
 *
 * The board on the other end speaks newline-delimited JSON-RPC at 115200 8N1.
 * That is already MCP; it just happens to arrive on a wire instead of a pipe.
 * So this program does the smallest possible thing: raw the tty, set the baud,
 * and copy bytes both ways.
 *
 *     mcp_client  ──stdio──▶  serial_bridge  ──UART──▶  board
 *
 * Why it is a separate process rather than a serial library inside the client:
 * every MCP client already knows how to launch a command and speak to its
 * stdio. Making the wire a process means the client needs no serial support at
 * all, on any platform, and the same client code works against a board, a
 * simulator, or a server on the far side of a network.
 *
 * Usage:  serial_bridge /dev/cu.usbmodemXXXX [baud]
 * Build:  cc -O2 -o serial_bridge serial_bridge.c
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

static speed_t baud_constant(long baud) {
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default: return 0;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <port> [baud]\n", argv[0]);
        return 2;
    }
    const char *port = argv[1];
    long baud = argc > 2 ? strtol(argv[2], NULL, 10) : 115200;
    speed_t speed = baud_constant(baud);
    if (speed == 0) {
        fprintf(stderr, "serial_bridge: unsupported baud %ld\n", baud);
        return 2;
    }

    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "serial_bridge: cannot open %s: %s\n", port, strerror(errno));
        return 1;
    }

    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        fprintf(stderr, "serial_bridge: tcgetattr: %s\n", strerror(errno));
        return 1;
    }
    cfmakeraw(&tio);          /* no echo, no line editing, no CR/LF mangling */
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= (tcflag_t)~CRTSCTS;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        fprintf(stderr, "serial_bridge: tcsetattr: %s\n", strerror(errno));
        return 1;
    }
    tcflush(fd, TCIOFLUSH);

    /* Boards print human-readable log lines on the same wire as their JSON-RPC
     * replies. Those lines are not protocol and would make a client's parser
     * unhappy, so only lines that look like JSON are forwarded. Everything else
     * goes to stderr, where it stays visible without being mistaken for a
     * reply. */
    /* A page the board serves is one reply line, and a real screen runs to
     * several kilobytes once every state has its own sentence. A line that
     * does not fit is dropped and said so — a truncated reply forwarded as
     * if whole makes the client wait on JSON that never closes. */
    static char up[32768];
    size_t up_len = 0;
    int up_overflow = 0;

    for (;;) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(0, &rd);
        FD_SET(fd, &rd);
        int maxfd = fd > 0 ? fd : 0;

        struct timeval tv = {0, 20000}; /* 20 ms */
        int n = select(maxfd + 1, &rd, NULL, NULL, &tv);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* client -> board */
        if (FD_ISSET(0, &rd)) {
            char buf[2048];
            ssize_t r = read(0, buf, sizeof(buf));
            if (r == 0) break;      /* client closed; we are done */
            if (r > 0) {
                ssize_t off = 0;
                while (off < r) {
                    ssize_t w = write(fd, buf + off, (size_t)(r - off));
                    if (w <= 0) break;
                    off += w;
                }
            }
        }

        /* board -> client, one line at a time */
        if (FD_ISSET(fd, &rd)) {
            char buf[2048];
            ssize_t r = read(fd, buf, sizeof(buf));
            for (ssize_t k = 0; k < r; k++) {
                char c = buf[k];
                if (c == '\r') continue;
                if (c != '\n') {
                    if (up_len + 1 < sizeof(up)) up[up_len++] = c;
                    else up_overflow = 1;
                    continue;
                }
                up[up_len] = '\0';
                if (up_overflow) {
                    fprintf(stderr, "serial_bridge: dropped a %zu+ byte line that did not fit\n", up_len);
                    up_overflow = 0;
                } else if (up_len > 0) {
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
