find_program(QPM_CLAZY_EXECUTABLE NAMES clazy-standalone)

function(_qpm_collect_compiled_targets directory out_var)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    set(result "")
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        if(type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
            list(APPEND result ${target})
        endif()
    endforeach()
    foreach(subdirectory IN LISTS subdirectories)
        _qpm_collect_compiled_targets("${subdirectory}" nested)
        list(APPEND result ${nested})
    endforeach()
    set(${out_var} ${result} PARENT_SCOPE)
endfunction()

# Adds the target "clazy", which runs clazy-standalone with the checks listed in
# .clazy over every project translation unit in compile_commands.json and fails
# on the first finding. Call it once, after every target has been defined: it
# depends on all of them, because the translation units include generated moc
# and resource code.
function(qpm_add_clazy_target)
    if(NOT QPM_CLAZY_EXECUTABLE)
        message(STATUS "clazy-standalone not found; the clazy target is not available")
        return()
    endif()

    _qpm_collect_compiled_targets("${PROJECT_SOURCE_DIR}" compiled_targets)

    add_custom_target(clazy
        COMMAND "${CMAKE_COMMAND}"
            "-DCLAZY_EXECUTABLE=${QPM_CLAZY_EXECUTABLE}"
            "-DCHECKS_FILE=${PROJECT_SOURCE_DIR}/.clazy"
            "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DBUILD_DIR=${PROJECT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunClazy.cmake"
        COMMENT "Running clazy over all project translation units"
        USES_TERMINAL
        VERBATIM
    )
    add_dependencies(clazy ${compiled_targets})
endfunction()
