SET( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -o2 -fno-strict-aliasing -lsodium -llz4" )
add_library (tinyraft STATIC server.c termlog.c buffers.c raftlet.c rwlock.c)
message(${TINYRAFT_INCLUDE_DIRS})
target_include_directories(tinyraft PUBLIC ${TINYRAFT_INCLUDE_DIRS})
