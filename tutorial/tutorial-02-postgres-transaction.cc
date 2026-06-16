#include <iostream>
#include <string>
#include "workflow/WFFacilities.h"
#include "WFPostgresConnection.h"

using namespace protocol;

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

    auto cb_rollback = [&wait_group](WFPostgresTask *task) {
        std::cout << "[ROLLBACK] finished with state: " << task->get_state() << std::endl;
        wait_group.done();
    };

    auto cb_error = [&conn, &cb_rollback](WFPostgresTask *task) {
        std::cerr << "Query failed (state: " << task->get_state() << ", error: " << task->get_error() << "), rolling back..." << std::endl;
        if (task->get_resp()->is_error()) {
            std::cerr << "DB Error: " << task->get_resp()->get_error().message << std::endl;
        }
        WFPostgresTask *rollback = conn.create_query_task("ROLLBACK", cb_rollback);
        series_of(task)->push_back(rollback);
    };

    auto cb_commit = [&wait_group](WFPostgresTask *task) {
        std::cout << "[COMMIT] finished with state: " << task->get_state() << std::endl;
        wait_group.done();
    };

    auto cb_insert2 = [&conn, cb_commit, cb_error](WFPostgresTask *task) {
        if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error()) {
            cb_error(task);
            return;
        }
        std::cout << "[INSERT 2] Success! Now committing..." << std::endl;
        WFPostgresTask *commit = conn.create_query_task("COMMIT", cb_commit);
        series_of(task)->push_back(commit);
    };

    auto cb_insert1 = [&conn, cb_insert2, cb_error](WFPostgresTask *task) {
        if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error()) {
            cb_error(task);
            return;
        }
        std::cout << "[INSERT 1] Success! Now executing bad query (Division by zero) to trigger error..." << std::endl;
        WFPostgresTask *bad_query = conn.create_query_task("SELECT 1 / 0;", cb_insert2);
        series_of(task)->push_back(bad_query);
    };

    auto cb_begin = [&conn, cb_insert1, cb_error](WFPostgresTask *task) {
        if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error()) {
            cb_error(task);
            return;
        }
        std::cout << "[BEGIN] Success! Now executing INSERT 1..." << std::endl;
        WFPostgresTask *insert1 = conn.create_query_task("SELECT 1 AS num;", cb_insert1);
        series_of(task)->push_back(insert1);
    };

    WFPostgresTask *begin_task = conn.create_query_task("BEGIN", cb_begin);
    begin_task->start();

    wait_group.wait();

    // Disconnect connection gracefully
    WFFacilities::WaitGroup disconnect_wg(1);
    WFPostgresTask *disconnect_task = conn.create_disconnect_task([&disconnect_wg](WFPostgresTask *task) {
        if (task->get_state() == WFT_STATE_SUCCESS) {
            std::cout << "[DISCONNECT] Connection terminated gracefully." << std::endl;
        } else {
            std::cerr << "[DISCONNECT] Failed with state: " << task->get_state() << " error: " << task->get_error() << std::endl;
        }
        disconnect_wg.done();
    });
    disconnect_task->start();
    disconnect_wg.wait();

    return 0;
}
