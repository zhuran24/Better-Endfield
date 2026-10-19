#pragma once
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <atomic>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)
struct TestDirectory {
    std::filesystem::path path;
    TestDirectory() {
        static std::atomic<unsigned> serial{0};
        for (unsigned attempt=0;attempt<100;++attempt) {
            path=std::filesystem::temp_directory_path()/("be-actions-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(serial++));
            if (std::filesystem::create_directory(path)) return;
        }
        throw std::runtime_error("could not create temporary test directory");
    }
    ~TestDirectory() { std::error_code error; std::filesystem::remove_all(path,error); }
};
