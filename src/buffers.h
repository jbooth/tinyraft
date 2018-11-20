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


/** Reads exactly RPC_REQ_LEN (64) bytes into header */
int traft_buff_readheader(uint8_t *header, int readfd);

/** Reads message body of provided length into our buffer */
int traft_buff_readbuff(traft_buff *buff, int readfd, size_t len);

/** Writes RPC_REQ_LEN (64) bytes from header and then the provided buffer */
int traft_buff_writemsg(append_entries_req *header, traft_buff *body, int writefd);

/** Writes buffer contents to specified fd.  */
int traft_buff_writebuff(traft_buff *buff, int writefd);


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
 *  Leader function.  Transcodes a partially received message into the final log format that will be replicated.
 *  We want to replace the forward_entries_req header with an append_entries_req containing a definitive index for this entry in this term.
 *  We also take the opportunity to reencrypt using that entry index as the nonce, since we have to recrypt anyways
 *  in order to sign our new header and attest to its accuracy.
 *
 *  After this method completes, the newly recoded message body will be in our
 *  buffer, matching the body_len and auth tag set in leader_header.
 */
int traft_buff_transcode_leader(traft_buf *b, uint8_t *messageTermKey, uint8_t *leaderTermKey
                                forward_entries_req *client_header, append_entries_req *leader_header);

/** Verifies message integrity via auth tag in header */
int traft_buff_verify_follower(traft_buf *b, append_entries_req *header);
/**
 *  State machine function.  Decrypts, authenticates and decompresses an encoded message.
 *  After completion, we'll have stored the header in *header, and the body in b->main_buff.
 *  Note that we take no argument for file position.  It's the caller's responsibility to call lseek() before calling this function.
 */
int traft_buff_decode(traft_buff *b_main, traft_buff *b_help, append_entries_req *header, int read_fd, unsigned char *key);

/** Decodes term config from a message body */
int traft_deser_termconfig(const uint8_t *buff, traft_term_config *cfg);

typedef uint8_t traft_termkey[32]; // crypto_secretbox_xchacha20poly1305_KEYBYTES
typedef traft_termconfig struct {
  traft_cluster_config  cluster_cfg;  // 4680
  traft_termkey         termkey;      // +32 = 4712
  traft_pub_key         leader_id;    // +32 = 4744
  uint64_t              term_id;      // +8  = 4752
} traft_termconfig;


#ifdef __cplusplus
}
#endif

