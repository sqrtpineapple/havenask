# Havenask 1.2.0 协程文件读取接口性能测试报告

## 1. 测试目的

本测试用于测量 Havenask 1.2.0 文件系统中的两条协程异步读取路径：

```text
BlockFileNode::ReadAsyncCoro
  -> BlockFileAccessor::ReadAsyncCoro
  -> FslibCommonFileWrapper::PReadVAsync
  -> LocalDirectFile::preadv
  -> future_lite::SimpleIOExecutor::submitIOV

BlockFileAccessor::GetBlockAsyncCoro
  -> BlockFileAccessor::DoGetBlockCoro
  -> BlockFileAccessor::ReadBlockFromFileToCache
  -> FslibCommonFileWrapper::PReadAsync
  -> LocalDirectFile::pread
  -> future_lite::SimpleIOExecutor::submitIO
```

测试没有使用 `FileNode::ReadAsyncCoro` 的默认同步退化实现，也没有测试同步 `Read` 或 `GetBlock` 接口。

## 2. 代码版本

- 官方 Havenask 版本：`v1.2.0`
- 官方提交：`98d8448552111ca113fe1b50d25447303fd91744`
- 测试机使用的隔离源码提交：`77d08826ee137f695edcdf7e8f7c447898974e46`
- 隔离提交说明：`feat: fork havenask-1.2.0 from github`

测试机无法直接连接 GitHub，因此从已有内部仓库的 Havenask 1.2.0 导入提交创建了独立 worktree。以下关键源码树与官方 `v1.2.0` 完全一致：

| 路径 | Git tree/blob hash |
| --- | --- |
| `aios/storage/indexlib/file_system` | `72f023406aaa9876f1d112302a7c60e98eee31ef` |
| `aios/future_lite` | `0a25dd756de70667f18f7c4d849a739fc6d6a3c7` |
| `WORKSPACE` | `73cc4904e3701a70a0da99cc9cef0f6ad74311a9` |

新增 `GetBlockAsyncCoro` 模式的 benchmark 源文件 SHA-256 为
`d7c4aef1550a9613c9f382c63438b65a6b901e2710eff62011399d4dbd10e151`。测试机仍使用上述隔离源码，
只同步替换该 benchmark 源文件后重新构建，没有修改被测文件系统实现。

## 3. 测试环境

测试日期：2026-09-10。

| 项目 | 配置 |
| --- | --- |
| 目标机 | `6.165.148.165` |
| Hostname | `A03-R40-I148-165-713001K.JD.LOCAL` |
| Kernel | `6.6.0-100.jd_b001.x86_64` |
| CPU | 384 logical CPUs，测试绑定 CPU `1-32` |
| 容器 | `ha3_dev` |
| 编译器 | GCC 10.2.1 |
| Bazel | 5.2.0 |
| 文件系统 | ext4，挂载参数 `rw,relatime,stripe=8` |
| 逻辑卷 | `/dev/mapper/docker-striped`，500 GiB，2 stripes，stripe size 16 KiB |
| 物理设备 | `/dev/nvme0n1p7` 和 `/dev/nvme1n1p2` |
| 测试文件 | `/export/data/havenask/test.dat` |
| 文件大小 | 10 GiB（10737418240 bytes） |
| inode | `14181398` |

宿主机 `/export/suez_data` 以读写 bind mount 映射到容器 `/export/data`。测试没有覆盖或重新生成已有的 10 GiB 文件。

## 4. 协程与异步 I/O 开关

构建命令：

```bash
bazel build \
  //aios/storage/indexlib/file_system/file/test:block_file_read_async_coro_benchmark \
  --define=use_coro=yes \
  --copt=-O2
```

仓库 `.bazelrc` 已默认包含：

```text
build --define indexlib_coroutine=yes
build --action_env=BAZEL_CXXOPTS="-fcoroutines"
build --define=fslib_use_async=yes
```

构建后确认生成的 `bazel-bin/aios/future_lite/future_lite/Config.h` 包含：

