#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <signal.h>

#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/signalfd.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "hyper.h"

static const int MAX_EVENTS = 128;

typedef struct conn_data_s {
    int fd;
    hyper_waker *read_waker;
    hyper_waker *write_waker;
} conn_data;

typedef enum task_state_type_e {
  TASK_STATE_NONE,
  TASK_STATE_SERVERCONN,
  TASK_STATE_CLIENTCONN,
} task_state_type;

typedef struct task_state_s {
  task_state_type type;
  union {
    conn_data* conn;
  } data;
} task_state;

static int listen_on(const char* host, const char* port) {
    struct addrinfo hints;
    struct addrinfo *result;

    // Work out bind address
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    int gai_rc = getaddrinfo(host, port, &hints, &result);
    if (gai_rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai_rc));
        return -1;
    }

    // Try each bind address until one works
    int sock = -1;
    for (struct addrinfo *resp = result; resp; resp = resp->ai_next) {
        sock = socket(resp->ai_family, resp->ai_socktype, resp->ai_protocol);
        if (sock < 0) {
            perror("socket");
            continue;
        }

        // Enable SO_REUSEADDR
        int reuseaddr = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int)) < 0) {
            perror("setsockopt");
        }

        // Attempt to bind to the address
        if (bind(sock, resp->ai_addr, resp->ai_addrlen) == 0) {
            break;
        }

        // Failed, tidy up
        close(sock);
        sock = -1;
    }

    freeaddrinfo(result);

    if (sock < 0) {
      return -1;
    }

    // Non-blocking for async
    if (fcntl(sock, F_SETFL, O_NONBLOCK) != 0) {
        perror("fcntl(O_NONBLOCK) (listening)\n");
        return -1;
    }

    // Close handle on exec(ve)
    if (fcntl(sock, F_SETFD, FD_CLOEXEC) != 0) {
        perror("fcntl(FD_CLOEXEC) (listening)\n");
        return 1;
    }

    // Enable listening mode
    if (listen(sock, 32) < 0) {
        perror("listen");
        return -1;
    }

    return sock;
}

// Register interest in various termination signals.  The returned fd can be
// polled with epoll.
static int register_signal_handler() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGQUIT);
    int signal_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd < 0) {
      perror("signalfd");
      return 1;
    }
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
      perror("sigprocmask");
      return 1;
    }

    return signal_fd;
}

static size_t read_cb(void *userdata, hyper_context *ctx, uint8_t *buf, size_t buf_len) {
    conn_data *conn = (conn_data *)userdata;
    ssize_t ret = read(conn->fd, buf, buf_len);

    if (ret >= 0) {
        // Normal (synchronous) read successful (or socket is closed)
        return ret;
    }

    if (errno != EAGAIN) {
        // kaboom
        return HYPER_IO_ERROR;
    }

    // Otherwise this would block, so register interest and return pending
    if (conn->read_waker != NULL) {
        hyper_waker_free(conn->read_waker);
    }
    conn->read_waker = hyper_context_waker(ctx);
    return HYPER_IO_PENDING;
}

static size_t write_cb(void *userdata, hyper_context *ctx, const uint8_t *buf, size_t buf_len) {
    conn_data *conn = (conn_data *)userdata;
    ssize_t ret = write(conn->fd, buf, buf_len);

    if (ret >= 0) {
        // Normal (synchronous) write successful (or socket is closed)
        return ret;
    }

    if (errno != EAGAIN) {
        // kaboom
        return HYPER_IO_ERROR;
    }

    // Otherwise this would block, so register interest and return pending
    if (conn->write_waker != NULL) {
        hyper_waker_free(conn->write_waker);
    }
    conn->write_waker = hyper_context_waker(ctx);
    return HYPER_IO_PENDING;
}

static conn_data* create_conn_data(int epoll, int fd) {
    conn_data *conn = malloc(sizeof(conn_data));

    // Add fd to epoll set, associated with this `conn`
    struct epoll_event transport_event;
    transport_event.events = EPOLLIN;
    transport_event.data.ptr = conn;
    if (epoll_ctl(epoll, EPOLL_CTL_ADD, fd, &transport_event) < 0) {
        perror("epoll_ctl (transport)");
        free(conn);
        return NULL;
    }

    conn->fd = fd;
    conn->read_waker = NULL;
    conn->write_waker = NULL;

    return conn;
}

