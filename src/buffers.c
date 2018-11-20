#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sodium.h>
#include <errno.h>
#include <unistd.h>
#include <lz4.h>
#include <sys/uio.h>
#include "tinyraft.h"
#include "wiretypes.h"
#include "buffers.h"
int traft_buff_alloc(traft_buff *b, size_t max_msg_size) {
  if (SODIUM_LIBRARY_VERSION_MAJOR < 10 || sodium_init() == -1) {
    return -1;
  }
  b->max_msg_size = max_msg_size;
  // Buffer size is worst-case compressed size + AEAD tag bytes
  b->buff_size = (size_t)LZ4_compressBound((int)max_msg_size) 
                        + RPC_REQ_LEN;
  b->buff = (uint8_t*) sodium_malloc(b->buff_size);
  if (b->buff == NULL) { return -1; }
  return 0;
}

void traft_buf_free(traft_buffers *b) {
  sodium_free(b->main_buffer);
  sodium_free(b->help_buffer);
}

// Writes all.  Returns 0 on success, -1 on failure.
static int write_all(int fd, uint8_t *buf, size_t count) {
  while (count) {
    ssize_t w = write(fd, buf, count);
    if (w == -1) {
      return -1;
    }
    count -= w;
    buf += w;
  }
  return 0;
}

// Reads all.  Returns 0 on success, -1 on failure.
static int read_all(int fd, uint8_t *buf, size_t count) {
  while (count) {
    ssize_t r = read(fd, buf, count);
    if (r == -1) {
      return -1;
    }
    count -= r;
    buf += r;
  }
  return 0;
}

int traft_buff_readheader(uint8_t *header, int readfd) {
  return read_all(readfd, header, RPC_REQ_LEN);
}

int traft_buff_readbuff(traft_buff *buff, int readfd, size_t len) {
  if (len > buff->max_msg_size) { return -1; }

  int err = read_all(readfd, buff->buff, len);
  if (err == -1) { return -1; }

  buff->msg_size = len;
}

int traft_buff_writemsg(append_entries_req *header, traft_buff *body, int writefd) {
  int err = write_all(writefd, header, RPC_REQ_LEN);
  if (err == -1) { return -1; }
  err = write_all(writefd, body->buff, body->msg_size);
  return err;
}

int traft_buff_writebuff(traft_buff *buff, int writefd) {
  return write_all(writefd, buff->buff, buff->msg_size);
}


typedef unsigned char nonceval[crypto_aead_chacha20poly1305_IETF_NPUBBYTES];

// Sets a nonce with all zeros except the first 4 bytes, which are the provided uint32.
static inline void nonce_for_i32(uint32_t i, nonceval *nonce) {
  memset(nonce, 0, crypto_aead_chacha20poly1305_IETF_NPUBBYTES);
  memcpy(nonce, &i, 4);
}

static inline void nonce_for_client(uint32_t client_idx, uint16_t client_id, nonceval *nonce) {
  memset(nonce, 0, crypto_aead_chacha20poly1305_IETF_NPUBBYTES);
  memcpy(nonce, &client_idx, 4);
  memcpy(nonce+4, &client_id, 2);
}