```c
#define FUTURE_LITE_USE_COROUTINES 1
```

运行时设置：

```bash
FSLIB_LOCAL_ASYNC_CORO_READ=1
```

benchmark 通过 `.via(&executor)` 将顶层协程绑定到 `future_lite::executors::SimpleExecutor`。这一步不可省略：如果当前协程没有 executor，`FslibCommonFileWrapper::PReadVAsync` 会退回同步 `preadv`。

## 5. 测试方法

- 接口：`BlockFileNode::ReadAsyncCoro`、`BlockFileAccessor::GetBlockAsyncCoro`
- I/O 模式：随机读、4 KiB、Direct I/O
- BlockCache：容量为 0，确保每次读取都产生底层 I/O
- 文件对象：一个 `BlockFileNode`，对应一个文件 wrapper
- Executor：一个 `SimpleExecutor`，32 个调度线程
- 并发度：1、32、320 个协程
- CPU affinity：`taskset -c 1-32`
- 每轮预热：5 秒
- 每轮采样：30 秒
- 每档重复：3 次
- 运行顺序：`1/32/320`、`320/32/1`、`32/1/320`，用于降低顺序漂移影响
- 随机偏移：固定种子，每次读取按 4 KiB 对齐
- 延迟：围绕每次被测接口调用测量端到端完成时间；`GetBlockAsyncCoro` 包含 `BlockHandle` 释放
- 错误检查：分别统计 coroutine exception、I/O error 和 short read

benchmark 使用 `--interface read|get-block` 选择接口，默认值为 `read`。两种模式复用相同的文件、随机数生成器、
协程调度、并发度和统计逻辑；`get-block` 模式不执行 `ReadAsyncCoro` 中将 block 数据复制到调用方 buffer 的步骤。

示例运行命令：

```bash
FSLIB_LOCAL_ASYNC_CORO_READ=1 taskset -c 1-32 \
  bazel-bin/aios/storage/indexlib/file_system/file/test/block_file_read_async_coro_benchmark \
  --file /export/data/havenask/test.dat \
  --interface get-block \
  --concurrency 32 \
  --executor-threads 32 \
  --block-size 4096 \
  --warmup-seconds 5 \
  --duration-seconds 30 \
  --seed 20260910
```

程序最后输出一行以 `RESULT_JSON` 开头的机器可读结果。

## 6. 测试结果

### 6.1 `BlockFileNode::ReadAsyncCoro`

下表为原测试每档 3 次的中位数：

| 协程并发 | IOPS | MiB/s | 平均延迟 | P50 | P95 | P99 | P99.9 | IOPS CV |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 13,897 | 54.287 | 71.910 us | 69.876 us | 79.367 us | 85.687 us | 92.835 us | 1.22% |
| 32 | 17,014 | 66.461 | 1.881 ms | 1.881 ms | 1.924 ms | 1.943 ms | 1.972 ms | 0.38% |
| 320 | 17,288 | 67.531 | 18.502 ms | 18.516 ms | 19.093 ms | 19.340 ms | 19.619 ms | 0.21% |

原始 IOPS：

| 协程并发 | 第 1 轮 | 第 2 轮 | 第 3 轮 |
| ---: | ---: | ---: | ---: |
| 1 | 14,215.704 | 13,824.400 | 13,897.494 |
| 32 | 17,142.421 | 16,998.821 | 17,014.082 |
| 320 | 17,214.291 | 17,287.900 | 17,293.996 |

所有 9 轮结果均为：

```text
errors=0
short_reads=0
```

### 6.2 `BlockFileAccessor::GetBlockAsyncCoro`

下表为新增测试每档 3 次的中位数：

| 协程并发 | IOPS | MiB/s | 平均延迟 | P50 | P95 | P99 | P99.9 | IOPS CV |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 14,202 | 55.476 | 70.368 us | 68.818 us | 78.835 us | 85.451 us | 92.461 us | 0.98% |
| 32 | 17,123 | 66.886 | 1.869 ms | 1.868 ms | 1.914 ms | 1.934 ms | 1.967 ms | 0.15% |
| 320 | 17,286 | 67.525 | 18.504 ms | 18.511 ms | 18.909 ms | 19.082 ms | 19.294 ms | 0.17% |

