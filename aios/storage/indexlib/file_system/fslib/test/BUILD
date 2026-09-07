load('//aios/storage:defs.bzl', 'strict_cc_fast_test')
strict_cc_fast_test(
    name='fslib_unittest',
    srcs=['FslibFileWrapperTest.cpp', 'FslibWrapperTest.cpp'],
    copts=['-fno-access-control'],
    deps=[
        '//aios/storage/indexlib/file_system/fslib',
        '//aios/storage/indexlib/util/testutil:unittest'
    ]
)
strict_cc_fast_test(
    name='DataFlushControllerTest',
    srcs=['DataFlushControllerTest.cpp'],
    copts=['-fno-access-control'],
    deps=[
        '//aios/storage/indexlib/file_system/fslib',
        '//aios/storage/indexlib/util/testutil:unittest'
    ]
)

strict_cc_fast_test(
    name='YltFslibFileWrapperTest',
    srcs=['YltFslibFileWrapperTest.cpp'],
    copts=['-std=c++20', '-fcoroutines'],
    deps=[
        '//aios/storage/indexlib/file_system/fslib:ylt_fslib_file_wrapper',
        '//aios/storage/indexlib/util/testutil:unittest',
        '//aios/ylt:ylt_io_context_executor',
    ],
)
