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
#include <sys/socket.h>
#include "rpc.h"

#define MAX_PEERS 15

/** Represents a member of the cluster. */
typedef struct peer {
  uuid_t peer_id;
  struct sockaddr *addr;
  socklen_t addrlen;
} peer;

/** 
	* Set of all peers in a given cluster.
	* Loaded from and periodically flushed to text file.
	* Updateable by RPC to leader.
	*/
typedef struct peer_set {
	peer peers[MAX_PEERS];
  uint8_t num_peers;
} peer_set;

/** Non-threadsafe set of clients */
typedef struct client_set {
  rpc_client clients[MAX_PEERS];
  int num_clients;
} client_set;

typedef struct cluster {
	peer_set membership;
  client_set clients;
} cluster;

int read_membership(unsigned char *file, peer_set *membership);

int connect_clients(peer_set *membership, client_set *clients);

#ifdef __cplusplus
}
#endif
