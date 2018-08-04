SET( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fno-strict-aliasing" )
add_library (tinyraft STATIC rpc.c log_store.c)
message(${TINYRAFT_INCLUDE_DIRS})
target_include_directories(tinyraft PUBLIC ${TINYRAFT_INCLUDE_DIRS})
