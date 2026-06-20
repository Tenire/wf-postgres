#include "workflow/WFFacilities.h"
#include "WFPostgresConnection.h"
#include "PostgresResult.h"
#include <iostream>
#include <atomic>

using namespace wfpg;
using namespace wfpg::protocol;

int main(int argc, char *argv[])
{
    std::string url = "postgres://wf_tester:wf_secret@127.0.0.1:5433/wf_testdb";
    if (argc > 1) {
        url = argv[1];
    }

    WFFacilities::WaitGroup wait_group(1);
    WFPostgresConnection conn(1);
    conn.init(url);

    std::atomic<int> exit_code{0};

    // Task 1: Ask the backend to kill its own connection.
    auto *kill_task = conn.create_query_task("SELECT pg_terminate_backend(pg_backend_pid());", [&conn, &wait_group, &exit_code](WFPostgresTask *task) {
        std::cout << "Kill task finished. State: " << task->get_state() << " Error: " << task->get_error() << "\n";
        // We expect it to fail with SYS_ERROR (ECONNRESET or similar) because the connection is dropped.
        if (task->get_state() == WFT_STATE_SUCCESS) {
            std::cerr << "Expected network error, but query succeeded?\n";
        }

        // Task 2: Try to use the same WFPostgresConnection again.
        // Thanks to the reconnect guard cleanup, target->state was reset to 0, so this will reconnect and succeed!
        auto *retry_task = conn.create_query_task("SELECT 1 AS recovery_test;", [&wait_group, &exit_code](WFPostgresTask *t) {
            std::cout << "Retry task finished. State: " << t->get_state() << " Error: " << t->get_error() << "\n";
            if (t->get_state() != WFT_STATE_SUCCESS || t->get_resp()->is_error()) {
                std::cerr << "Retry task failed! Reconnect guard is broken.\n";
                exit_code = 1;
            } else {
                PostgresResultCursor cursor(t->get_resp());
                std::vector<PostgresCell> row;
                if (cursor.fetch_row(row) && row.size() > 0 && row[0].as_int() == 1) {
                    std::cout << "Reconnect and recovery successful!\n";
                } else {
                    std::cerr << "Unexpected result from recovery task.\n";
                    exit_code = 1;
                }
            }
            wait_group.done();
        });
        
        retry_task->start();
    });

    kill_task->start();
    wait_group.wait();
    conn.deinit();

    return exit_code.load();
}
