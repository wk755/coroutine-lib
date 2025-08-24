# coroutine-lib
# coroutine-lib

> 轻量级 **C++20 协程（coroutines）** 运行时与工具集：可在单线程/多线程环境中以 `co_await` 的方式编写异步代码，提供调度器（Scheduler）、事件循环（Reactor）、计时器（Timer）、通道（Channel）、任务（Task）与常用 I/O 适配器。目标是 **易用、零依赖（或极少依赖）、可嵌入**。

---

## ✨ 特性 Highlights

- **C++20 协程原生风格**：`Task<T>` / `Awaitable` / `co_await` / `co_spawn`
- **可插拔调度**：单线程事件循环 / 线程池调度 / 自定义执行器（Executor）
- **事件驱动 I/O**：Reactor（epoll/kqueue/IOCP 中择一实现），支持 **TCP 套接字**、**定时器**、**信号**（可选）
- **同步原语**：`Channel<T>`、`AsyncMutex`、`AsyncCondition`
- **无锁/低锁** 数据结构（根据平台与实现裁剪）
- **零侵入集成**：`add_subdirectory` 直接嵌入，或安装后 `find_package`
- **跨平台**：Linux（epoll）/ macOS（kqueue）/ Windows（可扩展 IOCP）
- **可观测**：调度跟踪日志（可选宏）、统计计数器与自定义导出点

---

## 🚀 TL;DR（两分钟跑起来）

```bash
# 1) 构建
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)

# 2) 运行例子（假设 examples 已随库提供）
./examples/hello_coroutine
./examples/echo_server 127.0.0.1 9000
```

作为项目依赖：

```cmake
# 根 CMakeLists.txt
add_subdirectory(external/coroutine-lib)
target_link_libraries(your_app PRIVATE coroutine_lib)
```

---

## 🧱 总体架构 Architecture

```text
+--------------------+        +--------------------+
|  Your Application  |        |  Coroutine Library |
|  (co_await APIs)   |        |                    |
+----------+---------+        +----------+---------+
           |                             |
           |   Task / Awaitable / go()   |
           v                             v
      +----+---------+             +-----+--------+
      |  Scheduler   +<----------->+  Reactor    |
      |  (Executor)  |   wakes     | (epoll/...) |
      +----+---------+             +-----+--------+
           |                             |
           | run & resume                | I/O readiness / timers
           v                             v
        Coroutine                  AsyncSocket / Timer / Channel
```

- **Scheduler**：保存可运行协程队列，负责 resume；可为单线程或基于线程池。
- **Reactor**：封装 epoll/kqueue/…，将 I/O/定时器事件投递给 Scheduler。
- **Primitives**：`AsyncSocket`、`Timer`、`Channel<T>`、`AsyncMutex` 等均提供 `co_await` 接口。

---

## 🧪 示例 Examples

### 1) Hello coroutine（sleep + 输出）

```cpp
#include <coroutine_lib/task.h>
#include <coroutine_lib/scheduler.h>
#include <coroutine_lib/timer.h>
#include <cstdio>

using namespace coro;

Task<void> demo() {
  printf("A: start\n");
  co_await sleep_for(std::chrono::milliseconds(500));
  printf("A: after 500ms\n");
}

int main() {
  Scheduler sch;               // 单线程调度器
  sch.start();                 // 可选：显式启动事件循环
  co_spawn(sch, demo());       // 将任务投递给调度器
  sch.run();                   // 进入循环，直到所有任务完成或显式 stop()
}
```

### 2) Echo Server（TCP）

```cpp
#include <coroutine_lib/task.h>
#include <coroutine_lib/net/async_socket.h>
#include <coroutine_lib/net/async_acceptor.h>
#include <coroutine_lib/scheduler.h>
#include <array>

using namespace coro::net;

coro::Task<void> handle_client(AsyncSocket sock) {
  std::array<char, 4096> buf{};
  for (;;) {
    auto n = co_await sock.async_read(buf.data(), buf.size());
    if (n <= 0) break;
    co_await sock.async_write(buf.data(), (size_t)n);
  }
}

coro::Task<void> serve(uint16_t port) {
  AsyncAcceptor acc({"0.0.0.0", port});
  for (;;) {
    auto sock = co_await acc.async_accept();
    co_spawn(co_awaiting_scheduler(), handle_client(std::move(sock)));
  }
}

int main(int argc, char** argv) {
  uint16_t port = argc > 1 ? static_cast<uint16_t>(std::atoi(argv[1])) : 9000;
  coro::Scheduler sch;
  co_spawn(sch, serve(port));
  sch.run();
}
```

### 3) Channel（生产者/消费者）

