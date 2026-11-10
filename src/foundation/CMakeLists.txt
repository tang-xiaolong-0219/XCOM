add_library(xcom_foundation STATIC
    log.cpp
    log.h
)

target_include_directories(xcom_foundation PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(xcom_foundation PUBLIC Qt6::Core)
