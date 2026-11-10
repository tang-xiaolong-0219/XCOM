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
        set(XCOM_WINDEPLOYQT_OPTIONS
            "$<IF:$<CONFIG:Debug>,--debug,--release>"
            --no-translations
            --dir "$<TARGET_FILE_DIR:X-COM>"
        )
        if (MSVC OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            list(PREPEND XCOM_WINDEPLOYQT_OPTIONS --compiler-runtime)
        endif()
        add_custom_command(TARGET X-COM POST_BUILD
            COMMAND "${XCOM_WINDEPLOYQT_EXECUTABLE}"
                ${XCOM_WINDEPLOYQT_OPTIONS}
                "$<TARGET_FILE:X-COM>"
            COMMENT "Deploying Qt runtime for X-COM"
            VERBATIM
        )
    endif()
endif()
