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
  
TEST (BuffersTest, Encoding) { 
  ASSERT_EQ(64, sizeof(traft_appendentry_req));
  ASSERT_EQ(64, sizeof(traft_newentry_req));
  traft_buff b;

  uint32_t data_len = 128;
  uint8_t *data = (uint8_t*) malloc(data_len);
  randombytes_buf(data, data_len);

  // Encode through a pipe, transcode to a file, decode from file
  int pipes[2];
  ASSERT_EQ(0, pipe(pipes));
  
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
  ASSERT_EQ(0, traft_buff_alloc(&b, data_len));
  ASSERT_EQ(0, traft_buff_encode_client(&b, term_id, client_idx, client_short_id, key, data, data_len));
  
  // exercise write/read of a traft_newentry_req
  ASSERT_EQ(0, traft_buff_writemsg(&b, pipes[0]));
  ASSERT_EQ(0, traft_buff_readreq(&b, pipes[1]));

  // leader transcode out to clients
  ASSERT_EQ(0, traft_buff_transcode_leader(&b, key,key, this_entry, prev_entry, quorum_entry));

  // write/read of appendentry_req
  ASSERT_EQ(0, traft_buff_writemsg(&b, pipes[0]));
  ASSERT_EQ(0, traft_buff_readreq(&b, pipes[1]));


  // decode
  traft_buff out_buff;
  ASSERT_EQ(0, traft_buff_alloc(&out_buff, data_len));
  ASSERT_EQ(0, traft_buff_decode(&b, &out_buff, key));

  // assert contents
  ASSERT_EQ(0, memcmp(out_buff.buff + RPC_REQ_LEN, data, data_len));

  traft_buff_free(&b);
  traft_buff_free(&out_buff);

}
  
