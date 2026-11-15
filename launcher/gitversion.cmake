# write OUTPUT, a header that defines GIT_COMMIT as the commit that a build
# from a git checkout of the source tree TOP is of, with -dirty if files
# are changed, or as empty in a tarball, as src/Makefile.am does for
# powwow; run on each build, but OUTPUT is written only when it changes

set(commit "")
if(EXISTS "${TOP}/.git")
    execute_process(COMMAND git -C "${TOP}" rev-parse --short=8 HEAD
        OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT commit STREQUAL "")
        execute_process(COMMAND git -C "${TOP}" diff --quiet HEAD --
            RESULT_VARIABLE changed ERROR_QUIET)
        if(NOT changed EQUAL 0)
            string(APPEND commit "-dirty")
        endif()
    endif()
endif()
file(WRITE "${OUTPUT}.new" "#define GIT_COMMIT \"${commit}\"\n")
file(COPY_FILE "${OUTPUT}.new" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.new")
