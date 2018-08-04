add_library (tinyraft STATIC rpc.c log_store.c)
message(${TINYRAFT_INCLUDE_DIRS})
target_include_directories(tinyraft PUBLIC ${TINYRAFT_INCLUDE_DIRS})
