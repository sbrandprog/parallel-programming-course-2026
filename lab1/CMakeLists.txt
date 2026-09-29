
add_executable(lab1 main.cpp)

target_link_libraries(lab1 PRIVATE Threads::Threads)

target_compile_options(lab1 PRIVATE -Wall -Wpedantic)

set_target_properties(lab1
    PROPERTIES
    CXX_STANDARD 20
    CXX_EXTENSIONS OFF
)
