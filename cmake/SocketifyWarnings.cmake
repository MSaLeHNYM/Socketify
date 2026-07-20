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
        # CXX-only flags must not be applied to C sources (e.g. vendored sqlite3.c).
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-Wall>
            $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
            $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
            $<$<COMPILE_LANGUAGE:CXX>:-Wshadow>
            $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
            $<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast>
            $<$<COMPILE_LANGUAGE:CXX>:-Wcast-align>
            $<$<COMPILE_LANGUAGE:CXX>:-Wunused>
            $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>
            $<$<COMPILE_LANGUAGE:CXX>:-Wnull-dereference>
            $<$<COMPILE_LANGUAGE:CXX>:-Wdouble-promotion>
            $<$<COMPILE_LANGUAGE:C>:-Wall>
            $<$<COMPILE_LANGUAGE:C>:-Wextra>
            $<$<COMPILE_LANGUAGE:C>:-Wno-unused-parameter>
        )
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            target_compile_options(${target} PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:-Wduplicated-cond>
                $<$<COMPILE_LANGUAGE:CXX>:-Wduplicated-branches>
                $<$<COMPILE_LANGUAGE:CXX>:-Wlogical-op>
            )
        endif()
        if(SOCKETIFY_WERROR)
            target_compile_options(${target} PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:-Werror>
            )
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