static hyper_io* create_io(conn_data* conn) {
    // Hookup the IO
    hyper_io *io = hyper_io_new();
    hyper_io_set_userdata(io, (void *)conn);
    hyper_io_set_read(io, read_cb);
    hyper_io_set_write(io, write_cb);

    return io;
}

static void free_conn_data(int epoll, conn_data *conn) {
    // Disassociate with the epoll
    if (epoll_ctl(epoll, EPOLL_CTL_DEL, conn->fd, NULL) < 0) {
        perror("epoll_ctl (transport)");
    }

    // Drop any saved-off wakers
    if (conn->read_waker) {
        hyper_waker_free(conn->read_waker);
        conn->read_waker = NULL;
    }
    if (conn->write_waker) {
        hyper_waker_free(conn->write_waker);
        conn->write_waker = NULL;
    }

    // Shut down the socket connection
    close(conn->fd);

    // ...and clean up
    free(conn);
}

typedef enum {
    EXAMPLE_NOT_SET = 0, // tasks we don't know about won't have a userdata set
    EXAMPLE_HANDSHAKE,
    EXAMPLE_SEND,
    EXAMPLE_RESP_BODY
} example_id;

static void server_callback(void* userdata, hyper_request* request, hyper_response* response, hyper_response_channel* channel) {
    hyper_request_free(request);
    hyper_response_channel_send(channel, response);
}

