# NOZ

> **NGINX 的安全边界与整数尺寸运算实验**
>
> NOZ 基于 NGINX 1.31.9 源码，重点探索一条贯穿内存分配、容器扩容、HTTP 响应编码与 HTTP/3/QPACK 编码路径的改造主线：**在尺寸进入分配或指针运算之前，先做可检查的加法、乘法与对齐计算；遇到不可表示或超出约定范围的尺寸时，明确失败，而不是让整数回绕继续流入后续逻辑。**

[![NGINX](https://img.shields.io/badge/based%20on-NGINX%201.31.9-009639)](https://nginx.org/)
[![License: BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue.svg)](LICENSE)

## 项目在做什么

NOZ 保留 NGINX 的整体服务器与模块架构，并在核心尺寸计算路径加入私有的整数安全算术辅助层，再把它用于多个真实调用点。配套测试不只检查辅助函数本身，也覆盖了 HTTP 头部和 HTTP/3 编码相关的边界，以及分配失败时的响应行为。

这不是一份“已全面加固”或“已通过完整安全审计”的声明。它是一个围绕源码改造、边界条件验证和性能对照展开的工程实验；具体行为、兼容性与安全性仍需结合构建选项和实际测试结果判断。

## 主要改造与看点

### 1. 私有的 checked-size 算术层

新增 `src/core/private/include/ngx_math.h`，为 NGINX 内部提供轻量、无外部库依赖的尺寸运算工具：

- **尺寸有效性检查**：预留 `NGX_MAX_SIZE_T_VALUE` 作为不可分配的溢出哨兵值，避免把失败结果误当作正常尺寸。
- **checked add / multiply**：提供 `ngx_priv_math_add_size()`、`ngx_priv_math_mul_size()` 和 `ngx_priv_math_add_uint()`；出错时返回 `NGX_ERROR`，并写入哨兵值。
- **双实现路径**：支持编译器溢出内建函数；不可用或通过 `NGX_PRIV_MATH_NO_BUILTIN` 禁用时，回退到可移植的边界检查实现。
- **饱和累计**：提供饱和加法、乘法，以及累计 2–5 个尺寸的辅助函数，适合编码前预估多个字段或片段的总长度。
- **安全对齐**：验证对齐值为非零的 2 的幂，并在向上取整前检查加法溢出。

### 2. 把检查接入实际内存与容器路径

这套运算不是孤立的工具头文件：它已用于若干核心分配和尺寸计算入口，例如：

- `src/core/ngx_palloc.c`：检查内存池创建、普通分配和对齐分配的尺寸；超出可接受范围时在进入分配路径前失败。
- `src/core/ngx_array.h`、`src/core/ngx_array.c`：数组初始化、批量扩容、元素偏移和容量增长涉及的乘法、加法先行校验。
- `src/core/ngx_list.h`、`src/core/ngx_list.c`：列表初始化、分块分配和元素偏移计算做尺寸检查。
- `src/core/ngx_buf.c`：创建多缓冲区链时先检查缓冲区数量与单个缓冲区大小的乘积。
- `src/core/ngx_thread_pool.c`：分配线程任务时检查任务结构与附加上下文的总大小。
- `src/http/ngx_http_request_body.c`：请求体缓冲区尺寸增长使用饱和加法，避免扩展尺寸计算发生回绕。

这些改动的重点是让“长度计算”在抵达 malloc/pool 分配、数组下标或编码器之前就有明确的成功/失败边界。

### 3. HTTP 响应头尺寸预估与失败收敛

在 `src/http/ngx_http_header_filter_module.c` 中，响应状态行、常见头字段、Location、Keep-Alive、Early Hints 和用户自定义头字段的长度累计改用安全累计函数，再按计算结果创建输出缓冲区。

当响应头输出缓冲区无法分配时，代码会走 `ngx_http_allocation_failed()`：清理当前响应头状态并尝试以 **503 Service Unavailable** 结束响应，同时关闭 keep-alive，避免在���分响应状态下继续沿用原来的头部输出流程。相关错误响应处理位于 `src/http/ngx_http_special_response.c`。

### 4. HTTP/3 / QPACK 编码长度边界

`src/http/v3/ngx_http_v3_encode.c` 中的字段行和字段区前缀编码器支持以 `p == NULL` 预估所需长度。各类 literal/indexed 字段编码的长度组合通过饱和尺寸累计处理；HTTP/3 响应头、Early Hints、数据帧和 trailers 的相关路径也使用安全累计来预估编码缓冲区大小。

因此，测试可以直接针对“仅计算长度”的路径传入极端尺寸，而不必真的构造同等大小的输入缓冲区。

### 5. 独立边界测试、协议回归与基准

`tests/` 目录里的材料围绕上述改造提供了几层验证：

- **`ngx_private_math_test.c`**：覆盖有效尺寸、加法/乘法溢出、饱和运算、累计与对齐；编译器支持 `__int128` 时，还会用参考计算对一组边界值两两比较。
- **`ngx_http_v3_size_test.c`**：链接 NGINX HTTP/3 编码对象，验证 QPACK 字段和字段区前缀的长度预估，并用超大长度检查边界返回值。
- **`ngx_http_safety_test_module.c`**：仅用于测试的 HTTP 模块，通过 `safety_test` 指令触发正常响应、模拟分配失败，以及 pool/array/list/thread/header 尺寸溢出场景。
- **`ngx_http_h3_safety_client.py`** 与 **`nginx-safety.conf`**：使用 aioquic 对 HTTP/3 测试端点发起请求，并核对预期状态码。
- **`ngx_private_math_bench.c`**：对照 checked arithmetic 的便携回退实现与编译器内建溢出路径，输出每轮耗时和每操作耗时。基准数据受编译器、优化级别和硬件影响，不应脱离测试环境解读。

测试模块和客户端是验证工具，不是生产功能模块；运行测试时应使用隔离的本地环境，并按自己的构建目录调整 `tests/nginx-safety.conf` 中的路径。

## 代码导航

| 路径 | 用途 |
| --- | --- |
| `src/core/private/include/ngx_math.h` | 私有尺寸算术与边界检查辅助函数 |
| `src/core/ngx_palloc.c`、`src/core/ngx_array.*`、`src/core/ngx_list.*` | 内存池、数组和列表中的尺寸检查接入点 |
| `src/core/ngx_buf.c`、`src/core/ngx_thread_pool.c` | 缓冲区链和线程任务的分配尺寸检查 |
| `src/http/ngx_http_header_filter_module.c` | HTTP 响应头与 Early Hints 长度预估 |
| `src/http/ngx_http_special_response.c` | 分配失败时的 HTTP 错误响应处理 |
| `src/http/v3/ngx_http_v3_encode.c`、`src/http/v3/ngx_http_v3_filter_module.c` | HTTP/3/QPACK 编码与长度预估 |
| `tests/` | 算术边界测试、HTTP/HTTP/3 回归测试材料及微基准 |
| `CHANGES` | 上游版本变更记录 |

## 构建

NOZ 使用 NGINX 的 `configure` 构建流程。编译选项和依赖取决于目标系统及需要启用的模块；可先查看可用选项，再配置和编译：

```sh
./configure --help
./configure
make


















有关第三方库、TLS、HTTP/3/QUIC 及其他模块的构建要求，请结合所选 configure 参数和 NGINX 官方构建文档确认。构建测试二进制、模块或运行回归脚本时，也请检查各测试文件中的依赖和路径设置。
使用与安全说明

    面向生产环境部署前，请自行审查源码改动、编译配置、依赖版本和运行配置。
    仓库中的安全测试代码用于本地回归验证；它不构成完整安全审计，也不应直接暴露在公网服务中。
    上游 NGINX 的安全更新与变更记录请持续关注，并根据需要将相关修复同步到本仓库。

上游资料

    NGINX 官方文档
    NGINX GitHub 仓库
    本仓库变更记录

贡献与反馈

欢迎通过 GitHub Issues 反馈构建问题、边界案例或测试结果。提交问题时请附上操作系统、编译器、configure 参数、复现步骤和实际输出。安全问题请避免在公开讨论中披露未修复的细节。
许可证

本仓库许可证见 LICENSE：BSD 2-Clause。
