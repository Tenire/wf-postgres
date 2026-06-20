# WFPostgres: PostgreSQL Client for C++ Workflow

`WFPostgres`是基于[sogou/workflow](https://github.com/sogou/workflow)实现的异步PostgreSQL客户端插件。

`WFPostgres` is an asynchronous PostgreSQL client plugin based on [sogou/workflow](https://github.com/sogou/workflow).

> ⚠️ **Disclaimer / 免责声明**
>
> 1. **AI生成 (Vibe Coding)**: 本插件核心代码由 **AI 辅助生成**。虽然已通过全版本兼容测试 (PG13-PG18)、SSL/SCRAM 安全认证以及 ASAN 内存检测，且核心架构已对齐官方 MySQL Client，但测试也由AI生成，当前所有代码**尚未经过详尽的人工安全审计**。
> 2. **生产建议**: 内部已内置严格的协议状态机以防止误用，但在用于关键核心生产环境前，仍强烈建议自行进行充分的测试。
>
> ---
>
> 1. **Vibe Coding**: The core code of this plugin was **AI-assisted**. While it has passed full version compatibility testing (PG13-PG18), SSL/SCRAM security authentication, and ASAN memory checks, and its core architecture aligns with the official MySQL Client, the tests are also AI-generated, and all current code **has not undergone exhaustive manual security audits**.
> 2. **Production Recommendation**: Strict protocol state machines are built-in to prevent misuse. However, before deploying to critical core production environments, it is strongly recommended to independently conduct sufficient testing.


## Features / 基础特性

- ✨ **全异步架构 (Fully Asynchronous)**：实现了与 `C++ Workflow` 基础连接池和事件循环的深度集成，支持非阻塞的并发操作。
  *Implements deep integration with the `C++ Workflow` connection pool and event loop, supporting non-blocking concurrent operations.*
- 🔒 **生产级安全 (Production Security)**：完整支持 PostgreSQL STARTTLS 加密连接（`verify-ca` / `verify-full` 主机名校验），以及 SCRAM-SHA-256 和 MD5 协议认证。
  *Fully supports PostgreSQL STARTTLS encrypted connections (with `verify-ca` / `verify-full` hostname verification), along with SCRAM-SHA-256 and MD5 protocol authentication.*
- 🚀 **高级协议支持 (Advanced Protocols)**：支持原生 Extended Query (Parse/Bind/Execute) 防止 SQL 注入，提供对 `COPY` 流式批量传输协议以及 `LISTEN / NOTIFY` 机制的完善支持。
  *Supports native Extended Query (Parse/Bind/Execute) to prevent SQL injection, and provides comprehensive support for the `COPY` streaming bulk transfer protocol and the `LISTEN / NOTIFY` mechanism.*
- 🛡️ **事务与防毒 (Transaction & Anti-Poisoning)**：提供了连接级别绑定（Fixed Connection）接口，配合严格的 Reconnect Guard 机制，彻底杜绝连接复用导致的事务状态污染。
  *Provides a connection-level pinning (Fixed Connection) interface, paired with a strict Reconnect Guard mechanism to completely eliminate transaction state poisoning caused by connection reuse.*


## Getting Started / 快速开始

本插件当前使用 [Xmake](https://xmake.io/) 作为默认构建系统。

This plugin currently uses [Xmake](https://xmake.io/) as the default build system.

### 1. 安装依赖 / Install dependencies

构建时依赖编译器、`xmake`和`openssl`。

Build depends on the compiler, `xmake` and `openssl`.

- Ubuntu 24.04/22.04

    ```bash
    apt install g++ libssl-dev git
    # Install xmake
    bash <(curl -fsSL https://xmake.io/shget.text)
    ```

### 2. 在项目中使用 / Use WFPostgres in your project

如果您的主项目使用 Xmake，建议通过 `package()` 直接引入本仓库，您无需将本工程上传至 xrepo 即可直接复用：

If your main project uses Xmake, it is recommended to directly require this repository via `package()`. You can reuse it without uploading to xrepo:

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

### 3. 本地编译与测试 / Local compile and run

若想查阅源码并运行本地示例：

If you want to read the source code and run examples locally:

```bash
git clone https://github.com/tenire/wf-postgres.git
cd wf-postgres

# Build the static library and tutorials
xmake

# Run a specific tutorial or test
xmake run tutorial-01-postgres-cli
xmake run test_integration
```


## Examples / 使用示例

### Example 1: Basic Query

发送基本SQL查询并使用游标读取结果。

Send a basic SQL query and use the cursor to read the results.

```cpp
#include <iostream>
#include "workflow/WFFacilities.h"
#include "WFPostgresConnection.h"

using namespace protocol;

int main() {
    WFFacilities::WaitGroup wait_group(1);
    std::string url = "postgres://user:password@127.0.0.1:5432/db_name";

    auto *task = WFPostgresTaskFactory::create_postgres_task(url, 0, [&wait_group](WFPostgresTask *t) {
        if (t->get_state() == WFT_STATE_SUCCESS && !t->get_resp()->is_error()) {
            PostgresResultCursor cursor(t->get_resp());
            std::vector<std::vector<PostgresCell>> rows;
            if (cursor.fetch_all(rows)) {
                std::cout << "Query successful. Fetched " << rows.size() << " rows.\n";
            }
        } else {
            std::cerr << "Query failed.\n";
        }
        wait_group.done();
    });

    task->get_req()->set_query("SELECT 1;");
    task->start();
    wait_group.wait();
    return 0;
}
```

### Example 2: Transaction and Fixed Connection

使用`WFPostgresConnection`将一系列任务锁定在同一物理连接上，实现事务。

Use `WFPostgresConnection` to lock a series of tasks onto the same physical connection to implement a transaction.

```cpp
#include <iostream>
#include "workflow/WFFacilities.h"
#include "WFPostgresConnection.h"

using namespace protocol;

int main() {
    WFFacilities::WaitGroup wait_group(1);
    std::string url = "postgres://user:password@127.0.0.1:5432/db_name";

    WFPostgresConnection conn(1); // 1 is the unique ID for this fixed connection
    conn.init(url);

    auto cb = [&wait_group](WFPostgresTask *t) {
        if (t->get_state() != WFT_STATE_SUCCESS || t->get_resp()->is_error()) {
            std::cerr << "Transaction task failed!\n";
        } else {
            std::cout << "Transaction task success.\n";
        }
        wait_group.done();
    };

    // All these tasks will securely execute sequentially on the same pooled socket
    auto *begin_task = conn.create_query_task("BEGIN;", nullptr);
    auto *insert_task = conn.create_query_task("INSERT INTO my_table (val) VALUES ('hello');", nullptr);
    auto *commit_task = conn.create_query_task("COMMIT;", cb);

    workflow::series_of(begin_task)->push_back(insert_task);
    workflow::series_of(begin_task)->push_back(commit_task);
    
    begin_task->start();
    wait_group.wait();
    conn.deinit();
    
    return 0;
}
```

### Example 3: LISTEN / NOTIFY

使用Postgres原生的高效异步消息传递能力。

Use Postgres's native efficient asynchronous message passing capabilities.

```cpp
#include <iostream>
#include "workflow/WFFacilities.h"
#include "WFPostgresConnection.h"

using namespace protocol;

int main() {
    WFFacilities::WaitGroup wait_group(1);
    std::string url = "postgres://user:password@127.0.0.1:5432/db_name";

    WFPostgresConnection conn(1);
    conn.init(url);

    auto *listen_task = conn.create_query_task("LISTEN my_channel;", nullptr);
    
    // Create a special NOTIFY waiting task
    auto *notify_wait_task = conn.create_notify_task([](WFPostgresTask *t) {
        if (t->get_state() == WFT_STATE_SUCCESS) {
            auto notifies = t->get_resp()->get_notifications();
            for (const auto& n : notifies) {
                std::cout << "Received Notification: channel=" << n.channel 
                          << " payload=" << n.payload << "\n";
            }
        }
    });

    workflow::series_of(listen_task)->push_back(notify_wait_task);
    listen_task->start();

    // Trigger NOTIFY from another connection to awaken notify_wait_task...
    
    wait_group.wait();
    conn.deinit();
    return 0;
}
```

## Error Codes / 错误码

WFPostgres uses PostgreSQL-plugin-specific task errors in the private `14000` range:

- `WFT_ERR_POSTGRES_SSL_NOT_SUPPORTED` (`14001`): the PostgreSQL server rejected SSL negotiation.
- `WFT_ERR_POSTGRES_SSL_INIT_FAILED` (`14002`): local OpenSSL SSL/BIO/client context initialization failed.
- `WFT_ERR_POSTGRES_AUTH_FAILED` (`14003`): authentication or authorization failed during startup.
- `WFT_ERR_POSTGRES_UNSUPPORTED_AUTH` (`14004`): server requested an authentication method unsupported by this plugin.
- `WFT_ERR_POSTGRES_PROTOCOL_NOT_SUPPORTED` (`14005`): negotiated PostgreSQL protocol major version is unsupported.
- `WFT_ERR_POSTGRES_PROTOCOL_ERROR` (`14006`): startup/protocol-level error not represented by a more specific code.
- `WFT_ERR_POSTGRES_BAD_RESPONSE` (`14007`): malformed or incomplete backend response.
- `WFT_ERR_POSTGRES_SSL_CERT_FAILED` (`14008`): CA/root certificate loading or TLS trust configuration failed.

SQL execution errors are not converted into `WFT_ERR_POSTGRES_*`; inspect `PostgresResponse::get_sql_state()` and `get_error_msg()` instead.

## Attention / 注意事项



### 关于 Fixed Connection (事务隔离) / About Fixed Connection

当您需要执行 `BEGIN; ... COMMIT;` 这样的事务链，或使用 `COPY` 和 `LISTEN` 时，**必须**使用 `WFPostgresConnection`。它会在内部将连接池 URI 加上特有的 `transaction=` 标志，以此确保多步操作绝对锁定在同一根 TCP 连接上，并在遇到异常断线时提供严格的串话隔离（Reconnect Guard）。

When you need to execute transaction chains like `BEGIN; ... COMMIT;`, or use `COPY` and `LISTEN`, you **MUST** use `WFPostgresConnection`. It internally appends a unique `transaction=` flag to the pool URI, ensuring that multi-step operations are absolutely locked onto the same TCP connection. It also provides strict Reconnect Guard isolation against connection crosstalk upon abnormal disconnection.

### 暂不支持的功能 (Limitations)

- 当前仅实现了 PostgreSQL 客户端 API (Client-side API)。
- 服务端 / 代理侧 API (Server/Proxy API, 如 `WFPostgresServerTask`) 暂未实现。我们计划在未来的路线图中逐步支持 server 端握手、查询响应等特性，目前请仅将此库用作客户端。



## LICENSE

Apache 2.0
