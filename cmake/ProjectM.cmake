# libprojectM (https://github.com/projectM-visualizer/projectm, LGPL-2.1) draws the MilkDrop
# scenes behind Riddim Forge's 3D form. It is built here as its own shared library from a pinned
# commit and copied next to the plugin binaries, which load it at run time (Source/forge/MilkLayer).
# Keeping it a separate, unmodified library is what lets a closed plugin ship it under the LGPL.

include(ExternalProject)

set(RIDDIM_PROJECTM_COMMIT 349b7cc2db312d47cef6be24c6856ced6643a385) # master, 2026-10-10 (4.2 API)
set(RIDDIM_PROJECTM_DIR ${CMAKE_BINARY_DIR}/projectm-install)

ExternalProject_Add(projectm
    PREFIX ${CMAKE_BINARY_DIR}/projectm
    INSTALL_DIR ${RIDDIM_PROJECTM_DIR}
    DOWNLOAD_COMMAND ${CMAKE_COMMAND} -DDIR=<SOURCE_DIR> -DCOMMIT=${RIDDIM_PROJECTM_COMMIT}
                     -P ${CMAKE_CURRENT_LIST_DIR}/FetchProjectM.cmake
    UPDATE_COMMAND ""
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_INSTALL_BINDIR=bin
        -DBUILD_SHARED_LIBS=ON
        -DBUILD_TESTING=OFF
        -DENABLE_PLAYLIST=OFF
        -DENABLE_SDL_UI=OFF
        -DENABLE_SYSTEM_GLM=OFF
        -DENABLE_SYSTEM_PROJECTM_EVAL=OFF
        -DENABLE_DEBUG_POSTFIX=OFF
        -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded # no Visual C++ Redistributable needed
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --parallel
    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config Release)

if(WIN32)
    set(RIDDIM_PROJECTM_RUNTIME ${RIDDIM_PROJECTM_DIR}/bin/projectM-4.dll)
elseif(APPLE)
    set(RIDDIM_PROJECTM_RUNTIME ${RIDDIM_PROJECTM_DIR}/lib/libprojectM-4.4.dylib)
else()
    set(RIDDIM_PROJECTM_RUNTIME ${RIDDIM_PROJECTM_DIR}/lib/libprojectM-4.so.4)
endif()

# Builds projectM before `target` and copies the library next to the target's binary.
function(riddim_ship_projectm target)
    add_dependencies(${target} projectm)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RIDDIM_PROJECTM_RUNTIME} $<TARGET_FILE_DIR:${target}>
        VERBATIM)
endfunction()
