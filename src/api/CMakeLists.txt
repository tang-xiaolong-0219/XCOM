add_library(xcom_api SHARED
    api.cpp
)

target_compile_definitions(xcom_api PRIVATE XCOM_API_LIBRARY)
target_include_directories(xcom_api PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(xcom_api PUBLIC Qt6::Core)
set_target_properties(xcom_api PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
