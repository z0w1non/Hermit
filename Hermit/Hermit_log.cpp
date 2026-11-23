#include "Hermit_log.hpp"
#include "Hermit_errors.hpp"
#include "Hermit_filesystem.hpp"
#include "Hermit_value.hpp"
#include <boost/log/core.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/nowide/fstream.hpp>
#include <iostream>

namespace Hermit
{
    namespace log
    {
        using text_ostream_backend = boost::log::sinks::text_ostream_backend;
        using sinchronous_sink = boost::log::sinks::synchronous_sink<text_ostream_backend>;
#if defined(HERMIT_LOGGING_SOURCE_LOCATION)
        location_scope_guard::location_scope_guard(const char* file, int line, const char* function)
            : core(boost::log::core::get())
            , file_iterator{ core->add_thread_attribute("File", boost::log::attributes::make_constant(file)).first }
            , line_iterator{ core->add_thread_attribute("Line", boost::log::attributes::make_constant(line)).first }
            , function_iterator{ core->add_thread_attribute("Function", boost::log::attributes::make_constant(function)).first }
        {
        }

        location_scope_guard::~location_scope_guard()
        {
            core->remove_thread_attribute(file_iterator);
            core->remove_thread_attribute(line_iterator);
            core->remove_thread_attribute(function_iterator);
        }
#endif

        template<typename Sink>
        void set_formatter(Sink& sink)
        {
            sink->set_formatter
            (
                boost::log::expressions::stream
                << boost::log::expressions::format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S") << " "
                << "[" << boost::log::trivial::severity << "] "
#if defined(HERMIT_LOGGING_SOURCE_LOCATION)
                << "[" << boost::log::expressions::attr<std::string>("File")
                << "(" << boost::log::expressions::attr<int>("Line") << ") "
                << boost::log::expressions::attr<std::string>("Function") << ")] "
#endif
                << boost::log::expressions::smessage
            );
            boost::log::core::get()->add_global_attribute("TimeStamp", boost::log::attributes::local_clock());
        }

        boost::shared_ptr<sinchronous_sink> create_stream_sink(const boost::shared_ptr<std::ostream>& ostream)
        {
            const boost::shared_ptr<text_ostream_backend> backend{ boost::make_shared<text_ostream_backend>() };
            backend->add_stream(ostream);
            backend->auto_flush(true);

            const boost::shared_ptr<sinchronous_sink> sink{ boost::make_shared<sinchronous_sink>(backend) };
            set_formatter(sink);
            return sink;
        }

        boost::shared_ptr<sinchronous_sink> create_file_sink(const std::filesystem::path& log)
        {
            filesystem::create_parent_directories(log);
            boost::shared_ptr<boost::nowide::ofstream> ofs{ boost::make_shared<boost::nowide::ofstream>(log, std::ios::app) };
            if (!ofs->is_open())
            {
                Hermit::throw_exception(file_open_exception{} << error_info::path{ log });
            }
            return create_stream_sink(ofs);
        }

        boost::shared_ptr<sinchronous_sink> create_cout_sink()
        {
            const boost::shared_ptr<std::ostream> cout_stream{ &std::cout, boost::null_deleter{} };
            return create_stream_sink(cout_stream);
        }

        void init_log_cout()
        {
            const boost::shared_ptr<sinchronous_sink> sink{ create_cout_sink() };
            set_formatter(sink);
            boost::log::core::get()->add_sink(sink);
        }

        void init_log_file(const std::filesystem::path& log)
        {
            const boost::shared_ptr<sinchronous_sink> sink{ create_file_sink(log) };
            set_formatter(sink);
            boost::log::core::get()->add_sink(sink);
        }

        boost::log::trivial::severity_level string_to_severity_level(std::string_view log_level)
        {
            using map_type = string_unordered_map<boost::log::trivial::severity_level>;
            static const map_type map
            {
                { "trace", boost::log::trivial::trace },
                { "debug", boost::log::trivial::debug },
                { "info", boost::log::trivial::info },
                { "warning", boost::log::trivial::warning },
                { "error", boost::log::trivial::error },
                { "fatal", boost::log::trivial::fatal }
            };
            if (const map_type::const_iterator iter{ map.find(log_level) }; iter != map.end())
            {
                return iter->second;
            }
            Hermit::throw_exception(logic_error{} << error_info::description{ std::string{ log_level } });
        }

        void init_log(bool verbose, const std::optional<std::filesystem::path> log_file, boost::log::trivial::severity_level log_level)
        {
            boost::log::core::get()->remove_all_sinks();
            boost::log::core::get()->set_filter(boost::log::trivial::severity >= log_level);
            if (verbose)
            {
                log::init_log_cout();
            }
            if (log_file)
            {
                log::init_log_file(*log_file);
            }
        }
    } // namespace log
} // namespace Hermit
