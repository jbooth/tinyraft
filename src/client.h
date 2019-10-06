#pragma once

#include <stdint.h>
#include <stddef.h>
#include <sodium.h>
#include <lz4.h>
#include <sys/uio.h>
#include "tinyraft.h"
#include "wiretypes.h"

#ifdef __cplusplus
extern "C" {
#endif



/** xchacha20poly1305_KEYBYTES */
typedef uint8_t traft_symmetrickey_t[32];

/** 
 * Information about a connection between two peers.
 * Contains negotiated session key and identity information. 
 */
typedef struct traft_conninfo_t {
  uuid_t                cluster_id;
  traft_publickey_t     client_id;
  traft_publickey_t     server_id;
  traft_symmetrickey_t  client_session_key;
  traft_symmetrickey_t  server_session_key;
} traft_conninfo_t;

/** Represents local identity of a server */
typedef struct traft_raftletinfo_t {
  uuid_t                    cluster_id;
  traft_publickey_t         my_id; 
  traft_secretkey_t         my_sk;
} traft_raftletinfo_t;

typedef struct traft_conn {

} traft_conn;


#ifdef __cplusplus
}
#endif
