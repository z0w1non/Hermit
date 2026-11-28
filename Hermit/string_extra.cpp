#include "llm.hpp"
#include "filesystem.hpp"
#include "tcp.hpp"
#include "public_http_adapter.hpp"
#include "media_config.hpp"
#include "string_utils.hpp"
#include "value.hpp"
#include "errors.hpp"
#include "log.hpp"

#include <chrono>

namespace hermit
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
        return from_file(path, cfg, [](const std::filesystem::path& resolved_path, std::ios_base::openmode mode)
        {
            return filesystem::read_file_to_string(resolved_path, mode);
        });
    }

    image_info_type image_info_type::from_file(std::string_view path, const config& cfg, const filesystem::file_reader& reader)
    {
        const std::string base64_image{ string_utils::base64_encode(filesystem::read_binary_file_to_string(path, cfg, reader)) };
        const std::string mime_type{ filesystem::extension_to_mime_type(std::filesystem::path{ path }.extension().string()) };
        return image_info_type{ base64_image, mime_type };
    }

    nlohmann::json tg_parameters::get_request_for_completions(std::string_view prompt, int max_tokens) const
    {
        if (openai_compatible)
        {
            nlohmann::json json{ nlohmann::json::object() };
            json["model"] = common.model;
            json["prompt"] = prompt;
            json["max_tokens"] = max_tokens;
            json["temperature"] = common.temperature;
            json["top_p"] = common.top_p;
            json["stop"] = common.stop;
            json["stream"] = false;
            if (common.frequency_penalty != 0.0) json["frequency_penalty"] = common.frequency_penalty;
            if (common.presence_penalty != 0.0) json["presence_penalty"] = common.presence_penalty;
            if (common.seed != -1) json["seed"] = common.seed;
            return json;
        }

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
        if (openai_compatible)
        {
            nlohmann::json json{ get_request_for_completions(prompt, 0) };
            json["echo"] = true;
            return json;
        }
        nlohmann::json json{ nlohmann::json::object() };
        json["text"] = prompt;
        return json;
    }

    int tg_parameters::parse_response_for_token_count(const std::string& response) const
    {
        const nlohmann::json response_json{ nlohmann::json::parse(response) };
        if (openai_compatible)
        {
            return response_json.at("usage").at("prompt_tokens").get<int>();
        }
        return response_json.at("length").get<int>();
    }

    nlohmann::json tg_parameters::get_request_for_chat_completions(const nlohmann::json& messages) const
    {
        if (openai_compatible)
        {
            nlohmann::json json{ nlohmann::json::object() };
            json["model"] = common.model;
            json["messages"] = messages;
            json["max_tokens"] = get_max_tokens();
            json["temperature"] = common.temperature;
            json["top_p"] = common.top_p;
            json["stop"] = common.stop;
            json["stream"] = false;
            if (common.frequency_penalty != 0.0) json["frequency_penalty"] = common.frequency_penalty;
            if (common.presence_penalty != 0.0) json["presence_penalty"] = common.presence_penalty;
            if (common.seed != -1) json["seed"] = common.seed;
            return json;
        }
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
        if (ollama_compatible)
        {
            nlohmann::json json{ nlohmann::json::object() };
            json["model"] = model;
            json["prompt"] = prompt;
            json["stream"] = false;
            json["raw"] = true;
            json["options"] = {
                { "num_predict", max_tokens },
                { "temperature", common.temperature },
                { "top_p", common.top_p }
            };
            if (!common.stop_sequence.empty()) json["options"]["stop"] = common.stop_sequence;
            if (common.sampler_seed != -1) json["options"]["seed"] = common.sampler_seed;
            return json;
        }
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
        if (ollama_compatible) return response_json.at("response").get<std::string>();
        return response_json.at("results").at(0).at("text").get<std::string>();
    }

    nlohmann::json kc_parameters::get_request_for_token_count(std::string_view prompt) const
    {
        if (ollama_compatible)
        {
            return get_request_for_completions(prompt, 0);
        }
        nlohmann::json json{ nlohmann::json::object() };
        json["prompt"] = prompt;
        return json;
    }

    int kc_parameters::parse_response_for_token_count(const std::string& response) const
    {
        const nlohmann::json response_json{ nlohmann::json::parse(response) };
        if (ollama_compatible) return response_json.at("prompt_eval_count").get<int>();
        return response_json.at("value").get<int>();
    }

    nlohmann::json kc_parameters::get_request_for_chat_completions(const nlohmann::json& messages) const
    {
        if (ollama_compatible)
        {
            nlohmann::json json{
                { "model", model },
                { "messages", messages },
                { "stream", false },
                { "options", {
                    { "num_predict", common.max_length },
                    { "temperature", common.temperature },
                    { "top_p", common.top_p }
                } }
            };
            if (!common.stop_sequence.empty()) json["options"]["stop"] = common.stop_sequence;
            if (common.sampler_seed != -1) json["options"]["seed"] = common.sampler_seed;
            return json;
        }
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
        if (ollama_compatible) return response_json.at("message").at("content").get<std::string>();
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
        if (!cfg.llm.api_key.empty()) request.set(boost::beast::http::field::authorization, "Bearer " + cfg.llm.api_key);
        request.prepare_payload();

        const tcp::response_type response{ tcp.expires_after(std::chrono::seconds{ cfg.timeout_request }).request(request) };
        HERMIT_LOG(trace) << "Recieve JSON\n```\n" << response.body() << "\n```";

        return cfg.llm.backend->parse_response_for_token_count(response.body());
    }

    int llm::send_token_count_request(const config& cfg, std::string_view prompt, const tcp::request_executor& executor)
    {
        const std::string_view host{ cfg.llm.host };
        const std::string_view port{ cfg.llm.port };
        const std::string_view target{ cfg.llm.token_count_target };
        const std::string body{ cfg.llm.backend->get_request_for_token_count(prompt).dump() };
        tcp::request_type request{ tcp::make_post_json_request(host, target, body) };
        if (!cfg.llm.api_key.empty()) request.set(boost::beast::http::field::authorization, "Bearer " + cfg.llm.api_key);
        request.prepare_payload();
        const tcp::response_type response{ executor(host, port, request) };
        tcp::ensure_success(response);
        return cfg.llm.backend->parse_response_for_token_count(response.body());
    }

    int llm::send_token_count_request(const config& cfg, std::string_view prompt, const public_api::http_executor& executor)
    {
        public_api::http_transport_options options;
        options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
        options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
        if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;
        const std::string body{ cfg.llm.backend->get_request_for_token_count(prompt).dump() };
        public_api::http_request_data request{ public_api::make_http_request_data(
            cfg.llm.host, cfg.llm.port, cfg.llm.token_count_target, body, std::move(options)) };
        request.method = public_api::http_method::post;
        request.content_type = "application/json";
        const public_api::http_response_data response{ executor(request) };
        public_api::ensure_success(response);
        return cfg.llm.backend->parse_response_for_token_count(response.body);
    }

} // namespace Hermit
