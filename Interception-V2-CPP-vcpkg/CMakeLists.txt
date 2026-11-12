cmake_minimum_required(VERSION 3.24)
project(InterceptionV2SDK VERSION 2.0.0 LANGUAGES C CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This SDK requires Windows x64. No driver is installed by this build.")
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
add_library(TitanInterceptionV2 STATIC
  "sdk/library/interception.c"
)
add_library(InterceptionV2SDK::sdk ALIAS TitanInterceptionV2)
set_target_properties(TitanInterceptionV2 PROPERTIES EXPORT_NAME sdk)
target_compile_features(TitanInterceptionV2 PUBLIC c_std_11)
target_compile_definitions(TitanInterceptionV2 PUBLIC INTERCEPTION_STATIC)
target_compile_definitions(TitanInterceptionV2 PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
if(MSVC)
  target_compile_options(TitanInterceptionV2 PRIVATE /utf-8 /EHsc /W4)
endif()
target_include_directories(TitanInterceptionV2 PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk>"
  "$<INSTALL_INTERFACE:include/>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/library>"
  "$<INSTALL_INTERFACE:include/library>"
)
target_link_libraries(TitanInterceptionV2 PUBLIC )
install(TARGETS TitanInterceptionV2 EXPORT InterceptionV2SDKTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY sdk/ DESTINATION include FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl")
install(FILES LICENSE DESTINATION share/interception-v2)
install(EXPORT InterceptionV2SDKTargets NAMESPACE InterceptionV2SDK:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/InterceptionV2SDK)
configure_file("InterceptionV2SDKConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/InterceptionV2SDKConfig.cmake" @ONLY)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/InterceptionV2SDKConfigVersion.cmake" VERSION 2.0.0 COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/InterceptionV2SDKConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/InterceptionV2SDKConfigVersion.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/InterceptionV2SDK)
