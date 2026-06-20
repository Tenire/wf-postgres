#include "workflow/WFFacilities.h"
#include "PostgresTask.h"
#include "WFPostgresConnection.h"
#include <iostream>

using namespace wfpg;
using namespace wfpg::protocol;

static WFFacilities::WaitGroup wait_group(1);

void cancel_callback(WFPostgresTask *task)
{
    if (task->get_state() == WFT_STATE_SUCCESS) {
        std::cout << "Cancel task finished successfully (packet sent).\n";
    } else {
        std::cerr << "Cancel task failed! Error: " << task->get_error() << "\n";
    }
}

void pg_callback(WFPostgresTask *task)
{
    if (task->get_state() == WFT_STATE_SYS_ERROR && task->get_error() == ECONNRESET) {
        std::cout << "Query successfully canceled (ECONNRESET as expected).\n";
    } else if (task->get_state() == WFT_STATE_SYS_ERROR && task->get_error() == 0) {
        std::cout << "Query successfully canceled (EOF as expected).\n";
    } else if (task->get_state() == WFT_STATE_TASK_ERROR) {
        std::cout << "Query failed with task error (might be cancel error msg).\n";
    } else if (task->get_state() == WFT_STATE_SUCCESS) {
        if (task->get_resp()->is_error()) {
            std::cout << "Query canceled, returned FATAL_ERROR: " << task->get_resp()->get_error().message << "\n";
        } else {
            std::cerr << "Query finished successfully? That shouldn't happen!\n";
        }
    } else {
        std::cerr << "Query finished with unexpected state: " << task->get_state() << " error: " << task->get_error() << "\n";
    }
    wait_group.done();
}

int main(int argc, char *argv[])
{
    std::string url = "postgres://wf_tester:wf_secret@127.0.0.1:5433/wf_testdb";
    if (argc > 1) {
        url = argv[1];
    }

    WFFacilities::WaitGroup wait_group(2);
    WFPostgresConnection conn(1);
    conn.init(url);

    std::atomic<int> exit_code{0};

    auto *dummy = conn.create_query_task("SELECT 1;", [&conn, url, &wait_group, &exit_code](WFPostgresTask *task) {
        if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error()) {
            std::cerr << "Dummy query failed, state: " << task->get_state() << " error: " << task->get_error() << "\n";
            exit_code = 1;
            wait_group.done();
            wait_group.done();
            return;
        }

        int32_t pid = task->get_resp()->get_backend_pid();
        std::string secret_data = task->get_resp()->get_backend_secret_data();

        if (pid == 0) {
            std::cerr << "Failed to retrieve PID or Secret from backend.\n";
            exit_code = 1;
            wait_group.done();
            wait_group.done();
            return;
        }

        std::cout << "Target PID: " << pid << ", Secret len: " << secret_data.size() << "\n";

        auto *long_task = conn.create_query_task("SELECT pg_sleep(10);", [&wait_group, &exit_code](WFPostgresTask *t) {
            if (t->get_state() == WFT_STATE_SUCCESS) {
                if (t->get_resp()->is_error()) {
                    if (t->get_resp()->get_error().sql_state == "57014") { // query_canceled
                        std::cout << "Query canceled successfully!\n";
                    } else {
                        std::cerr << "Query failed with unexpected error: " << t->get_resp()->get_error().message << "\n";
                        exit_code = 1;
                    }
                } else {
                    std::cerr << "Query finished normally, but it should have been canceled!\n";
                    exit_code = 1;
                }
            } else {
                std::cerr << "Connection closed abnormally or failed. state: " << t->get_state() << " error: " << t->get_error() << "\n";
                exit_code = 1;
            }
            wait_group.done();
        });
        
        long_task->start();

        auto *timer = WFTaskFactory::create_timer_task(1000 * 1000, [url, pid, secret_data, &exit_code, &wait_group](WFTimerTask *) {
            auto *cancel_task = WFPostgresTaskFactory::create_cancel_task(url, pid, secret_data, 0, [&exit_code, &wait_group](WFPostgresTask *t) {
                if (t->get_state() != WFT_STATE_SUCCESS) {
                    std::cerr << "Cancel request network failure.\n";
                    exit_code = 1;
                } else {
                    std::cout << "Cancel request dispatched.\n";
                }
                wait_group.done();
            });
            cancel_task->start();
        });
        timer->start();
    });

    dummy->start();
    wait_group.wait();
    conn.deinit();

    return exit_code.load();
}
