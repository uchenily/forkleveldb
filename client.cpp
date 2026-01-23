#include <iostream>
#include <string>

#include "leveldb/db.h"

using namespace leveldb;

int main() {
    DB *db;
    Options options;
    Status status = DB::Open(options, "./client.db", &db);

    if (!status.ok()) {
        std::cerr << "Unable to open/create test database './client.db'" << std::endl;
        std::cerr << status.ToString() << std::endl;
        return -1;
    }

    std::string key;
    std::string value;
    std::string cmd;

    while (true) {
        std::cout << "leveldb> ";
        std::cin >> cmd;

        if (cmd == "set") {
            std::cin >> key >> value;
            status = db->Put(WriteOptions(), key, value);
            if (status.ok()) {
                std::cout << "OK" << std::endl;
            } else {
                std::cout << "Error setting value: " << status.ToString() << std::endl;
            }
        } else if (cmd == "get") {
            std::cin >> key;
            status = db->Get(ReadOptions(), key, &value);
            if (status.ok()) {
                std::cout << value << std::endl;
            } else {
                std::cout << "Not found" << std::endl;
            }
        } else if (cmd == "del") {
            std::cin >> key;
            status = db->Delete(WriteOptions(), key);
            if (status.ok()) {
                std::cout << "OK" << std::endl;
            } else {
                std::cout << "Error deleting key: " << status.ToString() << std::endl;
            }
        } else if (cmd == "exit") {
            break;
        } else {
            std::cout << "Unknown command. Supported commands are: set, get, del, exit" << std::endl;
        }
    }

    delete db;
    return 0;
}
