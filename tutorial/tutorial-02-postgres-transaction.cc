#include <iostream>
#include <string>
#include "workflow/WFFacilities.h"
#include "WFPostgresClient.h"

using namespace wfpg;
using namespace wfpg::protocol;
int main(int argc, char *argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <postgres_url>\n"
                  << "Example: " << argv[0] << " postgres://user:pass@127.0.0.1:5432/db" << std::endl;
        return 1;
    }

    std::string url = argv[1];

    WFPostgresConnection conn(1); // 1 is connection ID
    if (conn.init(url) != 0) {
        std::cerr << "Invalid URL" << std::endl;
        return 1;
    }

    WFFacilities::WaitGroup wait_group(1);

    auto cb_rollback = [&conn, &wait_group](WFPostgresTask *task) {
        PostgresStatus status = PostgresStatus::from_task(task);
        std::cout << "[ROLLBACK] finished: " << status.to_string()
                  << " (tx state: " << conn.get_last_transaction_state() << ")" << std::endl;
        wait_group.done();
    };

    auto cb_error = [&conn, &cb_rollback](WFPostgresTask *task) {
        PostgresStatus status = PostgresStatus::from_task(task);
        std::cerr << "Query failed: " << status.to_string()
                  << ", is_transaction_failed=" << conn.is_transaction_failed()
                  << ", rolling back..." << std::endl;
        WFPostgresTask *rollback = conn.create_query_task("ROLLBACK", cb_rollback);
        series_of(task)->push_back(rollback);
    };

    auto cb_commit = [&conn, &wait_group](WFPostgresTask *task) {
        PostgresStatus status = PostgresStatus::from_task(task);
        std::cout << "[COMMIT] finished: " << status.to_string()
                  << " (tx state: " << conn.get_last_transaction_state() << ")" << std::endl;
        wait_group.done();
    };

    auto cb_step2 = [&conn, cb_commit, cb_error](WFPostgresTask *task) {
        PostgresStatus status = PostgresStatus::from_task(task);
        if (!status.ok()) {
            cb_error(task);
            return;
        }
        std::cout << "[STEP 2] Success! Now committing..." << std::endl;
        WFPostgresTask *commit = conn.create_query_task("COMMIT", cb_commit);
        series_of(task)->push_back(commit);
    };

    auto cb_step1 = [&conn, cb_step2, cb_error](WFPostgresTask *task) {
        PostgresStatus status = PostgresStatus::from_task(task);
        if (!status.ok()) {
            cb_error(task);
            return;
        }
        std::cout << "[STEP 1] Success! Now executing parameterized query with bind_params..." << std::endl;
        // Using type-safe variadic bind_params directly on conn.create_query_task
        WFPostgresTask *step2 = conn.create_query_task(
            "SELECT $1::int AS id, $2::text AS note;",
            cb_step2,
            1001,
            "test transaction note"
        );
        series_of(task)->push_back(step2);
    };

    auto cb_begin = [&conn, cb_step1, cb_error](WFPostgresTask *task) {
        PostgresStatus status = PostgresStatus::from_task(task);
        if (!status.ok()) {
            cb_error(task);
            return;
        }
        std::cout << "[BEGIN] Success! In transaction: " << conn.in_transaction() << std::endl;
        WFPostgresTask *step1 = conn.create_query_task("SELECT 1 AS num;", cb_step1);
        series_of(task)->push_back(step1);
    };

    WFPostgresTask *begin_task = conn.create_query_task("BEGIN", cb_begin);
    begin_task->start();
    wait_group.wait();

    // Disconnect connection gracefully
    WFFacilities::WaitGroup disconnect_wg(1);
    WFPostgresTask *disconnect_task = conn.create_disconnect_task([&disconnect_wg](WFPostgresTask *task) {
        PostgresStatus status = PostgresStatus::from_task(task);
        if (status.ok()) {
            std::cout << "[DISCONNECT] Connection terminated gracefully." << std::endl;
        } else {
            std::cerr << "[DISCONNECT] Failed: " << status.to_string() << std::endl;
        }
        disconnect_wg.done();
    });
    disconnect_task->start();
    disconnect_wg.wait();

    conn.deinit();
    return 0;
}
