#include "Hermit_llm.hpp"
#include "Hermit_filesystem.hpp"
#include "Hermit_tcp.hpp"
#include "Hermit_media_config.hpp"
#include "Hermit_value.hpp"
#include "Hermit_errors.hpp"
#include "Hermit_log.hpp"

#include <chrono>

namespace Hermit
{
    std::size_t string_hash::operator()(std::string_view sv) const noexcept
    {
        return std::hash<std::string_view>{}(sv);
    }

    std::size_t string_hash::operator()(const std::string& s) const noexcept
    {
        return std::hash<std::string_view>{}(s);
    }

    std::size_t string_hash::operator()(const char* s) const noexcept
    {
        return std::hash<std::string_view>{}(s);
    }

    image_info_type image_info_type::from_file(std::string_view path, const config& cfg)
    {
        const std::string base64_image{ filesystem::image_path_to_base64_encoded_string(path, cfg) };
        const std::string mime_type{ filesystem::extension_to_mime_type(std::filesystem::path{ path }.extension().string()) };
        return image_info_type{ base64_image, mime_type };
    }

    nlohmann::json tg_parameters::get_request_for_completions(std::string_view prompt, int max_tokens) const
    {
        nlohmann::json json{ nlohmann::json::object() };
        json.update(nlohmann::json(common));
        json.update(nlohmann::json(completions));

        json["prompt"] = prompt;
        json["max_tokens"] = max_tokens;

        if (common.seed != -1)
        {
            json["seed"] = common.seed;
        }

        return json;
    }

    std::string tg_parameters::parse_response_for_completions(const std::string& response) const
    {
        const nlohmann::json response_json{ nlohmann::json::parse(response) };
        return response_json.at("choices").at(0).at("text").get<std::string>();
    }

    nlohmann::json tg_parameters::get_request_for_token_count(std::string_view prompt) const
    {
        nlohmann::json json{ nlohmann::json::object() };
        json["text"] = prompt;
        return json;
    }

    int tg_parameters::parse_response_for_token_count(const std::string& response) const
    {
        const nlohmann::json response_json{ nlohmann::json::parse(response) };
        return response_json.at("length").get<int>();
    }

    nlohmann::json tg_parameters::get_request_for_chat_completions(const nlohmann::json& messages) const
    {
        nlohmann::json json{ nlohmann::json::object() };
        json.update(nlohmann::json(common));
        json.update(nlohmann::json(chat_completions));

        json["max_tokens"] = get_max_tokens();

        if (common.seed != -1)
        {
            json["seed"] = common.seed;
        }

        const std::vector<std::string> stop_sequence
        {
            "{{[INPUT]}}",
            "{{[OUTPUT]}}"
        };

        json["stop_sequence"] = stop_sequence;
        json["messages"] = messages;

        return json;
    }

    std::string tg_parameters::parse_response_for_chat_completions(const std::string& response) const
    {
        const nlohmann::json response_json{ nlohmann::json::parse(response) };
        return response_json.at("choices").at(0).at("message").at("content").get<std::string>();
    }

    nlohmann::json kc_parameters::get_request_for_completions(std::string_view prompt, int max_tokens) const
    {
        nlohmann::json json{ nlohmann::json::object() };
        json.update(nlohmann::json(common));
        json.update(nlohmann::json(completions));

        json["max_length"] = max_tokens;
        json["prompt"] = prompt;

        if (common.sampler_seed != -1)
        {
            json["sampler_seed"] = common.sampler_seed;
        }

        return json;
    }

    std::string kc_parameters::parse_response_for_completions(const std::string& response) const
    {
        const nlohmann::json response_json{ nlohmann::json::parse(response) };
        return response_json.at("results").at(0).at("text").get<std::string>();
    }

    nlohmann::json kc_parameters::get_request_for_token_count(std::string_view prompt) const
    {
        nlohmann::json json{ nlohmann::json::object() };
        json["prompt"] = prompt;
        return json;
    }

    int kc_parameters::parse_response_for_token_count(const std::string& response) const
    {
        const nlohmann::json response_json{ nlohmann::json::parse(response) };
        return response_json.at("value").get<int>();
    }

    nlohmann::json kc_parameters::get_request_for_chat_completions(const nlohmann::json& messages) const
    {
        nlohmann::json json{ nlohmann::json::object() };
        json.update(nlohmann::json(common));

        //json["max_length"] = max_tokens;
        json["max_tokens"] = get_max_tokens();

        if (common.sampler_seed != -1)
        {
            json["sampler_seed"] = common.sampler_seed;
        }

        const std::vector<std::string> stop_sequence
        {
            "{{[INPUT]}}",
            "{{[OUTPUT]}}"
        };

        json["stop_sequence"] = stop_sequence;
        json["messages"] = messages;

        return json;
    }

    std::string kc_parameters::parse_response_for_chat_completions(const std::string& response) const
    {
        const nlohmann::json response_json{ nlohmann::json::parse(response) };
        return response_json.at("choices").at(0).at("message").at("content").get<std::string>();
    }

    int send_token_count_request(const config& cfg, std::string_view prompt)
    {
        const std::string_view host{ cfg.llm.host };
        const std::string_view port{ cfg.llm.port };
        const std::string_view target{ cfg.llm.token_count_target };

        tcp tcp;
        tcp.expires_after(std::chrono::seconds{ cfg.timeout_connect }).connect(host, port);

        const std::string request_body{ cfg.llm.backend->get_request_for_token_count(prompt).dump() };
        HERMIT_LOG(trace) << "Send JSON\n```\n" << request_body << "\n```";

        tcp::request_type request{ tcp::make_post_json_request(host, target, request_body) };
        request.prepare_payload();

        const tcp::response_type response{ tcp.expires_after(std::chrono::seconds{ cfg.timeout_request }).request(request) };
        HERMIT_LOG(trace) << "Recieve JSON\n```\n" << response.body() << "\n```";

        return cfg.llm.backend->parse_response_for_token_count(response.body());
    }

} // namespace Hermit
