add_executable(xcom_serial_test serial_core_test.cpp)

target_link_libraries(xcom_serial_test
    PRIVATE
        xcom_services
        xcom_serial
        xcom_api
        xcom_foundation
        Qt6::Core
        Qt6::SerialPort
)

add_test(NAME serial_core COMMAND xcom_serial_test)

add_executable(xcom_frame_test frame_splitter_test.cpp)

target_link_libraries(xcom_frame_test
    PRIVATE
        xcom_serial
        xcom_api
        xcom_foundation
        Qt6::Core
        Qt6::SerialPort
)

add_test(NAME frame_splitter COMMAND xcom_frame_test)
