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
        "SELECT DATE '2026-06-20', TIME '12:34:56.123456', TIMESTAMP '2026-06-20 12:34:56.123456';",
        [&wait_group, &conn](WFPostgresTask *task) {
            if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error()) {
                std::cerr << "Task failed\n";
                wait_group.done();
                return;
            }

            PostgresResultCursor cursor(task->get_resp());
            std::vector<PostgresCell> row;
            if (cursor.fetch_row(row)) {
                struct tm t;
                int usec;

                // Date
                if (row[0].as_date(&t)) {
                    if (t.tm_year + 1900 != 2026 || t.tm_mon + 1 != 6 || t.tm_mday != 20) {
                        std::cerr << "Date mismatch: " << t.tm_year + 1900 << "-" << t.tm_mon + 1 << "-" << t.tm_mday << "\n";
                    } else {
                        success_count++;
                    }
                } else { std::cerr << "Date format failed\n"; }

                // Time
                if (row[1].as_time(&t, &usec)) {
                    if (t.tm_hour != 12 || t.tm_min != 34 || t.tm_sec != 56 || usec != 123456) {
                        std::cerr << "Time mismatch\n";
                    } else {
                        success_count++;
                    }
                } else { std::cerr << "Time format failed\n"; }

                // Timestamp
                if (row[2].as_datetime(&t, &usec)) {
                    if (t.tm_year + 1900 != 2026 || t.tm_mon + 1 != 6 || t.tm_mday != 20 ||
                        t.tm_hour != 12 || t.tm_min != 34 || t.tm_sec != 56 || usec != 123456) {
                        std::cerr << "Datetime mismatch\n";
                    } else {
                        success_count++;
                    }
                } else { std::cerr << "Datetime format failed\n"; }
            }
            
            // Test invalid strings
            auto* t2 = conn.create_query_task(
                "SELECT '2026-06-20x'::text, '12:34:56.1234567'::text, '2026-06-20 12:34:56.abc'::text;",
                [&wait_group](WFPostgresTask *task2) {
                    if (task2->get_state() == WFT_STATE_SUCCESS && !task2->get_resp()->is_error()) {
                        PostgresResultCursor cursor2(task2->get_resp());
                        std::vector<PostgresCell> row2;
                        if (cursor2.fetch_row(row2)) {
                            struct tm t2;
                            int usec2;
                            if (!row2[0].as_date(&t2)) success_count++;
                            else std::cerr << "Date invalid passed\n";

                            if (!row2[1].as_time(&t2, &usec2)) success_count++;
                            else std::cerr << "Time invalid passed\n";

                            if (!row2[2].as_datetime(&t2, &usec2)) success_count++;
                            else std::cerr << "Datetime invalid passed\n";
                        }
                    } else {
                        std::cerr << "Task2 failed\n";
                    }
                    wait_group.done();
                }
            );
            t2->start();
        }
    );

    task->start();
    wait_group.wait();
    conn.deinit();

    if (success_count == 6) {
        std::cout << "All types correct\n";
        return 0;
    }
    return 1;
}
