#pragma once
#include <string>

struct Result {
    bool success = false;   // safety default val. for bool variable 
    std::string message;

    static Result ok(const std::string& msg = "Success") {
        return {true, msg};
    }
    static Result fail(const std::string& msg) {
        return {false, msg};
    }
};

