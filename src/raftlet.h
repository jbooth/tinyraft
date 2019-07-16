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
#include "storage.h"
#include "raftlet_state.h"

typedef struct traft_raftlet {
  raftlet_state state;
} traft_raftlet;

int traft_raftlet_handle_req(traft_raftlet *raftlet, traft_conninfo_t *client, traft_buff *req, traft_resp *resp);


#ifdef __cplusplus
}
#endif