```cpp
#include <coroutine_lib/channel.h>
#include <coroutine_lib/scheduler.h>
#include <iostream>

coro::Task<void> producer(coro::Channel<int>& ch) {
  for (int i = 1; i <= 5; ++i) {
    co_await ch.send(i);
  }
  ch.close();
  co_return;
}

coro::Task<void> consumer(coro::Channel<int>& ch) {
  while (auto v = co_await ch.recv()) {
    std::cout << "got " << *v << "\n";
  }
}

int main() {
  coro::Scheduler sch;
  coro::Channel<int> ch(2); // 容量 2
  co_spawn(sch, producer(ch));
  co_spawn(sch, consumer(ch));
  sch.run();
}
```

## 🔩 构建与集成

### 依赖选项（按实现选择）
- **必需**：C++20 编译器、CMake ≥ 3.16
- **可选**：`epoll`（Linux）、`kqueue`（macOS/FreeBSD）、`io_uring`/`IOCP`
- **日志**：可内置或接入 `spdlog`（示例）

### CMake 选项（示例）

```cmake
option(CORO_WITH_NET "Enable async net (epoll/kqueue)" ON)
option(CORO_WITH_TESTS "Build unit tests" ON)
option(CORO_WITH_EXAMPLES "Build examples" ON)
```

安装：
```bash
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
cmake --build . --target install
# find_package(coroutine-lib CONFIG REQUIRED)
```

---

## ⚡ 性能与压测（wrk）

本节给出 **三种实现** 的压测流程与结果模板：
- **Fiber 版本**（基于自研协程调度 + Reactor）
- **原生 epoll 版本**（直接事件循环 + 非阻塞 I/O）
- **libev 版本**（或你项目中的第三方事件库实现）


## 1) 服务器启动（3 个实现分别跑）

```bash
# Fiber
./http_echo_fiber 0.0.0.0 8080

# epoll
./http_echo_epoll 0.0.0.0 8888

# libevent
./http_echo_libevent 0.0.0.0 8080
```

---

## 2) 压测命令

```bash
# 每个实现各执行一轮 100k 请求、1000 并发
ab -n 100000 -c 1000 http://127.0.0.1:8080/  # fiber/libevent 示例
ab -n 100000 -c 1000 http://127.0.0.1:8888/  # epoll 示例
```

---

## 3) 结果（实测）

| 实现                   | 端口 | 并发 | 完成请求 | 总时长 (s) | RPS (Requests/sec) | Time/request (ms) | 失败 |
|------------------------|------|------|----------|------------|--------------------|-------------------|------|
| **epoll**              | 8888 | 1000 | 100,000  | **6.346**  | **15,758.69**      | **63.457**        | 0    |
| **libevent（优化后）** | 8080 | 1000 | 100,000  | 6.481      | 15,429.05          | 64.813            | 0    |
| **fiber**              | 8080 | 1000 | 100,000  | 9.099      | 10,990.52          | 90.988            | 0    |
| libevent（非阻塞初测） | 8080 | 1000 | 100,000  | 9.129      | 10,954.50          | 91.287            | 0    |

> 典型分位（来自 ab 输出）：  
> - epoll：P50≈60ms，P95≈76ms，P99≈96ms  
> - libevent（优化后）：P50≈63ms，P95≈80ms，P99≈91ms

---

## 4) 结论与解读

- 在同样 **100k 请求 / 1k 并发** 下：**epoll ≈ 15.76k RPS > libevent（优化后）≈ 15.43k RPS > fiber ≈ 10.99k RPS**。  
- libevent 经过优化后与 epoll 的吞吐与尾延迟**非常接近**；fiber 版本在该场景下**略逊**（可能受协程切换、调度策略或默认参数影响）。  
- 三者均 **0 失败**，吞吐与延迟稳定，适合写入 README/简历作为对比亮点。

---

---

## 🔭 可观测性 Observability

- **日志级别**：`CORO_LOG_LEVEL={trace,debug,info,warn,error}`（或编译期开关）
- **运行统计**：调度队列长度、协程数、触发次数（可通过回调导出）
- **集成建议**：Prometheus 文本导出（/metrics）或事件回调接入 tracing

---

## 🗺️ Roadmap

- [ ] Windows IOCP 支持
- [ ] `io_uring` 后端（Linux）
- [ ] TLS（OpenSSL/BoringSSL）异步适配器
- [ ] 更丰富的同步原语：`AsyncSemaphore`、`Barrier`
- [ ] 优化分配器（自定义 `await_suspend`/`Allocator`）
- [ ] 更完善的定时器轮（Hierarchical Timing Wheel）

---

## 🧠 FAQ

- **协程泄漏如何避免？**  
  使用 `Task<T>` + `co_spawn` 的所有任务都应被某个 `Scheduler` 接管；确保 `run()` 退出前已 `stop()` 或等待任务完成。

- **和 `std::async` 有何区别？**  
  `std::async` 基于线程池 + future；协程基于状态机重写，**零额外线程**，更适合大量并发 I/O。

- **可以只用 Channel/Timer，而不用网络模块吗？**  
  可以。组件均为可选，按需裁剪。

---


