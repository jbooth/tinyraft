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

/** Uniquely identifies an entry in the replicated log. */

/**
 * State machines do three things:
 * 1)  Have committed log entries applied to them to update their state.
 * 2)  Maintain a snapshot of that state, producing a new one on demand.
 * 3)  Provide facility to stream that snapshot out to another node.
 * 
 * The user will likely want to interact with it in some other way, providing
 * read methods that access state, etc.  
 * 
 * All apply_log and snapshot creating commands are invoked 
 * by a single thread in a guaranteed order.  
 * Streaming out a snapshot is done by a different thread.
 */
typedef struct traft_statemachine_ops {
    /** Callback to apply a log entry to our state machine */
    int(*apply_log_cb)(void *state_machine, struct iovec *entry, int num_args);

    /** Delete all local state and stream in a snapshot.  */
    int (*streamin_state)(void *state_machine, int in_fd);

    /** 
     * Orders the state machine to take a snapshot of current state, persist it and delete any previous snapshot.  
     * We only maintain 1 snapshot per state machine. 
     */
    int (*take_snapshot_cb)(void *state_machine); 

    /** Stream out the last snapshot image we took. 
     *  Note:  This will be called from a separate thread, it must be threadsafe.
     */
    int (*streamout_snapshot)(void *state_machine, int out_fd);
} traft_statemachine_ops;


/** Multiplexing server that can run multiple raftlets on a single port. */ 
typedef struct traft_server {
  void * server_state;
} traft_server;

typedef struct traft_server_config {
} traft_server_config;

/** 
  * Allocates and starts a server listening to the provided address. 
  * Points the provided ptr at it for usage in stop() and join() functions.
  * Server thread will clean up allocated resources on death.
  */
int traft_start_server(traft_server_config config, traft_server *ptr); 

/** Requests shutdown of the provided server. */
int traft_stop_server(traft_server *server);

/** Blocks until a server has actually shut down and released all resources. */
int traft_join_server(traft_server *server);

typedef struct raftlet {
  uuid_t                      cluster_id;
  traft_statemachine_ops   ops;
  void                        *state_machine;
  void                        *server_state;
} raftlet;

/** Configuration for a raftlet. */
typedef struct raftlet_config {
  // Leader will try to send a heartbeat at least this often.
  int64_t     heartbeat_interval_ms;

  // Followers will call for a new election if leader doesn't contact at least this often.
  int64_t     election_timeout_ms;

  // The following values govern the length of a term.  We'll start a new term if any are exceeded.
  int64_t     max_term_age_sec; // The time elapsed since the first entry was written
  int64_t     max_term_bytes;   // Size of all entries + 64 bytes per entry
  int64_t     max_term_entries; // Max entries in a term

  // The following values operate independently in that we'll try to discard old terms from disk if any are exceeded.
  // However, we will always maintain at least 2 terms on disk.  
  int64_t     max_retained_age_sec;  // Will try to discard any terms who's last entry is older than this.
  int64_t     max_retained_bytes;    // Will try to evict terms until we're below this total threshold.
  int64_t     max_retained_terms;    // Max number of terms to retain before we try to evict old ones
  int64_t     max_retained_entries;  // Max entries before we try to evict old terms
} raftlet_config;

/** Represents a member of the cluster. */
typedef struct traft_peer {
  uuid_t peer_id;
  struct sockaddr addr;
  socklen_t addrlen;
} traft_peer;

#define TINYRAFT_MAX_PEERS 15

/**
  * Inits a storage directory on disk for a raftlet.  Configuration and membership are stored at init time,
  * because they can change over the lifetime of a cluster.
  * 
  * The configuration information will be considered entry 0 of term 0.  All real terms are >0.
  */
int traft_init_raftlet_storage(const char* storagepath, raftlet_config *config, traft_peer *membership, uint8_t peer_count);

/** 
  * Starts a raftlet serving the provided, initialized storagepath on the provided server.  
  * Allocates all resources necessary to process entries and starts threads before returning.
  * 
  * 
  */
int traft_run_raftlet(const char *storagepath, traft_server *server, traft_statemachine_ops ops, void *state_machine, raftlet *raftlet);

/**
  * Requests that a server stop running.  It will clean up all resources associated before threads terminate.
  */
int traft_stop_raftlet(raftlet *raftlet);

/**
  * Block until a raftlet has stopped running.
  */
int traft_join_raftlet(raftlet *raftlet);



#ifdef __cplusplus
}
#endif
