set(PLUGIN_ID "com.xcom.tools.network-services")
set(PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/plugins/${PLUGIN_ID}")

find_package(Qt6 6.5 REQUIRED COMPONENTS Network)

add_library(xcom_network_services_plugin MODULE
    plugin.cpp
)

target_include_directories(xcom_network_services_plugin PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(xcom_network_services_plugin PRIVATE
    xcom_api Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
)
set_target_properties(xcom_network_services_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "xcom-network-services"
    LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${PLUGIN_OUTPUT_DIR}"
)

add_custom_command(TARGET xcom_network_services_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json"
        "$<TARGET_FILE_DIR:xcom_network_services_plugin>/manifest.json"
)
