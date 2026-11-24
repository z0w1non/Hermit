#include "Hermit_core.hpp"
#include "Hermit_templates.hpp"
#include "Hermit_lru_impl.hpp"

namespace Hermit
{
    template struct lru_cache<1000>;

    exception::exception()
    {
        *this << error_info::stacktrace{ boost::stacktrace::stacktrace() };
    }

    command_mode string_to_command_mode(std::string_view name)
    {
        using map_type = string_view_unordered_map<command_mode>;
        static const map_type macros
        {
            { "tg", command_mode::tg },
            { "kc", command_mode::kc },
            { "sd", command_mode::sd },
            { "sb", command_mode::sb },
            { "cu", command_mode::cu },
            { "extract-png-parameters", command_mode::extract_png_parameters },
            { "create-process", command_mode::create_process },
            { "terminate-process", command_mode::terminate_process }
        };

        if (const map_type::const_iterator iter{ macros.find(name) }; iter != macros.end())
        {
            return iter->second;
        }
        Hermit::throw_exception(command_line_exception{} << error_info::description{ "Unknown mode string " + std::string{ name } });
    }

    std::string value_to_string(const value_type& value)
    {
        return boost::apply_visitor(value_reference_to_string_visitor{}, value);
    }

    std::string value_reference_to_string(const value_reference_type& value_reference)
    {
        return boost::apply_visitor(value_reference_to_string_visitor{}, value_reference);
    }

    context::context()
    {
    }

    context::context(const context& ctx)
        : base{ &ctx }
    {
    }

    context context::make_pushed() const
    {
        return context{ *this };
    }

    void context::set(std::string_view key, const value_type& value)
    {
        variable_map[std::string{ key }] = value;
    }

    void context::erase(std::string_view key)
    {
        variable_map.erase(std::string{ key });
    }

    const value_type* context::get(std::string_view key) const
    {
        const std::string key_string{ key };
        const context* current{ this };

        while (current != nullptr)
        {
            const context::variable_map_type::const_iterator map_iterator{ current->variable_map.find(key_string) };
            if (map_iterator != current->variable_map.end())
            {
                return &map_iterator->second;
            }
            current = current->base;
        }
        return nullptr;
    }

    value_type* context::get(std::string_view key)
    {
        return const_cast<value_type*>(static_cast<const context&>(*this).get(key));
    }

    sd_mode string_to_sd_mode(std::string_view name)
    {
        sd_mode result{};
        if (boost::describe::enum_from_string(name, result))
        {
            return result;
        }
        Hermit::throw_exception(command_line_exception{} << error_info::description{ "Unknown sd-mode string " + std::string{ name } });
    }

    std::string sd_mode_to_target(sd_mode mode, const config& cfg)
    {
        if (mode == sd_mode::txt2img)
        {
            return cfg.sd.txt2img.target;
        }
        else if (mode == sd_mode::img2img)
        {
            return cfg.sd.img2img.target;
        }
        Hermit::throw_exception(logic_error{} << error_info::description{ "Unknown sd-mode" });
    }

    llm_mode string_to_llm_mode(std::string_view name)
    {
        llm_mode result{};
        if (boost::describe::enum_from_string(name, result))
        {
            return result;
        }
        Hermit::throw_exception(command_line_exception{} << error_info::description{ "Unknown llm-mode string " + std::string{ name } });
    }

    std::string llm_mode_to_target(llm_mode mode, const config& cfg)
    {
        if (mode == llm_mode::completions)
        {
            return cfg.llm.completions_target;
        }
        else if (mode == llm_mode::chat_completions)
        {
            return cfg.llm.chat_completions_target;
        }
        Hermit::throw_exception(logic_error{} << error_info::description{ "Unknown sd-mode" });
    }

    config::cache_type::callback_type config::make_lru_cache_callback()
    {
        return [this](std::string_view str) { return send_token_count_request(*this, str); };
    }

    std::string expand_macro(std::string_view input, const config& cfg, const context& ctx);

} // namespace Hermit

