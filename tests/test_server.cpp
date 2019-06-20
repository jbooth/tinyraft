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
#include "server.h"
#include "tinyraft.h"
#include "raftlet.h"
#include "buffers.h"

int mock_handle_req(traft_raftlet_s *raftlet, traft_buff *req, traft_resp *resp) {
  return 0;
}

int mock_init_raftlet(const char *storagepath, traft_statemachine_ops ops, void *state_machine, traft_raftlet_s *raftlet) {
  return 0;
}

int mock_destroy_raftlet(traft_raftlet_s *raftlet) {
  free(raftlet);
  return 0;
}

traft_server_ops mock_ops = {&mock_handle_req, &mock_init_raftlet, &mock_destroy_raftlet};
uint16_t SERVER_PORT = 1103;

TEST (ServerTest, NormalRpc) {
  traft_server_ops ops; // no actual ops

  traft_server server;
  traft_srv_start_server(SERVER_PORT, &server, mock_ops);
  
}