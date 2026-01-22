# CompileShaders.cmake
set(SHADERS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADERS_DEST_DIR "${CMAKE_SOURCE_DIR}/shaders")

# Try to find shaderc in the build directory first (when built), then source
if(EXISTS "${CMAKE_BINARY_DIR}/bgfx.cmake/cmake/bgfx/Release/shaderc.exe")
    set(SHADERC_PATH "${CMAKE_BINARY_DIR}/bgfx.cmake/cmake/bgfx/Release/shaderc.exe")
elseif(EXISTS "${CMAKE_BINARY_DIR}/bgfx.cmake/cmake/bgfx/Debug/shaderc.exe")
    set(SHADERC_PATH "${CMAKE_BINARY_DIR}/bgfx.cmake/cmake/bgfx/Debug/shaderc.exe")
else()
    message(WARNING "shaderc not found at expected locations. Shaders will not be compiled.")
    return()
endif()

set(BASESHADERS_DIR "${CMAKE_SOURCE_DIR}/baseshaders")
message(STATUS "Using shaderc: ${SHADERC_PATH}")

file(GLOB VERTEX_SHADERS "${SHADERS_SOURCE_DIR}/v_*.sc")
foreach(SHADER_FILE ${VERTEX_SHADERS})
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
    set(OUTPUT_FILE "${SHADERS_DEST_DIR}/${SHADER_NAME}.bin")
    execute_process(
        COMMAND "${SHADERC_PATH}" -f "${SHADER_FILE}" -o "${OUTPUT_FILE}" --platform windows --type vertex --include "${BASESHADERS_DIR}"
        RESULT_VARIABLE COMPILE_RESULT
    )
    if(NOT COMPILE_RESULT EQUAL 0)
        message(WARNING "Failed to compile ${SHADER_NAME}.sc")
    else()
        message(STATUS " Compiled: ${SHADER_NAME}.bin")
    endif()
endforeach()

file(GLOB FRAGMENT_SHADERS "${SHADERS_SOURCE_DIR}/f_*.sc")
foreach(SHADER_FILE ${FRAGMENT_SHADERS})
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
    set(OUTPUT_FILE "${SHADERS_DEST_DIR}/${SHADER_NAME}.bin")
    execute_process(
        COMMAND "${SHADERC_PATH}" -f "${SHADER_FILE}" -o "${OUTPUT_FILE}" --platform windows --type fragment --include "${BASESHADERS_DIR}"
        RESULT_VARIABLE COMPILE_RESULT
    )
    if(NOT COMPILE_RESULT EQUAL 0)
        message(WARNING "Failed to compile ${SHADER_NAME}.sc")
    else()
        message(STATUS " Compiled: ${SHADER_NAME}.bin")
    endif()
endforeach()

message(STATUS "Shader compilation complete!")
