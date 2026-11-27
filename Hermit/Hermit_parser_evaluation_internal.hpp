#ifndef HERMIT_PARSER_EVALUATION_INTERNAL_HPP
#define HERMIT_PARSER_EVALUATION_INTERNAL_HPP

#include <string>

#include "Hermit_parser.hpp"

namespace Hermit::parser {

std::string evaluate_ast(const abstract_syntax_tree_type& abstract_syntax_tree, const config& cfg, context& ctx);

}

#endif // HERMIT_PARSER_EVALUATION_INTERNAL_HPP

