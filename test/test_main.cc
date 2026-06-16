#include <iostream>
#include <string.h>
#include <arpa/inet.h>
#include <cassert>
#include "PostgresMessage.h"
#include "PostgresResult.h"

using namespace protocol;

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

void test_startup_error_no_z() {
    TestResponse resp;
    resp.set_is_startup(true);
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
    assert(cursor.fetch_row(row) == false); // Should reject malformed D frame
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
    assert(cursor.fetch_row(row) == false);
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
    data += make_frame('C', "SELECT 1\0");
    
    // Result Set 2 (NoData)
    data += make_frame('n', "");
    data += make_frame('C', "SELECT 0\0");
    data += make_frame('Z', "I");
    
    size_t size = data.size();
    resp.append_public(data.c_str(), &size);
    
    PostgresResultCursor cursor(&resp);
    std::vector<PostgresCell> row;
    int count = 0;
    while(cursor.fetch_row(row)) { count++; }
    assert(count == 1);
    assert(cursor.get_command_tag() == "SELECT 1");
    
    assert(cursor.next_result_set() == true);
    count = 0;
    while(cursor.fetch_row(row)) { count++; }
    assert(count == 0);
    assert(cursor.get_command_tag() == "SELECT 0");
    
    assert(cursor.next_result_set() == false);
}

int main() {
    std::cout << "Running Workflow Postgres Plugin Tests..." << std::endl;
    test_parser();
    test_malformed_frame();
    test_startup_error_no_z();
    test_malformed_metadata_t();
    test_malformed_data_d();
    test_malformed_command_c();
    test_malformed_sasl_mechanism_list();
    test_multi_result();
    std::cout << "All tests passed!" << std::endl;
    return 0;
}
