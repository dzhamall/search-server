cmake_minimum_required(VERSION 3.24)
project(search_server)

set(CMAKE_CXX_STANDARD 17)

add_executable(
        search_server
        main.cpp
        document.h document.cpp
        log_duration.h
        paginator.h
        read_input_functions.h read_input_function.cpp
        remove_duplicates.h remove_duplicates.cpp
        request_queue.h request_queue.cpp
        search_server.h search_server.cpp
        string_processing.h
        process_queries.h process_queries.cpp
        concurrent_map.h string_processing.cpp)

