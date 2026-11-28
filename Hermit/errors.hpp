#ifndef HERMIT_ERRORS_HPP
#define HERMIT_ERRORS_HPP

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <filesystem>
#include <exception>
#include <string>

#include <boost/system/error_code.hpp>
#include <boost/current_function.hpp>
#include <boost/exception/all.hpp>
#include <boost/stacktrace.hpp>

namespace hermit
{
    struct exception : virtual boost::exception, virtual std::exception { exception(); };
    struct runtime_exception : exception {};
    struct logic_error : exception {};
    struct io_exception : runtime_exception {};
    struct file_open_exception : io_exception {};
    struct socket_exception : runtime_exception {};
    struct text_generation_exception : runtime_exception {};
    struct image_generation_exception : runtime_exception {};
    struct comfy_ui_generation_exception : runtime_exception {};
    struct syntax_exception : runtime_exception {};
    struct json_parse_exception : runtime_exception {};
    struct macro_exception : runtime_exception {};
    struct command_line_exception : runtime_exception {};
    struct dns_resolve_exception : runtime_exception {};
    struct connect_exception : runtime_exception {};
    struct http_send_exception : runtime_exception {};
    struct http_receive_exception : runtime_exception {};
    struct http_status_exception : runtime_exception {};
    struct png_exception : runtime_exception {};

    namespace error_info
    {
        using stacktrace = boost::error_info<struct tag_stacktrace, boost::stacktrace::stacktrace>;
        using description = boost::error_info<struct tag_description, std::string>;
        using nested_exception = boost::error_info<struct tag_nested_exception, boost::exception_ptr>;
        using path = boost::error_info<struct tag_file_path, std::filesystem::path>;
        using host = boost::error_info<struct tag_host, std::string>;
        using port = boost::error_info<struct tag_port, std::string>;
        namespace asio { using error_code = boost::error_info<struct tag_error_code, boost::system::error_code>; }
        namespace http { namespace response {
            using status = boost::error_info<struct tag_status_int, int>;
            using reason = boost::error_info<struct tag_result_int, std::string>;
        }}
        namespace beast { using error_code = boost::error_info<struct tag_error_code, boost::system::error_code>; }
        namespace macro {
            using name = boost::error_info<struct tag_name, std::string>;
            using arguments = boost::error_info<struct tag_arguments, std::string>;
        }
        namespace system { using error_code = boost::error_info<struct tag_error_code, boost::system::error_code>; }
    }

    template<typename Exception>
    [[noreturn]] void throw_exception(const Exception& e, const boost::source_location& location = BOOST_CURRENT_LOCATION)
    {
        boost::throw_exception(e, location);
    }

    template<typename Exception>
    [[noreturn]] void throw_nested_exception(const Exception& e, const boost::source_location& location = BOOST_CURRENT_LOCATION)
    {
        if (boost::exception_ptr ptr{ boost::current_exception() }; ptr)
        {
            boost::throw_exception(boost::enable_error_info(e) << error_info::nested_exception{ ptr }, location);
        }
        boost::throw_exception(logic_error{} << error_info::description{ "throw_nested_exception must be called within catch block" }, location);
    }
}

#endif
