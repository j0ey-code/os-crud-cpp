// new header file to handle user "confirmation" 
// of file removal / delete actions in filemgr
// to be accessed by both main.cpp and FileManager,
// hence, the totally new header file

#pragma once
#include <functional>
#include <string>

// a type alias for a function that takes a warning message and
// returns true (proceed w/ deletion) or false (abort operation)
// by making this a std::function, the caller can provide
// a lambda, a free function, or anything callable
using ConfirmCallback = std::function<bool(const std::string& prompt)>;
