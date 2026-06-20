#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <iostream>
#include <random>
#include <arpa/inet.h>
#include "PostgresResult.h"
#include "PostgresResponse.h"

class TestResponse : public wfpg::protocol::PostgresResponse {
public:
    int append_public(const void *buf, size_t *size) {
        return this->append(buf, size);
    }
};

void fuzz_once(const std::string& data) {
    TestResponse resp;
    size_t append_size = data.size();
    int ret = resp.append_public(data.data(), &append_size);

    if (ret == 1) {
        wfpg::protocol::PostgresResultCursor cursor(&resp);
        std::vector<wfpg::protocol::PostgresCell> row;
        while (cursor.fetch_row(row)) {
            for (const auto& cell : row) {
                cell.as_string();
                cell.as_int();
                cell.as_double();
                cell.as_bool();
                cell.as_array();
            }
        }
        while (cursor.next_result_set()) {
            while (cursor.fetch_row(row)) {}
        }
    }
}

int main() {
    std::cout << "Running Custom Lightweight Fuzzer (100,000 iterations)..." << std::endl;
    std::mt19937 gen(42);
    std::uniform_int_distribution<int> len_dist(1, 2048);
    std::uniform_int_distribution<int> char_dist(0, 255);

    for (int i = 0; i < 100000; i++) {
        int len = len_dist(gen);
        std::string garbage;
        garbage.reserve(len);
        for (int j = 0; j < len; j++) {
            garbage.push_back((char)char_dist(gen));
        }
        
        // Sometimes inject valid protocol headers to bypass early length checks
        if (i % 5 == 0 && len >= 5) {
            garbage[0] = 'D';
            uint32_t fake_len = htonl(len - 1);
            memcpy(&garbage[1], &fake_len, 4);
        }

        fuzz_once(garbage);
    }
    
    std::cout << "Fuzzing complete! No memory violations or crashes detected." << std::endl;
    return 0;
}
