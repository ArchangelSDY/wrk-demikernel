// Copyright (C) 2013 - Will Glozer.  All rights reserved.

#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>

#if (HAVE_DEMIKERNEL)
#include <demi/libos.h>
#endif

#include "net.h"

status sock_connect(connection *c, char *host) {
    return OK;
}

status sock_close(connection *c) {
    return OK;
}

status sock_read(connection *c, size_t *n) {
#if (HAVE_DEMIKERNEL)
    aeFileEvent *fe = &c->thread->loop->events[c->fd];
    demi_qtoken_t qt;
    if (fe->readable) {
        *n = (size_t) fe->readable;
        c->response_buf = fe->rsga;
        c->buf = fe->rsga.sga_segs[0].sgaseg_buf;
        fe->readable = 0;
        return OK;
    }
    if (!(fe->pending & AE_READABLE)) {
        if (demi_pop(&qt, c->fd) != 0) {
            return ERROR;
        }

        fe->pending |= AE_READABLE;

        aeRegisterQToken(c->thread->loop, qt);

        return RETRY;
    }
    return RETRY;
#else
    ssize_t r = read(c->fd, c->buf, sizeof(c->buf));
    *n = (size_t) r;
    return r >= 0 ? OK : ERROR;
#endif
}

status sock_write(connection *c, char *buf, size_t len, size_t *n) {
#if (HAVE_DEMIKERNEL)
    if (len == 0) {
        *n = len;
        return OK;
    }
    aeFileEvent *fe = &c->thread->loop->events[c->fd];
    demi_qtoken_t qt;
    if (fe->pending & AE_WRITABLE) {
        *n = 0;
        return RETRY;
    }
    if (demi_push(&qt, c->fd, &c->request_buf) != 0) {
        return ERROR;
    }
    fe->pending |= AE_WRITABLE;
    aeRegisterQToken(c->thread->loop, qt);
    *n = len;
    return OK;
#else
    ssize_t r;
    if ((r = write(c->fd, buf, len)) == -1) {
        switch (errno) {
            case EAGAIN: return RETRY;
            default:     return ERROR;
        }
    }
    *n = (size_t) r;
    return OK;
#endif
}

size_t sock_readable(connection *c) {
#if (HAVE_DEMIKERNEL)
    aeFileEvent *fe = &c->thread->loop->events[c->fd];
    return fe->readable;
#else
    int n, rc;
    rc = ioctl(c->fd, FIONREAD, &n);
    return rc == -1 ? 0 : n;
#endif
}
