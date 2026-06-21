#include "workflow/WFFacilities.h"
#include "PostgresTask.h"
#include <iostream>
#include <atomic>

using namespace wfpg;
using namespace wfpg::protocol;

std::atomic<int> exit_code{0};

int main(int argc, char *argv[])
{
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <pg_url>\n";
        return 1;
    }

    WFFacilities::WaitGroup wait_group(2);

    // Test 1: Ordinary URL disconnect
    std::cout << "Starting Test 1...\n";
    auto* t1 = WFPostgresTaskFactory::create_disconnect_task(argv[1], 0, [&wait_group](WFPostgresTask *task) {
        std::cout << "Test 1 Callback entered. state=" << task->get_state() << " error=" << task->get_error() << "\n";
        if (task->get_state() != WFT_STATE_SUCCESS) {
            std::cerr << "Test 1 Failed: state=" << task->get_state() << " error=" << task->get_error() << "\n";
            exit_code = 1;
        } else {
            std::cout << "Test 1 Passed: Factory disconnect task completed\n";
        }
        wait_group.done();
    });
    t1->start();
    std::cout << "Test 1 started.\n";

    // Test 2: Fixed connection URL disconnect
    // Note: upper-layer wrappers may manage routing URL; ordinary users should prefer WFPostgresConnection.
    std::string fixed_url = std::string(argv[1]);
    if (fixed_url.find('?') == std::string::npos) {
        fixed_url += "?transaction=INTERNAL_CONN_ID_TEST_FACTORY_DISCONNECT";
    } else {
        fixed_url += "&transaction=INTERNAL_CONN_ID_TEST_FACTORY_DISCONNECT";
    }

    auto* t2_query = WFPostgresTaskFactory::create_postgres_task(fixed_url, 0, [fixed_url, &wait_group](WFPostgresTask *task) {
        if (task->get_state() != WFT_STATE_SUCCESS) {
            std::cerr << "Test 2 Query Failed\n";
            exit_code = 1;
            wait_group.done();
            return;
        }

        auto* t2_disc = WFPostgresTaskFactory::create_disconnect_task(fixed_url, 0, [&wait_group](WFPostgresTask *task2) {
            if (task2->get_state() != WFT_STATE_SUCCESS) {
                std::cerr << "Test 2 Disconnect Failed\n";
                exit_code = 1;
            } else {
                std::cout << "Test 2 Passed: Fixed connection factory disconnect task completed\n";
            }
            wait_group.done();
        });
        t2_disc->start();
    });
    t2_query->get_req()->set_query("SELECT 1;");
    t2_query->start();

    wait_group.wait();
    return exit_code.load();
}
