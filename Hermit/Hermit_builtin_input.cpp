#include "Hermit_builtin.hpp"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/nowide/iostream.hpp>
#include <boost/predef.h>

#include <iterator>
#include <locale>
#include <sstream>

#if BOOST_OS_WINDOWS
#include <io.h>
#else
#include <unistd.h>
#endif

namespace Hermit::builtin
{
    namespace
    {
        std::string datetime_impl(const char* time_facet_format)
        {
            using boost::posix_time::ptime;
            using boost::posix_time::second_clock;
            using boost::posix_time::time_facet;

            const ptime local_time{ second_clock::local_time() };
            const time_facet* facet{ new time_facet(time_facet_format) };
            std::ostringstream oss;
            oss.imbue(std::locale(oss.getloc(), facet));
            oss << local_time;
            return oss.str();
        }
    }

    std::string date()
    {
        return datetime_impl("%Y%m%d");
    }

    std::string time()
    {
        return datetime_impl("%H%M%S");
    }

    std::string datetime()
    {
        return datetime_impl("%Y%m%d%H%M%S");
    }

    bool is_interactive_input()
    {
#if BOOST_OS_WINDOWS
        return (_isatty(0) != 0);
#else
        return (isatty() != 0);
#endif
    }

    std::string stdin_(const config&)
    {
        if (is_interactive_input())
        {
            return std::string{};
        }
        return std::string{ std::istreambuf_iterator<char>{ boost::nowide::cin }, std::istreambuf_iterator<char>{} };
    }
}
