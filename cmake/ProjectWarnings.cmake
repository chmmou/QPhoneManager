# Turns on the compiler warnings of this project and hides Qt API that is
# deprecated up to the minimum Qt version. Applies to the given target only, so
# code under third_party/ keeps its own settings.
function(qpm_set_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    target_compile_definitions(${target} PRIVATE QT_DISABLE_DEPRECATED_UP_TO=0x060900)
endfunction()
