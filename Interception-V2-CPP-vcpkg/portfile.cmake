vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/TitanCSA/Interception-V2/releases/download/sdk-v2.0.0/interception-v2-sdk-2.0.0.tar.gz"
    FILENAME "interception-v2-sdk-2.0.0.tar.gz"
    SHA512 0e0e3916d1c77c55f4b6dae6f7e8cce7e64a1f6d928f61d532328d1e1b9bf9c7cd6f71a562b076f522cae1c3ebfd489245ea78c47a443b1c204ee5713abdb930
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}" NO_REMOVE_ONE_LEVEL)
vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME InterceptionV2SDK CONFIG_PATH lib/cmake/InterceptionV2SDK)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