int traft_buff_encode_client(traft_buff *b, uint64_t term_id, int32_t client_idx, uint16_t client_short_id,
                    unsigned char *key, uint8_t *entry_data, int32_t entry_len) {
  // Set up header into buffer
  forward_entries_req *header = (forward_entries_req*) b->main_buffer;
  memset(header, 0, RPC_REQ_LEN);
  header->term_id = term_id;
  header->client_idx = client_idx;

  // Compress data behind header
  uint8_t *body_section = b->buff + RPC_REQ_LEN;
  int compressed_size = LZ4_compress_default((char*) entry_data, (char*) body_section, entry_len, b->buff_size);
  if (compressed_size == 0) {
      return -1;
  }
  printf("compressed.\n");
  b->msg_size = compressed_size + RPC_REQ_LEN;
  header->body_len = compressed_size;

  // Encrypt compressed data in place, storing auth tag in header.
  nonceval nonce;
  nonceForClient(client_idx, client_short_id, &nonce);

  if (crypto_aead_chacha20poly1305_ietf_encrypt_detached(
        body_section, header->auth_tag, NULL, body_section, header->body_len,
        (unsigned char*) header, forward_entries_AD_len, NULL, nonce, key) == -1) {
    return -1;
  }

  printf("encrypted\n");

  // remove this
  int buf_len = 64+90;
  int hex_len = (buf_len * 2) + 1;
  char hex_buf[350];
  sodium_bin2hex(hex_buf, 350, b->main_buffer, b->message_size);
  printf("message hex: %s\n", hex_buf);
  char buf_contents[64+90];
  // end remove

  // TODO remove
  char nonce_hex[33];
  sodium_bin2hex(nonce_hex, 25, nonce, 12);
  printf("Nonce i32 %d \n", header->client_idx);
  printf("Nonce hex: %s\n", nonce_hex);
  char AD_hex[33];
  sodium_bin2hex(AD_hex, 33, (unsigned char*)header, forward_entries_AD_len);
  printf("AD hex: %s\n", AD_hex);
  char message_hex[512];
  sodium_bin2hex(message_hex, 512, body_section, header->body_len);
  printf("message hex %s\n", message_hex);
  char key_hex[128];
  sodium_bin2hex(key_hex, 128, key, crypto_aead_chacha20poly1305_ietf_KEYBYTES);
  printf("key hex %s\n", key_hex);
  char mac_hex[64];
  sodium_bin2hex(mac_hex, 64, header->auth_tag, 12);
  printf("mac hex %s\n", mac_hex);
  printf("header->body_len %d\n", header->body_len);
  // end remove 

  return 0;
}

int traft_buff_transcode_leader(traft_buf *b, uint8_t *messageTermKey, uint8_t *leaderTermKey
                                forward_entries_req *client_header, append_entries_req *leader_header) {

  printf("TRANSCODE \n\n");

  nonceval client_nonce;
  nonceForI32(client_header->client_idx, &client_nonce);

  // TODO remove
  char nonce_hex[33];
  sodium_bin2hex(nonce_hex, 25, client_nonce, 12);
  printf("Nonce i32 %d \n", client_header->client_idx);
  printf("Nonce hex: %s\n", nonce_hex);
  char AD_hex[33];
  sodium_bin2hex(AD_hex, 33, (unsigned char*)client_header, forward_entries_AD_len);
  printf("AD hex: %s\n", AD_hex);
  char MAC_hex[33];
  sodium_bin2hex(MAC_hex, 33, (unsigned char*)client_header->auth_tag, 12);
  printf("MAC hex: %s\n", MAC_hex);
  char message_hex[512];
  sodium_bin2hex(message_hex, 512, b->main_buffer, client_header->body_len);
  printf("message hex %s\n", message_hex);
  char key_hex[128];
  sodium_bin2hex(key_hex, 128, key, crypto_aead_chacha20poly1305_ietf_KEYBYTES);
  printf("key hex %s\n", key_hex);
  char mac_hex[64];
  sodium_bin2hex(mac_hex, 64, client_header->auth_tag, 12);
  printf("mac hex %s\n", mac_hex);

  char header_hex[256];
  sodium_bin2hex(header_hex, 256, (unsigned char*)client_header, RPC_REQ_LEN);
  printf("header hex %s\n", header_hex);
  char hex_buf[350];
  sodium_bin2hex(hex_buf, 350, b->main_buffer, b->message_size);
  printf("t: message hex: %s\n", hex_buf);
  printf("header->body_len %d\n", client_header->body_len);
  // end remove 

  // Authenticate and decrypt in place using client nonce
  if (crypto_aead_chacha20poly1305_ietf_decrypt_detached(
      b->buff, NULL, b->main_buffer, client_header->body_len, 
      client_header->auth_tag, (unsigned char*)client_header, forward_entries_AD_len,
      client_nonce, key) == -1) {
    printf("decrypt error: %s \n", strerror(errno));
    return -1;
  }
 
  printf("decrypted\n");
  // Make append_entries_req header for persisted log entry
  append_entries_req leader_header;
  leader_header.this_term = this_entry.term_id;
  leader_header.this_idx = this_entry.idx;
  leader_header.prev_term = prev_entry.term_id;
  leader_header.prev_idx = prev_entry.idx;
  leader_header.quorum_term = quorum_entry.term_id;
  leader_header.quorum_idx = quorum_entry.idx;

  // Use index into current term as nonce
  nonceval leader_nonce;
  nonceForI32(leader_header.this_idx, &leader_nonce);
  leader_header.body_len = client_header->body_len;
  b->message_size += RPC_REQ_LEN;

  uint8_t *body_section = b->main_buffer + RPC_REQ_LEN;
  if (crypto_aead_chacha20poly1305_ietf_encrypt_detached(
			body_section, leader_header.auth_tag, NULL, b->help_buffer, leader_header.body_len, 
			(unsigned char*) &leader_header, append_entries_AD_len, NULL, leader_nonce, key) == -1) {
		return -1;
	}
  memset(b->main_buffer, 0, RPC_REQ_LEN);
  memcpy(b->main_buffer, &leader_header, RPC_REQ_LEN);
  printf("recrypted\n");
  sodium_bin2hex(message_hex, 512, b->main_buffer, b->message_size);
  printf("buffer contents after recrypt: %s\n", message_hex);
  sodium_bin2hex(message_hex, 512, leader_header.auth_tag, 16);
  printf("encrypted with MAC %s\n", message_hex);
  sodium_bin2hex(message_hex, 512, leader_nonce, 12);
  return 0;
}

