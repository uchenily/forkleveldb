g++ -std=c++20 -Iinclude -Lbuild -Wl,-rpath,./build -lleveldb -lpthread client.cpp
