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
    if (argc != 2) {
        std::cerr << "USAGE: " << argv[0] << " <url>\n";
        return 1;
    }

    WFFacilities::WaitGroup wait_group(1);

    std::string url = argv[1];
    if (url.find('?') == std::string::npos) {
        url += "?application_name=wfpg_test&client_encoding=UTF8&timezone=UTC&pgparam.search_path=public";
    }

    WFPostgresConnection conn(1);
    conn.init(url);

    auto* task = conn.create_query_task(
        "SHOW application_name; SHOW client_encoding; SHOW TimeZone; SHOW search_path;",
        [&wait_group](WFPostgresTask *task) {
            int state = task->get_state();
            int error = task->get_error();

            if (state != WFT_STATE_SUCCESS) {
                std::cerr << "Task failed: state=" << state << " error=" << error << "\n";
                wait_group.done();
                return;
            }

            PostgresResultCursor cursor(task->get_resp());
            std::vector<PostgresCell> row;

            // application_name
            cursor.fetch_row(row);
            if (row[0].as_string() != "wfpg_test") {
                std::cerr << "FAIL: expected application_name='wfpg_test', got '" << row[0].as_string() << "'\n";
            } else {
                success_count++;
            }

            // client_encoding
            cursor.next_result_set();
            cursor.fetch_row(row);
            if (row[0].as_string() != "UTF8") {
                std::cerr << "FAIL: expected client_encoding='UTF8', got '" << row[0].as_string() << "'\n";
            } else {
                success_count++;
            }

            // TimeZone
            cursor.next_result_set();
            cursor.fetch_row(row);
            if (row[0].as_string() != "UTC") {
                std::cerr << "FAIL: expected TimeZone='UTC', got '" << row[0].as_string() << "'\n";
            } else {
                success_count++;
            }

            // search_path
            cursor.next_result_set();
            cursor.fetch_row(row);
            if (row[0].as_string().find("public") == std::string::npos) {
                std::cerr << "FAIL: expected search_path to contain 'public', got '" << row[0].as_string() << "'\n";
            } else {
                success_count++;
            }

            wait_group.done();
        }
    );

    task->start();
    wait_group.wait();
    
    conn.deinit();

    if (success_count == 4) {
        std::cout << "SUCCESS: all startup params set correctly\n";
        return 0;
    }
    
    return 1;
}
