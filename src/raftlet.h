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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <uuid/uuid.h>
#include "tinyraft.h"
#include "wiretypes.h"

typedef struct traft_raftlet_s {
  uuid_t          cluster_id;
  uint8_t         raftlet_id[32];  // Raftlet public key
  uint8_t         private_key[32]; // Raftlet private key
  traft_storage   storage;         // entry log storage
} traft_raftlet_s;

typedef struct traft_clientinfo {
  uint8_t remote_id[32];
  uint8_t session_key[32];
} traft_clientinfo;

int traft_handle_req(traft_raftlet_s *raftlet, traft_clientinfo *client, traft_req *request, uint8_t *body);


#ifdef __cplusplus
}
#endif
