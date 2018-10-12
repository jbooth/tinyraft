#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sodium.h>
#include <errno.h>
#include <unistd.h>
#include <lz4.h>
#include <sys/uio.h>
#include "wiretypes.h"
#include "buffers.h"

int init_buffs(buffers *b, size_t max_msg_size) {
  if (SODIUM_LIBRARY_VERSION_MAJOR < 10 || sodium_init() == -1) {
    return -1;
  }
  b->max_msg_size = max_msg_size;
  // Buffer size is worst-case compressed size + AEAD tag bytes
  b->buff_size = (size_t)LZ4_compressBound((int)max_msg_size) 
                        + RPC_REQ_LEN;
  b->main_buffer = (uint8_t*) sodium_malloc(b->buff_size);
  if (b->main_buffer == NULL) { return -1; }
  b->help_buffer = (uint8_t*) sodium_malloc(b->buff_size);
  if (b->help_buffer == NULL) {
    sodium_free(b->main_buffer);
    return -1;
  }
  return 0;
}

void free_buffs(buffers *b) {
  sodium_free(b->main_buffer);
  sodium_free(b->help_buffer);
}

typedef unsigned char nonceval[crypto_aead_chacha20poly1305_IETF_NPUBBYTES];

// Sets a nonce with all zeros except the first 4 bytes, which are the provided uint32.
static inline void nonceForI32(uint32_t i, nonceval *nonce) {
  memset(nonce, 0, crypto_aead_chacha20poly1305_IETF_NPUBBYTES);
  memcpy(nonce, &i, 4);
}

// Writes all.  Returns 0 on success, -1 on failure.
static inline int write_all(int fd, uint8_t *buf, size_t count) {
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
static inline int read_all(int fd, uint8_t *buf, size_t count) {
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

int encode_and_send(buffers *b, int send_fd, uint64_t term_id, int32_t client_idx, 
                    unsigned char *key, struct iovec *args, int32_t num_args) {
  // Import args into main buffer
  if (import_iovecs(b, args, num_args) == -1) {
    return -1;
  }
  // Compress into help buffer
  int compressed_size = LZ4_compress_default((char*) b->main_buffer, (char*) b->help_buffer, b->message_size, b->buff_size);
  if (compressed_size == 0) {
      return -1;
  }
  b->message_size = compressed_size + crypto_aead_chacha20poly1305_ABYTES;

  // Set up header into main buffer
  forward_entries_req *header = (forward_entries_req*) b->main_buffer;
  header->term_id = term_id;
  header->client_idx = client_idx;
  header->body_len = b->message_size;
  b->message_size += sizeof(forward_entries_req);

  // Encrypt compressed data behind it, storing auth tag in header.
  uint8_t *body_section = b->main_buffer + sizeof(forward_entries_req);
  nonceval nonce;
  nonceForI32(client_idx, &nonce);
  if (crypto_aead_chacha20poly1305_ietf_encrypt_detached(
			body_section, header->auth_tag, NULL, b->help_buffer, compressed_size, 
			(unsigned char*) &header, forward_entries_AD_len, NULL, nonce, key) == -1) {
		return -1;
	}
  return write_all(send_fd, b->main_buffer, b->message_size);
}

int transcode(buffers *b, int recv_fd, unsigned char *key, forward_entries_req *client_header, append_entries_req *leader_header) {
  if (client_header->body_len > b->max_msg_size) {
    // TODO set errno
    return -1;
  }
  // Read into main buffer
  if (read_all(recv_fd, b->main_buffer, client_header->body_len) == -1) { 
    return -1;
  }
  nonceval client_nonce;
  nonceForI32(client_header->client_idx, &client_nonce);

  // Authenticate and decrypt into help buffer using client nonce
  if (crypto_aead_chacha20poly1305_ietf_decrypt_detached(
      b->help_buffer, NULL, b->main_buffer, client_header->body_len, 
      client_header->auth_tag, (unsigned char*)&client_header, forward_entries_AD_len,
      client_nonce, key) == -1) {
    return -1;
  }
  // Recrypt into main buffer with header, using log entry_idx as nonce
  nonceval leader_nonce;
  nonceForI32(leader_header->this_idx, &leader_nonce);
  memcpy(b->main_buffer, leader_header, RPC_REQ_LEN);
  uint8_t *body_section = b->main_buffer + RPC_REQ_LEN;
  if (crypto_aead_chacha20poly1305_ietf_encrypt_detached(
			body_section, leader_header->auth_tag, NULL, b->help_buffer, leader_header->body_len, 
			(unsigned char*) &leader_header, append_entries_AD_len, NULL, leader_nonce, key) == -1) {
		return -1;
	}
  return 0;
}

int decode(buffers *b, append_entries_req *header, int read_fd, unsigned char *key) {
  // Read header to provided pointer, encrypted body to main buffer
  if (read_all(read_fd, (uint8_t*)header, RPC_REQ_LEN) == -1) {
    return -1;
  }
  if (read_all(read_fd, b->main_buffer, header->body_len) == -1) {
    return -1;
  }
  // Decrypt to help buffer
  nonceval nonce;
  nonceForI32(header->this_idx, &nonce);
  if (crypto_aead_chacha20poly1305_ietf_decrypt_detached(
      b->help_buffer, NULL, b->main_buffer, header->body_len,
      header->auth_tag, (unsigned char*)header, append_entries_AD_len,
      nonce, key) == -1) {
    return -1;
  }
  // Decompress back to main buffer
  if (LZ4_decompress_safe((char*) b->help_buffer, (char*) b->main_buffer, header->body_len, b->buff_size) < 0) {
    return -1;
  }
  return 0;
}


int import_iovecs(buffers *b, struct iovec *args, int32_t num_args) {
  size_t total_size = 4;
  for (int i = 0 ; i < num_args ; i++) {
    if (args[i].iov_len > UINT32_MAX) {
      // Should never even be close to this.
      return -1;
    }
    total_size += args[i].iov_len;
    total_size += 4;
  }
  if (total_size > b->max_msg_size) { return -1; }
  b->message_size = total_size;
  // Write number of args and length for each.
  int32_t *header_section = (int32_t*) b->main_buffer;
  header_section[0] = num_args;
  for (int i = 0 ; i < num_args ; i++) {
    header_section[i+1] = (int32_t) args[i].iov_len;
  }
  // Write data for each arg.
  uint8_t *data_section = ((uint8_t*) &header_section[num_args + 1]);
  for (int i = 0 ; i < num_args ; i++) {
    memcpy(data_section, args[i].iov_base, args[i].iov_len);
    data_section += args[i].iov_len;
  }
  return 0;
}

int view_iovecs(buffers *b, struct iovec *args, int32_t max_args) {
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
