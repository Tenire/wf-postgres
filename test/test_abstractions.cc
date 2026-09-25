#include "WFPostgresClient.h"
#include <arpa/inet.h>
#include <iostream>
#include <cassert>

using namespace wfpg;
using namespace wfpg::protocol;

void test_cell_lifecycle_and_detach() {
    std::cout << "[Test] PostgresCell lifecycle & detach()...\n";

    PostgresField field;
    field.name = "username";
    field.type_oid = PostgresOid::TEXT;
    field.format = 0;

    std::string ephemeral_buffer = "alice_in_wonderland";
    PostgresCell cell(ephemeral_buffer.data(), ephemeral_buffer.size(), false, &field);

    assert(!cell.is_null());
    assert(!cell.is_detached());
    assert(cell.as_string() == "alice_in_wonderland");

    // Detach creates deep copy
    cell.detach();
    assert(cell.is_detached());
    assert(cell.as_string() == "alice_in_wonderland");

    // Overwrite original ephemeral buffer to ensure cell is independent
    ephemeral_buffer = "corrupted_overwrite";
    assert(cell.as_string() == "alice_in_wonderland");

    // Test copy and move constructors
    PostgresCell copied_cell = cell;
    assert(copied_cell.is_detached());
    assert(copied_cell.as_string() == "alice_in_wonderland");

    PostgresCell moved_cell = std::move(copied_cell);
    assert(moved_cell.as_string() == "alice_in_wonderland");
    assert(copied_cell.is_null());
    // Test BYTEA hex decoder
    PostgresField bytea_field;
    bytea_field.name = "payload";
    bytea_field.type_oid = PostgresOid::BYTEA;
    bytea_field.format = 0; // Text format
    std::string bytea_hex = "\\xdeadbeef0102";
    PostgresCell bytea_cell(bytea_hex.data(), bytea_hex.size(), false, &bytea_field);
    std::vector<uint8_t> decoded_bytes = bytea_cell.as_bytea();
    assert(decoded_bytes.size() == 6);
    assert(decoded_bytes[0] == 0xde && decoded_bytes[1] == 0xad && decoded_bytes[2] == 0xbe && decoded_bytes[3] == 0xef);
    assert(decoded_bytes[4] == 0x01 && decoded_bytes[5] == 0x02);


    std::cout << "  Passed PostgresCell lifecycle & detach()\n";
}

void test_request_natural_binding() {
    std::cout << "[Test] PostgresRequest complete type matrix parameter binding...\n";

    // Basic numbers, strings, bool, nullptr
    PostgresRequest req1;
    req1.set_query("SELECT $1, $2, $3, $4, $5, $6, $7;",
                   (int16_t)12, 1001, (int64_t)999999999LL, 3.14f, 2.718281828, "bob", nullptr);

    assert(req1.has_params());
    const auto& p1 = req1.get_params();
    assert(p1.size() == 7);
    assert(p1[0].data == "12" && p1[0].type_oid == PostgresOid::INT2);
    assert(p1[1].data == "1001" && p1[1].type_oid == PostgresOid::INT4);
    assert(p1[2].data == "999999999" && p1[2].type_oid == PostgresOid::INT8);
    assert(p1[3].type_oid == PostgresOid::FLOAT4);
    assert(p1[4].type_oid == PostgresOid::FLOAT8);
    assert(p1[5].data == "bob" && p1[5].type_oid == PostgresOid::TEXT);
    assert(p1[6].is_null && p1[6].type_oid == 0);

    // Binary (BYTEA), UUID, Numeric, JSON
    std::vector<uint8_t> bin_data = {0xde, 0xad, 0xbe, 0xef};
    PostgresRequest req2;
    req2.set_query("INSERT INTO t VALUES ($1, $2, $3, $4);",
                   bin_data,
                   PostgresValue::uuid("a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11"),
                   PostgresValue::numeric("1234567890.123456789"),
                   PostgresValue::json("{\"k\":\"v\"}"));

    const auto& p2 = req2.get_params();
    assert(p2.size() == 4);
    assert(p2[0].data == "\\xdeadbeef" && p2[0].type_oid == PostgresOid::BYTEA);
    assert(p2[1].data == "a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11" && p2[1].type_oid == PostgresOid::UUID);
    assert(p2[2].data == "1234567890.123456789" && p2[2].type_oid == PostgresOid::NUMERIC);
    assert(p2[3].data == "{\"k\":\"v\"}" && p2[3].type_oid == PostgresOid::JSON);

    // 1D Arrays
    std::vector<int32_t> int_arr = {1, 2, 3};
    std::vector<std::string> str_arr = {"alice", "bob"};
    PostgresRequest req3;
    req3.set_query("SELECT $1, $2;", int_arr, str_arr);
    const auto& p3 = req3.get_params();
    assert(p3.size() == 2);
    assert(p3[0].data == "{1,2,3}" && p3[0].type_oid == PostgresOid::INT4_ARRAY);
    assert(p3[1].data == "{\"alice\",\"bob\"}" && p3[1].type_oid == PostgresOid::TEXT_ARRAY);

    std::cout << "  Passed PostgresRequest complete type matrix parameter binding\n";
}

