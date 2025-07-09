load("@fbsource//tools/build_defs:fb_xplat_cxx_binary.bzl", "fb_xplat_cxx_binary")
load("@fbsource//tools/build_defs:fb_xplat_cxx_library.bzl", "fb_xplat_cxx_library")
load("@fbsource//tools/build_defs:fb_xplat_cxx_test.bzl", "fb_xplat_cxx_test")
load(
    "@fbsource//tools/build_defs:platform_defs.bzl",
    "ANDROID",
    "APPLE",
    "CXX",
    "FBCODE",
    "IOS",
    "MACOSX",
    "WINDOWS",
)

CXXFLAGS = [
    "-frtti",
    "-fexceptions",
    "-Wno-error",
    "-Wno-unused-local-typedefs",
    "-Wno-unused-variable",
    "-Wno-sign-compare",
    "-Wno-comment",
    "-Wno-return-type",
    "-Wno-global-constructors",
]

FBANDROID_CXXFLAGS = [
    "-ffunction-sections",
    "-Wno-uninitialized",
]

FBOBJC_CXXFLAGS = [
    "-Wno-global-constructors",
]

WINDOWS_MSVC_CXXFLAGS = [
    "/EHs",
]

WINDOWS_CLANG_CXX_FLAGS = [
    "-Wno-deprecated-declarations",
    "-Wno-microsoft-cast",
    "-Wno-missing-braces",
    "-Wno-unused-function",
    "-msse4.2",
    "-DBOOST_HAS_THREADS",
]

DEFAULT_APPLE_SDKS = (IOS, MACOSX)

DEFAULT_PLATFORMS = (ANDROID, APPLE, CXX, FBCODE, WINDOWS)

def _compute_include_directories():
    base_path = native.package_name()
    if base_path == "xplat/wangle":
        return [".."]
    thrift_path = base_path[6:]
    return ["/".join(len(thrift_path.split("/")) * [".."])]

def wangle_cxx_library(
        name,
        compiler_flags = [],
        fbandroid_compiler_flags = [],
        fbobjc_compiler_flags = [],
        windows_compiler_flags = [],
        windows_msvc_compiler_flags_override = [],
        **kwargs):
    """Translate a simpler declartion into the more complete library target"""
    fb_xplat_cxx_library(
        name = name,
        header_namespace = "",
        public_include_directories = _compute_include_directories(),
        enable_static_variant = True,
        apple_sdks = DEFAULT_APPLE_SDKS,
        compiler_flags = compiler_flags + CXXFLAGS,
        platforms = DEFAULT_PLATFORMS,
        preferred_linkage = "static",
        fbandroid_compiler_flags = fbandroid_compiler_flags + FBANDROID_CXXFLAGS,
        fbobjc_compiler_flags = fbobjc_compiler_flags + FBOBJC_CXXFLAGS,
        windows_compiler_flags = windows_compiler_flags + WINDOWS_CLANG_CXX_FLAGS,
        windows_msvc_compiler_flags_override = windows_msvc_compiler_flags_override + WINDOWS_MSVC_CXXFLAGS,
        visibility = ["PUBLIC"],
        **kwargs
    )

def wangle_cxx_binary(name, **kwargs):
    fb_xplat_cxx_binary(
        name = name,
        platforms = (CXX,),
        **kwargs
    )

def wangle_cxx_test(name, **kwargs):
    fb_xplat_cxx_test(
        name = name,
        platforms = (CXX,),
        **kwargs
    )
