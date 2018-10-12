#pragma once

#include <stdint.h>
#include <stddef.h>
#include <sodium.h>
#include <lz4.h>
#include <sys/uio.h>
#include "wiretypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Buffer utility struct to manage compression/encryption of messages in flight.
 */
typedef struct buffers {
  uint8_t *main_buffer;
  uint8_t *help_buffer;
  size_t  max_msg_size;
  size_t  buff_size;
  size_t  message_size;
} buffers;

/** Allocates buffers using sodium_malloc */
int init_buffs(buffers *b, size_t max_msg_size);

/** Frees associated buffers */
void free_buffs(buffers *b);


/**
 *  Encodes and sends a ForwardEntriesReq from any of the peers to the leader, with args_in as the body.
 *  Compresses, then uses the provided key to encrypt, with the provided client_idx as a nonce.  
 *  The message header will be authenticated as well as the encrypted data.
 *  We then write the header, encrypted body and authentication token to the provided send_fd.
 *  Note that we try to append to the provided term ID.  
 *    -- If the leader has moved on to a new term, this request will be rejected and we'll need to retry.
 *  Also note that we don't read any response in this function, it's a fire-and-forget.
 */
int encode_and_send(buffers *b, int send_fd, uint64_t term_id, int32_t client_idx, 
                    unsigned char *key, struct iovec *args_in, int32_t num_args);

/**
 *  Leader function.  Transcodes a partially received message into the final log format that will be replicated.
 *  Our polling loop reads headers but leaves message bytes on the wire.  We want to replace the forward_entries_req header
 *  with an append_entries_req containing a definitive index for this entry in this term.
 *  We also take the opportunity to reencrypt using that entry index as the nonce, since we have to recrypt anyways
 *  in order to sign our new header and attest to its accuracy.
 *  Doesn't write to log storage.  See storage.c for that.
 */
int transcode(buffers *b, int recv_fd, unsigned char *key, forward_entries_req *client_header, append_entries_req *leader_header);

/**
 *  State machine function.  Used to read and decode an entry from the persisted log.  
 *  We store the header in *header, and the body in b->main_buffer.
 *  After this function completes, view_iovecs will work.
 *  Note that we take no argument for file position.  It's the caller's responsibility to call lseek() before calling this function.
 */
int decode(buffers *b, append_entries_req *header, int read_fd, unsigned char *key);

/** 
 * Copies a view of our decoded iovecs to the provided args
 */
int view_iovecs(buffers *b, struct iovec *args, int32_t max_args);

/**
 * Used by encode_and_send to do initial import prior to encoding.  Exposed for testing.
 */
int import_iovecs(buffers *b, struct iovec *args, int32_t num_args);


#ifdef __cplusplus
}
#endif

