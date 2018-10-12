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
#include <sys/uio.h>
#include <string.h>
#include "gtest/gtest.h"
#include "buffers.h"
#include "buffers.c"

TEST (BuffersTest, ImportExport) { 
  buffers b;

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

  ASSERT_EQ(0, init_buffs(&b, allBuffsLen));

  ASSERT_EQ(0, import_iovecs(&b, &vecs[0], 4));

  ASSERT_EQ(20 + (4*16), b.message_size);

  ASSERT_EQ(0, view_iovecs(&b, &vecs[0], 4));

  for (int i = 0 ; i < 4 ; i++) {
    ASSERT_EQ(0, memcmp(bufs[i], vecs[i].iov_base, buflen));
  }

  for (int i = 0 ; i < 4 ; i++) {
    free(bufs[i]);
  }
  free_buffs(&b);
}
  

TEST (BuffersTest, Encoding) { 
  buffers b;

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
  ASSERT_GT(0, tmp_fd);

  unsigned char key[crypto_aead_chacha20poly1305_ietf_KEYBYTES];
  randombytes_buf(key, crypto_aead_chacha20poly1305_ietf_KEYBYTES);

  int term_id = 1;
  int client_idx = 2;
  int entry_idx = 3;

  ASSERT_EQ(0, init_buffs(&b, allBuffsLen));
  ASSERT_EQ(0, encode_and_send(&b, pipes[0], term_id, client_idx, key, vecs, 4));
  
  ASSERT_EQ(20 + (4*16), b.message_size);
  ASSERT_EQ(0, view_iovecs(&b, &vecs[0], 4));

  for (int i = 0 ; i < 4 ; i++) {
    ASSERT_EQ(0, memcmp(bufs[i], vecs[i].iov_base, buflen));
  }

  for (int i = 0 ; i < 4 ; i++) {
    free(bufs[i]);
  }
  free_buffs(&b);
}
  
