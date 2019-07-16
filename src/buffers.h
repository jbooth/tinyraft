#pragma once

#include <stdint.h>
#include <stddef.h>
#include <sodium.h>
#include <lz4.h>
#include <sys/uio.h>
#include "tinyraft.h"
#include "wiretypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Buffer utility struct to manage compression/encryption of messages in flight.
 * Used in conjunction with either append_entries_req or forward_entries_req.
 */
typedef struct traft_buff {
  uint8_t *buff;
  size_t  msg_size;     // current message size
  size_t  max_msg_size; // max message size that this buffer can hold
  size_t  buff_size;    // size that was allocated (max_msg_size + overhead)
} traft_buff;

/** Allocates traft_buffers with proper overhead sizing */
int traft_buff_alloc(traft_buff *b, size_t max_msg_size);

/** Frees associated traft_buffers */
void traft_buff_free(traft_buff *b);


/** Reads header of RPC_REQ_LEN and, for requests with bodies, body of variable length into our buffer */
int traft_buff_readreq(traft_buff *buff, int readfd);

/** Writes buffer contents to specified fd.  */
int traft_buff_writemsg(traft_buff *buff, int writefd);

/** Writes a request with no body to the specified fd */
int traft_buff_writereq(traft_req *req, int writefd);

/** 
 * Generates a session key, stores it in session_key, and writes a hello request 
 * with encrypted session key to the provided FD.
 */
int traft_buff_writehello(const traft_publickey_t server_id, const traft_publickey_t client_id, const traft_secretkey_t client_sk, const uuid_t cluster_uuid, traft_symmetrickey_t session_key, int writefd);

/** Reads a hello request off of the provided FD, leaivng session key encrypted */
int traft_buff_readhello(traft_hello *hello, int readfd);

/** Decrypts a hello request using the provided raftlet's secret key */
int traft_buff_decrypt_sessionkey(traft_hello *hello, traft_secretkey_t raftlet_sk, traft_symmetrickey_t session_key);

/**
 *  Encodes a ForwardEntriesReq with provided body data into the supplied buffer.
 *
 *  Compresses, then uses the provided key to encrypt, with the provided client_idx/client_short_id combo as a nonce.
 *  The message header will be authenticated as well as the encrypted data.
 *  We then write the header, encrypted body and authentication token to the provided send_fd.
 *  Note that we try to append to the provided term ID.
 *    -- If the leader has moved on to a new term, this request will be rejected and we'll need to retry.
 *  Also note that we don't read any response in this function, it's a fire-and-forget.
 */
int traft_buff_encode_client(traft_buff *b, uint64_t term_id, int32_t client_idx, uint16_t client_short_id,
                    traft_symmetrickey_t session_key, uint8_t *entry_data, int32_t entry_len);

/**
 *  Leader function.  Transcodes a received newentry request into the appendentry request that we'll persist.
 *  Expects newentry header and body in buffer, transcodes to appendentry header and body.
 */
int traft_buff_transcode_leader(traft_buff *b, traft_symmetrickey_t message_sessionkey, traft_symmetrickey_t leader_termkey,
                                traft_entry_id this_entry, traft_entry_id prev_entry, traft_entry_id quorum_entry);

/** Verifies an appendentries request via auth tag in header */
int traft_buff_verify_follower(traft_buff *b);

/** Returns this buffer's first 64 bytes as a generic request */
traft_req traft_buff_get_header(traft_buff *b);

/**
 *  State machine function.  Decrypts, authenticates and decompresses an encoded message.
 *  After completion, we'll have stored the decoded and decompressed body in out_buff.
 */
int traft_buff_decode(traft_buff *b, traft_buff *out_buff, const uint8_t *termkey);

int traft_write_resp(traft_resp *resp, int fd);

int traft_read_resp(traft_resp *resp, int fd);


/** termconfig struct attached to termlog struct, used to encode/decode entries.  */
typedef struct traft_termconfig {
  traft_cluster_config  cluster_cfg;  // 4688
  traft_symmetrickey_t  termkey;      // +32 = 4720
  traft_publickey_t     leader_id;    // +32 = 4752
  uint64_t              term_id;      // +8  = 4760
} traft_termconfig;

/** Generates a termconfig and stores it in the provided buffer. */
int traft_gen_termconfig(traft_buff *buff, traft_cluster_config *membership, uint64_t term_id, traft_entry_id prev_idx, traft_entry_id quorum_idx, const traft_publickey_t leader_id, const traft_secretkey_t leader_sk);

/** Decodes term config from an appendentries request, decrypting our termkey for this node. */
int traft_deser_termconfig(traft_buff *b, traft_termconfig *cfg, const traft_publickey_t my_id, const traft_secretkey_t my_sk);


#ifdef __cplusplus
}
#endif

