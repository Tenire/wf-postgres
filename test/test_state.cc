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

    std::atomic<int> exit_code(0);

    auto *setup = conn.create_query_task("CREATE TABLE IF NOT EXISTS test_state (id SERIAL PRIMARY KEY, val TEXT);", [&conn, &wait_group, &exit_code, url](WFPostgresTask *t) {
        if (t->get_state() != WFT_STATE_SUCCESS) {
            std::cerr << "Setup failed\n";
            exit_code = 1;
            wait_group.done();
            return;
        }

        auto *copy_in = conn.create_query_task("COPY test_state(val) FROM STDIN;", [&conn, &wait_group, &exit_code, url](WFPostgresTask *t2) {
            std::cout << "COPY IN started. Attempting normal query...\n";
            auto *bad_query = conn.create_query_task("SELECT 1;", [&conn, &wait_group, &exit_code, url](WFPostgresTask *t3) {
                if (t3->get_state() == WFT_STATE_SYS_ERROR && t3->get_error() == EPROTO) {
                    std::cout << "SUCCESS: Bad query rejected with EPROTO during COPY IN.\n";
                } else {
                    std::cerr << "FAILURE: Bad query not rejected with EPROTO! State=" << t3->get_state() << " error=" << t3->get_error() << "\n";
                    exit_code = 1;
                }

                // Disconnect to recover from COPY IN
                conn.deinit();
                conn.init(url);

                // Test NOTIFY WAIT recovery
                auto *notify_wait = conn.create_query_task("LISTEN test_chan;", [&conn, &wait_group, &exit_code](WFPostgresTask *t4) {
                    if (t4->get_state() != WFT_STATE_SUCCESS) {
                        std::cerr << "Listen failed\n";
                        exit_code = 1;
                        wait_group.done();
                        return;
                    }
                    
                    auto *wait = conn.create_query_task("", [&conn, &wait_group, &exit_code](WFPostgresTask *t5) {
                        std::cout << "Wait notification finished. Attempting normal query...\n";
                        auto *good_query = conn.create_query_task("SELECT 1;", [&wait_group, &exit_code](WFPostgresTask *t6) {
                            if (t6->get_state() == WFT_STATE_SUCCESS) {
                                std::cout << "SUCCESS: Query succeeded after wait notification.\n";
                            } else {
                                std::cerr << "FAILURE: Query failed after wait notification! State=" << t6->get_state() << "\n";
                                exit_code = 1;
                            }
                            wait_group.done();
                        });
                        good_query->start();
                    });
                    wait->get_req()->set_wait_notification(true);
                    
                    auto *notify = conn.create_query_task("NOTIFY test_chan;", nullptr);
                    wait->start();
                    notify->start();
                });
                notify_wait->start();
            });
            bad_query->start();
        });
        copy_in->start();
    });
    setup->start();
    wait_group.wait();

    // Verify Goal 4 API accessors in a separate task
    if (exit_code == 0) {
        WFFacilities::WaitGroup wg2(1);
        auto *test_api = conn.create_query_task("SELECT * FROM unknown_table;", [&conn, &wg2, &exit_code](WFPostgresTask *t) {
            if (t->get_state() != WFT_STATE_SUCCESS) {
                std::cerr << "FAILURE: expected SUCCESS state for SQL error.\n";
                exit_code = 1;
            }
            auto *resp = t->get_resp();
            if (resp->is_error()) {
                if (resp->get_error_msg().find("unknown_table") != std::string::npos && resp->get_sql_state() == "42P01") {
                    std::cout << "SUCCESS: get_error_msg and get_sql_state correct.\n";
                } else {
                    std::cerr << "FAILURE: error accessors not returning expected values.\n";
                    exit_code = 1;
                }
            } else {
                std::cerr << "FAILURE: expected error for unknown_table.\n";
                exit_code = 1;
            }

            auto *test_api2 = conn.create_query_task("INSERT INTO test_state(val) VALUES ('abc');", [&wg2, &exit_code](WFPostgresTask *t2) {
                auto *resp2 = t2->get_resp();
                PostgresResultCursor cursor(resp2);
                cursor.next_result_set();
                if (cursor.get_affected_rows() == 1) {
                    std::cout << "SUCCESS: get_affected_rows correct.\n";
                } else {
                    std::cerr << "FAILURE: expected 1 affected row, got " << cursor.get_affected_rows() << ".\n";
                    exit_code = 1;
                }
                wg2.done();
            });
            test_api2->start();
        });
        test_api->start();
        wg2.wait();
    }

    return exit_code.load();
}
