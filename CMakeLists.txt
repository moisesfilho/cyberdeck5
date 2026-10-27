cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cyberdeck5)

execute_process(
    COMMAND "${PYTHON}" "${CMAKE_SOURCE_DIR}/tools/prepare_esp_hosted_cpu_staging.py" verify
    RESULT_VARIABLE hosted_staging_result
    OUTPUT_VARIABLE hosted_staging_output
    ERROR_VARIABLE hosted_staging_error
)
if(NOT hosted_staging_result EQUAL 0)
    message(FATAL_ERROR
        "ESP-Hosted CPU-staging is missing or changed. Run\n"
        "  python3 tools/prepare_esp_hosted_cpu_staging.py apply\n"
        "then reconfigure. ${hosted_staging_error}")
endif()
message(STATUS "${hosted_staging_output}")

add_custom_target(verify_esp_hosted_cpu_staging ALL
    COMMAND "${PYTHON}" "${CMAKE_SOURCE_DIR}/tools/prepare_esp_hosted_cpu_staging.py" verify
    COMMENT "Verifying ESP-Hosted CPU-staging sources"
    VERBATIM
)
add_dependencies(${CMAKE_PROJECT_NAME}.elf verify_esp_hosted_cpu_staging)
