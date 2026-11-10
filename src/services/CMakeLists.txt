add_library(xcom_services STATIC
    subscription.h
    subscription.cpp
    command_registry.h
    command_registry.cpp
    event_bus.h
    event_bus.cpp
    settings_service.h
    settings_service.cpp
    logger.h
    logger.cpp
    plugin_context.h
    plugin_context.cpp
    plugin_host.h
    plugin_host.cpp
    core_commands.h
    core_commands.cpp
)

target_include_directories(xcom_services PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(xcom_services
    PUBLIC
        xcom_api
        xcom_foundation
        Qt6::Core
)