int traft_buf_decode(traft_buffers *b, append_entries_req *header, int read_fd, unsigned char *key) {
  // Read header to provided pointer, encrypted body to main buffer
  if (read_all(read_fd, (uint8_t*)header, RPC_REQ_LEN) == -1) {
    printf("Error reading header\n");
    return -1;
  }
  char hex[512];
  sodium_bin2hex(hex, 512, (uint8_t*)header, RPC_REQ_LEN);
  printf("decrypting header %s\n", hex);
  if (read_all(read_fd, b->main_buffer, header->body_len) == -1) {
    printf("Error reading body\n");
    return -1;
  }
  sodium_bin2hex(hex, 512, b->main_buffer, header->body_len);
  printf("decrypting body %s\n", hex);
  sodium_bin2hex(hex, 512, header->auth_tag, 12);
  printf("MAC hex: %s\n", hex);
  // Decrypt to help buffer
  nonceval nonce;
  nonceForI32(header->this_idx, &nonce);
  if (crypto_aead_chacha20poly1305_ietf_decrypt_detached(
      b->help_buffer, NULL, b->main_buffer, header->body_len,
      header->auth_tag, (unsigned char*)header, append_entries_AD_len,
      nonce, key) == -1) {
    printf("decrypt error\n");
    return -1;
  }
  // Decompress back to main buffer
  if (LZ4_decompress_safe((char*) b->help_buffer, (char*) b->main_buffer, header->body_len, b->buff_size) < 0) {
    printf("decompress error\n");
    return -1;
  }
  return 0;
}


int traft_buf_view_iovecs(traft_buffers *b, struct iovec *args, int32_t max_args) {
  int32_t *header_section = (int32_t*) b->main_buffer;
  int32_t data_num_args = header_section[0];
  int32_t num_args_to_return = data_num_args;
  if (num_args_to_return > max_args) { num_args_to_return = max_args; }

  char *arg_data = (char*)&header_section[data_num_args + 1];
  for (int i = 0 ; i < max_args ; i++) {
    args[i].iov_base = arg_data;
    args[i].iov_len = (size_t)header_section[i+1];
    arg_data += header_section[i+1];
  }
  return 0;
}
