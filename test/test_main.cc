#include <iostream>
#include <string.h>
#include <arpa/inet.h>
#include <cassert>
#include "PostgresResult.h"
#include "PostgresResponse.h"
#include "../src/protocol/PostgresInternal.h"

using namespace wfpg;
using namespace wfpg::protocol;

std::string make_frame(char type, const std::string& payload) {
    std::string frame;
    frame.push_back(type);
    uint32_t len = htonl(payload.size() + 4);
    frame.append((char*)&len, 4);
    frame.append(payload);
    return frame;
}

class TestResponse : public PostgresResponse {
public:
    int append_public(const void *buf, size_t *size) {
        return this->append(buf, size);
    }
};

void test_parser() {
    TestResponse resp;
    
    std::string data;
    data += make_frame('R', std::string(4, '\0')); // AuthOk
    data += make_frame('S', "server_version\0 18.0\0");
    data += make_frame('Z', "I"); // Idle
    
    // Add some pipeline garbage that shouldn't be consumed
    data += "garbage";

    size_t size = data.size();
    
    int ret = resp.append_public(data.c_str(), &size);
    assert(ret == 1);
    assert(size == data.size() - 7);
              
    // Check over-read garbage truncation
    assert(resp.get_buf_size() == size);
}

void test_malformed_frame() {
    TestResponse resp;
    std::string data;
    data.push_back('R');
    uint32_t bad_len = htonl(2); // Length < 4
    data.append((char*)&bad_len, 4);
    
    size_t size = data.size();
    int ret = resp.append_public(data.c_str(), &size);
    assert(ret == 1);
    assert(resp.is_error());
    assert(resp.get_error().message.find("< 4") != std::string::npos);
}

void test_parameter_and_backend_key() {
    TestResponse resp;
    
    // Construct S (ParameterStatus) frame: "client_encoding" = "UTF8"
    std::string param_data;
    param_data.append("client_encoding");
    param_data.push_back('\0');
    param_data.append("UTF8");
    param_data.push_back('\0');
    std::string s_frame = make_frame('S', param_data);
    
    // Construct K (BackendKeyData) frame: PID 1234, Secret 5678
    std::string k_data;
    uint32_t pid = htonl(1234);
    uint32_t secret = htonl(5678);
    k_data.append((char*)&pid, 4);
    k_data.append((char*)&secret, 4);
    std::string k_frame = make_frame('K', k_data);
    
    std::string data = s_frame + k_frame;
    size_t size = data.size();
    resp.append_public(data.c_str(), &size);
    
    // Verify results
    assert(resp.get_parameters().at("client_encoding") == "UTF8");
    assert(PostgresInternalAccess::get_backend_pid(&resp) == 1234);
    assert(PostgresInternalAccess::get_backend_secret_key(&resp) == 5678);
}

void test_startup_error_no_z() {
    TestResponse resp;
    PostgresInternalAccess::set_is_startup(&resp, true);
    std::string data = make_frame('E', "SFATAL\0MInvalid password\0\0");
    size_t size = data.size();
    int ret = resp.append_public(data.c_str(), &size);
    assert(ret == 1);
    assert(resp.is_error());
}

void test_malformed_metadata_t() {
    TestResponse resp;
    std::string data;
    // RowDescription length 5 (no num_fields)
    data.push_back('T');
    uint32_t bad_len = htonl(5);
    data.append((char*)&bad_len, 4);
    data.push_back(0); // 1 byte garbage
    
    size_t size = data.size();
    resp.append_public(data.c_str(), &size);
    PostgresResultCursor cursor(&resp);
    assert(cursor.get_fields().empty()); // Should clear and return gracefully
}

void test_malformed_data_d() {
    TestResponse resp;
    std::string data;
    data += make_frame('T', "\0\0"); // 0 fields
    // DataRow length 5 (no num_cols)
    data.push_back('D');
    uint32_t bad_len = htonl(5);
    data.append((char*)&bad_len, 4);
    data.push_back(0);
    
    size_t size = data.size();
    resp.append_public(data.c_str(), &size);
    PostgresResultCursor cursor(&resp);
    std::vector<PostgresCell> row;
    bool ok1 = cursor.fetch_row(row);
    assert(ok1 == false); // Should reject malformed D frame
}

