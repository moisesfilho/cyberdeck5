#include <errno.h>
#include <string.h>
#include "lwip/sockets.h"
#include "socketpair.h"

int socketpair(int domain, int type, int protocol, int sv[2])
{
    if (sv == NULL || domain != AF_UNIX || type != SOCK_STREAM || protocol != 0) {
        errno = EINVAL;
        return -1;
    }
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener < 0) return -1;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(listener, 1) < 0) {
        close(listener);
        return -1;
    }
    socklen_t length = sizeof(address);
    if (getsockname(listener, (struct sockaddr *)&address, &length) < 0) {
        close(listener);
        return -1;
    }
    int first = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (first < 0 || connect(first, (struct sockaddr *)&address, length) < 0) {
        if (first >= 0) close(first);
        close(listener);
        return -1;
    }
    int second = accept(listener, NULL, NULL);
    close(listener);
    if (second < 0) {
        close(first);
        return -1;
    }
    sv[0] = first;
    sv[1] = second;
    return 0;
}

int pipe(int pipefd[2])
{
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd) < 0) return -1;
    if (shutdown(pipefd[0], SHUT_WR) < 0 || shutdown(pipefd[1], SHUT_RD) < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    return 0;
}
