# pub

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")

add_lib_batch(ducker-pub STATIC "*.c")

target_link_libraries(ducker-pub)
