#include "workflow/WFFacilities.h"
#include "WFPostgresConnection.h"
#include <iostream>
#include <atomic>

using namespace wfpg;

std::atomic<int> exit_code{0};

int main(int argc, char *argv[])
{
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <pg_url>\n";
        return 1;
    }

    WFFacilities::WaitGroup wait_group(1);

    WFPostgresConnection conn(1001);
    if (conn.init(argv[1]) != 0) {
        std::cerr << "Connection init failed\n";
        return 1;
    }

    auto* query_task = conn.create_query_task("SELECT 1;", [&conn, &wait_group](WFPostgresTask *task) {
        if (task->get_state() != WFT_STATE_SUCCESS) {
            std::cerr << "Query before disconnect failed\n";
            exit_code = 1;
            wait_group.done();
            return;
        }
        std::cout << "Query succeeded, issuing connection disconnect task...\n";
        auto* disc_task = conn.create_disconnect_task([&wait_group](WFPostgresTask *t2) {
            if (t2->get_state() != WFT_STATE_SUCCESS) {
                std::cerr << "Disconnect task failed: state=" << t2->get_state() << "\n";
                exit_code = 1;
            } else {
                std::cout << "Disconnect task succeeded!\n";
            }
            wait_group.done();
        });
        disc_task->start();
    });

    query_task->start();
    wait_group.wait();
    return exit_code.load();
}
