config_setting(name='use_coro', values={'define': 'use_coro=yes'})

genrule(
    name='async_simple_coro_config_h',
    srcs=['Config.h.in'],
    outs=['Config.h'],
    cmd=select({
        ':use_coro':
            'sed s/ASYNC_SIMPLE_CORO_SWITCH/ASYNC_SIMPLE_USE_COROUTINES/ $(SRCS) > $(OUTS)',
        '//conditions:default':
            'sed s/ASYNC_SIMPLE_CORO_SWITCH/ASYNC_SIMPLE_NOT_USE_COROUTINES/ $(SRCS) > $(OUTS)'
    })
)

cc_library(
    name='async_simple',
    srcs=[
        'Alarm.cpp',
        'ExecutorCreator.cpp',
        'NamedTaskScheduler.cpp',
        'TaskScheduler.cpp',
        'Trace.cpp',
    ],
    hdrs=[
        'Alarm.h',
        'CoroInterface.h',
        'Executor.h',
        'ExecutorCreator.h',
        'Helper.h',
        'Log.h',
        'NamedTaskScheduler.h',
        'TaskScheduler.h',
        'Trace.h',
        ':async_simple_coro_config_h',
    ],
    includes=['.'],
    visibility=['//visibility:public'],
    copts=['-std=c++20', '-fcoroutines'],
    linkopts=['-lpthread', '-laio'],
    deps=[
        '@yalantinglibs//:async_simple',
        '//aios/alog',
        '//aios/autil:log',
        '@com_google_absl//absl/synchronization',
        '@com_google_absl//absl/time',
    ],
)

cc_library(
    name='simple_executor',
    srcs=['SimpleExecutor.cpp'],
    deps=[
        ':async_simple',
        '//aios/alog:alog',
        '//aios/autil:mem_pool_base',
        '//aios/autil:string_type',
        '//aios/autil:thread',
    ],
    visibility=['//visibility:public'],
    includes=['.'],
    copts=['-std=c++20', '-fcoroutines'],
    strip_include_prefix='',
    include_prefix='async_simple/executors',
    alwayslink=True,
)

cc_library(
    name='simple_async_io_executor',
    srcs=['SimpleAsyncIOExecutor.cpp'],
    deps=[':simple_executor'],
    visibility=['//visibility:public'],
    includes=['.'],
    copts=['-std=c++20', '-fcoroutines'],
    alwayslink=True,
)

cc_library(
    name='ylt_io_context_executor',
    srcs=['YltIOContextExecutor.cpp'],
    hdrs=['YltIOContextExecutor.h'],
    deps=[
        ':async_simple',
        '@yalantinglibs//:coro_io_context_pool',
    ],
    visibility=['//visibility:public'],
    includes=['.'],
    copts=['-std=c++20', '-fcoroutines'],
    strip_include_prefix='',
    include_prefix='async_simple/executors',
    alwayslink=True,
)

cc_test(
    name='ylt_io_context_executor_test',
    srcs=['YltIOContextExecutorTest.cpp'],
    deps=[
        ':ylt_io_context_executor',
        '@com_google_googletest//:gtest_main',
    ],
)