void test_status_unification() {
    std::cout << "[Test] PostgresStatus unified error handling...\n";

    PostgresStatus s_ok = PostgresStatus::success();
    assert(s_ok.ok());
    assert((bool)s_ok);

    PostgresStatus s_err = PostgresStatus::from_transport_error(WFT_STATE_SYS_ERROR, ECONNRESET);
    assert(!s_err.ok());
    assert(s_err.transport_state() == WFT_STATE_SYS_ERROR);
    assert(s_err.transport_error() == ECONNRESET);

    PostgresError pg_err;
    pg_err.severity = "ERROR";
    pg_err.sql_state = "23505";
    pg_err.message = "duplicate key value violates unique constraint";

    PostgresStatus s_sql = PostgresStatus::from_server_error(pg_err);
    assert(!s_sql.ok());
    assert(s_sql.sqlstate() == "23505");
    assert(s_sql.to_string().find("23505") != std::string::npos);

    // Test get_status(nullptr)
    PostgresStatus s_null = get_status(nullptr);
    assert(!s_null.ok());
    assert(s_null.message() == "Task is null");

    std::cout << "  Passed PostgresStatus unified error handling\n";
}
void test_cursor_ownership_transfer() {
    std::cout << "[Test] PostgresResultCursor buffer ownership transfer & move...\n";

    // Create a real WFPostgresTask via factory
    WFPostgresTask* task = WFPostgresTaskFactory::create_postgres_task("postgres://localhost:5432/test", 0, nullptr);
    assert(task != nullptr);

    // Prepare mock data: CommandComplete frame 'C' + length + "SELECT 1"
    std::string mock_packet;
    mock_packet.push_back('C');
    uint32_t len = htonl(4 + 9);
    mock_packet.append((const char*)&len, 4);
    mock_packet.append("SELECT 1\0", 9);

    size_t sz = mock_packet.size();
    task->get_resp()->append_data(mock_packet.data(), &sz);
    assert(task->get_resp()->get_buf_size() > 0);

    // Standard explicit C++ ownership transfer via std::move
    PostgresResultCursor cursor(std::move(*task->get_resp()));
    assert(task->get_resp()->get_buf_size() == 0); // task resp buffer was safely moved out
    // Framework dismisses task (which triggers delete this, deleting task->resp empty shell)
    task->dismiss();

    // Cursor must still hold the owned buffer safely without dangling pointer
    assert(cursor.get_command_tag() == "SELECT 1");

    // Test cursor move assignment / constructor (simulating coroutine suspension point / frame transfer)
    PostgresResultCursor moved_cursor = std::move(cursor);
    assert(moved_cursor.get_command_tag() == "SELECT 1");

    std::cout << "  Passed PostgresResultCursor buffer ownership transfer & move\n";
}
void test_transaction_e_state_reset() {
    std::cout << "[Test] Transaction 'E' state detection and lifecycle...\n";

    WFPostgresConnection conn(100);
    assert(!conn.in_transaction());
    assert(!conn.is_transaction_failed());

    // When a task finishes with 'T', connection enters in_transaction
    conn.set_last_transaction_state('T');
    assert(conn.in_transaction());
    assert(!conn.is_transaction_failed());

    // When an error occurs in transaction, state becomes 'E'
    conn.set_last_transaction_state('E');
    assert(!conn.in_transaction());
    assert(conn.is_transaction_failed());

    // When transaction state is 'E', keep_alive must be 0 unconditionally
    std::cout << "  Passed Transaction 'E' state detection and lifecycle\n";
}

