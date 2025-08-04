#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>

#include <iostream>

#include "ThriftHiveMetastore.h"

using namespace apache::thrift;
using namespace apache::thrift::transport;
using namespace apache::thrift::protocol;

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "requires 4 arguments" << std::endl;
        return 1;
    }
    const std::string hms_ip = argv[1];
    const int hms_port = std::atoi(argv[2]);
    const std::string db_name = argv[3];
    const std::string table_name = argv[4];

    std::cout << "hms_ip: " << hms_ip << ", hms_port: " << hms_port << ", db_name: " << db_name
              << ", table_name: " << table_name << std::endl;

    std::shared_ptr<TTransport> socket(new TSocket(hms_ip, hms_port));
    std::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
    std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
    Apache::Hadoop::Hive::ThriftHiveMetastoreClient client(protocol);

    try {
        transport->open();

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

        transport->close();
    } catch (TException& tx) {
        std::cerr << "Exception occurred: " << tx.what() << std::endl;
    }

    return 0;
}
