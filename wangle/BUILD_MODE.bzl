""" build mode definitions for wangle """

load("@fbcode//:BUILD_MODE.bzl", get_parent_modes = "get_empty_modes")
load("@fbcode_macros//build_defs:create_build_mode.bzl", "extend_build_modes")

_lsan_suppressions = [
    "AcceptRoutingHandlerTest::SetUp",
    "CRYPTO_malloc",
    "CRYPTO_realloc",
    "folly::IOBuf::createSeparate",
    "TestClientPipelineFactory::newPipeline",
    "wangle::Acceptor::makeNewAsyncSocket",
    "wangle::AsyncSocketHandler::getReadBuffer",
    "wangle::ServerAcceptor",
]

_tags = [
]

_modes = extend_build_modes(
    get_parent_modes(),
    lsan_suppressions = _lsan_suppressions,
    tags = _tags,
)

def get_modes():
    """ Return modes for this file """
    return _modes
