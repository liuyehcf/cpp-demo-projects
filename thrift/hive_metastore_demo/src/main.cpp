#include <sasl/sasl.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>

#include <iostream>
#include <memory>
#include <utility>

#include "TSasl.h"
#include "TSaslClientTransport.h"
#include "ThriftHiveMetastore.h"

using namespace apache::thrift;
using namespace apache::thrift::transport;
using namespace apache::thrift::protocol;

int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "requires 5 arguments" << std::endl;
        return 1;
    }
    const std::string hms_ip = argv[1];
    const int hms_port = std::atoi(argv[2]);
    const std::string hms_principal = argv[3];
    const std::string db_name = argv[4];
    const std::string table_name = argv[5];

    std::cout << "hms_ip: " << hms_ip << ", hms_port: " << hms_port << ", hms_principal: " << hms_principal
              << ", db_name: " << db_name << ", table_name: " << table_name << std::endl;

    bool use_sasl = !hms_principal.empty();

    auto execute = [](std::shared_ptr<TProtocol> protocol, const std::string& db_name, const std::string& table_name) {
        Apache::Hadoop::Hive::ThriftHiveMetastoreClient client(std::move(protocol));

        // Fetch and print the list of databases
        std::vector<std::string> databases;
        client.get_all_databases(databases);
        std::cout << "Databases:" << std::endl;
        for (const auto& db : databases) {
            std::cout << "    " << db << std::endl;
        }

        // Fetch and print the list of tables in a specific database
        std::vector<std::string> tables;
        client.get_all_tables(tables, db_name);
        std::cout << "Tables in database '" << db_name << "':" << std::endl;
        for (const auto& table : tables) {
            std::cout << "    " << table << std::endl;
        }

        Apache::Hadoop::Hive::GetTableResult get_table_res;
        Apache::Hadoop::Hive::GetTableRequest get_table_req;
        get_table_req.dbName = db_name;
        get_table_req.tblName = table_name;
        client.get_table_req(get_table_res, get_table_req);
        Apache::Hadoop::Hive::Table table = get_table_res.table;
        std::cout << "Table details for '" << table_name << "':" << std::endl;
        std::cout << "    Table name: " << table.tableName << std::endl;
        std::cout << "    Database name: " << table.dbName << std::endl;
        std::cout << "    Owner: " << table.owner << std::endl;
        std::cout << "    Create time: " << table.createTime << std::endl;
        std::cout << "    Location: " << table.sd.location << std::endl;
    };

    try {
        if (use_sasl) {
            int result = sasl_client_init(nullptr);
            if (result != SASL_OK) {
                std::cerr << "Failed to initialize SASL client library: " << sasl_errstring(result, nullptr, nullptr)
                          << std::endl;
                return 1;
            }

            size_t slash_pos = hms_principal.find('/');
            size_t at_pos = hms_principal.find('@');
            const std::string service = hms_principal.substr(0, slash_pos);
            const std::string server_fqdn = hms_principal.substr(slash_pos + 1, at_pos - slash_pos - 1);
            std::cout << "service_name: " << service << ", hostname_fqdn: " << server_fqdn << std::endl;

            std::shared_ptr<TTransport> socket(new TSocket(hms_ip, hms_port));
            std::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
            std::shared_ptr<sasl::TSasl> sasl(new sasl::TSaslClient("GSSAPI",    // mechanisms
                                                                    "",          // authenticationId
                                                                    service,     // service
                                                                    server_fqdn, // serverFQDN
                                                                    {},          // props
                                                                    nullptr      // callbacks
                                                                    ));
            std::shared_ptr<TSaslClientTransport> sasl_transport(new TSaslClientTransport(sasl, transport));
            std::shared_ptr<TProtocol> protocol = std::make_shared<TBinaryProtocol>(sasl_transport);

            transport->open();
            sasl_transport->open();
            execute(protocol, db_name, table_name);
            sasl_transport->close();
            transport->close();
        } else {
            std::shared_ptr<TTransport> socket(new TSocket(hms_ip, hms_port));
            std::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
            std::shared_ptr<TProtocol> protocol = std::make_shared<TBinaryProtocol>(transport);

            transport->open();
            execute(protocol, db_name, table_name);
            transport->close();
        }

    } catch (TException& tx) {
        std::cerr << "Exception occurred: " << tx.what() << std::endl;
    }

    return 0;
}
