# Modified by the Seihai project, 2026-11-20: see CHANGES-SEIHAI.md.
unset(VCPKG_TARGET_IS_WINDOWS)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO libtom/libtomcrypt
    REF v1.18.2
    SHA512 53accb4f92077ff1c52102bece270e77c497e599c392aa0bf4dbc173b6789e7e4f1012d8b5f257c438764197cb7bac8ba409a9d4e3a70e69bec5863b9495329e
    HEAD_REF develop
)

if(VCPKG_TARGET_IS_WINDOWS)
    vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

    if(VCPKG_CRT_LINKAGE STREQUAL "dynamic")
        set(CRTFLAG "/MD")
    else()
        set(CRTFLAG "/MT")
    endif()

    # Make sure we start from a clean slate
    vcpkg_execute_build_process(
        COMMAND nmake -f ${SOURCE_PATH}/makefile.msvc clean
        WORKING_DIRECTORY ${SOURCE_PATH}
        LOGNAME clean-${TARGET_TRIPLET}-dbg
    )

    #Debug Build
    vcpkg_execute_build_process(
        COMMAND nmake -f ${SOURCE_PATH}/makefile.msvc "CFLAGS=${CRTFLAG}d /DUSE_LTM /DLTM_DESC \"/I${CURRENT_INSTALLED_DIR}/include\""
        WORKING_DIRECTORY ${SOURCE_PATH}
        LOGNAME build-${TARGET_TRIPLET}-dbg
    )

    file(INSTALL
        ${SOURCE_PATH}/tomcrypt.lib
        DESTINATION ${CURRENT_PACKAGES_DIR}/debug/lib
    )

    # Clean up necessary to rebuild without debug symbols
    vcpkg_execute_build_process(
        COMMAND nmake -f ${SOURCE_PATH}/makefile.msvc clean
        WORKING_DIRECTORY ${SOURCE_PATH}
        LOGNAME clean-${TARGET_TRIPLET}-rel
    )

    #Release Build
    vcpkg_execute_build_process(
        COMMAND nmake -f ${SOURCE_PATH}/makefile.msvc "CFLAGS=${CRTFLAG} /Ox /DNDEBUG /DUSE_LTM /DLTM_DESC \"/I${CURRENT_INSTALLED_DIR}/include\""
        WORKING_DIRECTORY ${SOURCE_PATH}
        LOGNAME build-${TARGET_TRIPLET}-rel
    )

    file(INSTALL
        ${SOURCE_PATH}/tomcrypt.lib
        DESTINATION ${CURRENT_PACKAGES_DIR}/lib
    )

    file(INSTALL
        ${SOURCE_PATH}/src/headers/
        DESTINATION ${CURRENT_PACKAGES_DIR}/include
    )
else()
    if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
        set(MAKE_FILE "makefile.shared")
    else()
        set(MAKE_FILE "makefile")
    endif()

    # Seihai: plain make builds for the host CPU. On macOS, build for the triplet's
    # architectures, so arm64-osx cross-built on an Intel runner gets an arm64 archive.
    set(ARCH_FLAGS "")
    if(VCPKG_TARGET_IS_OSX)
        foreach(arch IN LISTS VCPKG_OSX_ARCHITECTURES)
            string(APPEND ARCH_FLAGS " -arch ${arch}")
        endforeach()
        set(ENV{LDFLAGS} "${ARCH_FLAGS}")
    endif()
    set(ENV{CFLAGS} "-fPIC -DUSE_LTM -DLTM_DESC -I${CURRENT_INSTALLED_DIR}/include${ARCH_FLAGS}")

    vcpkg_execute_build_process(
        COMMAND make -f ${MAKE_FILE} clean
        WORKING_DIRECTORY ${SOURCE_PATH}
    )
    vcpkg_execute_build_process(
        COMMAND make -j${VCPKG_CONCURRENCY} -f ${MAKE_FILE} PREFIX=${CURRENT_PACKAGES_DIR}/debug LTC_DEBUG=1 install
        WORKING_DIRECTORY ${SOURCE_PATH}
    )
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

    vcpkg_execute_build_process(
        COMMAND make -f ${MAKE_FILE} clean
        WORKING_DIRECTORY ${SOURCE_PATH}
    )
    vcpkg_execute_build_process(
        COMMAND make -j${VCPKG_CONCURRENCY} -f ${MAKE_FILE} PREFIX=${CURRENT_PACKAGES_DIR} install
        WORKING_DIRECTORY ${SOURCE_PATH}
    )

endif()

#Copy license
file(
    INSTALL
    ${SOURCE_PATH}/LICENSE
    DESTINATION ${CURRENT_PACKAGES_DIR}/share/${PORT} RENAME copyright
)
