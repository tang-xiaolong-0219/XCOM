add_executable(xcom-cli main.cpp)

target_link_libraries(xcom-cli
    PRIVATE
        xcom_services
        xcom_serial
        xcom_api
        xcom_foundation
        Qt6::Core
        Qt6::SerialPort
)
