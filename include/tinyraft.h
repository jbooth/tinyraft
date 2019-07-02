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

#define VERSION 1

#include <stdint.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <uuid/uuid.h>
#include <sodium.h>

/** Uniquely identifies an entry in the replicated log. */
typedef struct traft_entry_id {
  uint64_t  term_id;
  uint32_t  idx;
} traft_entry_id;

/**
 * State machines do three things:
 * 1)  Have committed log entries applied to them to update their state.
 * 2)  Maintain a snapshot of that state, producing a new one on demand.
 * 3)  Provide facility to stream that snapshot out to another node.
 * 
 * The user will likely want to interact with it in some other way, providing
 * read methods that access state, etc.  
 * 
 * All apply_log and snapshot creation and snapshot installation commands 
 * are invoked by a single thread in a guaranteed order.  
 * Streaming out a previously taken snapshot is done by a different thread.
 */
typedef struct traft_statemachine_ops {
    /** Callback to apply a log entry to our state machine */
    int(*apply_log)(void *state_machine, const char *entry);

    /** 
     * Orders the state machine to take a snapshot of current state, persist it and delete any previous snapshot.  
     * We only maintain 1 snapshot per state machine. 
     */
    int (*take_snapshot)(void *state_machine);  

    /** Revert to previously taken snapshot */
    int (*revert_snapshot)(void *state_machine);

    /** Delete all local state and stream in a snapshot.  */
    int (*streamin_snapshot)(void *state_machine, int in_fd);

    /** Stream out the last snapshot image we took. 
     *  Note:  This will be called from a separate thread, it must be threadsafe.
     */
    int (*streamout_snapshot)(void *state_machine, int out_fd);
} traft_statemachine_ops;


/**  Acceptor that can run multiple raftlets on a single port. */ 
typedef void * traft_server;

typedef struct traft_server_config {
} traft_server_config;

/**
  * Allocates and starts a server listening to the provided address. 
  * Points the provided ptr at it for usage in stop() and join() functions.
  * Server thread will clean up allocated resources on death.
  */
int traft_start_server(uint16_t port, traft_server *ptr); 

/** Shuts down server with all attached raftlets, cleaning up all resources.  Blocks until done.   */
int traft_stop_server(traft_server server);


typedef uint8_t traft_publickey_t[32];    // crypto_box_curve25519xchacha20poly1305_PUBLICKEYBYTES
typedef uint8_t traft_secretkey_t[32]; // crypto_box_curve25519xchacha20poly1305_SECRETKEYBYTES

#define TRAFT_MAX_PEERS 16
/** Cluster-wide configuration values. */
typedef struct traft_cluster_config {
  // Leader will try to send a heartbeat at least this often.
  int64_t     heartbeat_interval_ms; // 8

  // Followers will call for a new election if leader doesn't contact at least this often.
  int64_t     election_timeout_ms;  // 16

  // The oldest we'll allow a snapshot to be before taking a new one and starting a new term.
  int64_t     max_snapshot_age_sec; // 24

  // The following values operate independently in that we'll try to discard old terms from disk if any are exceeded.
  // However, we will always maintain at least 2 terms on disk.
  int64_t     max_retained_age_sec;  // 32, Will try to discard any terms who's last entry is older than this.
  int64_t     max_retained_bytes;    // 40, Will try to evict terms until we're below this total threshold.

  // Cluster membership: IDs and hostname/ports
  uint16_t        ports[TRAFT_MAX_PEERS];           // + 2  * 16 = 72
  traft_publickey_t   peer_ids[TRAFT_MAX_PEERS];        // + 32 * 16 = 584
  char            hostnames[TRAFT_MAX_PEERS][256];  // + 4096 = 4680
  uint64_t        num_peers;                  // 4688
} traft_cluster_config;
#define TRAFT_CLUSTER_CONFIG_SIZE 4688

typedef struct traft_raftlet_identity {
  traft_publickey_t     raftlet_id; // 32
  traft_secretkey_t     secret_key; // 64
} traft_raftlet_identity;

#define TRAFT_DEFAULT_PORT 1103
/**
 * Generates keys and default raftlet_config values for peer_count peers, storing them in *cfg.
 * cfg must point to a memory region at least peer_count * TRAFT_CLUSTER_CONFIG_SIZE in length.
 * If hostnames is non-null and points to a list of strings of length peer_count, we will use those hostnames.  Otherwise localhost.
 * If ports is non-null, we will use those ports.  Otherwise TRAFT_DEFAULT_PORT.
 */
void gen_cluster(traft_cluster_config *cfg, int peer_count, char *hostnames, uint16_t *ports);

typedef struct traft_raftlet {
  uuid_t                      cluster_id;
  traft_statemachine_ops      ops;
  void                        *state_machine;
  void                        *server_state;
} traft_raftlet;

/** 
  * Starts a raftlet serving the provided, initialized storagepath on the provided server.  
  * Allocates all resources necessary to process entries and starts threads before returning.
  */
int traft_run_raftlet(const char *storagepath, traft_server server, traft_statemachine_ops ops, void *state_machine, traft_raftlet *raftlet);

/**
  * Requests that a raftlet stop running.  It will clean up all resources and terminate threads before returning.
  */
int traft_stop_raftlet(traft_raftlet *raftlet);



#ifdef __cplusplus
}
#endif
