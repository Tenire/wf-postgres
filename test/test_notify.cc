#include "workflow/WFFacilities.h"
#include "WFPostgresConnection.h"
#include <iostream>
#include <atomic>

using namespace wfpg;
using namespace wfpg::protocol;

int main(int argc, char *argv[])
{
    std::string url = "postgres://wf_tester:wf_secret@127.0.0.1:5433/wf_testdb";
    if (argc > 1) url = argv[1];

    WFFacilities::WaitGroup wait_group(1);
    WFPostgresConnection conn(1);
    conn.init(url);

    std::atomic<int> exit_code{0};

    auto *listen_task = conn.create_query_task("LISTEN my_channel;", [&conn, url, &wait_group, &exit_code](WFPostgresTask *task) {
        if (task->get_state() != WFT_STATE_SUCCESS) {
            std::cerr << "LISTEN failed\n";
            exit_code = 1;
            wait_group.done();
            return;
        }

        std::cout << "LISTEN my_channel successful.\n";

        // 2. Dispatch a waiting task (sends nothing, waits for 'A' frame)
        auto *wait_task = conn.create_query_task("", [&wait_group, &exit_code](WFPostgresTask *t) {
            if (t->get_state() == WFT_STATE_SUCCESS) {
                auto notifs = t->get_resp()->get_notifications();
                if (notifs.empty()) {
                    std::cerr << "Wait task succeeded but no notifications\n";
                    exit_code = 1;
                } else {
                    bool matched = false;
                    for (const auto& n : notifs) {
                        std::cout << "Received Notification: channel=" << n.channel 
                                  << " payload=" << n.payload << "\n";
                        if (n.channel == "my_channel" && n.payload == "hello from another connection") {
                            matched = true;
                        }
                    }
                    if (!matched) {
                        std::cerr << "Did not receive the expected notification payload/channel.\n";
                        exit_code = 1;
                    }
                }
            } else {
                std::cerr << "Wait task failed\n";
                exit_code = 1;
            }
            wait_group.done();
        });
        
        wait_task->get_req()->set_wait_notification(true);
        wait_task->get_resp()->set_notify_mode(true);
        wait_task->start();

        // 3. Fire a timer to send NOTIFY from a DIFFERENT connection
        auto *timer = WFTaskFactory::create_timer_task(500 * 1000, [url, &exit_code](WFTimerTask *) {
            auto *notify_conn = new WFPostgresConnection(2);
            notify_conn->init(url);
            auto *notify_task = notify_conn->create_query_task("NOTIFY my_channel, 'hello from another connection';", [notify_conn, &exit_code](WFPostgresTask *t) {
                if (t->get_state() != WFT_STATE_SUCCESS) {
                    std::cerr << "NOTIFY query failed.\n";
                    exit_code = 1;
                } else {
                    std::cout << "NOTIFY sent successfully.\n";
                }
                notify_conn->deinit();
                delete notify_conn;
            });
            notify_task->start();
        });
        timer->start();
    });

    listen_task->start();
    wait_group.wait();
    conn.deinit();
    
    return exit_code.load();
}
