add_executable(X-COM WIN32 main.cpp)

target_link_libraries(X-COM
    PRIVATE
        xcom_shell
)

if (WIN32)
    find_program(XCOM_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${Qt6_DIR}/../../../bin"
    )
    if (XCOM_WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET X-COM POST_BUILD
            COMMAND "${XCOM_WINDEPLOYQT_EXECUTABLE}"
                "$<IF:$<CONFIG:Debug>,--debug,--release>"
                --compiler-runtime
                --no-translations
                --dir "$<TARGET_FILE_DIR:X-COM>"
                "$<TARGET_FILE:X-COM>"
            COMMENT "Deploying Qt runtime for X-COM"
            VERBATIM
        )
    endif()
endif()
