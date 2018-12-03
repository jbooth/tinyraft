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

void traft_buff_free(traft_buff *b) {
  sodium_free(b->buff);
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

int traft_buff_readreq(traft_buff *buff, int readfd) {
  int err = read_all(readfd, buff->buff, RPC_REQ_LEN);
  if (err != 0) { return err; }
  traft_req *header = (traft_req*) buff->buff;
  uint8_t *body_section = buff->buff + RPC_REQ_LEN;
  uint32_t body_len = header->info.body_len;
  err = read_all(readfd, body_section, (size_t)body_len);
  if (err != 0) { return err; }
  buff->msg_size = RPC_REQ_LEN + body_len;
  return 0;
}

int traft_buff_writemsg(traft_buff *buff, int writefd) {
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

static inline void nonce_for_i64(uint64_t term_id, nonceval *nonce) {
  memset(nonce, 0, crypto_aead_chacha20poly1305_IETF_NPUBBYTES);
  memcpy(nonce, &i, 8);
}

int traft_buff_encode_client(traft_buff *b, uint64_t term_id, int32_t client_idx, uint16_t client_short_id,
                    unsigned char *key, uint8_t *entry_data, int32_t entry_len) {
  // Set up header into buffer
  traft_newentry_req *header = (traft_newentry_req*) b->buff;
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
  nonce_for_client(client_idx, client_short_id, &nonce);

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
  sodium_bin2hex(hex_buf, 350, b->buff, b->msg_size);
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

int traft_buff_transcode_leader(traft_buff *b, uint8_t *message_termkey, uint8_t *leader_termkey,
                                traft_entry_id this_entry, traft_entry_id prev_entry, traft_entry_id quorum_entry) {

  printf("TRANSCODE \n\n");
  // Get view of client header, calculate nonce
	traft_newentry_req *client_header = (traft_newentry_req*) b->buff;
  uint8_t *body_section = b->buff + RPC_REQ_LEN;
  nonceval client_nonce;
  nonce_for_client(client_header->client_idx, client_header->client_id, &client_nonce);


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
  sodium_bin2hex(message_hex, 512, body_section, client_header->body_len);
  printf("message hex %s\n", message_hex);
  char key_hex[128];
  sodium_bin2hex(key_hex, 128, message_termkey, crypto_aead_chacha20poly1305_ietf_KEYBYTES);
  printf("key hex %s\n", key_hex);
  char mac_hex[64];
  sodium_bin2hex(mac_hex, 64, client_header->auth_tag, 12);
  printf("mac hex %s\n", mac_hex);

  char header_hex[256];
  sodium_bin2hex(header_hex, 256, (unsigned char*)client_header, RPC_REQ_LEN);
  printf("header hex %s\n", header_hex);
  char hex_buf[350];
  sodium_bin2hex(hex_buf, 350, body_section, b->msg_size);
  printf("t: message hex: %s\n", hex_buf);
  printf("header->body_len %d\n", client_header->body_len);
  // end remove 

  // Authenticate and decrypt in place using client nonce
  if (crypto_aead_chacha20poly1305_ietf_decrypt_detached(
      body_section, NULL, body_section, client_header->body_len, 
      client_header->auth_tag, (unsigned char*)client_header, forward_entries_AD_len,
      client_nonce, message_termkey) == -1) {
    printf("decrypt error: %s \n", strerror(errno));
    return -1;
  }
 
  printf("decrypted\n");

  // Make append_entries_req header for persisted log entry
  traft_appendentry_req *leader_header = (traft_appendentry_req*) b->buff;
  leader_header->this_term = this_entry.term_id;
  leader_header->this_idx = this_entry.idx;
  leader_header->prev_term = prev_entry.term_id;
  leader_header->prev_idx = prev_entry.idx;
  leader_header->quorum_term = quorum_entry.term_id;
  leader_header->quorum_idx = quorum_entry.idx;
  // Use index into current term as nonce
  nonceval leader_nonce;
  nonce_for_i32(leader_header->this_idx, &leader_nonce);
  leader_header->body_len = client_header->body_len;

  if (crypto_aead_chacha20poly1305_ietf_encrypt_detached(
			body_section, leader_header->auth_tag, NULL, body_section, leader_header->body_len, 
			(unsigned char*) leader_header, append_entries_AD_len, NULL, leader_nonce, leader_termkey) == -1) {
		return -1;
	}
  b->msg_size = RPC_REQ_LEN + leader_header->body_len;

  // TODO remove
  printf("recrypted\n");
  sodium_bin2hex(message_hex, 512, b->buff, b->msg_size);
  printf("buffer contents after recrypt: %s\n", message_hex);
  sodium_bin2hex(message_hex, 512, leader_header->auth_tag, 16);
  printf("encrypted with MAC %s\n", message_hex);
  sodium_bin2hex(message_hex, 512, leader_nonce, 12);
  // end remove
  return 0;
}

traft_appendentry_req traft_buff_get_ae_header(traft_buff *b) {
  traft_appendentry_req *view = (traft_appendentry_req*) b;
  return *view;
}


int traft_buff_decode(traft_buff *b, traft_buff *out_buff, unsigned char *termkey) {
  traft_appendentry_req *header = (traft_appendentry_req*) b->buff;
  uint8_t *body_section = b->buff + RPC_REQ_LEN;
  
  char hex[512];
  sodium_bin2hex(hex, 512, (uint8_t*)header, RPC_REQ_LEN);
  printf("header %s\n", hex);
  sodium_bin2hex(hex, 512, body_section, header->body_len);
  printf("body %s\n", hex);
  sodium_bin2hex(hex, 512, header->auth_tag, 12);
  printf("MAC hex: %s\n", hex);
  // Decrypt in place
  nonceval nonce;
  nonce_for_i32(header->this_idx, &nonce);
  if (crypto_aead_chacha20poly1305_ietf_decrypt_detached(
      body_section, NULL, body_section, header->body_len,
      header->auth_tag, (unsigned char*)header, append_entries_AD_len,
      nonce, termkey) == -1) {
    printf("decrypt error\n");
    return -1;
  }
  // Decompress to output buffer
  int output_length = LZ4_decompress_safe((char*) body_section, (char*) out_buff->buff, header->body_len, b->buff_size);
  if (output_length < 0) {
    printf("decompress error\n");
    return -1;
  }
  out_buff->msg_size = output_length;
  return 0;
}

// Used to encode a termkey for each peer, their eyes only
typedef struct termkey_bin {
  traft_pub_key peer_id; // 32
  uint8_t       boxed_termkey[48]; // 80, 32 bytes key + 16 bytes MAC
} termkey_bin;

// serialized representation of termconfig as appendentry body
typedef struct termconfig_bin {
  traft_cluster_config  cluster_cfg;        // 4688
  termkey_bin   termkeys[TRAFT_MAX_PEERS];  // + (80 * 16) = 5968
  uint64_t      term_id;                    // 5976
  traft_pub_key leader_id;                  // 6008
} termconfig_bin;
#define TRAFT_TERMCONFIG_BIN_SIZE 6008

int traft_gen_termconfig(traft_buff *buff, traft_cluster_config *membership,
                          uint64_t term_id, traft_entry_id prev_idx,
                          traft_pub_key my_id, const uint8_t *leader_private_key) {
  // TODO setup ae header
  memset(header, 0, RPC_REQ_LEN);
  traft_appendentry_req *header = (traft_appendentry_req*) buff->buff;

  // view buff as termconfig_bin
  uint8_t *body_section = buff->buff + RPC_REQ_LEN;
  termconfig_bin *bin_cfg_view = (termconfig_bin*) body_section;

  // copy structs
  memcpy(&bin_cfg_view->cluster_cfg, membership, TRAFT_CLUSTER_CONFIG_SIZE);
  bin_cfg_view->term_id = term_id;
  bin_cfg_view->leader_id = my_id;
  // gen keys
  char termkey[32];
  crypto_secretbox_keygen(termkey);
  nonceval termnonce;
  nonce_for_i64(nonceval, term_id);
  for (int i = 0 ; i < orig_cfg->num_members ; i++) {
    bin_cfg_view->termkeys[i].peer_id = membership->peer_ids[i];
    if (crypto_box_easy(&bin_cfg_view->termkeys[i].boxed_termkey, termkey, 32, termnonce, membership->peer_ids[i], leader_private_key) != 0) {
      // encryption error
      return -1;
    }
  }
  return 0;
}

int traft_deser_termconfig(traft_buff *buff, traft_termconfig *cfg, traft_pub_key my_id, const uint8_t *my_secret_key) {
  // view buff as termconfig_bin
  traft_appendentry_req *header = (traft_appendentry_req*) buff->buff;
  uint8_t *body_section = buff->buff + RPC_REQ_LEN;
  termconfig_bin *bin_cfg_view = (termconfig_bin*) body_section;
  // copy structs
  memcpy(&cfg->cluster_cfg, &bin_cfg_view->cluster_cfg,  TRAFT_CLUSTER_CONFIG_SIZE);
  cfg->term_id = bin_cfg_view->term_id;
  cfg->leader_id = bin_cfg_view->leader_id;
  // find our key
  nonceval termnonce;
  nonce_for_i64(nonceval, bin_cfg_view->term_id);
  for (int i = 0 ; i < bin_cfg_view->cluster_cfg->num_peers ; i++) {
    if (memcmp(bin_cfg_view->termkeys[i].peer_id, &my_id, 32) == 0) {
      if (crypto_box_open_easy(&cfg->termkey, &bin_cfg_view->termkeys[i].boxed_termkey, 48,
                                termnonce, bin_cfg_view->leader_id, my_secret_key) != 0) {
        // decryption error
        return -1;
      }
      return 0;
    }
  }
  // my_id not found in membership
  return -1;
}
