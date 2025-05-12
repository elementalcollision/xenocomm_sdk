#pragma once
#include <variant>
#include <string>
#include <optional>
#include <vector>
#include <memory>

namespace xenocomm {
namespace utils {

// Generic Result<T> template
// Holds either a value of type T or an error message

template <typename T>
class Result {
public:
    // Success constructor
    Result(const T& value) : data_(value) {}
    Result(T&& value) : data_(std::move(value)) {}
    // Error constructor
    Result(const std::string& error) : data_(error) {}
    Result(std::string&& error) : data_(std::move(error)) {}

    bool has_value() const { return std::holds_alternative<T>(data_); }
    bool has_error() const { return std::holds_alternative<std::string>(data_); }

    const T& value() const { return std::get<T>(data_); }
    T& value() { return std::get<T>(data_); }
    const std::string& error() const { return std::get<std::string>(data_); }

private:
    std::variant<T, std::string> data_;
};

// Specialization for void

template <>
class Result<void> {
public:
    // Success constructor
    Result() : success_(true) {}
    // Error constructor
    Result(const std::string& error) : success_(false), error_(error) {}
    Result(std::string&& error) : success_(false), error_(std::move(error)) {}

    bool has_value() const { return success_; }
    bool has_error() const { return !success_; }
    const std::string& error() const { return error_; }

private:
    bool success_ = false;
    std::string error_;
};

} // namespace utils
} // namespace xenocomm

// For convenience, provide a top-level alias in the global namespace
namespace xenocomm {
using utils::Result;
} 