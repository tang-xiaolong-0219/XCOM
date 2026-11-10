add_library(xcom_shell STATIC
    ui_context.h
    ui_context.cpp
    notification_hub.h
    notification_hub.cpp
    theme.h
    theme.cpp
    command_palette.h
    command_palette.cpp
    command_terminal.h
    command_terminal.cpp
    session_dialog.h
    session_dialog.cpp
    plugin_manager_view.h
    plugin_manager_view.cpp
    main_window.h
    main_window.cpp
)

target_include_directories(xcom_shell PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(xcom_shell
    PUBLIC
        xcom_api
        xcom_services
        xcom_serial
        xcom_ui_kit
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::SerialPort
)
