/*
   Copyright 2018 Google LLC

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   Author Jay Booth
*/

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/uio.h>
#include <string.h>
#include "gtest/gtest.h"
#include "wiretypes.h"
#include "buffers.h"
#include "buffers.c"

TEST (BuffersTest, EntriesRequests) {
  ASSERT_EQ(RPC_REQ_LEN, sizeof(traft_appendentry_req));
  ASSERT_EQ(RPC_REQ_LEN, sizeof(traft_newentry_req));
  traft_buff b;

  uint32_t data_len = 128;
  uint8_t *data = (uint8_t*) malloc(data_len);
  randombytes_buf(data, data_len);
  memcpy(data, "hello", 6);

  // Encode through a pipe, transcode to a file, decode from file
  int pipes[2];
  EXPECT_EQ(0, pipe(pipes));

  unsigned char key[crypto_aead_chacha20poly1305_ietf_KEYBYTES];
  randombytes_buf(key, crypto_aead_chacha20poly1305_ietf_KEYBYTES);

  int term_id = 1;
  int client_idx = 2;
  int entry_idx = 3;
  uint16_t client_short_id;
  traft_entry_id this_entry, prev_entry, quorum_entry;
  this_entry.term_id = term_id;
  this_entry.idx = entry_idx;
  prev_entry.term_id = term_id;
  prev_entry.idx = entry_idx - 1;
  quorum_entry.term_id = term_id;
  quorum_entry.idx = entry_idx - 1;

  // init buffers and send fwd_entries_req
  EXPECT_EQ(0, traft_buff_alloc(&b, data_len + RPC_REQ_LEN));
  printf("encoding...\n");
  int encode_res;
  encode_res = traft_buff_encode_client(&b, term_id, client_idx, client_short_id, key, data, data_len);
  printf("encoded\n");
  EXPECT_EQ(0, encode_res);


  // exercise write/read of a traft_newentry_req
  printf("msg size %d \n", b.msg_size);
  EXPECT_EQ(0, traft_buff_writemsg(&b, pipes[1]));
  EXPECT_EQ(0, traft_buff_readreq(&b, pipes[0]));

  // leader transcode out to clients
  EXPECT_EQ(0, traft_buff_transcode_leader(&b, key,key, this_entry, prev_entry, quorum_entry));

  // write/read of appendentry_req
  EXPECT_EQ(0, traft_buff_writemsg(&b, pipes[1]));
  EXPECT_EQ(0, traft_buff_readreq(&b, pipes[0]));


  // decode
  traft_buff out_buff;
  EXPECT_EQ(0, traft_buff_alloc(&out_buff, data_len));
  EXPECT_EQ(0, traft_buff_decode(&b, &out_buff, key));
  printf("decoded\n");

  // assert contents
  char hex[512];
  sodium_bin2hex(hex, 512, data, data_len);
  printf("data %s\n", hex);
  sodium_bin2hex(hex, 512, out_buff.buff, data_len);
  printf("decoded %s\n", hex);

  EXPECT_EQ(0, memcmp(out_buff.buff, data, data_len));

  traft_buff_free(&b);
  traft_buff_free(&out_buff);

}

TEST (BuffersTest, Hello) {
  ASSERT_EQ(RPC_HELLO_LEN, sizeof(traft_hello));
  EXPECT_EQ(64, sizeof(traft_newentry_req));
  traft_buff b;


  int pipes[2];
  EXPECT_EQ(0, pipe(pipes));

  int server_read_fd = pipes[0];
  int client_write_fd = pipes[1];

  traft_symmetrickey_t key;

}

TEST (BuffersTest, TermConfig) {
  const char *host1 = "maggie";
  const char *host2 = "leo";
  const char *host3 = "eanan";


  uint8_t host1pk[32], host1sk[32], host2pk[32], host2sk[32], host3pk[32], host3sk[32];
  crypto_box_keypair(host1pk, host1sk);
  crypto_box_keypair(host2pk, host2sk);
  crypto_box_keypair(host3pk, host3sk);

  traft_cluster_config cfg;
  memcpy(cfg.hostnames[0], host1, strlen(host1) + 1);
  memcpy(cfg.peer_ids[0], host1pk, 32);
  memcpy(cfg.hostnames[1], host1, strlen(host2) + 1);
  memcpy(cfg.peer_ids[1], host2pk, 32);
  memcpy(cfg.hostnames[2], host1, strlen(host3) + 1);
  memcpy(cfg.peer_ids[2], host3pk, 32);
  cfg.num_peers = 3;

  traft_buff buff;
  EXPECT_EQ(0, traft_buff_alloc(&buff, TRAFT_CLUSTER_CONFIG_SIZE * 2));
  uint64_t term_id = 5;
  traft_entry_id prev_idx;
  prev_idx.term_id = 4;
  prev_idx.idx = 10;
  traft_entry_id quorum_idx;
  quorum_idx.term_id = 4;
  quorum_idx.idx = 9;

  EXPECT_EQ(0, traft_gen_termconfig(&buff, &cfg, term_id, prev_idx, quorum_idx, host1pk, host1sk));

  traft_termconfig cfg1, cfg2, cfg3;

  EXPECT_EQ(0, traft_deser_termconfig(&buff, &cfg1, host1pk, host1sk));
  EXPECT_EQ(0, traft_deser_termconfig(&buff, &cfg2, host2pk, host2sk));
  EXPECT_EQ(0, traft_deser_termconfig(&buff, &cfg3, host3pk, host3sk));

  EXPECT_EQ(0, memcmp(cfg1.termkey, cfg2.termkey, 32));
  EXPECT_EQ(3, cfg1.cluster_cfg.num_peers);
  EXPECT_EQ(0, memcmp(cfg2.cluster_cfg.hostnames[0], host1, strlen(host1)));
}
