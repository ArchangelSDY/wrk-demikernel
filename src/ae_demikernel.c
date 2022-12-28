#include <demi/libos.h>
#include <demi/sga.h>
#include <demi/wait.h>

typedef struct aeApiState {
    demi_qtoken_t *qts;
    uint32_t nqts;
} aeApiState;

static int aeApiCreate(aeEventLoop *eventLoop) {
    aeApiState *state = zmalloc(sizeof(aeApiState));

    if (!state) return -1;
    state->qts = zmalloc(sizeof(demi_qtoken_t)*eventLoop->setsize);
    if (!state->qts) {
        zfree(state);
        return -1;
    }
    state->nqts = 0;
    eventLoop->apidata = state;
    return 0;
}

static int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    aeApiState *state = eventLoop->apidata;

    state->qts = zrealloc(state->qts, sizeof(demi_qtoken_t)*setsize);
    return 0;
}

static void aeApiFree(aeEventLoop *eventLoop) {
    aeApiState *state = eventLoop->apidata;

    zfree(state->qts);
    zfree(state);
}

static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    return 0;
}

static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int delmask) {
    aeFileEvent *fe = &eventLoop->events[fd];
    fe->connecting = 0;
    fe->reading = 0;
    fe->writing = 0;
    fe->fail = 0;
}

static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;
    aeFileEvent *fe;
    int err, offset, mask, numevents;
    demi_qresult_t qr;
    struct timespec timeout = {
        .tv_sec = tvp->tv_sec,
        .tv_nsec = tvp->tv_usec * 1000
    };

    err = demi_wait_any(&qr, &offset, state->qts, state->nqts, &timeout);

    numevents = 0;
    if (err == 0) {
        state->nqts--;
        state->qts[offset] = state->qts[state->nqts];

        mask = 0;
        fe = &eventLoop->events[qr.qr_qd];
        switch (qr.qr_opcode) {
            case DEMI_OPC_PUSH:
                mask |= AE_WRITABLE;
                fe->writing = 0;
                break;

            case DEMI_OPC_POP:
                mask |= AE_READABLE;
                fe->reading = 0;
                fe->rsga = qr.qr_value.sga;
                fe->readable = fe->rsga.sga_segs[0].sgaseg_len;
                break;

            case DEMI_OPC_CONNECT:
                mask |= AE_WRITABLE;
                mask |= AE_READABLE;
                fe->connecting = 0;
                break;

            case DEMI_OPC_FAILED:
                fe->fail = 1;
                break;

            default:
                return 0;
        }

        eventLoop->fired[0].fd = qr.qr_qd;
        eventLoop->fired[0].mask = mask;
        numevents++;
    } else {
        if (err != ETIMEDOUT) {
            return 0;
        }
    }

    for (int i = eventLoop->maxfd; i >= 1000000; i--) {
        fe = &eventLoop->events[i];
        mask = 0;

        if (fe->connecting || fe->fail) {
            continue;
        }
        if (!fe->reading) {
            mask |= AE_READABLE;
        }
        if (!fe->writing) {
            mask |= AE_WRITABLE;
        }

        if (numevents == 1 && i == eventLoop->fired[0].fd) {
            eventLoop->fired[0].mask |= mask;
        } else {
            eventLoop->fired[numevents].fd = i;
            eventLoop->fired[numevents].mask = mask;
            numevents++;
        }
    }

    return numevents;
}

void aeRegisterQToken(aeEventLoop *eventLoop, demi_qtoken_t qt) {
    aeApiState *state = eventLoop->apidata;
    state->qts[state->nqts] = qt;
    state->nqts++;
}

static char *aeApiName(void) {
    return "demikernel";
}
