#include "workflow/WFFacilities.h"
#include "WFPostgresConnection.h"
#include "PostgresResult.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

using namespace wfpg;
using namespace wfpg::protocol;

struct Result {
    int state = -1;
    int error = 0;
    bool sql_error = false;
    int pid = 0;
    long long value = -1;
};

static Result query(WFPostgresConnection& conn, const std::string& sql, int keepalive = -2,
                    bool parameters = false)
{
    Result result;
    WFFacilities::WaitGroup done(1);
    postgres_callback_t callback = [&](WFPostgresTask *task) {
        result.state = task->get_state();
        result.error = task->get_error();
        result.sql_error = task->get_resp()->is_error();
        result.pid = task->get_resp()->get_backend_pid();
        if (result.state == WFT_STATE_SUCCESS && !result.sql_error) {
            PostgresResultCursor cursor(task->get_resp());
            std::vector<PostgresCell> row;
            if (cursor.fetch_row(row)) result.value = row[0].as_int();
        }
        done.done();
    };
    WFPostgresTask *task = parameters
        ? conn.create_query_task(sql, std::vector<PostgresParameter>{PostgresParameter("7")}, callback)
        : conn.create_query_task(sql, callback);
    if (keepalive != -2) task->set_keep_alive(keepalive);
    task->set_receive_timeout(3000);
    task->start();
    done.wait();
    return result;
}

static void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

static bool ok(const Result& result)
{
    return result.state == WFT_STATE_SUCCESS && !result.sql_error;
}

static void disconnect(WFPostgresConnection& conn)
{
    WFFacilities::WaitGroup done(1);
    auto *task = conn.create_disconnect_task([&](WFPostgresTask *t) {
        require(t->get_state() == WFT_STATE_SUCCESS, "explicit disconnect failed");
        done.done();
    });
    task->start();
    done.wait();
}

static void idle_expiry(const std::string& url, int id, bool parameters)
{
    WFPostgresConnection conn(id);
    require(conn.init(url) == 0, "connection init failed");
    auto first = query(conn, "SELECT pg_backend_pid()", 100);
    require(ok(first) && first.value == first.pid, "initial backend query failed");
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    auto after = query(conn, parameters ? "SELECT $1::int" : "SELECT 7", -2, parameters);
    require(ok(after) && after.value == 7 && after.pid != first.pid,
            "default idle reconnect did not execute query on new backend");
    disconnect(conn);
    require(ok(query(conn, "SELECT 7")), "explicit disconnect did not reset logical session");
    disconnect(conn);
    std::cout << "PASS default idle expiry parameters=" << parameters << '\n';
}

static void server_expiry(const std::string& url)
{
    WFPostgresConnection conn(82004);
    conn.init(url);
    require(ok(query(conn, "SET idle_session_timeout='100ms'")), "server timeout setup failed");
    auto before = query(conn, "SELECT pg_backend_pid()");
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    auto after = query(conn, "SELECT 7");
    require(ok(after) && after.value == 7 && after.pid != before.pid, "server expiry idle reconnect failed");
    disconnect(conn);
    std::cout << "PASS server idle expiry\n";
}

