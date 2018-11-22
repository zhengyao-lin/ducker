# core

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")

add_lib_batch(ducker-core STATIC "*.c")

target_link_libraries(ducker-core)