原始 IOPS：

| 协程并发 | 第 1 轮 | 第 2 轮 | 第 3 轮 |
| ---: | ---: | ---: | ---: |
| 1 | 14,201.794 | 14,309.679 | 13,975.747 |
| 32 | 17,129.028 | 17,072.919 | 17,122.825 |
| 320 | 17,286.344 | 17,325.562 | 17,253.440 |

所有 9 轮均为 `errors=0`、`short_reads=0`。

### 6.3 接口对比

| 协程并发 | `ReadAsyncCoro` IOPS | `GetBlockAsyncCoro` IOPS | 差异 |
| ---: | ---: | ---: | ---: |
| 1 | 13,897 | 14,202 | +2.19% |
| 32 | 17,014 | 17,123 | +0.64% |
| 320 | 17,288 | 17,286 | -0.01% |

两组测试在同一天、同一机器和同一文件上运行，但没有逐轮交错执行。因此小于约 2% 的差异应视为运行波动，
不能据此断言两个接口存在稳定的吞吐差距。

## 7. 资源开销

资源数据来自额外短时运行期间的 `pidstat -t -u -w` 采样：

`ReadAsyncCoro`：

| 协程并发 | 用户态 CPU | 内核态 CPU | 总 CPU | voluntary cs/s | involuntary cs/s |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 100.4% | 17.5% | 117.9% | 68,164 | 0.3 |
| 32 | 95.8% | 27.6% | 123.4% | 106,005 | 0.4 |
| 320 | 84.2% | 59.9% | 144.1% | 254,504 | 0.5 |

`GetBlockAsyncCoro`：

| 协程并发 | 用户态 CPU | 内核态 CPU | 总 CPU | voluntary cs/s | involuntary cs/s |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 99.6% | 17.3% | 116.9% | 60,994 | 0.2 |
| 32 | 93.6% | 26.1% | 119.6% | 108,125 | 0.3 |
| 320 | 77.2% | 65.1% | 142.3% | 265,341 | 0.7 |

`/proc/<pid>/fdinfo` 中测试文件描述符 flags 为八进制 `0140000`，其中包含 `O_DIRECT`。

## 8. 结论

1. `ReadAsyncCoro` 和 `GetBlockAsyncCoro` 在并发 32 时都达到约 17.1K IOPS，并发继续增加到 320 没有明显吞吐收益。
2. 两个接口的吞吐差异处于约 -0.01% 到 +2.19% 范围，和本次运行波动接近；省略一次 4 KiB 内存复制没有改变整体瓶颈。
3. 并发从 32 增加到 320 时，两个接口的 P50 都从约 1.87 ms 增长到约 18.5 ms，说明约 17K IOPS 后主要增加排队延迟。
4. `GetBlockAsyncCoro` 的 cache miss 路径通过 `PReadAsync`/`submitIO`，`ReadAsyncCoro` 单 block 路径通过 `PReadVAsync`/`submitIOV`；两者在本测试条件下表现相当。
5. 并发升高时系统态 CPU 和线程上下文切换显著增加。1.2.0 使用的后端是 `SimpleExecutor` 中的 POSIX AIO，不是 io_uring。
6. 本结果衡量的是完整 Havenask 文件系统调用链，包括 block 分配、缓存查询、handle 管理、数据复制和 coroutine 调度开销，不等同于裸设备 fio 上限。

## 9. 原始记录

测试机原始数据保存在：

```text
/export/suez_data/readasync-coro-perf-20260910-134625/
```

其中：

- `results.jsonl`：9 轮机器可读原始结果
- `summary.json`：聚合统计
- `c*-r*.log`：各轮 benchmark 输出
- `c*-r*.pidstat.log`：各轮进程资源数据
- `resource-profile-c*.pidstat-thread.log`：线程级资源数据
- `direct-io-verification.log`：Direct I/O 文件描述符验证
- `preflight.log`：机器与存储环境预检
- `build.log`：最终构建日志