void test_malformed_command_c() {
    TestResponse resp;
    std::string data;
    data.push_back('C');
    uint32_t bad_len = htonl(4); // No payload
    data.append((char*)&bad_len, 4);
    data.push_back('Z');
    uint32_t z_len = htonl(5);
    data.append((char*)&z_len, 4);
    data.push_back('I');
    
    size_t size = data.size();
    resp.append_public(data.c_str(), &size);
    PostgresResultCursor cursor(&resp);
    std::vector<PostgresCell> row;
    bool ok2 = cursor.fetch_row(row);
    assert(ok2 == false);
    assert(cursor.get_command_tag() == ""); // Should safely ignore tag if len=4
}

void test_malformed_sasl_mechanism_list() {
    TestResponse resp;
    std::string data;
    data.push_back('R');
    uint32_t len = htonl(21); // 4 (len) + 4 (auth_type) + 13 (string)
    data.append((char*)&len, 4);
    uint32_t auth_type = htonl(10); // SASL
    data.append((char*)&auth_type, 4);
    // mechanism list without terminating NUL
    data.append("SCRAM-SHA-256"); // No \0 at the end!
    
    size_t size = data.size();
    resp.append_public(data.c_str(), &size);
    if (!resp.is_error() || resp.get_error().message.find("does not support") == std::string::npos) {
        std::cerr << "FAILED! is_error: " << resp.is_error() 
                  << " message: '" << resp.get_error().message << "'" << std::endl;
    }
    assert(resp.is_error() == true);
    assert(resp.get_error().message.find("does not support") != std::string::npos);
}

void test_multi_result() {
    TestResponse resp;
    std::string data;
    // RowDescription: 1 field
    std::string rd;
    int16_t num_fields = htons(1);
    rd.append((char*)&num_fields, 2);
    rd += "col1"; rd.push_back('\0');
    uint32_t zero32 = 0; uint16_t zero16 = 0;
    rd.append((char*)&zero32, 4); rd.append((char*)&zero16, 2);
    rd.append((char*)&zero32, 4); rd.append((char*)&zero16, 2);
    rd.append((char*)&zero32, 4); rd.append((char*)&zero16, 2);
    
    data += make_frame('T', rd);
    // DataRow: 1 col, len 1, "A"
    std::string dr;
    dr.append((char*)&num_fields, 2);
    uint32_t col_len = htonl(1);
    dr.append((char*)&col_len, 4);
    dr += "A";
    data += make_frame('D', dr);
    data += make_frame('C', std::string("SELECT 1\0", 9));
    
    // Result Set 2 (NoData)
    data += make_frame('n', "");
    data += make_frame('C', std::string("SELECT 0\0", 9));
    data += make_frame('Z', "I");
    
    size_t size = data.size();
    resp.append_public(data.c_str(), &size);
    
    PostgresResultCursor cursor(&resp);
    std::vector<PostgresCell> row;
    int count = 0;
    while(cursor.fetch_row(row)) { count++; }
    assert(count == 1);
    assert(cursor.get_command_tag() == "SELECT 1");
    
    bool has_next1 = cursor.next_result_set();
    assert(has_next1 == true);
    count = 0;
    while(cursor.fetch_row(row)) { count++; }
    assert(count == 0);
    assert(cursor.get_command_tag() == "SELECT 0");
    
    bool has_next2 = cursor.next_result_set();
    assert(has_next2 == false);
}

