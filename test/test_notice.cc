#include "workflow/WFFacilities.h"
#include "WFPostgresConnection.h"
#include "PostgresResult.h"
#include <iostream>
#include <atomic>

using namespace wfpg;
using namespace wfpg::protocol;

std::atomic<int> success_count{0};

int main(int argc, char *argv[])
{
    if (argc != 2) return 1;

    WFFacilities::WaitGroup wait_group(1);
    WFPostgresConnection conn(1);
    conn.init(argv[1]);

    auto* task = conn.create_query_task(
        "DO $$ BEGIN RAISE WARNING 'wfpg warning'; END $$;",
        [&wait_group, &conn](WFPostgresTask *task) {
            if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error()) {
                std::cerr << "Task failed\n";
                wait_group.done();
                return;
            }

            PostgresResponse *resp = task->get_resp();
            if (resp->get_warning_count() == 1) {
                const auto& notices = resp->get_notices();
                if (!notices.empty() && notices[0].message.find("wfpg warning") != std::string::npos) {
                    success_count++;
                } else {
                    std::cerr << "Notice message mismatch\n";
                }
            } else {
                std::cerr << "Warning count mismatch: " << resp->get_warning_count() << "\n";
            }
            
            auto* t2 = conn.create_query_task("SELECT 1;", [&wait_group](WFPostgresTask *task2) {
                if (task2->get_state() == WFT_STATE_SUCCESS && !task2->get_resp()->is_error()) {
                    if (task2->get_resp()->get_warning_count() == 0 && task2->get_resp()->get_notices().empty()) {
                        success_count++;
                    } else {
                        std::cerr << "Notice not cleared\n";
                    }
                } else {
                    std::cerr << "Task2 failed\n";
                }
                wait_group.done();
            });
            t2->start();
        }
    );

    task->start();
    wait_group.wait();
    conn.deinit();

    if (success_count == 2) {
        std::cout << "Notice correct\n";
        return 0;
    }
    return 1;
}