新增 `GetBlockAsyncCoro` 数据保存在：

```text
/export/suez_data/getblock-coro-perf-20260910-153800/
```

其中：

- `results.jsonl`：9 轮机器可读原始结果
- `summary.json`：聚合统计
- `c*-r*.log`：各轮 benchmark 输出
- `c*-r*.pidstat.log`：各轮进程资源数据
- `resource-profile-c*.pidstat-thread.log`：线程级资源数据
- `preflight.log`：机器、存储、测试文件和 benchmark SHA-256
- `build.log`：新增模式的构建日志
- `runner.status`：完整性校验结果，`0` 表示 9 轮完成且无错误或短读

## 10. 仅调参数达到 200 万聚合 IOPS

### 10.1 结论与适用边界

在不修改 Havenask 业务代码和文件系统实现的前提下，两个接口都可以在本测试机上达到 200 万以上聚合 IOPS：

| 接口 | 30 秒三轮 IOPS | 中位数 | 中位 MiB/s | IOPS CV |
| --- | --- | ---: | ---: | ---: |
| `BlockFileNode::ReadAsyncCoro` | 2,137,637 / 2,098,256 / 2,075,916 | **2,098,256** | 8,196.306 | 1.21% |
| `BlockFileAccessor::GetBlockAsyncCoro` | 2,137,972 / 2,144,695 / 2,114,037 | **2,137,972** | 8,351.451 | 0.62% |

所有 6 轮均收齐 192/192 个进程的结果，且 `errors=0`、`short_reads=0`。

这个结论有一个重要边界：表中的 200 万是 **192 个 benchmark 进程、192 个 `SimpleExecutor`、192 个
`SimpleIOExecutor` 的整机聚合吞吐**，不是单进程或单 Havenask 实例的吞吐。每个进程各创建一个
`BlockFileNode`，但都读取同一个 10 GiB 测试文件。

单实例调参不能接近 200 万。固定协程并发为 64，把同一个 `SimpleExecutor` 的调度线程数从 1 依次增加到
2、4、8、32、64 后：

| 接口 | 最低 IOPS | 最高 IOPS | 最优调度线程数 |
| --- | ---: | ---: | ---: |
| `BlockFileNode::ReadAsyncCoro` | 17,151 | 17,418 | 8 |
| `BlockFileAccessor::GetBlockAsyncCoro` | 17,174 | 17,357 | 8 |

原因是 Havenask 1.2.0 的每个 `SimpleExecutor` 无论配置多少调度线程，内部都只有一个
`SimpleIOExecutor`。增加 `--executor-threads` 只增加协程调度线程，不会增加 POSIX AIO poller。若要求单进程
达到 200 万，则需要修改 executor/I/O backend 架构，不再属于“仅调参数”的范围。

### 10.2 最终参数

| 参数 | 最终值 |
| --- | --- |
| 进程数 | 192 |
| 每进程 `SimpleExecutor` 调度线程 | 1 |
| 每进程协程并发 | 4 |
| 总在途协程请求 | 768 |
| CPU affinity | 每个进程绑定一个物理核的两个 SMT sibling，覆盖 192 个物理核 |
| 读取模式 | 4 KiB 随机 Direct I/O |
| BlockCache | 0 bytes |
| 测试文件 | `/export/data/havenask/test.dat`，10 GiB，所有进程共享 |
| 预热 / 采样 | 5 秒 / 30 秒 |
| 运行时开关 | `FSLIB_LOCAL_ASYNC_CORO_READ=1` |
| 构建参数 | `--define=use_coro=yes --copt=-O2` |

调参过程中，使用共享 CPU affinity 的 96 进程、每进程并发 32 只能达到约 112 万至 119 万 IOPS。
改为按物理核隔离后，每进程并发 4 优于并发 32；随着进程数增加，144、160、168、172、192 进程的吞吐
大致从 180 万、195 万、201 万、205 万增长到 210 万以上。继续提高单进程并发只会增加协程调度和排队开销。

