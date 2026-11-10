add_library(xcom_serial STATIC
    transport.h
    transport.cpp
    session.h
    session.cpp
    session_service.h
    session_service.cpp
    frame_splitter.h
    frame_splitter.cpp
)

target_include_directories(xcom_serial PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(xcom_serial
    PUBLIC
        xcom_api
        xcom_foundation
        Qt6::Core
        Qt6::SerialPort
)
