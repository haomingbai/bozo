from conans import ConanFile, CMake
from conans.errors import ConanInvalidConfiguration
from conans.tools import Version, check_min_cppstd, load
import re


def get_version():
    try:
        content = load("CMakeLists.txt")
        version = re.search(r"^\s*project\(bozo\s+VERSION\s+([^\s)]+)", content, re.M).group(1)
        return version.strip()
    except Exception:
        return None


class OzoConan(ConanFile):
    name = "bozo"
    version = get_version()
    license = "PostgreSQL"
    topics = ("bozo", "yandex", "postgres", "postgresql", "cpp17", "database", "db", "asio")
    url = "https://github.com/yandex/bozo"
    description = "Conan package for yandex bozo"
    settings = "os", "compiler"

    exports_sources = "include/*", "CMakeLists.txt", "cmake/*", "LICENCE", "AUTHORS"

    generators = "cmake_find_package"
    requires = ("boost/1.74.0", "resource_pool/0.1.0", "libpq/13.1")

    def _configure_cmake(self):
        cmake = CMake(self)
        cmake.configure()
        return cmake

    def configure(self):
        if self.settings.os == "Windows":
            raise ConanInvalidConfiguration("BOZO is not compatible with Winows")
        if self.settings.compiler.get_safe("cppstd"):
            check_min_cppstd(self, "17")

    def build(self):
        cmake = self._configure_cmake()
        cmake.build()

    def package(self):
        cmake = self._configure_cmake()
        cmake.install()

    def package_id(self):
        self.info.header_only()

    def package_info(self):
        self.cpp_info.components["_bozo"].includedirs = ["include"]
        self.cpp_info.components["_bozo"].requires = ["boost::boost", "boost::system", "boost::thread", "boost::coroutine",
                                                     "resource_pool::resource_pool", # == elsid::resource_pool in cmake
                                                     "libpq::pq",                    # == PostgreSQL::PostgreSQL in cmake
                                                     ]
        self.cpp_info.components["_bozo"].defines = [
            "BOOST_COROUTINES_NO_DEPRECATION_WARNING",
            "BOOST_HANA_CONFIG_ENABLE_STRING_UDL",
            "BOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT"
        ]

        compiler = self.settings.compiler
        version = Version(compiler.version)
        if compiler == "clang" or compiler == "apple-clang" or (compiler == "gcc" and version >= 9):
            self.cpp_info.components["_bozo"].cxxflags = [
                "-Wno-gnu-string-literal-operator-template",
                "-Wno-gnu-zero-variadic-macro-arguments",
            ]

        self.cpp_info.filenames["cmake_find_package"] = "bozo"
        self.cpp_info.filenames["cmake_find_package_multi"] = "bozo"
        self.cpp_info.names["cmake_find_package"] = "yandex"
        self.cpp_info.names["cmake_find_package_multi"] = "yandex"
        self.cpp_info.components["_bozo"].names["cmake_find_package"] = "bozo"
        self.cpp_info.components["_bozo"].names["cmake_find_package_multi"] = "bozo"
