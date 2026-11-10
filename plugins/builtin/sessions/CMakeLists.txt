set(PLUGIN_ID "com.xcom.sessions")
set(PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/plugins/${PLUGIN_ID}")

add_library(xcom_sessions_plugin MODULE
    plugin.cpp
)

target_include_directories(xcom_sessions_plugin PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(xcom_sessions_plugin PRIVATE xcom_api Qt6::Core Qt6::Gui Qt6::Widgets)
set_target_properties(xcom_sessions_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "xcom-sessions"
    LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${PLUGIN_OUTPUT_DIR}"
)

add_custom_command(TARGET xcom_sessions_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json"
        "$<TARGET_FILE_DIR:xcom_sessions_plugin>/manifest.json"
)
