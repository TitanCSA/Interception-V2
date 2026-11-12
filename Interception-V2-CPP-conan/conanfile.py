from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMake, cmake_layout
class NativeSDK(ConanFile):
    name = "interception-v2-sdk"
    version = "2.0.0"
    license = "LGPL-3.0-or-later"
    description = "Public Windows x64 user-mode SDK for Interception-V2; driver not included."
    settings = "os", "arch", "compiler", "build_type"
    exports_sources = "CMakeLists.txt", "*.cmake.in", "sdk/*", "LICENSE", "licenses/*", "NOTICE*", "README.txt"
    generators = "CMakeToolchain", "CMakeDeps"
    def validate(self):
        if str(self.settings.os) != "Windows" or str(self.settings.arch) != "x86_64":
            raise ConanInvalidConfiguration("Windows x64 is required")
    def layout(self):
        cmake_layout(self)
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
    def package(self):
        CMake(self).install()
    def package_info(self):
        self.cpp_info.defines = ["INTERCEPTION_STATIC"]
        self.cpp_info.libs = ["TitanInterceptionV2"]
        self.cpp_info.includedirs = ['include', 'include/library']
        self.cpp_info.system_libs = []
        self.cpp_info.set_property("cmake_file_name", "InterceptionV2SDK")
        self.cpp_info.set_property("cmake_target_name", "InterceptionV2SDK::sdk")
