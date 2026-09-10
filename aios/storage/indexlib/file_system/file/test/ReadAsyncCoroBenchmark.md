# Havenask 1.2.0 `ReadAsyncCoro` 性能测试报告

## 1. 测试目的

本测试用于测量 Havenask 1.2.0 文件系统中真正的协程异步读取路径：

```text
BlockFileNode::ReadAsyncCoro
  -> BlockFileAccessor::ReadAsyncCoro
  -> FslibCommonFileWrapper::PReadVAsync
  -> LocalDirectFile::preadv
  -> future_lite::SimpleIOExecutor::submitIOV
```

测试没有使用 `FileNode::ReadAsyncCoro` 的默认同步退化实现，也没有测试同步 `Read` 接口。

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

- 接口：`BlockFileNode::ReadAsyncCoro`
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
- 延迟：围绕每次 `ReadAsyncCoro` 调用测量端到端完成时间
- 错误检查：分别统计 coroutine exception、I/O error 和 short read

示例运行命令：

```bash
FSLIB_LOCAL_ASYNC_CORO_READ=1 taskset -c 1-32 \
  bazel-bin/aios/storage/indexlib/file_system/file/test/block_file_read_async_coro_benchmark \
  --file /export/data/havenask/test.dat \
  --concurrency 32 \
  --executor-threads 32 \
  --block-size 4096 \
  --warmup-seconds 5 \
  --duration-seconds 30 \
  --seed 20260910
```

程序最后输出一行以 `RESULT_JSON` 开头的机器可读结果。

## 6. 测试结果

下表为每档 3 次测试的中位数：

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

## 7. 资源开销

资源数据来自额外的 10 秒 `pidstat -t -u -w` 采样：

| 协程并发 | 用户态 CPU | 内核态 CPU | 总 CPU | voluntary cs/s | involuntary cs/s |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 100.4% | 17.5% | 117.9% | 68,164 | 0.3 |
| 32 | 95.8% | 27.6% | 123.4% | 106,005 | 0.4 |
| 320 | 84.2% | 59.9% | 144.1% | 254,504 | 0.5 |

`/proc/<pid>/fdinfo` 中测试文件描述符 flags 为八进制 `0140000`，其中包含 `O_DIRECT`。

## 8. 结论

1. `ReadAsyncCoro` 在并发 1 时约为 13.9K IOPS；并发提升至 32 后约为 17.0K IOPS，提升约 22.4%。
2. 并发从 32 提升至 320 后，吞吐仅继续提升约 1.6%，但 P50 从约 1.88 ms 增长到约 18.52 ms，接口已在约 17K IOPS 附近进入明显排队状态。
3. 并发升高时系统态 CPU 和线程上下文切换显著增加。
4. 1.2.0 使用的后端是 `SimpleExecutor` 中的 POSIX AIO，不是 io_uring。`SimpleIOExecutor` 使用单个轮询线程扫描受互斥锁保护的 outstanding I/O，并且每轮只处理一个完成项；测试结果与该软件路径成为瓶颈的现象一致，但本次没有采集函数级 profiling，不能仅凭吞吐数据断言唯一根因。
5. 本结果衡量的是完整 Havenask 文件系统调用链，包括 block 分配、缓存查询、数据复制和 coroutine 调度开销，不等同于裸设备 fio 上限。

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

