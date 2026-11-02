# This file is part of Whitegram for Desktop.

function(generate_backend_key target_name)
    set(gen_dst ${CMAKE_CURRENT_BINARY_DIR}/gen)
    file(MAKE_DIRECTORY ${gen_dst})

    set(gen_files ${gen_dst}/whitegram_backend_key.h)
    set(gen_script ${CMAKE_CURRENT_SOURCE_DIR}/cmake/backend_key_header.cmake)
    set(key_file ${CMAKE_CURRENT_SOURCE_DIR}/build/whitegram_backend_key.txt)

    set(gen_depends ${gen_script})
    if(EXISTS ${key_file})
        list(APPEND gen_depends ${key_file})
    endif()

    add_custom_command(
    OUTPUT
        ${gen_files}
    COMMAND
        ${CMAKE_COMMAND}
        -Dkey_file=${key_file}
        -Doutput=${gen_files}
        -P ${gen_script}
    COMMENT "Generating backend key header (${target_name})"
    DEPENDS
        ${gen_depends}
    )
    generate_target(${target_name} backend_key ${gen_files} "${gen_files}" ${gen_dst})
endfunction()