int main(int argc, char *argv[]) {
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    const char *port = argc > 2 ? argv[2] : "1234";
    printf("listening on port %s on %s...\n", port, host);

    // The main listening socket
    int listen_fd = listen_on(host, port);
    if (listen_fd < 0) {
        return 1;
    }

    int signal_fd = register_signal_handler();
    if (signal_fd < 0) {
      return 1;
    }

    // Use epoll cos' it's cool
    int epoll = epoll_create1(EPOLL_CLOEXEC);
    if (epoll < 0) {
        perror("epoll");
        return 1;
    }

    // Always await new connections from the listen socket
    struct epoll_event listen_event;
    listen_event.events = EPOLLIN;
    listen_event.data.ptr = &listen_fd;
    if (epoll_ctl(epoll, EPOLL_CTL_ADD, listen_fd, &listen_event) < 0) {
        perror("epoll_ctl (add listening)");
        return 1;
    }

    // Always await signals on the signal socket
    struct epoll_event signal_event;
    signal_event.events = EPOLLIN;
    signal_event.data.ptr = &signal_fd;
    if (epoll_ctl(epoll, EPOLL_CTL_ADD, signal_fd, &signal_event) < 0) {
        perror("epoll_ctl (add signal)");
        return 1;
    }


    printf("http handshake (hyper v%s) ...\n", hyper_version());

    // We need an executor generally to poll futures
    const hyper_executor *exec = hyper_executor_new();

    // Configure the server HTTP stack
    hyper_serverconn_options *opts = hyper_serverconn_options_new(exec);

    // Might have an error
    hyper_error *err;

    while (1) {
        while (1) {
            hyper_task* task = hyper_executor_poll(exec);
            if (!task) {
                break;
            }
            if (hyper_task_type(task) == HYPER_TASK_ERROR) {
                printf("handshake error!\n");

                err = hyper_task_value(task);
                printf("error code: %d\n", hyper_error_code(err));
                uint8_t errbuf [256];
                size_t errlen = hyper_error_print(err, errbuf, sizeof(errbuf));
                printf("details: %.*s\n", (int) errlen, errbuf);

                // clean up the error
                hyper_error_free(err);

                // clean up the task
                conn_data* conn = hyper_task_userdata(task);
                if (conn) {
                    free_conn_data(epoll, conn);
                }
                hyper_task_free(task);

                continue;
            }

            if (hyper_task_type(task) == HYPER_TASK_EMPTY) {
                conn_data* conn = hyper_task_userdata(task);
                if (conn) {
                    printf("server connection complete\n");
                    free_conn_data(epoll, conn);
                } else {
                    printf("internal hyper task complete\n");
                }
                hyper_task_free(task);

                continue;
            }
        }

        printf("Processed all tasks - polling for events\n");

        struct epoll_event events[MAX_EVENTS];

        int nevents = epoll_wait(epoll, events, MAX_EVENTS, -1);
        if (nevents < 0) {
            perror("epoll");
            return 1;
        }

        printf("Poll reported %d events\n", nevents);

        for (int n = 0; n < nevents; n++) {
            if (events[n].data.ptr == &listen_fd) {
                // Incoming connection(s) on listen_fd
                int new_fd;
                struct sockaddr_storage remote_addr_storage;
                struct sockaddr* remote_addr = (struct sockaddr*)&remote_addr_storage;
                socklen_t remote_addr_len = sizeof(struct sockaddr_storage);
                while ((new_fd = accept(listen_fd, (struct sockaddr*)&remote_addr_storage, &remote_addr_len)) >= 0) {
                  char remote_host[128];
                  char remote_port[8];
                  if (getnameinfo(remote_addr, remote_addr_len, remote_host, sizeof(remote_host), remote_port, sizeof(remote_port), NI_NUMERICHOST | NI_NUMERICSERV) < 0) {
                    perror("getnameinfo");
                    printf("New incoming connection from (unknown)\n");
                  } else {
                    printf("New incoming connection from (%s:%s)\n", remote_host, remote_port);
                  }

                  // Set non-blocking
                  if (fcntl(new_fd, F_SETFL, O_NONBLOCK) != 0) {
                      perror("fcntl(O_NONBLOCK) (transport)\n");
                      return 1;
                  }

                  // Close handle on exec(ve)
                  if (fcntl(new_fd, F_SETFD, FD_CLOEXEC) != 0) {
                    perror("fcntl(FD_CLOEXEC) (transport)\n");
                    return 1;
                  }

                  // Wire up IO
                  conn_data *conn = create_conn_data(epoll, new_fd);
                  hyper_io* io = create_io(conn);

                  // Ask hyper to drive this connection
                  hyper_service *service = hyper_service_new(server_callback);
                  hyper_task *serverconn = hyper_serve_connection(opts, io, service);
                  hyper_task_set_userdata(serverconn, conn);
                  hyper_executor_push(exec, serverconn);
                }

                if (errno != EAGAIN) {
                    perror("accept");
                }
            } else if (events[n].data.ptr == &signal_fd) {
                struct signalfd_siginfo siginfo;
                if (read(signal_fd, &siginfo, sizeof(struct signalfd_siginfo)) != sizeof(struct signalfd_siginfo)) {
                    perror("read (signal_fd)");
                    return 1;
                }

                if (siginfo.ssi_signo == SIGINT) {
                    printf("Caught SIGINT... exiting\n");
                    goto EXIT;
                } else if (siginfo.ssi_signo == SIGTERM) {
                    printf("Caught SIGTERM... exiting\n");
                    goto EXIT;
                } else if (siginfo.ssi_signo == SIGQUIT) {
                    printf("Caught SIGQUIT... exiting\n");
                    goto EXIT;
                } else {
                    printf("Caught unexpected signal %d... ignoring\n", siginfo.ssi_signo);
                }
            } else {
                // Existing transport socket, poke the wakers or close the socket
                conn_data* conn = events[n].data.ptr;
                if ((events[n].events & EPOLLIN) && conn->read_waker) {
                    hyper_waker_wake(conn->read_waker);
                    conn->read_waker = NULL;
                }
                if ((events[n].events & EPOLLOUT) && conn->write_waker) {
                    hyper_waker_wake(conn->write_waker);
                    conn->write_waker = NULL;
                }
            }
        }
    }

EXIT:
    hyper_serverconn_options_free(opts);
    hyper_executor_free(exec);

    return 1;
}
