# Shared warning configuration for OmaChat-owned targets.
function(omachat_set_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
        -Wcast-align -Wunused -Woverloaded-virtual
        -Wimplicit-fallthrough)
    if(OMACHAT_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()
