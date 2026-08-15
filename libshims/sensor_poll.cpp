/*
 * Compatibility shim for the legacy Qualcomm sensor daemon.
 *
 * The daemon builds two adjacent pollfd entries but passes nfds == 1, then
 * checks revents in both entries.  Poll both descriptors when that exact
 * layout is detected so the second revents field is initialized correctly.
 */

#include <poll.h>
#include <sys/syscall.h>
#include <unistd.h>

extern "C" int poll(struct pollfd* fds, nfds_t nfds, int timeout) {
    const short sensor_events = POLLIN | POLLPRI | POLLERR;

    if (fds != nullptr && nfds == 1 &&
            fds[0].fd >= 0 && fds[0].events == sensor_events &&
            fds[1].fd >= 0 && fds[1].events == sensor_events) {
        nfds = 2;
    }

    return syscall(__NR_poll, fds, nfds, timeout);
}
