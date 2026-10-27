
add_executable(lab2
    student-cpp/src/main.cpp
    student-cpp/src/scheduler.cpp
)
add_executable(stress student-cpp/extra/stress.cpp)
add_executable(ministress student-cpp/extra/ministress.cpp)

foreach(target lab2 stress ministress)
    target_include_directories(${target} PRIVATE student-cpp/src)
    target_link_libraries(${target} PRIVATE Threads::Threads)
    target_compile_options(${target} PRIVATE -Wall -Wpedantic)
    set_target_properties(${target}
        PROPERTIES
        CXX_STANDARD 20
        CXX_EXTENSIONS OFF
    )
endforeach()
