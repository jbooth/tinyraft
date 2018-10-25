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

TEST (BuffersTest, ImportExport) { 
  traft_buffers b;

  int buflen = 16;
  int allBuffsLen = 1024 * 1024;
  uint8_t *bufs[4];
  for (int i = 0 ; i < 4 ; i++) {
    bufs[i] = (uint8_t*)malloc(buflen);
    randombytes_buf(bufs[i], buflen);
  }
  iovec vecs[4];
  for (int i = 0 ; i < 4 ; i++) {
    vecs[i].iov_base = &bufs[i][0];
    vecs[i].iov_len = buflen;
  }

  ASSERT_EQ(0, traft_buf_alloc(&b, allBuffsLen));

  ASSERT_EQ(0, import_iovecs(&b, &vecs[0], 4));

  ASSERT_EQ(20 + (4*16), b.message_size);

  ASSERT_EQ(0, traft_buf_view_iovecs(&b, &vecs[0], 4));

  for (int i = 0 ; i < 4 ; i++) {
    ASSERT_EQ(0, memcmp(bufs[i], vecs[i].iov_base, buflen));
  }

  for (int i = 0 ; i < 4 ; i++) {
    free(bufs[i]);
  }
  traft_buf_free(&b);
}
  
TEST (BuffersTest, Encoding) { 
  ASSERT_EQ(64, sizeof(forward_entries_req));
  ASSERT_EQ(64, sizeof(append_entries_req));
  traft_buffers b;

  int buflen = 16;
  int allBuffsLen = 1024 * 1024;
  uint8_t *bufs[4];
  for (int i = 0 ; i < 4 ; i++) {
    bufs[i] = (uint8_t*)malloc(buflen);
    randombytes_buf(bufs[i], buflen);
  }
  iovec vecs[4];
  for (int i = 0 ; i < 4 ; i++) {
    vecs[i].iov_base = &bufs[i][0];
    vecs[i].iov_len = buflen;
  }

  // Encode through a pipe, transcode to a file, decode from file
  int pipes[2];
  ASSERT_EQ(0, pipe(pipes));
  
  char tmpfilename[12] = "/tmp/XXXXXX";
  int tmp_fd = mkstemp(tmpfilename);
  ASSERT_GT(tmp_fd, 0);

  unsigned char key[crypto_aead_chacha20poly1305_ietf_KEYBYTES];
  randombytes_buf(key, crypto_aead_chacha20poly1305_ietf_KEYBYTES);

  int term_id = 1;
  int client_idx = 2;
  int entry_idx = 3;
  traft_entry_id this_entry, prev_entry, quorum_entry;
  this_entry.term_id = term_id;
  this_entry.idx = entry_idx;
  prev_entry.term_id = term_id;
  prev_entry.idx = entry_idx - 1;
  quorum_entry.term_id = term_id;
  quorum_entry.idx = entry_idx - 1;

  // init buffers and send fwd_entries_req
  ASSERT_EQ(0, traft_buf_alloc(&b, allBuffsLen));
  ASSERT_EQ(0, traft_buf_encode_and_send(&b, pipes[1], term_id, client_idx, key, vecs, 4));
  // transcode from leader side
  // first, read fwd_entries_req from client
  forward_entries_req fwd_req;
  read_all(pipes[0], (uint8_t*) &fwd_req, RPC_REQ_LEN);
  // leader transcode out to clients
  ASSERT_EQ(0, traft_buf_transcode(&b, pipes[0], key, &fwd_req, this_entry, prev_entry, quorum_entry));
	write_all(tmp_fd, b.main_buffer, b.message_size);
	fsync(tmp_fd);

  // read append_entries req from leader, follower-side
	lseek(tmp_fd, 0, SEEK_SET);
  append_entries_req leader_header;
	ASSERT_EQ(0, traft_buf_decode(&b, &leader_header, tmp_fd, key));
  
  ASSERT_EQ(0, traft_buf_view_iovecs(&b, &vecs[0], 4));

  for (int i = 0 ; i < 4 ; i++) {
    ASSERT_EQ(0, memcmp(bufs[i], vecs[i].iov_base, buflen));
  }

  for (int i = 0 ; i < 4 ; i++) {
    free(bufs[i]);
  }
  traft_buf_free(&b);
}
  
