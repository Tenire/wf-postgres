#include <iostream>
#include <string>
#include "workflow/WFFacilities.h"
#include "PostgresTask.h"
#include "PostgresResult.h"

using namespace wfpg;
using namespace wfpg::protocol;

int main(int argc, char *argv[])
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <postgres_url> <sql_query> [params...]\n"
                  << "Example: " << argv[0] << " postgres://user:pass@127.0.0.1:5432/db 'SELECT $1, $2;' '1' 'hello'" << std::endl;
        return 1;
    }

    std::string url = argv[1];
    std::string query = argv[2];
    std::vector<std::string> params;
    for (int i = 3; i < argc; ++i) {
        params.push_back(argv[i]);
    }

    WFFacilities::WaitGroup wait_group(1);

    auto callback = [&wait_group](WFPostgresTask *task) {
        int state = task->get_state();
        int error = task->get_error();

        if (state != WFT_STATE_SUCCESS) {
            std::cerr << "Task failed! State: " << state << " Error: " << error << std::endl;
        } else {
            auto* resp = task->get_resp();
            if (resp->is_error()) {
                const auto& err = resp->get_error();
                std::cerr << "PostgreSQL Error: " << err.severity << ": " << err.message << "\n";
                if (!err.detail.empty()) std::cerr << "Detail: " << err.detail << "\n";
                if (!err.hint.empty()) std::cerr << "Hint: " << err.hint << "\n";
                if (!err.position.empty()) std::cerr << "Position: " << err.position << "\n";
                std::cerr << "SQLSTATE: " << err.sql_state << "\n";
            } else {
                std::cout << "Task success! Backend has completed the query." << std::endl;
                std::cout << "Response buffer size: " << resp->get_buf_size() << std::endl;

                PostgresResultCursor cursor(resp);
                const auto& fields = cursor.get_fields();
                for (const auto& field : fields) {
                    std::cout << field.name << "\t";
                }
                std::cout << "\n----------------------------------------\n";

                std::vector<PostgresCell> row;
                while (cursor.fetch_row(row)) {
                    for (const auto& cell : row) {
                        if (cell.is_null()) {
                            std::cout << "NULL\t";
                        } else {
                            if (cell.field()->type_oid == PostgresOid::INT4) {
                                std::cout << cell.as_int() << "(int)\t";
                            } else if (cell.field()->type_oid == PostgresOid::UUID) {
                                std::cout << cell.as_uuid_string() << "(uuid)\t";
                            } else if (cell.field() && cell.field()->type_oid == PostgresOid::FLOAT4) {
                                std::cout << cell.as_float() << "(float4)\t";
                            } else if (cell.field() && cell.field()->type_oid == PostgresOid::FLOAT8) {
                                std::cout << cell.as_double() << "(float8)\t";
                            } else if (cell.field() && (cell.field()->type_oid == PostgresOid::TIMESTAMP || cell.field()->type_oid == PostgresOid::TIMESTAMPTZ)) {
                                std::cout << cell.as_datetime_string() << "(ts)\t";
                            } else if (cell.field() && cell.field()->type_oid == PostgresOid::DATE) {
                                std::cout << cell.as_date_string() << "(date)\t";
                            } else {
                                std::cout << cell.as_string() << "\t";
                            }
                        }
                    }
                    std::cout << "\n";
                }
                std::cout << "Command Tag: " << cursor.get_command_tag() << std::endl;
            }
        }

        wait_group.done();
    };

    WFPostgresTask *task = WFPostgresTaskFactory::create_postgres_task(url, 0, callback);
    if (params.empty()) {
        task->get_req()->set_query(query);
    } else {
        task->get_req()->set_query(query, params);
        task->get_req()->set_result_format(1); // request binary format
    }

    task->start();

    wait_group.wait();
    return 0;
}
