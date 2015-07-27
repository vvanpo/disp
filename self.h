#ifndef SELF_H
#define SELF_H

/// includes
#include <arpa/inet.h>
#include <assert.h>
#include <openssl/ripemd.h>
#include <openssl/rsa.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include "disponible.h"

/// macro definitions
#define DIGEST_LENGTH RIPEMD160_DIGEST_LENGTH
#define B64_DIGEST_LENGTH (4 * ((DIGEST_LENGTH + 2) / 3) + 1)
#define RSA_MODULUS_LENGTH 256
#define RSA_EXPONENT_LENGTH 4
#define PUB_KEY_LENGTH (RSA_EXPONENT_LENGTH + RSA_MODULUS_LENGTH)
#define RSA_PADDING RSA_PKCS1_OAEP_PADDING
#define RSA_MESSAGE_MAX_LENGTH (RSA_MODULUS_LENGTH - 41)
#define HMAC_KEY_LENGTH 16
#define UDP_RECV_THREADS 4
#define UDP_MAX_PAYLOAD 512
#define DEFAULT_BUCKET_SIZE 20
#define DEFAULT_BUCKET_MAX_DEPTH 4

/// type definitions
typedef unsigned char byte;
struct self {
    struct {
        char *file_folder;
        int udp_port;
        int tcp_port;
        int bucket_size;
        int bucket_max_depth;
    } config;
    byte fingerprint[DIGEST_LENGTH];
    RSA *rsa_key;
    byte **authorized_keys;
    struct peers *peers;
    struct files *files;
    int udp_msg_socket;
    pthread_mutex_t udp_recv_mutex;
    pthread_cond_t udp_recv_cond;
    // message buffer protected by udp_recv_mutex
    // threads should test for !udp_recv_buf || !udp_recv_addr
    //  || !udp_recv_length
    byte *udp_recv_buf;
    struct address *udp_recv_addr;
    int udp_recv_length;
};
struct address {
    char *fqdn;
    enum { ipv4, ipv6 } ip_version;
    byte ip[16];
    int udp_port;
    int tcp_port;
};
struct peer {
    byte *fingerprint;
    RSA *rsa_public_key;
    struct address addr;
    // current sequence number in communication with this peer
    uint32_t sequence_no;
    byte hmac_key[HMAC_KEY_LENGTH];
    time_t last_recv;
    //TODO: known file list for this node
    struct peer *next;
    struct peer *prev;
};
enum message_class { request, response };
enum message_type { key_exchange, find_peer, find_file, store_ref, command,
    resend, NUM_MESSAGE_TYPES };
/*
union request {
    // find_peer requests information about a specific fingerprint
    struct { byte *fingerprint; } find_peer;
    // find_file includes a file's hash, and returns a list of nearby peers,
    // or if the requestee knows owners of the file, a list of owners along
    // with more nearby peers if there is room left in list_max
    // if find_file is sent to an owner of the file, the owner responds
    // with a list of more owners/nearby peers just like above, but also
    // sets up a bulk file transfer to begin download of the file by the
    // requester
    struct { byte *file_hash; } find_file;
    // store_ref is used request to a peer nearby a given file hash to store
    // this node's fingerprint/file hash pair in their remote file table
    // optionally, the requestee can forward the fingerprint/hash pair to
    // any peers it knows even closer to the file
    struct {
        byte *file_hash;
        // file_owner field used when forwarding
        byte *file_owner;
    } store_ref;
    // command is used by a client, and is always encrypted
    struct {} command;
    // resend is request only
    // if a node receives a message from an unknown peer and the sequence_no of
    // the message is not 0, then it is likely that the node removed said peer
    // from its list
    // to add the peer back to its list, the node can request key_exchange
    struct { uint32_t sequence_no; } resend;
};
union response {
    // find_peer replies with a list of peers close to the requested, or the
    // address info of the peer
    union {
        // list of fingerprints
        byte **list;
        struct address addr;
    } find_peer;
}; */
struct message {
    byte hmac[DIGEST_LENGTH];
    // a given sequence_no uniquely identifies a message for that peer
    // note that since udp is unreliable, not all sequence numbers will be
    // received, and they won't necessarily be in order
    // the number can roll over, hence not truly unique
    uint32_t sequence_no;
    enum message_class class;
    enum message_type type;
    // recipient's fingerprint
    byte fingerprint[DIGEST_LENGTH];
    // message body
    byte *data;
    int length;
    struct peer *peer;
    struct address *addr;
    // for message queue
    struct message *next;
};
typedef struct files files;

/// extern function declarations
files *         file_create_list();
void            file_read_table(files *);
void            hash_digest(byte *, byte *, int);
void            hash_file_digest(byte *, char *);
void            hash_base64_encode(char *, byte *);
int             hash_cmp(byte *, byte *);
void            hash_distance(byte *, byte *, byte *);
void            hash_rsa_fingerprint(byte *, RSA *);
void *          message_recv_start(void *arg);
struct message *message_new(enum message_class, enum message_type,
                    struct peer *);
void            message_send(struct self *, struct message *, byte *, int);
struct peers *  peer_create_list();
void            peer_read_table(struct peers *);
struct peer *   peer_find(struct peers *, byte *);
struct peer *   peer_add(struct peers *, byte *);
void            peer_remove(struct peers *, struct peer *);
void            protocol_key_exchange_recv(struct self *, struct message *);
void            protocol_key_exchange_req(struct self *, byte *,
                    struct address *);
void            protocol_resend_req(struct self *, byte *, uint32_t);
int             util_read_file(byte **, char *);
void            util_write_file(char *, byte *, int);
char *          util_base64_encode(byte *, int);
int             util_base64_decode(byte **, char *);
void            util_get_address(struct address *, struct sockaddr *);
struct sockaddr *util_get_sockaddr(struct sockaddr_storage *, struct address *);
void            util_hmac_key(byte *);
void            util_hmac(byte *, byte *, int, byte *);
RSA *           util_read_rsa_pem(char *);
void            util_rsa_encrypt(byte *, RSA *, byte *, int);
int             util_rsa_decrypt(byte *, RSA *, byte *);
void            util_rsa_sign(byte *, byte *, int, RSA *);
bool            util_rsa_verify(byte *, byte *, int, RSA *);
void            util_rsa_pub_encode(byte *, RSA *);
RSA *           util_rsa_pub_decode(byte *);

#endif
