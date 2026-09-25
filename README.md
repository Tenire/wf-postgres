# WFPostgres: Asynchronous PostgreSQL Client for C++ Workflow

<p align="left">
  <img src="https://img.shields.io/badge/license-Apache%202.0-blue.svg" alt="License" />
  <img src="https://img.shields.io/badge/C%2B%2B-11-blue.svg" alt="C++" />
  <img src="https://img.shields.io/badge/PostgreSQL-13%20%7E%2018-blue.svg" alt="PostgreSQL" />
  <img src="https://img.shields.io/badge/build-Xmake%20%7C%20CMake-brightgreen.svg" alt="Build" />
</p>

[English](README.md) | [中文说明](README_zh.md)

`WFPostgres` is an asynchronous PostgreSQL client protocol plugin for [Sogou Workflow](https://github.com/sogou/workflow). It natively implements the PostgreSQL Frontend/Backend wire protocol (Protocol 3.0 and Protocol 3.2 with negotiation fallback) with zero dependency on `libpq`.

---

## Features

- ✨ **Fully Asynchronous**: Deep integration with Workflow's connection pool, event loop, and task framework (`WFComplexClientTask`).
- 🔒 **Production Security**:
  - Native PostgreSQL STARTTLS protocol upgrade (`postgress://` and `postgresqls://` schemes).
  - Strict TLS verification (`verify-ca`, `verify-full`) and custom CA roots (`sslrootcert`).
  - Full authentication support: Trust, SCRAM-SHA-256 (RFC 5802/7677), MD5, and Cleartext.
- 🚀 **Advanced Wire Protocol & Typing**:
  - Simple Query and Extended Query (`Parse` -> `Bind` -> `Describe` -> `Execute` -> `Sync`).
  - Type-safe parameter binding (`bind_params`) protecting against SQL injection.
  - Symmetrical PostgreSQL type matrix in `PostgresValue` and `PostgresCell` (integers, floats, numeric, strings, bytea, UUID, bool, OID, dates, timestamps, JSON/JSONB, 1D arrays).
  - High-performance `PostgresResultCursor` supporting zero-copy access and response buffer ownership transfer.
  - Native COPY streaming sub-protocol and asynchronous LISTEN / NOTIFY mechanism.
  - Out-of-band CancelRequest supporting both legacy 4-byte and Protocol 3.2 32-byte keys.
- 🛡️ **Transaction Safety & Reconnect Guard**:
  - Connection-level pinning via `WFPostgresConnection`.
  - Reconnect Guard intercepts abnormal socket disconnects during transactions to eliminate connection crosstalk and session poisoning.
  - Automatic connection cleanup on failed transactions (`'E'` state): unconditional `keep_alive = 0` to close socket and prevent poisoned sockets in connection pools.
- 🚦 **Unified Error Handling**:
  - `PostgresStatus` combines network transport errors and database SQLSTATE/server errors under a single `status.ok()` check.
- 🧪 **Cross-Version Hardening**:
  - Verified against PostgreSQL 13, 14, 15, 16, and 18.
  - Clean execution under AddressSanitizer (ASan) and LeakSanitizer (LSan).

---

## Architecture Overview

```
wf-postgres/
├── include/                     # Public API headers
│   ├── WFPostgresClient.h       # Top-level client facade & global shortcut factories
│   ├── PostgresStatus.h         # Unified outcome status (collapsing transport & SQL errors)
│   ├── PostgresValue.h          # Type-safe parameter deduction and bind_params()
│   ├── PostgresTask.h           # Task factory & callback declarations
│   ├── WFPostgresConnection.h   # Fixed connection & transaction pinning
│   ├── PostgresRequest.h        # Request builder (Simple & Extended Query with variadic bind)
│   ├── PostgresResponse.h       # Response metadata & packet statuses
│   ├── PostgresResult.h         # PostgresResultCursor (zero-copy & owned copy fetch)
│   ├── PostgresTypes.h          # PostgreSQL protocol type OIDs and enums
│   └── WFPostgresError.h        # Dedicated error codes
├── src/
│   ├── auth/                    # SCRAM-SHA-256 (RFC 5802/7677) & MD5 state machines
│   ├── protocol/                # Wire stream framing, packet serializer & event parser
│   ├── factory/                 # ComplexPostgresTask implementation & lifecycle
│   └── client/                  # WFPostgresConnection pooling and Reconnect Guard
├── tutorial/                    # Runnable command-line and transaction examples
├── test/                        # Complete integration, fuzzing, and version matrix tests
├── CMakeLists.txt               # Standard CMake build definition
└── xmake.lua                    # Native Xmake build definition
```

---

## Building & Integration

### Prerequisites

- C++11 compatible compiler (GCC >= 4.8.5, Clang, or MSVC)
- [Sogou Workflow](https://github.com/sogou/workflow)
- OpenSSL (libssl & libcrypto)

### 1. Using in an Xmake Project (Recommended)

Add directly into your `xmake.lua`:

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

### 2. Using in a CMake Project

#### Method A: Subdirectory (`add_subdirectory`)

```cmake
add_subdirectory(wf-postgres)
target_link_libraries(my_app PRIVATE wfpg::wf_postgres)
```

#### Method B: Pre-built Static Library

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

#### Method C: Direct Source Integration

Copy `include/` and `src/` directly into your project tree. Only Workflow and OpenSSL are required as dependencies.

### 3. Local Compilation & Testing

```bash
# Build static library and tutorial executables with Xmake
xmake
xmake run tutorial-01-postgres-cli

# Run unit and architectural tests
xmake f --tests=true
xmake
xmake run test_abstractions

# Run integration tests against a live PostgreSQL server
xmake run test_integration postgres://user:password@127.0.0.1:5432/testdb

# Build with CMake
mkdir -p build && cd build
cmake .. -DBUILD_TUTORIAL=ON
make -j$(nproc)
```

---

## Usage Examples

### 1. Basic Query & Unified Error Handling

Using `WFPostgresClient.h` and `PostgresStatus`:

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

### 2. Parameterized Query with Type-Safe Binding

Use `bind_params(...)` to automatically deduce types and bind query parameters:

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

    // Supports integers, floats, strings, booleans, timestamps, arrays, and nullptr
    task->get_req()->set_query(
        "SELECT username, score FROM users WHERE id = $1 AND is_active = $2;",
        wfpg::bind_params(42, true)
    );

    task->start();
    wg.wait();
    return 0;
}
```

### 3. Transaction with Fixed Connection & Reconnect Guard

Sequential execution pinned to a single connection with automatic dirty socket disposal:

```cpp
#include <iostream>
#include "workflow/WFFacilities.h"
#include "WFPostgresClient.h"

int main() {
    WFFacilities::WaitGroup wg(1);
    std::string url = "postgres://user:password@127.0.0.1:5432/dbname";

    WFPostgresConnection conn(1); // Unique connection identifier
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

    // Sequential tasks executed on the same pinned connection
    auto *begin_task  = conn.create_query_task("BEGIN;", nullptr);
    auto *insert_task = conn.create_query_task("INSERT INTO tbl(id, val) VALUES ($1, $2);", nullptr, 100, "sample");
    auto *commit_task = conn.create_query_task("COMMIT;", cb);

    workflow::series_of(begin_task)->push_back(insert_task);
    workflow::series_of(begin_task)->push_back(commit_task);
    
    begin_task->start();
    wg.wait();

    // Gracefully disconnect
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

### 4. Asynchronous LISTEN / NOTIFY

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

## Detailed Tutorials

Comprehensive runnable tutorials are provided under [`tutorial/`](tutorial/):
- [`tutorial-01-postgres-cli.cc`](tutorial/tutorial-01-postgres-cli.cc): Interactive CLI client with type decoding for integers, floats, timestamps, JSON, and UUIDs.
- [`tutorial-02-postgres-transaction.cc`](tutorial/tutorial-02-postgres-transaction.cc): Multi-step transaction pipeline, error handling, rollback, and graceful connection teardown.

---

## URL Schemes & Connection Options

| Scheme | Description |
|---|---|
| `postgres://` / `postgresql://` | Standard plaintext TCP connection |
| `postgress://` / `postgresqls://` | Encrypted STARTTLS connection (RFC-compliant protocol upgrade) |

Supported query parameters:
- `sslmode=disable` | `require` | `verify-ca` | `verify-full`
- `sslrootcert=/path/to/ca.crt` (custom CA bundle for trust validation)
- `application_name=my_service`
- `transaction=ID` (internal connection pinning routing flag)

---

## Error Codes

WFPostgres uses PostgreSQL-plugin-specific task errors in the private `14000` range (`WFPostgresError.h`):

| Code | Value | Description |
|---|---|---|
| `WFT_ERR_POSTGRES_SSL_NOT_SUPPORTED` | `14001` | The PostgreSQL server rejected SSL negotiation |
| `WFT_ERR_POSTGRES_SSL_INIT_FAILED` | `14002` | Local OpenSSL context initialization failed |
| `WFT_ERR_POSTGRES_AUTH_FAILED` | `14003` | Authentication or authorization failed during startup |
| `WFT_ERR_POSTGRES_UNSUPPORTED_AUTH` | `14004` | Server requested an authentication method unsupported by this plugin |
| `WFT_ERR_POSTGRES_PROTOCOL_NOT_SUPPORTED` | `14005` | Negotiated PostgreSQL protocol version is unsupported |
| `WFT_ERR_POSTGRES_PROTOCOL_ERROR` | `14006` | Startup or protocol framing error |
| `WFT_ERR_POSTGRES_BAD_RESPONSE` | `14007` | Malformed or incomplete backend response |
| `WFT_ERR_POSTGRES_SSL_CERT_FAILED` | `14008` | CA/root certificate loading or TLS trust configuration failed |

---

## Limitations

- Currently implements **Client-side API** only.
- Server/Proxy side API (e.g., `WFPostgresServerTask`) is not yet implemented.

---

## License

Apache License 2.0
