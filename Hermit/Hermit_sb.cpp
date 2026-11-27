#include "Hermit_generation.hpp"
#include "Hermit_filesystem.hpp"
#include "Hermit_http.hpp"
#include "Hermit_tcp.hpp"
#include "Hermit_log.hpp"
#include "Hermit_public_http_adapter.hpp"

#include <chrono>

namespace Hermit
{
    namespace sb
    {
        namespace
        {
            std::string make_query_url(std::string_view target, std::string_view key, std::string_view value)
            {
                boost::urls::url request_url{ target };
                request_url.params().set(key, value);
                return std::string{ request_url.encoded_target() };
            }

            public_api::http_response_data execute_json_request(const config& cfg, std::string_view target,
                public_api::http_method method, std::string body, const public_api::http_executor& executor)
            {
                public_api::http_transport_options options;
                options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
                options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
                public_api::http_request_data request{ public_api::make_http_request_data(
                    cfg.sb.host, cfg.sb.port, target, std::move(body), std::move(options)) };
                request.method = method;
                if (method == public_api::http_method::post)
                {
                    request.content_type = "application/json";
                }
                const public_api::http_response_data response{ executor(request) };
                public_api::ensure_success(response);
                return response;
            }

            public_api::http_executor make_default_executor(const config& cfg, tcp& transport)
            {
                return public_api::make_http_executor([&transport, &cfg](std::string_view host, std::string_view port,
                    const tcp::request_type& request)
                {
                    transport.expires_after(std::chrono::seconds{ cfg.timeout_connect }).connect(host, port);
                    return transport.expires_after(std::chrono::seconds{ cfg.timeout_request }).request(request);
                });
            }
        }

        std::string make_text_request_url(std::string_view target, std::string_view text)
        {
            return make_query_url(target, "text", text);
        }

        std::string make_request_url(std::string_view target, const sb_parameters& params, std::string_view text)
        {
            boost::urls::url request_url{ target };
            url_params_setter{ request_url }
                ("text", text)
                ("sdp_ratio", params.sdp_ratio)
                ("noise", params.noise)
                ("noisew", params.noisew)
                ("length", params.length)
                ("language", params.language)
                ("auto_split", params.auto_split)
                ("split_interval", params.split_interval)
                .set_if_else(!params.model_name.empty(),
                    "model_name", params.model_name,
                    "model_id", params.model_id)
                .set_if_else(!params.speaker_name.empty(),
                    "speaker_name", params.speaker_name,
                    "speaker_id", params.speaker_id)
                .set_if(!params.assist_text.empty(),
                    "assist_text", params.assist_text)
                .set_if(!params.assist_text.empty(),
                    "assist_text_weight", params.assist_text_weight)
                .set_if(!params.style.empty(),
                    "style", params.style)
                .set_if(!params.style.empty(),
                    "style_weight", params.style_weight)
                .set_if(!params.reference_audio_path.empty(),
                    "reference_audio_path", params.reference_audio_path);
            return std::string{ request_url.encoded_target() };
        }

        std::string send_request(const config& cfg, std::string_view text)
        {
            tcp transport;
            return send_request(cfg, text,
                [&transport, &cfg](std::string_view host, std::string_view port, const tcp::request_type& request)
                {
                    transport.expires_after(std::chrono::seconds{ cfg.timeout_connect }).connect(host, port);
                    return transport.expires_after(std::chrono::seconds{ cfg.timeout_request }).request(request);
                });
        }

        std::string send_request(const config& cfg, std::string_view text, const tcp::request_executor& executor)
        {
            const std::string_view host{ cfg.sb.host };
            const std::string_view port{ cfg.sb.port };

            const std::string target{ make_request_url(cfg.sb.target, cfg.sb, text) };

            HERMIT_LOG(info) << "Send target\n```\n" << target.c_str() << "\n```";
            tcp::request_type request{ tcp::make_get_json_request(host, target) };
            request.prepare_payload();
            const tcp::response_type response{ executor(host, port, request) };
            tcp::ensure_success(response);
            return response.body();
        }

        std::string send_request(const config& cfg, std::string_view text, const public_api::http_executor& executor)
        {
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            const std::string target{ make_request_url(cfg.sb.target, cfg.sb, text) };
            const auto request{ public_api::make_http_request_data(cfg.sb.host, cfg.sb.port, target, {}, std::move(options)) };
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return response.body;
        }

        std::string send_post_request(const config& cfg, std::string_view text, const public_api::http_executor& executor)
        {
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            const std::string target{ make_request_url(cfg.sb.target, cfg.sb, text) };
            public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.sb.host, cfg.sb.port, target, {}, std::move(options)) };
            request.method = public_api::http_method::post;
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return response.body;
        }

        std::string send_post_request(const config& cfg, std::string_view text)
        {
            tcp transport;
            return send_post_request(cfg, text, make_default_executor(cfg, transport));
        }

        nlohmann::json get_models_info(const config& cfg)
        {
            tcp transport;
            return get_models_info(cfg, make_default_executor(cfg, transport));
        }

        nlohmann::json get_models_info(const config& cfg, const public_api::http_executor& executor)
        {
            const auto response{ execute_json_request(cfg, "/models/info", public_api::http_method::get, {}, executor) };
            return nlohmann::json::parse(response.body);
        }

        nlohmann::json refresh_models(const config& cfg)
        {
            tcp transport;
            return refresh_models(cfg, make_default_executor(cfg, transport));
        }

        nlohmann::json refresh_models(const config& cfg, const public_api::http_executor& executor)
        {
            const auto response{ execute_json_request(cfg, "/models/refresh", public_api::http_method::post, "{}", executor) };
            return nlohmann::json::parse(response.body);
        }

        nlohmann::json get_status(const config& cfg)
        {
            tcp transport;
            return get_status(cfg, make_default_executor(cfg, transport));
        }

        nlohmann::json get_status(const config& cfg, const public_api::http_executor& executor)
        {
            const auto response{ execute_json_request(cfg, "/status", public_api::http_method::get, {}, executor) };
            return nlohmann::json::parse(response.body);
        }

        nlohmann::json get_phonemes(const config& cfg, std::string_view text)
        {
            tcp transport;
            return get_phonemes(cfg, text, make_default_executor(cfg, transport));
        }

        nlohmann::json get_phonemes(const config& cfg, std::string_view text, const public_api::http_executor& executor)
        {
            const std::string target{ make_text_request_url("/g2p", text) };
            const auto response{ execute_json_request(cfg, target, public_api::http_method::post, {}, executor) };
            return nlohmann::json::parse(response.body);
        }

        std::string get_audio(const config& cfg, std::string_view path)
        {
            tcp transport;
            return get_audio(cfg, path, make_default_executor(cfg, transport));
        }

        std::string get_audio(const config& cfg, std::string_view path, const public_api::http_executor& executor)
        {
            const std::string target{ make_query_url("/tools/get_audio", "path", path) };
            const auto response{ execute_json_request(cfg, target, public_api::http_method::get, {}, executor) };
            return response.body;
        }
    } // namespace sb

} // namespace Hermit

