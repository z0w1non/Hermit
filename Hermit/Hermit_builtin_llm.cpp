#include "Hermit_builtin.hpp"
#include "Hermit_llm.hpp"
#include "Hermit_string_utils.hpp"
#include "Hermit_templates.hpp"

namespace Hermit::builtin
{
    value_type code_block(macro_argument_type args)
    {
        builtin::validate_arguments_size(args.arguments, 2, 2);
        const std::string_view markdown{ get_or_throw<std::string>(args.arguments[0]) };
        const std::string_view block_name{ get_or_throw<std::string>(args.arguments[1]) };
        const llm::code_blocks map{ llm::extract_code_block_from_markdown(markdown) };
        if (const llm::code_blocks::const_iterator iter{ map.find(block_name) }; iter != map.end())
        {
            return iter->second;
        }
        return std::string{};
    }

    value_type generated(macro_argument_type args)
    {
        builtin::validate_arguments_size(args.arguments, 1);
        const std::string_view prompt{ get_or_throw<std::string>(args.arguments[0]) };

        context pushed{ args.ctx.make_pushed() };
        return llm::completions(args.cfg, prompt, pushed);
    }

    value_type summary(macro_argument_type args)
    {
        builtin::validate_arguments_size(args.arguments, 3, 3);
        const std::string_view prompt{ get_or_throw<std::string>(args.arguments[0]) };
        const std::string& target{ get_or_throw<std::string>(args.arguments[1]) };
        const int max_tokens{ get_or_throw<int>(args.arguments[2]) };

        context pushed{ args.ctx.make_pushed() };
        pushed.set("target", target);
        std::string output{ llm::completions(args.cfg, prompt, pushed) };
        output = llm::remove_reasoning(output, args.cfg.llm.reasoning_prefix, args.cfg.llm.reasoning_suffix);
        const token_count_string truncated{ string_utils::truncate_by_tokens(output, max_tokens, args.cfg, false) };
        return truncated.str;
    }
}
