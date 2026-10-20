# Fetches one projectM commit (and its submodules) into DIR without the full history.
# Run as: cmake -DDIR=<dir> -DCOMMIT=<sha> -P FetchProjectM.cmake

if(EXISTS "${DIR}/CMakeLists.txt")
    return()
endif()

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
find_package(Git REQUIRED)

foreach(step
        "init --quiet"
        "remote add origin https://github.com/projectM-visualizer/projectm.git"
        "fetch --depth 1 origin ${COMMIT}"
        "checkout --quiet FETCH_HEAD"
        "submodule update --init --depth 1")
    separate_arguments(args UNIX_COMMAND "${step}")
    execute_process(COMMAND "${GIT_EXECUTABLE}" ${args} WORKING_DIRECTORY "${DIR}" RESULT_VARIABLE failed)
    if(failed)
        message(FATAL_ERROR "projectM download failed at: git ${step}")
    endif()
endforeach()
