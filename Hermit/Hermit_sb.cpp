#include "Hermit_generation.hpp"
#include "Hermit_http.hpp"
#include "Hermit_tcp.hpp"
#include "Hermit_log.hpp"
#include "Hermit_public_http_adapter.hpp"

#include <chrono>

namespace Hermit
{
    namespace sb
    {
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
            return executor(host, port, request).body();
        }

        std::string send_request(const config& cfg, std::string_view text, const public_api::http_executor& executor)
        {
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            const std::string target{ make_request_url(cfg.sb.target, cfg.sb, text) };
            const auto request{ public_api::make_http_request_data(cfg.sb.host, cfg.sb.port, target, {}, std::move(options)) };
            return executor(request).body;
        }
    } // namespace sb

} // namespace Hermit

