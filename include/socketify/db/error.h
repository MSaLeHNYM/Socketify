#pragma once
/**
 * @file error.h
 * @brief Database error type for socketify::db.
 */

#include <stdexcept>
#include <string>

namespace socketify::db {

class Error : public std::runtime_error {
public:
    Error(std::string message, int code = 0, std::string sqlstate = {})
        : std::runtime_error(message), code_(code), sqlstate_(std::move(sqlstate)) {}

    int code() const noexcept { return code_; }
    const std::string& sqlstate() const noexcept { return sqlstate_; }

private:
    int code_{0};
    std::string sqlstate_;
};

} // namespace socketify::db
