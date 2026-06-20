#include <iostream>
#include <string>
#include <vector>
#include "workflow/WFFacilities.h"
#include "WFPostgresConnection.h"
#include "PostgresResult.h"

using namespace wfpg;
using namespace wfpg::protocol;

int test_prepare_integration(const std::string& url) {
    WFPostgresConnection conn(1);
    if (conn.init(url) != 0) {
        std::cerr << "Invalid URL" << std::endl;
        return -1;
    }

    WFFacilities::WaitGroup wait_group(1);
    int test_result = -1;

    auto cb_select = [&wait_group, &test_result](WFPostgresTask *task) {
        if (task->get_state() != WFT_STATE_SUCCESS) {
            std::cerr << "Task failed: state=" << task->get_state() << " error=" << task->get_error() << std::endl;
            wait_group.done();
            return;
        }

        auto *resp = task->get_resp();
        if (resp->is_error()) {
            std::cerr << "DB Error: " << resp->get_error().message << std::endl;
            wait_group.done();
            return;
        }

        PostgresResultCursor cursor(resp);
        std::vector<PostgresCell> row;
        if (cursor.fetch_row(row) && row.size() == 2) {
            if (row[0].as_string() == "42" && row[1].as_string() == "workflow-postgres") {
                test_result = 0; // Success
            }
        }
        
        wait_group.done();
    };

    // Extended Query Protocol by providing parameters
    std::string query = "SELECT $1::int AS num, $2::text AS str;";
    std::vector<std::string> params = {"42", "workflow-postgres"};

    WFPostgresTask *task = conn.create_query_task(query, cb_select);
    task->get_req()->set_query(query, params);

    task->start();
    wait_group.wait();

    // Disconnect connection gracefully
    WFFacilities::WaitGroup disconnect_wg(1);
    WFPostgresTask *disconnect_task = conn.create_disconnect_task([&disconnect_wg](WFPostgresTask *) {
        disconnect_wg.done();
    });
    disconnect_task->start();
    disconnect_wg.wait();

    return test_result;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <postgres_url>\n";
        return 1;
    }

    std::string url = argv[1];
    
    std::cout << "Running Extended Query (Prepare) Integration Test..." << std::endl;
    int ret = test_prepare_integration(url);
    if (ret == 0) {
        std::cout << "Test passed successfully." << std::endl;
    } else {
        std::cerr << "Test failed." << std::endl;
    }
    
    return ret;
}