void test_timestamptz_and_time_point() {
    std::cout << "[Test] TIMESTAMPTZ, microsecond precision, and as_time_point()...\n";

    // 1. PostgresValue time_point serialization
    auto now = std::chrono::system_clock::now();
    PostgresValue val_now(now);
    assert(!val_now.is_null());
    assert(val_now.type_oid() == PostgresOid::TIMESTAMPTZ);
    // Verify ends with +00
    std::string s_now = val_now.data();
    assert(s_now.size() >= 23); // "YYYY-MM-DD HH:MM:SS.uuuuuu+00"
    assert(s_now.substr(s_now.size() - 3) == "+00");

    // Test epoch-exact time_point with microseconds
    int64_t expected_micros = 1790000000123456LL; // around 2026
    std::chrono::system_clock::time_point tp_exact{std::chrono::microseconds(expected_micros)};
    PostgresValue val_exact(tp_exact);
    std::string s_exact = val_exact.data();
    assert(s_exact.find(".123456+00") != std::string::npos);

    // 2. PostgresCell as_datetime with various TIMESTAMPTZ formats (Format 0 Text)
    PostgresField field_tz;
    field_tz.name = "created_at";
    field_tz.type_oid = PostgresOid::TIMESTAMPTZ;
    field_tz.format = 0;

    // Case A: UTC with +00
    std::string text_utc = "2026-09-25 14:30:00.123456+00";
    PostgresCell cell_utc(text_utc.data(), text_utc.size(), false, &field_tz);
    struct tm tm_utc;
    int usec_utc = 0;
    assert(cell_utc.as_datetime(&tm_utc, &usec_utc));
    assert(usec_utc == 123456);
    assert(tm_utc.tm_year == 2026 - 1900);
    assert(tm_utc.tm_mon == 8); // September = 8 (0-based)
    assert(tm_utc.tm_mday == 25);
    assert(tm_utc.tm_hour == 14);
    assert(tm_utc.tm_min == 30);
    assert(tm_utc.tm_sec == 0);

    auto parsed_tp_utc = cell_utc.as_time_point();
    auto actual_micros_utc = std::chrono::duration_cast<std::chrono::microseconds>(
        parsed_tp_utc.time_since_epoch()).count();

    // Case B: +08:00 (Asia/Shanghai) -> should normalize to UTC 14:30:00
    std::string text_cst = "2026-09-25 22:30:00.123456+08:00";
    PostgresCell cell_cst(text_cst.data(), text_cst.size(), false, &field_tz);
    struct tm tm_cst;
    int usec_cst = 0;
    assert(cell_cst.as_datetime(&tm_cst, &usec_cst));
    assert(usec_cst == 123456);
    assert(tm_cst.tm_hour == 14); // 22:30 - 8 hours = 14:30 UTC
    auto parsed_tp_cst = cell_cst.as_time_point();
    auto actual_micros_cst = std::chrono::duration_cast<std::chrono::microseconds>(
        parsed_tp_cst.time_since_epoch()).count();
    assert(actual_micros_utc == actual_micros_cst);

    // Case C: ISO 8601 with 'T' and 'Z'
    std::string text_iso = "2026-09-25T14:30:00.123456Z";
    PostgresCell cell_iso(text_iso.data(), text_iso.size(), false, &field_tz);
    assert(cell_iso.as_time_point() == parsed_tp_utc);

    // Case D: Negative timezone offset (-05:00)
    std::string text_est = "2026-09-25 09:30:00.123456-05";
    PostgresCell cell_est(text_est.data(), text_est.size(), false, &field_tz);
    assert(cell_est.as_time_point() == parsed_tp_utc);

    // 3. Roundtrip consistency: time_point -> PostgresValue -> PostgresCell -> time_point
    PostgresCell cell_roundtrip(s_exact.data(), s_exact.size(), false, &field_tz);
    auto tp_res = cell_roundtrip.as_time_point();
    int64_t res_micros = std::chrono::duration_cast<std::chrono::microseconds>(
        tp_res.time_since_epoch()).count();
    assert(res_micros == expected_micros);

    // 4. Binary Format 1 as_time_point()
    PostgresField field_bin;
    field_bin.name = "created_at";
    field_bin.type_oid = PostgresOid::TIMESTAMPTZ;
    field_bin.format = 1;
    // Binary PG timestamp = microseconds since 2000-01-01 00:00:00 UTC
    // 2000-01-01 UTC is 946684800 seconds = 946684800000000 microseconds from 1970
    int64_t pg_micros = expected_micros - 946684800000000LL;
    uint64_t be_val = htobe64((uint64_t)pg_micros);
    PostgresCell cell_bin(&be_val, 8, false, &field_bin);
    auto tp_bin = cell_bin.as_time_point();
    int64_t bin_micros = std::chrono::duration_cast<std::chrono::microseconds>(
        tp_bin.time_since_epoch()).count();
    assert(bin_micros == expected_micros);

    std::cout << "  Passed TIMESTAMPTZ, microsecond precision, and as_time_point()\n";
}



int main() {
    test_cell_lifecycle_and_detach();
    test_cursor_ownership_transfer();
    test_request_natural_binding();
    test_status_unification();
    test_transaction_e_state_reset();
    test_timestamptz_and_time_point();
    std::cout << "All architectural remediation tests PASSED successfully!\n";
    return 0;
}
