# WFPostgres: 基于 C++ Workflow 的异步 PostgreSQL 客户端

<p align="left">
  <img src="https://img.shields.io/badge/license-Apache%202.0-blue.svg" alt="License" />
  <img src="https://img.shields.io/badge/C%2B%2B-11-blue.svg" alt="C++" />
  <img src="https://img.shields.io/badge/PostgreSQL-13%20%7E%2018-blue.svg" alt="PostgreSQL" />
  <img src="https://img.shields.io/badge/build-Xmake%20%7C%20CMake-brightgreen.svg" alt="Build" />
</p>

[English](README.md) | [中文说明](README_zh.md)

`WFPostgres` 是专为 [Sogou Workflow](https://github.com/sogou/workflow) 打造的异步 PostgreSQL 客户端协议插件。原生实现 PostgreSQL 前后端协议（支持 v3.0 与带协商回退的 v3.2），**无任何 `libpq` 依赖**。

---

## 核心特性

- ✨ **全异步架构**：深度集成 Workflow 任务流模型（`WFComplexClientTask`）、连接池与事件循环。
- 🔒 **生产级安全保障**：
  - 原生 PostgreSQL STARTTLS 协议升级机制（`postgress://` 与 `postgresqls://` 协议头）。
  - 严格的 TLS 校验模式（`verify-ca`, `verify-full`）与自定义 CA 根证书（`sslrootcert`）。
  - 全认证机制支持：Trust、SCRAM-SHA-256 (RFC 5802/7677)、MD5 以及明文密码。
- 🚀 **高级协议支持与完备类型系统**：
  - 支持简单查询与扩展查询协议（`Parse` -> `Bind` -> `Describe` -> `Execute` -> `Sync`）。
  - 类型安全参数绑定（`bind_params`），自动推导类型 OID 并防御 SQL 注入。
  - `PostgresValue` 与 `PostgresCell` 对称完整的 PostgreSQL 类型矩阵（整型、浮点、数值型、字符串、二进制 BYTEA、UUID、布尔、OID、日期时间、JSON/JSONB、一维数组）。
  - 高性能 `PostgresResultCursor` 游标，支持零拷贝访问与响应数据缓冲区所有权转移。
  - 原生 COPY 流式批量传输子协议支持。
  - 异步 LISTEN / NOTIFY 事件发布与订阅机制。
  - 带外 CancelRequest 取消正在执行的查询（兼容传统 4 字节与 Protocol 3.2 32 字节密钥）。
- 🛡️ **事务安全与重连保护（Reconnect Guard）**：
  - 通过 `WFPostgresConnection` 实现连接级别绑定（Fixed Connection）。
  - Reconnect Guard 拦截异常断开，杜绝连接池复用导致的事务状态污染与串话。
  - 事务自愈机制：事务异常（'E' 状态）无条件触发 `keep_alive = 0` 关闭并释放底层连接，杜绝脏连接回池。
- 🚦 **统一错误模型**：
  - `PostgresStatus` 统一合并网络传输错误与数据库服务端 SQLSTATE 错误，通过 `status.ok()` 统一判断。
- 🧪 **多版本严格验证**：
  - 覆盖 PostgreSQL 13、14、15、16 与 18 版本兼容测试。
  - 在 AddressSanitizer (ASan) 与 LeakSanitizer (LSan) 下运行无内存泄漏与越界。

---

## 模块结构

```
wf-postgres/
├── include/                     # 公共对外 API 头文件
│   ├── WFPostgresClient.h       # 统一顶层门面与全局快捷工厂函数
│   ├── PostgresStatus.h         # 统一结果状态（合并网络与 SQL 错误）
│   ├── PostgresValue.h          # 类型安全参数推导与 bind_params()
│   ├── PostgresTask.h           # 任务工厂与回调声明
│   ├── WFPostgresConnection.h   # 事务与固定连接管理
│   ├── PostgresRequest.h        # 请求构造（简单查询与变参绑定扩展查询）
│   ├── PostgresResponse.h       # 响应元数据与包状态
│   ├── PostgresResult.h         # PostgresResultCursor（零拷贝与所有权转移游标）
│   ├── PostgresTypes.h          # PostgreSQL 协议 OID 及枚举
│   └── WFPostgresError.h        # 专用错误码定义
├── src/
│   ├── auth/                    # SCRAM-SHA-256 与 MD5 状态机
│   ├── protocol/                # 协议数据包编解码、流分帧与事件解析器
│   ├── factory/                 # ComplexPostgresTask 生命周期与工厂实现
│   └── client/                  # WFPostgresConnection 连接池管理与重连保护
├── tutorial/                    # 命令行 CLI 与事务实战示例
├── test/                        # 单元测试、架构测试、模糊测试与集成测试
├── CMakeLists.txt               # 标准 CMake 构建定义
└── xmake.lua                    # 原生 Xmake 构建定义
```

---

## 构建与集成

### 依赖环境

- 支持 C++11 的编译器（GCC >= 4.8.5, Clang 或 MSVC）
- [Sogou Workflow](https://github.com/sogou/workflow)
- OpenSSL (libssl 与 libcrypto)

### 1. 使用 Xmake 项目集成（推荐）

在您的 `xmake.lua` 中直接引用：

```lua
-- xmake.lua
package("wf_postgres")
    add_deps("workflow", "openssl")
    add_urls("https://github.com/tenire/wf-postgres.git")
    on_install("linux", "macosx", function (package)
        import("package.tools.xmake").install(package)
    end)
package_end()

add_requires("wf_postgres")

target("my_app")
    set_kind("binary")
    add_files("src/*.cc")
    add_packages("wf_postgres")
```

### 2. 使用 CMake 项目集成

#### 方式 A：子目录包含（add_subdirectory）

```cmake
add_subdirectory(wf-postgres)
target_link_libraries(my_app PRIVATE wfpg::wf_postgres)
```

#### 方式 B：链接预编译静态库

```cmake
find_package(OpenSSL REQUIRED)

target_include_directories(my_app PRIVATE /path/to/wf-postgres/include)
target_link_libraries(my_app PRIVATE 
    /path/to/wf-postgres/build/libwf_postgres.a 
    Workflow::workflow 
    OpenSSL::SSL 
    OpenSSL::Crypto 
    pthread
)
```

#### 方式 C：源码直接引入

直接将 `include/` 与 `src/` 复制到您的工程源码树中，仅需链接 Workflow 和 OpenSSL 即可。

### 3. 本地编译与测试

```bash
# 使用 Xmake 编译静态库并运行 CLI 示例
xmake
xmake run tutorial-01-postgres-cli

# 运行单元测试与架构验证测试
xmake f --tests=true
xmake
xmake run test_abstractions

# 对实际 PostgreSQL 实例运行集成测试
xmake run test_integration postgres://user:password@127.0.0.1:5432/testdb

# 使用 CMake 构建
mkdir -p build && cd build
cmake .. -DBUILD_TUTORIAL=ON
make -j$(nproc)
```

---

## 使用示例

### 1. 基础查询与统一错误处理

引入 `WFPostgresClient.h`，并通过 `PostgresStatus` 统一判断请求结果：

```cpp
#include <iostream>
#include "workflow/WFFacilities.h"
#include "WFPostgresClient.h"

int main() {
    WFFacilities::WaitGroup wg(1);
    std::string url = "postgres://user:password@127.0.0.1:5432/dbname";

    auto *task = create_postgres_task(url, 0, [&wg](WFPostgresTask *t) {
        PostgresStatus status = PostgresStatus::from_task(t);
        if (!status.ok()) {
            std::cerr << "Query failed: " << status.to_string() << "\n";
            wg.done();
            return;
        }

        PostgresResultCursor cursor(t->get_resp());
        std::vector<std::vector<PostgresCell>> rows;
        if (cursor.fetch_all(rows)) {
            std::cout << "Fetched " << rows.size() << " rows successfully.\n";
        }
        wg.done();
    });

    task->get_req()->set_query("SELECT version();");
    task->start();
    wg.wait();
    return 0;
}
```

### 2. 扩展查询与类型安全参数绑定

使用 `bind_params(...)`，支持自动类型推导和精准的 PostgreSQL 类型 OID 绑定，防止 SQL 注入：

```cpp
#include <iostream>
#include "workflow/WFFacilities.h"
#include "WFPostgresClient.h"

int main() {
    WFFacilities::WaitGroup wg(1);
    std::string url = "postgres://user:password@127.0.0.1:5432/dbname";

    auto *task = create_postgres_task(url, 0, [&wg](WFPostgresTask *task) {
        PostgresStatus status = PostgresStatus::from_task(task);
        if (!status.ok()) {
            std::cerr << "Query failed: " << status.to_string() << "\n";
            wg.done();
            return;
        }

        PostgresResultCursor cursor(task->get_resp());
        std::map<std::string, PostgresCell> row;
        while (cursor.fetch_row(row)) {
            std::cout << "User: " << row["username"].as_string()
                      << ", Score: " << row["score"].as_double() << "\n";
        }
        wg.done();
    });

    // 支持整型、浮点、字符串、布尔、时间戳、数组及 nullptr
    task->get_req()->set_query(
        "SELECT username, score FROM users WHERE id = $1 AND is_active = $2;",
        wfpg::bind_params(42, true)
    );

    task->start();
    wg.wait();
    return 0;
}
```

### 3. 事务与固定连接（防串话保护）

多步 SQL 顺序绑定至同一 TCP Socket，自动享受 Reconnect Guard 保护与脏连接淘汰：

```cpp
#include <iostream>
#include "workflow/WFFacilities.h"
#include "WFPostgresClient.h"

int main() {
    WFFacilities::WaitGroup wg(1);
    std::string url = "postgres://user:password@127.0.0.1:5432/dbname";

    WFPostgresConnection conn(1); // 唯一连接标识符
    conn.init(url);

    auto cb = [&wg, &conn](WFPostgresTask *t) {
        PostgresStatus status = PostgresStatus::from_task(t);
        if (!status.ok()) {
            std::cerr << "Transaction task failed: " << status.to_string() << "\n";
        } else {
            std::cout << "Transaction task success.\n";
        }
        wg.done();
    };

    // 连续任务安全地顺序运行在同一个固定连接上
    auto *begin_task  = conn.create_query_task("BEGIN;", nullptr);
    auto *insert_task = conn.create_query_task("INSERT INTO tbl(id, val) VALUES ($1, $2);", nullptr, 100, "sample");
    auto *commit_task = conn.create_query_task("COMMIT;", cb);

    workflow::series_of(begin_task)->push_back(insert_task);
    workflow::series_of(begin_task)->push_back(commit_task);
    
    begin_task->start();
    wg.wait();

    // 优雅断开连接
    WFFacilities::WaitGroup disc_wg(1);
    auto *disc_task = conn.create_disconnect_task([&disc_wg](WFPostgresTask *t) {
        disc_wg.done();
    });
    disc_task->start();
    disc_wg.wait();

    conn.deinit();
    return 0;
}
```

### 4. 异步 LISTEN / NOTIFY

```cpp
#include <iostream>
#include "workflow/WFFacilities.h"
#include "WFPostgresClient.h"

int main() {
    WFFacilities::WaitGroup wg(1);
    std::string url = "postgres://user:password@127.0.0.1:5432/dbname";

    WFPostgresConnection conn(1);
    conn.init(url);

    auto *listen_task = conn.create_query_task("LISTEN my_channel;", nullptr);
    auto *notify_wait_task = conn.create_notify_task([](WFPostgresTask *t) {
        PostgresStatus status = PostgresStatus::from_task(t);
        if (status.ok()) {
            auto notifies = t->get_resp()->get_notifications();
            for (const auto& n : notifies) {
                std::cout << "Received Notification: channel=" << n.channel 
                          << " payload=" << n.payload << "\n";
            }
        }
    });

    workflow::series_of(listen_task)->push_back(notify_wait_task);
    listen_task->start();
    
    wg.wait();
    conn.deinit();
    return 0;
}
```

---

## 完整实战教程

更详细的可运行教程位于 [`tutorial/`](tutorial/) 目录：
- [`tutorial-01-postgres-cli.cc`](tutorial/tutorial-01-postgres-cli.cc)：交互式命令行查询工具，展示整型、浮点、时间、JSON、UUID 等各种数据类型的解码与参数解析。
- [`tutorial-02-postgres-transaction.cc`](tutorial/tutorial-02-postgres-transaction.cc)：完整事务流程、错误分支触发、事务回滚与优雅断连。

---

## 连接 URL 格式与选项

| 协议头 Scheme | 说明 |
|---|---|
| `postgres://` / `postgresql://` | 标准明文 TCP 连接 |
| `postgress://` / `postgresqls://` | STARTTLS 加密连接（原生协议升级） |

支持的 URL 参数：
- `sslmode=disable` | `require` | `verify-ca` | `verify-full`
- `sslrootcert=/path/to/ca.crt`（自定义信任根证书路径）
- `application_name=my_service`
- `transaction=ID`（内部连接绑定路由标识）

---

## 错误码定义

WFPostgres 在私有 `14000` 错误码段定义任务错误（`WFPostgresError.h`）：

| 错误码 | 数值 | 描述 |
|---|---|---|
| `WFT_ERR_POSTGRES_SSL_NOT_SUPPORTED` | `14001` | 服务端拒绝 SSL 握手协商 |
| `WFT_ERR_POSTGRES_SSL_INIT_FAILED` | `14002` | 本地 OpenSSL 上下文初始化失败 |
| `WFT_ERR_POSTGRES_AUTH_FAILED` | `14003` | 用户认证或授权失败 |
| `WFT_ERR_POSTGRES_UNSUPPORTED_AUTH` | `14004` | 服务端请求了不支持的认证方式 |
| `WFT_ERR_POSTGRES_PROTOCOL_NOT_SUPPORTED` | `14005` | 协商的主协议版本不受支持 |
| `WFT_ERR_POSTGRES_PROTOCOL_ERROR` | `14006` | 启动或协议分帧错误 |
| `WFT_ERR_POSTGRES_BAD_RESPONSE` | `14007` | 服务端响应格式不合法或截断 |
| `WFT_ERR_POSTGRES_SSL_CERT_FAILED` | `14008` | CA/根证书加载或 TLS 校验配置失败 |

---

## 注意事项与限制

- 当前仅实现**客户端 API**（Client-side API）。
- 服务端 / 代理侧 API（如 `WFPostgresServerTask`）暂未实现。

---

## 开源协议

Apache License 2.0
