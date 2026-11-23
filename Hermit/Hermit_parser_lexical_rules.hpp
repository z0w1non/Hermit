#ifndef HERMIT_PARSER_LEXICAL_RULES_HPP
#define HERMIT_PARSER_LEXICAL_RULES_HPP

#include "Hermit_parser_types.hpp"
#include <boost/spirit/include/qi.hpp>

namespace Hermit::parser
{
    template<typename Iterator>
    struct lexical_rules
    {
        boost::spirit::qi::rule<Iterator, std::string()> name;
        boost::spirit::qi::rule<Iterator, char()> character;
        boost::spirit::qi::rule<Iterator, std::string()> string;
        boost::spirit::qi::rule<Iterator, value_type()> value;
    };
}

#endif
