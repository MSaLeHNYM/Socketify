# SocketifyWarnings.cmake — defines socketify_set_warnings(<target>)
#
# Usage:
#   include(cmake/SocketifyWarnings.cmake)
#   socketify_set_warnings(socketify)
#
# Options:
#   SOCKETIFY_WERROR — treat warnings as errors (default OFF)

option(SOCKETIFY_WERROR "Treat compiler warnings as errors" OFF)

function(socketify_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /w14265  # class has virtual functions but non-virtual destructor
            /w14640  # thread-unsafe static member initialization
        )
        if(SOCKETIFY_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wnull-dereference
            -Wdouble-promotion
        )
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            target_compile_options(${target} PRIVATE
                -Wduplicated-cond
                -Wduplicated-branches
                -Wlogical-op
            )
        endif()
        if(SOCKETIFY_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# socketify_enable_sanitizers(<target>) — honors SOCKETIFY_SANITIZE
# (comma/semicolon-separated list, e.g. "address,undefined").
set(SOCKETIFY_SANITIZE "" CACHE STRING "Sanitizers to enable (e.g. address,undefined,thread)")

function(socketify_enable_sanitizers target)
    if(SOCKETIFY_SANITIZE STREQUAL "")
        return()
    endif()
    if(MSVC)
        message(WARNING "SOCKETIFY_SANITIZE is not supported with MSVC; ignoring")
        return()
    endif()
    string(REPLACE "," ";" _san_list "${SOCKETIFY_SANITIZE}")
    string(REPLACE ";" "," _san_flags "${_san_list}")
    target_compile_options(${target} PUBLIC -fsanitize=${_san_flags} -fno-omit-frame-pointer)
    target_link_options(${target} PUBLIC -fsanitize=${_san_flags})
endfunction()
