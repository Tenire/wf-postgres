# WFPostgres: Production Asynchronous PostgreSQL Client for C++ Workflow

<p align="left">
  <img src="https://img.shields.io/badge/license-Apache%202.0-blue.svg" alt="License" />
  <img src="https://img.shields.io/badge/C%2B%2B-11-blue.svg" alt="C++" />
  <img src="https://img.shields.io/badge/PostgreSQL-13%20%7E%2018-blue.svg" alt="PostgreSQL" />
  <img src="https://img.shields.io/badge/build-Xmake%20%7C%20CMake-brightgreen.svg" alt="Build" />
</p>

`WFPostgres` is a high-performance, fully asynchronous PostgreSQL client plugin for the [Sogou Workflow](https://github.com/sogou/workflow) C++ parallel computing and networking engine.

It implements the native PostgreSQL Frontend/Backend Wire Protocol (supporting both Protocol 3.0 and Protocol 3.2 with negotiation fallback) with zero dependency on `libpq`.

---

## Key Features

- ✨ Fully Asynchronous Architecture: Native integration with Workflow's connection pool, event loop, and task graph model (`WFComplexClientTask`).
- 🔒 Production Security:
  - Native PostgreSQL STARTTLS protocol upgrade (`postgress://` scheme).
  - Strict TLS verification (`verify-ca`, `verify-full`) and custom CA roots (`sslrootcert`).
  - Full authentication support: Trust, SCRAM-SHA-256, MD5, and Cleartext.
- 🚀 Advanced Wire Protocol:
  - Extended Query Protocol (`Parse` -> `Bind` -> `Describe` -> `Execute` -> `Sync`) with parameter binding to prevent SQL injection.
  - Zero-copy row extraction via `PostgresResultCursor` with type-aware decoders (INT, FLOAT, NUMERIC, TIMESTAMP, JSONB, Arrays, BYTEA).
  - Native COPY streaming sub-protocol for bounded-memory chunked bulk transfer.
  - Asynchronous LISTEN / NOTIFY event pub/sub mechanism.
  - Out-of-band CancelRequest supporting both legacy 4-byte and Protocol 3.2 32-byte keys.
- 🛡️ Transaction Safety & Reconnect Guard:
  - Connection-level pinning via `WFPostgresConnection`.
  - Reconnect Guard intercepts abnormal socket disconnects during transactions to eliminate connection crosstalk and session poisoning.
- 🧪 Cross-Version Hardening:
  - Verified against PostgreSQL 13, 14, 15, 16, and 18.
  - Clean execution under AddressSanitizer (ASan) and LeakSanitizer (LSan).
---

## Architecture Overview

```
wf-postgres/
├── include/                     # Public API headers
│   ├── WFPostgresClient.h       # Top-level client facade & global factory shortcuts
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

## Integration Guide

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

#### Method A: Add as Subdirectory

Include the repository in your project's `CMakeLists.txt`:

```cmake
add_subdirectory(wf-postgres)
target_link_libraries(my_app PRIVATE wfpg::wf_postgres)
```

#### Method B: Link as Static Library

If you built `wf-postgres` separately:

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

#### Method C: Copy Source Files

Copy `include/` and `src/` directly into your tree and add the sources to your target. Only Workflow and OpenSSL are required.

### 3. Local Build & Test

```bash
# Build static library and run CLI tutorial with Xmake
xmake
xmake run tutorial-01-postgres-cli

# Run test suite with Xmake
xmake f --tests=true
xmake
xmake run test_integration

# Build with CMake
mkdir -p build && cd build
cmake .. -DBUILD_TUTORIAL=ON
make -j$(nproc)
```

---

## Quick Start Examples

### 1. Basic Query (Simple Query)

```cpp
#include <iostream>
#include "workflow/WFFacilities.h"
#include "WFPostgresClient.h"

using namespace protocol;

int main() {
    WFFacilities::WaitGroup wg(1);
    std::string url = "postgres://username:password@127.0.0.1:5432/dbname";

    auto *task = create_postgres_task(url, 0, [&wg](WFPostgresTask *t) {
        if (t->get_state() == WFT_STATE_SUCCESS && !t->get_resp()->is_error()) {
            PostgresResultCursor cursor(t->get_resp());
            std::vector<std::vector<PostgresCell>> rows;
            if (cursor.fetch_all(rows)) {
                std::cout << "Fetched " << rows.size() << " rows successfully.\n";
            }
        } else {
            std::cerr << "Query failed.\n";
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

```cpp
#include "WFPostgresClient.h"
#include "workflow/WFFacilities.h"
#include <iostream>

int main() {
    WFFacilities::WaitGroup wg(1);

    auto *task = create_postgres_task("postgres://username:password@127.0.0.1:5432/dbname", 0,
        [&wg](WFPostgresTask *task) {
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

    task->get_req()->set_query(
        "SELECT username, score FROM users WHERE id = $1 AND is_active = $2;",
        wfpg::bind_params(42, true)
    );

    task->start();
    wg.wait();
    return 0;
}
```

### 3. Transaction with Fixed Connection

Locks multi-step execution sequentially to a single TCP socket with automatic Reconnect Guard:

```cpp
#include <iostream>
#include "workflow/WFFacilities.h"
#include "WFPostgresClient.h"

using namespace protocol;

int main() {
    WFFacilities::WaitGroup wg(1);
    std::string url = "postgres://username:password@127.0.0.1:5432/dbname";

    WFPostgresConnection conn(1); // Unique connection identifier
    conn.init(url);

    auto cb = [&wg](WFPostgresTask *t) {
        if (t->get_state() != WFT_STATE_SUCCESS || t->get_resp()->is_error()) {
            std::cerr << "Transaction task failed!\n";
        } else {
            std::cout << "Transaction task success.\n";
        }
        wg.done();
    };

    auto *begin_task  = conn.create_query_task("BEGIN;", nullptr);
    auto *insert_task = conn.create_query_task("INSERT INTO tbl(val) VALUES ('test');", nullptr);
    auto *commit_task = conn.create_query_task("COMMIT;", cb);

    workflow::series_of(begin_task)->push_back(insert_task);
    workflow::series_of(begin_task)->push_back(commit_task);
    
    begin_task->start();
    wg.wait();
    conn.deinit();
    return 0;
}
```

### 4. Asynchronous LISTEN / NOTIFY

```cpp
#include <iostream>
#include "workflow/WFFacilities.h"
#include "WFPostgresClient.h"

using namespace protocol;

int main() {
    WFFacilities::WaitGroup wg(1);
    std::string url = "postgres://username:password@127.0.0.1:5432/dbname";

    WFPostgresConnection conn(1);
    conn.init(url);

    auto *listen_task = conn.create_query_task("LISTEN my_channel;", nullptr);
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
    
    wg.wait();
    conn.deinit();
    return 0;
}
```

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

## License

Apache License 2.0