void test_map_fetch() {
    TestResponse resp;
    std::string data;
    // RowDescription: 2 fields (id, name)
    std::string rd;
    int16_t num_fields = htons(2);
    rd.append((char*)&num_fields, 2);
    rd += "id"; rd.push_back('\0');
    uint32_t zero32 = 0; uint16_t zero16 = 0;
    rd.append((char*)&zero32, 4); rd.append((char*)&zero16, 2);
    rd.append((char*)&zero32, 4); rd.append((char*)&zero16, 2);
    rd.append((char*)&zero32, 4); rd.append((char*)&zero16, 2);
    
    rd += "name"; rd.push_back('\0');
    rd.append((char*)&zero32, 4); rd.append((char*)&zero16, 2);
    rd.append((char*)&zero32, 4); rd.append((char*)&zero16, 2);
    rd.append((char*)&zero32, 4); rd.append((char*)&zero16, 2);
    
    data += make_frame('T', rd);
    
    // DataRow: 2 cols
    std::string dr;
    dr.append((char*)&num_fields, 2);
    uint32_t col_len1 = htonl(1);
    dr.append((char*)&col_len1, 4);
    dr += "1";
    uint32_t col_len2 = htonl(3);
    dr.append((char*)&col_len2, 4);
    dr += "Bob";
    data += make_frame('D', dr);
    data += make_frame('C', std::string("SELECT 1\0", 9));
    data += make_frame('Z', "I");
    
    size_t size = data.size();
    resp.append_public(data.c_str(), &size);
    
    PostgresResultCursor cursor(&resp);
    std::map<std::string, PostgresCell> row_map;
    bool ok3 = cursor.fetch_row(row_map);
    assert(ok3 == true);
    assert(row_map.at("id").as_int() == 1);
    assert(row_map.at("name").as_string() == "Bob");
    bool ok4 = cursor.fetch_row(row_map);
    assert(ok4 == false);
}

void test_jsonb_and_array() {
    TestResponse resp;
    std::string data;
    // RowDescription: 2 fields (data_jsonb, ids_array)
    std::string rd;
    int16_t num_fields = htons(2);
    rd.append((char*)&num_fields, 2);
    
    // Format = 1 (Binary) for both to trigger our new parsers
    rd += "data_jsonb"; rd.push_back('\0');
    uint32_t zero32 = 0; uint16_t zero16 = 0;
    rd.append((char*)&zero32, 4); rd.append((char*)&zero16, 2);
    rd.append((char*)&zero32, 4); rd.append((char*)&zero16, 2);
    uint16_t format1 = htons(1);
    rd.append((char*)&zero32, 4); rd.append((char*)&format1, 2);
    
    rd += "ids_array"; rd.push_back('\0');
    rd.append((char*)&zero32, 4); rd.append((char*)&zero16, 2);
    rd.append((char*)&zero32, 4); rd.append((char*)&zero16, 2);
    rd.append((char*)&zero32, 4); rd.append((char*)&format1, 2);
    
    data += make_frame('T', rd);
    
    // DataRow
    std::string dr;
    dr.append((char*)&num_fields, 2);
    
    // JSONB: \x01 + {"key": "val"}
    std::string jsonb_str = "{\"key\": \"val\"}";
    uint32_t col_len1 = htonl(1 + jsonb_str.size());
    dr.append((char*)&col_len1, 4);
    dr.push_back(1); // JSONB version
    dr += jsonb_str;
    
    // Array: {1, 2}
    std::string arr_data;
    uint32_t ndims = htonl(1); arr_data.append((char*)&ndims, 4);
    uint32_t has_nulls = htonl(0); arr_data.append((char*)&has_nulls, 4);
    uint32_t element_oid = htonl(23); arr_data.append((char*)&element_oid, 4); // INT4
    uint32_t dim_len = htonl(2); arr_data.append((char*)&dim_len, 4);
    uint32_t lbound = htonl(1); arr_data.append((char*)&lbound, 4);
    
    uint32_t elem_len = htonl(4);
    uint32_t val1 = htonl(1);
    arr_data.append((char*)&elem_len, 4); arr_data.append((char*)&val1, 4);
    uint32_t val2 = htonl(2);
    arr_data.append((char*)&elem_len, 4); arr_data.append((char*)&val2, 4);
    
    uint32_t col_len2 = htonl(arr_data.size());
    dr.append((char*)&col_len2, 4);
    dr += arr_data;
    
    data += make_frame('D', dr);
    data += make_frame('C', std::string("SELECT 1\0", 9));
    data += make_frame('Z', "I");
    
    size_t size = data.size();
    resp.append_public(data.c_str(), &size);
    
    PostgresResultCursor cursor(&resp);
    std::map<std::string, PostgresCell> row_map;
    bool ok5 = cursor.fetch_row(row_map);
    assert(ok5 == true);
    
    for (auto& kv : row_map) {
        std::cerr << "Key: '" << kv.first << "'" << std::endl;
    }
    
    std::cerr << "Checking data_jsonb..." << std::endl;
    assert(row_map.count("data_jsonb") == 1);
    assert(row_map.at("data_jsonb").as_jsonb_string() == "{\"key\": \"val\"}");
    
    std::cerr << "Checking ids_array..." << std::endl;
    assert(row_map.count("ids_array") == 1);
    auto arr = row_map.at("ids_array").as_array();
    assert(arr.size() == 2);
    assert(arr[0].as_int() == 1);
    assert(arr[1].as_int() == 2);
}

