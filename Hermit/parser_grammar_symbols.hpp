#ifndef HERMIT_PARSER_GRAMMAR_SYMBOLS_HPP
#define HERMIT_PARSER_GRAMMAR_SYMBOLS_HPP

#include "parser_types.hpp"
#include <boost/spirit/include/qi_symbols.hpp>

namespace hermit::parser
{
    struct assignment_symbols : boost::spirit::qi::symbols<char, assignment_operator> { assignment_symbols(); };
    struct equality_symbols : boost::spirit::qi::symbols<char, equality_operator> { equality_symbols(); };
    struct relational_symbols : boost::spirit::qi::symbols<char, relational_operator> { relational_symbols(); };
    struct shift_symbols : boost::spirit::qi::symbols<char, shift_operator> { shift_symbols(); };
    struct additive_symbols : boost::spirit::qi::symbols<char, additive_operator> { additive_symbols(); };
    struct multiplicative_symbols : boost::spirit::qi::symbols<char, multiplicative_operator> { multiplicative_symbols(); };
    struct prefix_symbols : boost::spirit::qi::symbols<char, prefix_operator> { prefix_symbols(); };
    struct suffix_symbols : boost::spirit::qi::symbols<char, suffix_operator> { suffix_symbols(); };
    struct escaped_chars : boost::spirit::qi::symbols<char, char> { escaped_chars(); };
}

#endif
