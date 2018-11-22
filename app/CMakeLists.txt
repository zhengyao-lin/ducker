# app

add_exe_batch(ducker-main STATIC "*.c")

target_link_libraries(ducker-main ducker-core)
