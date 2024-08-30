#include <leveldb/db.h>
#include <leveldb/options.h>

#include <iostream>

int main() {
    leveldb::DB* db;
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::Status status = leveldb::DB::Open(options, "/tmp/testdb", &db);

    const std::string key = "key";

    std::string value;
    {
        db->Put({}, key, "original");
        db->Get({}, key, &value);
        std::cout << "value=" << value << std::endl;
    }

    const leveldb::Snapshot* snapshot;

    {
        snapshot = db->GetSnapshot();
        db->Put({}, key, "updated");
        db->Get({}, key, &value);
        std::cout << "updated_value=" << value << std::endl;
    }

    {
        leveldb::ReadOptions readops;
        readops.snapshot = snapshot;
        db->Get(readops, key, &value);
        std::cout << "snapshot value=" << value << std::endl;
    }
}
