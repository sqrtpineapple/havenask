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
