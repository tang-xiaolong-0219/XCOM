add_library(xcom_ui_kit STATIC
    console_view.h
    console_view.cpp
    command_line.h
    command_line.cpp
    frame_parser_view.h
    frame_parser_view.cpp
    session_workspace.h
    session_workspace.cpp
)

target_include_directories(xcom_ui_kit PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(xcom_ui_kit
    PUBLIC
        xcom_api
        xcom_serial
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)
