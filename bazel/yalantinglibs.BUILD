cc_library(
    name='async_simple',
    srcs=(
        glob([
            'include/ylt/thirdparty/async_simple/uthread/internal/*.cc',
        ]) + select({
            '@com_taobao_aios//:arm_cpu':
                glob(['include/ylt/thirdparty/async_simple/uthread/internal/Linux/aarch64/*.S']),
            '//conditions:default':
                glob(['include/ylt/thirdparty/async_simple/uthread/internal/Linux/x86_64/*.S']),
        })
    ),
    hdrs=glob([
        'include/ylt/thirdparty/async_simple/**/*.h',
        'include/ylt/thirdparty/async_simple/**/*.cppm',
    ]),
    includes=['include/ylt/thirdparty'],
    visibility=['//visibility:public'],
    copts=['-std=c++20', '-fcoroutines'],
    linkopts=['-lpthread'],
)

cc_library(
    name='asio',
    hdrs=glob([
        'include/ylt/thirdparty/asio/**/*.h',
        'include/ylt/thirdparty/asio/**/*.hpp',
        'include/ylt/thirdparty/asio/**/*.ipp',
        'include/ylt/thirdparty/asio.hpp',
    ]),
    includes=['include/ylt/thirdparty'],
    visibility=['//visibility:public'],
    defines=[
        'ASIO_DISABLE_EPOLL',
        'ASIO_HAS_FILE',
        'ASIO_HAS_IO_URING',
        'ASIO_STANDALONE',
    ],
    copts=['-std=c++20', '-fcoroutines'],
    linkopts=['-luring'],
)

cc_library(
    name='coro_io_context_pool',
    hdrs=['include/ylt/coro_io/io_context_pool.hpp'],
    includes=['include'],
    visibility=['//visibility:public'],
    copts=['-std=c++20', '-fcoroutines'],
    deps=[
        ':asio',
        ':async_simple',
    ],
)

cc_library(
    name='util',
    hdrs=glob([
        'include/ylt/util/*.h',
        'include/ylt/util/*.hpp',
        'include/ylt/util/tl/*.hpp',
    ]),
    includes=['include'],
    visibility=['//visibility:public'],
    copts=['-std=c++20'],
)

cc_library(
    name='easylog',
    hdrs=[
        'include/ylt/easylog.hpp',
    ] + glob([
        'include/ylt/easylog/*.hpp',
    ]),
    includes=['include'],
    visibility=['//visibility:public'],
    copts=['-std=c++20'],
    deps=[':util'],
)

cc_library(
    name='coro_file',
    hdrs=[
        'include/ylt/coro_io/coro_file.hpp',
        'include/ylt/coro_io/coro_io.hpp',
    ],
    includes=['include'],
    visibility=['//visibility:public'],
    copts=['-std=c++20', '-fcoroutines'],
    deps=[
        ':asio',
        ':async_simple',
        ':coro_io_context_pool',
        ':easylog',
        ':util',
    ],
)