static void transaction_series(const std::string& url)
{
    WFPostgresConnection conn(82005);
    conn.init(url);
    require(ok(query(conn, "SET idle_in_transaction_session_timeout='100ms'")), "transaction timeout setup failed");
    WFPostgresConnection observer(82010);
    observer.init(url);
    const std::string sequence = "reconnect_probe_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    require(ok(query(observer, "CREATE SEQUENCE " + sequence)), "continuation probe setup failed");
    WFFacilities::WaitGroup done(1);
    int rejected = 0;
    auto *begin = conn.create_query_task("BEGIN", [&](WFPostgresTask *t) {
        require(t->get_state() == WFT_STATE_SUCCESS && conn.in_transaction(), "BEGIN state not recorded before callback");
    });
    postgres_callback_t body_callback = [&](WFPostgresTask *t) {
        require(t->get_state() == WFT_STATE_SYS_ERROR && t->get_error() == ECONNRESET, "precreated transaction body ran on replacement backend");
        require(conn.in_transaction(), "failed response default I erased transaction state");
        ++rejected;
    };
    auto *body = conn.create_query_task("SELECT nextval('" + sequence + "') + $1::int", std::vector<PostgresParameter>{PostgresParameter("7")}, std::move(body_callback));
    auto *commit = conn.create_query_task("COMMIT", [&](WFPostgresTask *t) {
        require(t->get_state() == WFT_STATE_SYS_ERROR && t->get_error() == ECONNRESET, "COMMIT ran on replacement backend");
        ++rejected;
    });
    auto *timer = WFTaskFactory::create_timer_task(350000, nullptr);
    auto *series = Workflow::create_series_work(begin, [&](const SeriesWork *) { done.done(); });
    *series << timer << body << commit;
    series->start();
    done.wait();
    require(rejected == 2, "transaction continuation not rejected");
    require(query(observer, "SELECT nextval('" + sequence + "')").value == 1,
            "rejected continuation SQL executed on replacement backend");
    require(ok(query(observer, "DROP SEQUENCE " + sequence)), "continuation probe cleanup failed");
    disconnect(observer);
    disconnect(conn);
    std::cout << "PASS precreated transaction series\n";
}

static void copy_expiry(const std::string& url)
{
    WFPostgresConnection conn(82006);
    conn.init(url);
    require(ok(query(conn, "CREATE TEMP TABLE reconnect_copy(value int)")), "COPY setup failed");
    require(ok(query(conn, "COPY reconnect_copy FROM STDIN", 100)), "COPY IN failed");
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    auto after = query(conn, "SELECT 7");
    require(after.state == WFT_STATE_SYS_ERROR && after.error == ECONNRESET, "COPY loss treated as idle session");
    disconnect(conn);
    std::cout << "PASS COPY loss protection\n";
}

static void unknown_outcome(const std::string& url)
{
    WFPostgresConnection conn(82007);
    conn.init(url);
    require(ok(query(conn, "SELECT 7")), "unknown outcome setup failed");
    require(!ok(query(conn, "SELECT pg_terminate_backend(pg_backend_pid())")), "backend termination unexpectedly succeeded");
    auto after = query(conn, "SELECT 7");
    require(after.state == WFT_STATE_SYS_ERROR && after.error == ECONNRESET, "ambiguous transport failure allowed automatic continuation");
    disconnect(conn);
    std::cout << "PASS unknown outcome protection\n";
}

static void notification_expiry(const std::string& url)
{
    WFPostgresConnection conn(82008);
    conn.init(url);
    require(ok(query(conn, "LISTEN reconnect_notification", 100)), "LISTEN setup failed");
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    WFFacilities::WaitGroup done(1);
    auto *wait = conn.create_query_task("", [&](WFPostgresTask *t) {
        require(t->get_state() == WFT_STATE_SYS_ERROR && t->get_error() == ECONNRESET, "notification wait continued after subscription loss");
        done.done();
    });
    wait->get_req()->set_wait_notification(true);
    wait->get_resp()->set_notify_mode(true);
    wait->set_receive_timeout(1000);
    wait->start();
    done.wait();
    disconnect(conn);
    std::cout << "PASS notification continuation protection\n";
}

static void failed_transaction(const std::string& url)
{
    WFPostgresConnection conn(82009);
    conn.init(url);
    require(ok(query(conn, "BEGIN")), "failed transaction setup failed");
    auto error = query(conn, "SELECT 1/0");
    require(error.state == WFT_STATE_SUCCESS && error.sql_error && conn.is_transaction_failed(), "failed transaction E was not recorded");
    auto after = query(conn, "SELECT 7");
    require(after.state == WFT_STATE_SYS_ERROR && after.error == ECONNRESET, "failed transaction socket disposal erased continuity protection");
    disconnect(conn);
    std::cout << "PASS failed transaction protection\n";
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <postgres_url>\n";
        return 1;
    }
    const std::string url = argv[1];
    idle_expiry(url, 82002, false);
    idle_expiry(url, 82003, true);
    server_expiry(url);
    transaction_series(url);
    copy_expiry(url);
    unknown_outcome(url);
    notification_expiry(url);
    failed_transaction(url);
    return 0;
}