void test_transaction_state() {
    TestResponse resp;
    size_t size;
    
    // Simulate 'Z' frame with 'T' (In Transaction)
    std::string packet = make_frame('Z', "T");
    size = packet.size();
    int ret = resp.append_public(packet.data(), &size);
    assert(ret == 1);
    assert(size == packet.size());
    assert(resp.get_transaction_state() == 'T');

    // Simulate 'Z' frame with 'E' (Failed Transaction)
    TestResponse resp2;
    packet = make_frame('Z', "E");
    size = packet.size();
    ret = resp2.append_public(packet.data(), &size);
    assert(ret == 1);
    assert(size == packet.size());
    assert(resp2.get_transaction_state() == 'E');
}

void test_notification() {
    TestResponse resp;
    size_t size;
    
    // NotificationResponse: 'A' + len + pid(4) + channel(\0) + payload(\0)
    std::string packet;
    uint32_t pid = htonl(9999);
    packet.append((char*)&pid, 4);
    packet.append("test_channel");
    packet.push_back('\0');
    packet.append("test_payload");
    packet.push_back('\0');
    
    std::string frame = make_frame('A', packet);
    frame += make_frame('Z', "I");
    size = frame.size();
    int ret = resp.append_public(frame.data(), &size);
    assert(ret == 1);
    
    const auto& notifs = resp.get_notifications();
    assert(notifs.size() == 1);
    assert(notifs[0].backend_pid == 9999);
    assert(notifs[0].channel == "test_channel");
    assert(notifs[0].payload == "test_payload");
}

void test_copy() {
    TestResponse resp;
    size_t size;
    
    // Simulate 'H' (CopyOutResponse)
    std::string packet_H;
    packet_H.append("\0", 1); // text format
    uint16_t num_cols = htons(2);
    packet_H.append((char*)&num_cols, 2);
    uint16_t fmt = htons(0); // text
    packet_H.append((char*)&fmt, 2);
    packet_H.append((char*)&fmt, 2);
    std::string frame_H = make_frame('H', packet_H);
    
    // Simulate 'd' (CopyData)
    std::string frame_d = make_frame('d', "1\t2\n");
    
    // Simulate 'c' (CopyDone)
    std::string frame_c = make_frame('c', "");
    
    // Simulate 'C' (CommandComplete)
    std::string frame_C = make_frame('C', std::string("COPY 1\0", 7));
    
    // Simulate 'Z' (ReadyForQuery)
    std::string frame_Z = make_frame('Z', "I");
    
    // Combine them all
    std::string full_stream = frame_H + frame_d + frame_c + frame_C + frame_Z;
    
    size = full_stream.size();
    int ret = resp.append_public(full_stream.data(), &size);
    assert(ret == 1); // finishes on 'Z'
    
    PostgresResultCursor cursor(&resp);
    std::vector<PostgresCell> row;
    bool has_row = cursor.fetch_row(row);
    assert(has_row);
    assert(row.size() == 1); // It's treated as one block of data per 'd' frame
    assert(row[0].as_string() == "1\t2\n");
}

int main() {
    std::cout << "Running Workflow Postgres Plugin Tests..." << std::endl;
    std::cerr << "test_parser..." << std::endl;
    test_parser();
    std::cerr << "test_malformed..." << std::endl;
    test_malformed_sasl_mechanism_list();
    std::cerr << "test_multi_result..." << std::endl;
    test_multi_result();
    std::cerr << "test_parameter..." << std::endl;
    test_parameter_and_backend_key();
    std::cerr << "test_map_fetch..." << std::endl;
    test_map_fetch();
    std::cerr << "test_jsonb_and_array..." << std::endl;
    test_jsonb_and_array();
    std::cerr << "test_transaction_state..." << std::endl;
    test_transaction_state();
    std::cerr << "test_notification..." << std::endl;
    test_notification();
    std::cerr << "test_copy..." << std::endl;
    test_copy();
    std::cout << "All tests passed!" << std::endl;
    return 0;
}
