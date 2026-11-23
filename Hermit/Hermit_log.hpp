#ifndef HERMIT_LOG_HPP
#define HERMIT_LOG_HPP

#include <filesystem>
#include <optional>
#include <string_view>
#include <boost/log/trivial.hpp>

#if defined(HERMIT_LOGGING_SOURCE_LOCATION)
#include <boost/log/core.hpp>
#include <boost/log/attributes/attribute_set.hpp>
#include <boost/current_function.hpp>
#endif

namespace Hermit
{
    namespace log
    {
#if defined(HERMIT_LOGGING_SOURCE_LOCATION)
        struct location_scope_guard
        {
            location_scope_guard(const char* file, int line, const char* function);
            ~location_scope_guard();

        private:
            boost::log::core_ptr core;
            boost::log::attribute_set::iterator file_iterator, line_iterator, function_iterator;
        };
#endif

        void init_log_cout();
        void init_log_file(const std::filesystem::path& log);
        boost::log::trivial::severity_level string_to_severity_level(std::string_view log_level);
        void init_log(bool verbose, std::optional<std::filesystem::path> log_file, boost::log::trivial::severity_level log_level);
    } // namespace log

}

#if defined(HERMIT_LOGGING_SOURCE_LOCATION)
#define HERMIT_LOG(lvl) if (Hermit::log::location_scope_guard location_scope_guard_{__FILE__, __LINE__, BOOST_CURRENT_FUNCTION}; true) BOOST_LOG_TRIVIAL(lvl)
#else
#define HERMIT_LOG(lvl) BOOST_LOG_TRIVIAL(lvl)
#endif

#endif
