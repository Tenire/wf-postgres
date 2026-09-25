#include "workflow/WFFacilities.h"
#include "WFPostgresConnection.h"
#include "PostgresResult.h"
#include <iostream>
#include <atomic>
#include <vector>

using namespace wfpg;
using namespace wfpg::protocol;

// Removed read_copy_data

void write_copy_data(WFPostgresConnection *conn, const std::vector<std::string>& data, WFFacilities::WaitGroup *wg, std::atomic<int> *exit_code)
{
    auto *task = conn->create_query_task("", [conn, wg, exit_code](WFPostgresTask *t) {
        if (t->get_state() != WFT_STATE_SUCCESS) {
            std::cerr << "Write CopyData failed\n";
            *exit_code = 1;
            wg->done();
        } else {
            std::cout << "Write CopyData and CopyDone successful\n";
            // Check contents
            auto *check_task = conn->create_query_task("SELECT COUNT(*) FROM test_copy_in;", [wg, exit_code](WFPostgresTask *t2) {
                if (t2->get_state() != WFT_STATE_SUCCESS) {
                    std::cerr << "Count failed\n";
                    *exit_code = 1;
                } else {
                    PostgresResultCursor cursor(t2->get_resp());
                    std::vector<PostgresCell> row;
                    if (cursor.fetch_row(row) && !row.empty()) {
                        int count = row[0].as_int();
                        if (count == 2) {
                            std::cout << "COPY IN successful. Count is 2.\n";
                        } else {
                            std::cerr << "COPY IN failed. Expected 2, got " << count << "\n";
                            *exit_code = 1;
                        }
                    } else {
                        std::cerr << "Failed to fetch count\n";
                        *exit_code = 1;
                    }
                }
                wg->done();
            });
            check_task->start();
        }
    });

    std::string all_data;
    for (const auto& d : data) {
        all_data += d;
    }
    task->get_req()->set_copy_data(all_data, true); // Send CopyDone
    task->start();
}

int main(int argc, char *argv[])
{
    std::string url = "postgres://wf_tester:wf_secret@127.0.0.1:5433/wf_testdb";
    if (argc > 1) url = argv[1];

    WFFacilities::WaitGroup wait_group(2); // One for COPY OUT, one for COPY IN
    WFPostgresConnection conn_out(1);
    WFPostgresConnection conn_in(2);
    conn_out.init(url);
    conn_in.init(url);

    std::atomic<int> exit_code{0};

    // COPY OUT Test
    auto *setup_out = conn_out.create_query_task("CREATE TABLE IF NOT EXISTS test_copy (id SERIAL PRIMARY KEY, val TEXT); TRUNCATE test_copy; INSERT INTO test_copy(val) VALUES ('A'), ('B'), ('C');", [&conn_out, &wait_group, &exit_code](WFPostgresTask *t) {
        if (t->get_state() != WFT_STATE_SUCCESS) {
            std::cerr << "Setup OUT failed\n";
            exit_code = 1;
            wait_group.done();
            return;
        }

        auto *copy_out = conn_out.create_query_task("COPY test_copy(val) TO STDOUT;", [&wait_group, &exit_code](WFPostgresTask *t2) {
            if (t2->get_state() == WFT_STATE_SUCCESS && !t2->get_resp()->is_error()) {
                std::cout << "COPY TO STDOUT finished reading frames.\n";
                auto frames = t2->get_resp()->get_copy_data();
                for (const auto& f : frames) {
                    std::cout << "Read a Copy frame: " << f;
                }
                if (t2->get_resp()->is_copy_done()) {
                    std::cout << "CopyDone flag is set.\n";
                } else {
                    std::cerr << "Expected CopyDone flag to be set.\n";
                    exit_code = 1;
                }
                if (frames.size() != 3) {
                    std::cerr << "Expected 3 rows, got " << frames.size() << "\n";
                    exit_code = 1;
                } else {
                    if (frames[0] != "A\n" || frames[1] != "B\n" || frames[2] != "C\n") {
                        std::cerr << "Rows did not match A/B/C.\n";
                        exit_code = 1;
                    } else {
                        std::cout << "All rows verified successfully.\n";
                    }
                }
            } else {
                std::cerr << "COPY TO STDOUT failed\n";
                exit_code = 1;
            }
            wait_group.done();
        });
        copy_out->start();
    });

    // COPY IN Test
    auto *setup_in = conn_in.create_query_task("CREATE TABLE IF NOT EXISTS test_copy_in (val TEXT); TRUNCATE test_copy_in;", [&conn_in, &wait_group, &exit_code](WFPostgresTask *t) {
        if (t->get_state() != WFT_STATE_SUCCESS) {
            std::cerr << "Setup IN failed\n";
            exit_code = 1;
            wait_group.done();
            return;
        }

        auto *copy_in = conn_in.create_query_task("COPY test_copy_in FROM STDIN;", [&conn_in, &wait_group, &exit_code](WFPostgresTask *t2) {
            if (t2->get_state() == WFT_STATE_SUCCESS) {
                std::cout << "COPY FROM STDIN started. Writing frames...\n";
                std::vector<std::string> data = {"hello\n", "world\n"};
                write_copy_data(&conn_in, data, &wait_group, &exit_code);
            } else {
                std::cerr << "COPY FROM STDIN failed\n";
                exit_code = 1;
                wait_group.done();
            }
        });
        copy_in->start();
    });

    setup_out->start();
    setup_in->start();
    wait_group.wait();
    conn_out.deinit();
    conn_in.deinit();
    
    return exit_code.load();
}
