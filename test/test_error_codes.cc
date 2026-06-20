#include <iostream>
#include <string>
#include "workflow/WFFacilities.h"
#include "WFPostgresError.h"
#include "PostgresTask.h"

using namespace wfpg;
using namespace wfpg::protocol;

static int run_expect_error(const std::string& url, int expected_error, const std::string& test_name) {
    WFFacilities::WaitGroup wait_group(1);
    bool passed = false;

    auto *task = WFPostgresTaskFactory::create_postgres_task(url, 0, [&wait_group, expected_error, test_name, &passed](WFPostgresTask *t) {
        int state = t->get_state();
        int error = t->get_error();

        if (state == WFT_STATE_TASK_ERROR && error == expected_error) {
            std::cout << "SUCCESS [" << test_name << "]: correctly intercepted error " << error << std::endl;
            passed = true;
        } else {
            std::cerr << "FAILED [" << test_name << "]: Expected TASK_ERROR with error " << expected_error << "." << std::endl;
            std::cerr << "Got state: " << state << ", error: " << error << std::endl;
            passed = false;
        }

        wait_group.done();
    });

    task->get_req()->set_query("SELECT 1;");
    task->start();
    wait_group.wait();

    return passed ? 0 : 1;
}

int main(int argc, char* argv[]) {
    int ret = 0;

    // 1. SSL NOT_SUPPORTED
    if (run_expect_error("postgres://wf_tester:wf_secret@127.0.0.1:5433/wf_testdb?sslmode=require", WFT_ERR_POSTGRES_SSL_NOT_SUPPORTED, "SSL NOT_SUPPORTED") != 0) ret = 1;

    // 2. SSL CERT_FAILED
    if (run_expect_error("postgres://wf_tester:wf_secret@localhost:5434/wf_testdb?sslmode=verify-full&sslrootcert=/tmp/opencode/not-exist.crt", WFT_ERR_POSTGRES_SSL_CERT_FAILED, "SSL CERT_FAILED") != 0) ret = 1;

    // 3. AUTH_FAILED
    if (run_expect_error("postgres://wf_tester:wrong@127.0.0.1:5439/wf_testdb", WFT_ERR_POSTGRES_AUTH_FAILED, "AUTH_FAILED") != 0) ret = 1;

    return ret;
}
