#ifndef HERMIT_TEST_SUPPORT_HPP
#define HERMIT_TEST_SUPPORT_HPP

#include <chrono>
#include <initializer_list>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>

#include "Hermit_llm.hpp"
#include "Hermit_tcp.hpp"

struct commandline_args
{
    explicit commandline_args(const std::vector<std::string>& args) : args_{ args } { build_argv(); }
    commandline_args(std::initializer_list<std::string> args) : args_{ args.begin(), args.end() } { build_argv(); }
    commandline_args(const commandline_args&) = delete;
    commandline_args(commandline_args&&) = delete;
    commandline_args& operator=(const commandline_args&) = delete;
    commandline_args& operator=(commandline_args&&) = delete;
    int argc() const { return static_cast<int>(args_.size()); }
    char** argv() { return argv_.data(); }
private:
    void build_argv()
    {
        argv_.clear();
        argv_.reserve(args_.size() + 1);
        for (std::string& arg : args_) argv_.push_back(arg.data());
        argv_.push_back(nullptr);
    }
    std::vector<std::string> args_;
    std::vector<char*> argv_;
};

struct scoped_ostream_redirect
{
    explicit scoped_ostream_redirect(std::ostream& ostream) : ostream_{ ostream }, old_buf_{ ostream_.rdbuf(buffer_.rdbuf()) } {}
    ~scoped_ostream_redirect() { ostream_.rdbuf(old_buf_); }
    scoped_ostream_redirect(const scoped_ostream_redirect&) = delete;
    scoped_ostream_redirect(scoped_ostream_redirect&&) = delete;
    scoped_ostream_redirect& operator=(const scoped_ostream_redirect&) = delete;
    scoped_ostream_redirect& operator=(scoped_ostream_redirect&&) = delete;
    std::string str() const { return buffer_.str(); }
private:
    std::ostream& ostream_;
    std::ostringstream buffer_;
    std::streambuf* old_buf_;
};

class MockLlmBackendParameters : public Hermit::llm_backend_parameters
{
public:
    MOCK_METHOD(nlohmann::json, get_request_for_completions, (std::string_view, int), (const, override));
    MOCK_METHOD(std::string, parse_response_for_completions, (const std::string&), (const, override));
    MOCK_METHOD(nlohmann::json, get_request_for_token_count, (std::string_view), (const, override));
    MOCK_METHOD(int, parse_response_for_token_count, (const std::string&), (const, override));
    MOCK_METHOD(nlohmann::json, get_request_for_chat_completions, (const nlohmann::json&), (const, override));
    MOCK_METHOD(std::string, parse_response_for_chat_completions, (const std::string&), (const, override));
    MOCK_METHOD(int, get_max_tokens, (), (const, override));
    MOCK_METHOD(int, get_truncation_length, (), (const, override));
};

class MockHttpTransport
{
public:
    MOCK_METHOD(MockHttpTransport&, expires_after, (std::chrono::seconds), ());
    MOCK_METHOD(void, connect, (std::string_view, std::string_view), ());
    MOCK_METHOD(Hermit::tcp::response_type, request, (const Hermit::tcp::request_type&), ());
};

#endif
