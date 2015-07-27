/// implementing header
#include "self.h"

#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#define X(n, s) s,
char *error_str[] = {
    DI_ERRORS
};
#undef X

/// static function declarations
static struct self *self_init();
static void self_destroy(struct self *);
static void self_run_daemon(struct self *);
static void load_config(struct self *);
static void self_listen(struct self *);
static void try_bind(int, int*);

enum di_error di_daemon_load(struct di_daemon **d, struct di_options *opts){
    struct self *self = self_init();
    *d = (struct di_daemon *) self;
    return DI_ESUCCESS;
}
enum di_error di_daemon_run(struct di_daemon *d){
    self_run_daemon((struct self *) d);
    return DI_ESUCCESS;
}

// self_load loads all the necessary data structures into self required for
// starting the daemon
struct self *self_init(){
    struct self *self = calloc(1, sizeof(struct self));
    if (!self); //error
    load_config(self);
    self->rsa_key = util_read_rsa_pem("keys/private");
    byte tmp[PUB_KEY_LENGTH];
    util_rsa_pub_encode(tmp, self->rsa_key);
    hash_digest(self->fingerprint, tmp, PUB_KEY_LENGTH);
    self->peers = peer_create_list();
    peer_read_table(self->peers);
    //self->files = file_create_list();
    //file_read_table(self->files);
    int err = pthread_mutex_init(&self->udp_recv_mutex, NULL);
    if (err); //error
    err = pthread_cond_init(&self->udp_recv_cond, NULL);
    if (err); //error
    return self;
}

void self_destroy(struct self *self){
}

// self_run_daemon starts the main run loop and processing threads
void self_run_daemon(struct self *self){
    pthread_t recv_thread[UDP_RECV_THREADS];
    for (int i = 0; i < UDP_RECV_THREADS; i++){
        int err = pthread_create(recv_thread + i, NULL, message_recv_start,
                self);
        if (err); // system error
    }
    self_listen(self);
    // wait for recv threads
    for (int i = 0; i < UDP_RECV_THREADS; i++){
        int err = pthread_join(recv_thread[i], NULL);
        if (err); //error
    }
}

// load_config sets self->config values to those in the config file, or the
// defaults if absent
void load_config(struct self *self){
    //buffer file = read_file("config");
    self->config.file_folder = "";
    self->config.udp_port = 1024;
}

// self_listen opens the configured socket and begins a listener thread to
// service requests from peers
void self_listen(struct self *self){
    //TODO: open a separate UDP socket for exclusive use with bulk file
    //  tranfers (the result of a successful rq_find_file request)
    //  this is to ensure that bulk transfers don't crowd out incoming messages
    //  if it fills the socket buffer
    int sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sockfd == -1); //error
    try_bind(sockfd, &self->config.udp_port);
    self->udp_msg_socket = sockfd;
    byte *buf = malloc(UDP_RECV_THREADS * UDP_MAX_PAYLOAD);
    if (!buf); //error
    int i = 0;
    while (1){
        // cycle to next buffer
        byte *buf_cur = buf + (i * UDP_MAX_PAYLOAD);
        int length = UDP_MAX_PAYLOAD;;
        struct sockaddr_storage sa;
        socklen_t salen = sizeof(sa);
        // read datagram and put in buffer corresponding to thread 'i'
        int ret = recvfrom(sockfd, buf_cur, length, MSG_TRUNC,
                (struct sockaddr *) &sa, &salen);
        if (ret == -1); //error
        if (length < ret) continue; //error, payload too large
        struct address *addr = calloc(1, sizeof(struct address));
        util_get_address(addr, (struct sockaddr *) &sa);
        // lock mutex before setting predicate
        int err = pthread_mutex_lock(&self->udp_recv_mutex);
        if (err); // system error
        // set predicate for recv threads
        self->udp_recv_length = ret;
        self->udp_recv_buf = buf_cur;
        self->udp_recv_addr = addr;
        // hand off buffer to a recv thread
        err = pthread_cond_signal(&self->udp_recv_cond);
        if (err); // system error
        // unlock recv mutex so signalled thread can move forward
        err = pthread_mutex_unlock(&self->udp_recv_mutex);
        if (err); // system error
        //TODO:  the problem here is that if no threads are available when
        //  self_listen sends a signal, the loop will continue and the message
        //  will get skipped
        //  either not enough threads are configured, or the system is at
        //  capacity and skipped messages are inevitable
        //  in any case self_listen should be able to detect when no threads
        //  pick up the signal and push the message to a queue instead
        //  e.g. implementation:
        //  self_listen always pushes buffers to a queue, but when it notices
        //  queue is full waits on a condition instead
        //  recv threads always send a signal to that condition whenever they're
        //  done processing a particular buffer, which most of the time gets
        //  ignored because the queue isn't full, but when it is unblocks the
        //  listen loop so it can continue reading from the socket
    }
    free(buf);
    self->udp_msg_socket = 0;
    if (close(sockfd)); //error
}

// try_bind tries to bind to the configured port, but if it is busy or not
// accessible tries again with an ephemeral port, setting 'port' to the used 
// ephemeral port number
static void try_bind(int sockfd, int *port){
    // network byte order required for sockaddr
    in_port_t be_port = htons(*port);
    struct sockaddr_in6 sa = { AF_INET6, be_port, 0, IN6ADDR_ANY_INIT, 0 };
    socklen_t salen = sizeof(sa);
    // loop at most once
    while (bind(sockfd, (struct sockaddr *) &sa, salen)){
        if (errno == EADDRINUSE && port == 0); // system error, all ports in use
        else if (port != 0 && (errno == EACCES || errno == EADDRINUSE)){
            sa.sin6_port = 0;
            continue;
        }
        else {
            assert(false); // system error
            return;
        }
    }
    if (getsockname(sockfd, (struct sockaddr *) &sa, &salen)){
        if (errno == ENOBUFS); // system error
        assert(false);
    }
    *port = ntohs(sa.sin6_port);
}
