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
                    unsigned char *key, uint8_t *entry_data, int32_t entry_len);

/**
 *  Leader function.  Transcodes a received newentry request into the appendentry request that we'll persist.
 *  Expects newentry header and body in buffer, transcodes to appendentry header and body.
 */
int traft_buff_transcode_leader(traft_buff *b, uint8_t *message_termkey, uint8_t *leader_termkey,
                                traft_entry_id this_entry, traft_entry_id prev_entry, traft_entry_id quorum_entry);

/** Verifies and appendentries request via auth tag in header */
int traft_buff_verify_follower(traft_buff *b);

/** Returns this buffer's first 64 bytes as an appendentries_req */
traft_appendentry_req traft_buff_get_ae_header(traft_buff *b);

/**
 *  State machine function.  Decrypts, authenticates and decompresses an encoded message.
 *  After completion, we'll have stored the decoded and decompressed body in out_buff.
 */
int traft_buff_decode(traft_buff *b, traft_buff *out_buff,  unsigned char *termkey);


typedef uint8_t traft_termkey[32]; // crypto_secretbox_xchacha20poly1305_KEYBYTES
typedef struct traft_termconfig {
  traft_cluster_config  cluster_cfg;  // 4680
  traft_termkey         termkey;      // +32 = 4712
  traft_pub_key         leader_id;    // +32 = 4744
  uint64_t              term_id;      // +8  = 4752
} traft_termconfig;

/** Decodes term config from a message body */
int traft_deser_termconfig(const uint8_t *buff, traft_termconfig *cfg);

#ifdef __cplusplus
}
#endif

