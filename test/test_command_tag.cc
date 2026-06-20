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

    auto* t1 = conn.create_query_task("DROP TABLE IF EXISTS wfpg_tag_test; CREATE TABLE wfpg_tag_test (id INT);", [&conn, &wait_group](WFPostgresTask *task) {
        if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error()) {
            std::cerr << "Init failed\n";
            wait_group.done(); return;
        }

        auto* t2 = conn.create_query_task("INSERT INTO wfpg_tag_test VALUES (1), (2);", [&conn, &wait_group](WFPostgresTask *task) {
            if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error()) {
                std::cerr << "INSERT query failed\n";
                wait_group.done(); return;
            }
            PostgresResultCursor cursor(task->get_resp());
            if (cursor.get_command() == "INSERT" && cursor.get_affected_rows() == 2 && cursor.get_insert_oid() == 0) success_count++;
            else std::cerr << "INSERT failed: " << cursor.get_command() << " " << cursor.get_affected_rows() << " " << cursor.get_insert_oid() << "\n";

            auto* t3 = conn.create_query_task("UPDATE wfpg_tag_test SET id=3 WHERE id=1;", [&conn, &wait_group](WFPostgresTask *task) {
                if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error()) {
                    std::cerr << "UPDATE query failed\n";
                    wait_group.done(); return;
                }
                PostgresResultCursor cursor(task->get_resp());
                if (cursor.get_command() == "UPDATE" && cursor.get_affected_rows() == 1) success_count++;
                else std::cerr << "UPDATE failed: " << cursor.get_command() << " " << cursor.get_affected_rows() << "\n";

                auto* t4 = conn.create_query_task("DELETE FROM wfpg_tag_test WHERE id=2;", [&conn, &wait_group](WFPostgresTask *task) {
                    if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error()) {
                        std::cerr << "DELETE query failed\n";
                        wait_group.done(); return;
                    }
                    PostgresResultCursor cursor(task->get_resp());
                    if (cursor.get_command() == "DELETE" && cursor.get_affected_rows() == 1) success_count++;
                    else std::cerr << "DELETE failed: " << cursor.get_command() << " " << cursor.get_affected_rows() << "\n";

                    auto* t5 = conn.create_query_task("SELECT * FROM wfpg_tag_test;", [&wait_group](WFPostgresTask *task) {
                        if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error()) {
                            std::cerr << "SELECT query failed\n";
                            wait_group.done(); return;
                        }
                        PostgresResultCursor cursor(task->get_resp());
                        std::vector<std::vector<PostgresCell>> rows;
                        cursor.fetch_all(rows);
                        if (cursor.get_command() == "SELECT" && cursor.get_affected_rows() == 1) success_count++;
                        else std::cerr << "SELECT failed: " << cursor.get_command() << " " << cursor.get_affected_rows() << "\n";
                        wait_group.done();
                    });
                    t5->start();
                });
                t4->start();
            });
            t3->start();
        });
        t2->start();
    });
    t1->start();

    wait_group.wait();
    conn.deinit();

    if (success_count == 4) {
        std::cout << "SUCCESS: command tag tests passed\n";
        return 0;
    }
    return 1;
}
