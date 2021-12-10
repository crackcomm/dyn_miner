#include "nlohmann/json.hpp"

#ifdef _WIN32
#include "winsock2.h"
#else
#include <netdb.h>
#endif

#ifdef _WIN32
#define write(sock, buf, len) send(sock, buf, len, 0)
#endif

#define CBSIZE 2048

using json = nlohmann::json;

struct cbuf_t {
    char buf[CBSIZE] = {0};
    int fd;
    unsigned int rpos = 0;
    unsigned int wpos = 0;
};

inline int read_line(cbuf_t* cbuf, char* dst, unsigned int size) {
    unsigned int i = 0;
    size_t n;
    while (i < size) {
        if (cbuf->rpos == cbuf->wpos) {
            size_t wpos = cbuf->wpos % CBSIZE;
            if ((n = recv(cbuf->fd, cbuf->buf + wpos, (CBSIZE - wpos), 0)) < 0) {
                if (errno == EINTR) continue;
                return -1;
            } else if (n == 0)
                return 0;
            cbuf->wpos += n;
        }
        dst[i++] = cbuf->buf[cbuf->rpos++ % CBSIZE];
        if (dst[i - 1] == '\n') break;
    }
    if (i == size) {
        fprintf(stderr, "line too large: %d %d\n", i, size);
        return -1;
    }

    dst[i] = 0;
    return i;
}