每个进程的等价运行命令为：

```bash
FSLIB_LOCAL_ASYNC_CORO_READ=1 taskset -c <physical-core>,<smt-sibling> \
  bazel-bin/aios/storage/indexlib/file_system/file/test/block_file_read_async_coro_benchmark \
  --file /export/data/havenask/test.dat \
  --interface read \
  --concurrency 4 \
  --executor-threads 1 \
  --block-size 4096 \
  --warmup-seconds 5 \
  --duration-seconds 30 \
  --seed <per-process-seed>
```

将 `--interface read` 改为 `--interface get-block` 即测试 `GetBlockAsyncCoro`。192 个进程在同一目标时间启动，
每个进程使用不同随机种子；两组接口按 `read/get-block` 交错执行 3 轮。

### 10.3 延迟与设备侧验证

下表中的延迟是每轮 192 个进程各自统计值的中位数，不是把所有请求合并后重新计算的全局分位数：

| 接口 | 轮次 | 平均延迟中位数 | P50 中位数 | P95 中位数 | P99 中位数 | P99.9 中位数 | 单进程 P99 最大值 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `ReadAsyncCoro` | 1 | 356.132 us | 344.666 us | 442.324 us | 506.984 us | 593.928 us | 5,986.871 us |
| `ReadAsyncCoro` | 2 | 366.886 us | 348.028 us | 489.623 us | 581.415 us | 1,355.904 us | 3,001.062 us |
| `ReadAsyncCoro` | 3 | 364.343 us | 347.712 us | 481.879 us | 564.408 us | 677.776 us | 4,675.358 us |
| `GetBlockAsyncCoro` | 1 | 356.591 us | 343.493 us | 447.118 us | 516.603 us | 670.511 us | 3,044.523 us |
| `GetBlockAsyncCoro` | 2 | 358.923 us | 345.865 us | 457.001 us | 526.725 us | 626.136 us | 2,994.398 us |
| `GetBlockAsyncCoro` | 3 | 359.787 us | 344.303 us | 462.029 us | 541.299 us | 1,216.690 us | 6,025.936 us |

设备上限和应用计数经过两种方式交叉验证：

- 同一文件上的 host fio，4 KiB 随机 Direct I/O、`libaio`、32 jobs × QD32，达到 **4,378,654 IOPS**；设备能力高于目标。
- 额外 15 秒 `iostat` 运行中，应用分别报告 2,143,862 和 2,150,047 IOPS；`dm-0` 同期约为
  213 万至 217 万 r/s，两块 NVMe 各约 106 万至 109 万 r/s。应用结果求和与块设备观测一致。

因此，200 万结果不是 page cache、BlockCache 或统计口径造成的虚高；同时设备 `%util` 已接近 100%，最终配置
基本使用了整台 192 物理核测试机和两块 NVMe。该配置约产生 768 个 benchmark 线程，不适合作为单实例生产能力
结论，只能证明“在不改代码时，可以通过扩展进程和 I/O poller 数量达到 200 万整机聚合 IOPS”。

### 10.4 本轮原始记录

本轮参数扫描、最终三轮结果、fio 和 `iostat` 原始数据保存在：

```text
/export/suez_data/native-coro-2m-tuning-20260910-163000/
```

关键文件：

- `final-summary.json`：最终两接口三轮聚合结果和 CV
- `final-validation.log`：最终交错长测输出
- `final-*-c4-p192-w5-d30-r*/`：每轮 192 个进程的原始日志和汇总
- `single-executor-sweep.jsonl`：单实例调度线程数扫描
- `partitioned-target-sweep.log`、`partitioned-target-margin-sweep.log`：进程数扫描
- `partitioned-concurrency-sweep.log`：每进程协程并发扫描
- `fio-32x32.json`：同文件 fio 设备上限
- `iostat-read.log`、`iostat-get-block.log`：设备侧逐秒 IOPS
- `run_partitioned_stage.sh`：按物理核分区的多进程运行脚本
