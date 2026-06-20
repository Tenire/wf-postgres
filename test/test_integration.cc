#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include "workflow/WFFacilities.h"
#include "WFPostgresConnection.h"

using namespace wfpg;
using namespace wfpg::protocol;

std::atomic<int> success_count{0};
std::atomic<int> fail_count{0};

void cb_insert(WFPostgresTask *task) {
    if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error()) {
        fail_count++;
        if (task->get_state() != WFT_STATE_SUCCESS) {
            std::cerr << "Fail state=" << task->get_state() << " error=" << task->get_error() << std::endl;
        } else {
            std::cerr << "Fail PG error: " << task->get_resp()->get_error().message << std::endl;
        }
    } else {
        success_count++;
    }
}

int main(int argc, char *argv[]) {
    std::string url = "postgres://wf_tester:wf_secret@127.0.0.1:5433/wf_testdb";
    if (argc > 1) {
        url = argv[1];
    }

    WFFacilities::WaitGroup wg(1);
    
    auto cb_setup = [&url, &wg](WFPostgresTask *task) {
        if (task->get_state() != WFT_STATE_SUCCESS || task->get_resp()->is_error()) {
            std::cerr << "Setup failed!" << std::endl;
            wg.done();
            return;
        }
        
        WFFacilities::WaitGroup insert_wg(90);
        
        for (int i = 0; i < 90; i++) {
            WFPostgresTask *insert_task = WFPostgresTaskFactory::create_postgres_task(url, 0, [&insert_wg, i](WFPostgresTask *t){
                cb_insert(t);
                insert_wg.done();
            });
            std::vector<std::string> params = {std::to_string(i)};
            insert_task->get_req()->set_query("INSERT INTO test_matrix (val) VALUES ($1);", params);
            insert_task->start();
        }
        
        insert_wg.wait();
        std::cout << "Inserts completed. Success: " << success_count << ", Fails: " << fail_count << std::endl;
        wg.done();
    };

    WFPostgresConnection setup_conn(1);
    setup_conn.init(url);
    WFPostgresTask *setup_task = setup_conn.create_query_task("CREATE TABLE IF NOT EXISTS test_matrix (id SERIAL PRIMARY KEY, val TEXT); TRUNCATE test_matrix;", cb_setup);
    setup_task->start();

    wg.wait();
    if (success_count.load() != 90 || fail_count.load() != 0) {
        std::cerr << "Integration test failed: success_count=" << success_count << ", fail_count=" << fail_count << std::endl;
        return 1;
    }
    return 0;
}
