#include <algorithm>
#include <cctype>
#include <chrono>
#include <stdexcept>
#include <utility>
#include <boost/url.hpp>

#include "generation.hpp"
#include "filesystem.hpp"
#include "tcp.hpp"
#include "log.hpp"
#include "errors.hpp"
#include "string_utils.hpp"
#include "public_http_adapter.hpp"

namespace hermit
{
    namespace sd
    {
        // unused
        std::string make_png_parameters(const sd_parameters& parameters, std::string_view prompt, std::string_view negative_prompt)
        {
            std::ostringstream oss;
            oss
                << prompt << '\n'
                << "Negative prompt: " << negative_prompt << '\n'
                << "Steps: " << parameters.common.steps << ", "
                << "Sampler: " << parameters.common.sampler_name << ", "
                << "CFG scale: " << parameters.common.cfg_scale << ", "
                << "Seed: " << parameters.common.seed << ", "
                << "Size: " << parameters.common.width << "x" << parameters.common.height << ", "
                //<< "Model hash: "
                << "Denoising strength: " << parameters.common.denoising_strength << ", "
                << "Hires upscale: " << parameters.txt2img.hr_scale << ", "
                << "Hires steps: " << parameters.txt2img.hr_second_pass_steps << ", "
                << "Hires upscaler: " << parameters.txt2img.hr_upscaler
                << std::flush;
            return oss.str();
        }

        nlohmann::json make_txt2img_request(const config& cfg, std::string_view prompt, std::string_view negative_prompt)
        {
            nlohmann::json json(cfg.sd.txt2img);
            if (!cfg.sd.txt2img.firstpass_image.empty())
            {
                json["firstpass_image"] = filesystem::image_path_to_base64_encoded_string(cfg.sd.txt2img.firstpass_image, cfg);
            }
            return json;
        }

        static nlohmann::json make_txt2img_request(const config& cfg, std::string_view prompt, std::string_view negative_prompt, const filesystem::file_reader& reader)
        {
            nlohmann::json json(cfg.sd.txt2img);
            if (!cfg.sd.txt2img.firstpass_image.empty())
            {
                const auto image{ image_info_type::from_file(cfg.sd.txt2img.firstpass_image, cfg, reader) };
                json["firstpass_image"] = image.base64_image;
            }
            return json;
        }

        nlohmann::json make_img2img_request(const config& cfg, std::string_view prompt, std::string_view negative_prompt)
        {
            nlohmann::json json(cfg.sd.img2img);
            if (cfg.sd.img2img.seed_resize_from_h == -1)
            {
                json["seed_resize_from_h"] = cfg.sd.common.seed_resize_from_h;
            }
            if (cfg.sd.img2img.seed_resize_from_w == -1)
            {
                json["seed_resize_from_w"] = cfg.sd.common.seed_resize_from_w;
            }
            json["init_images"] = filesystem::image_paths_to_base64_encoded_strings(cfg.sd.img2img.init_images, cfg);
            json["mask"] = filesystem::image_path_to_base64_encoded_string(cfg.sd.img2img.mask, cfg);
            json["latent_mask"] = filesystem::image_path_to_base64_encoded_string(cfg.sd.img2img.latent_mask, cfg);
            return json;
        }

        static nlohmann::json make_img2img_request(const config& cfg, std::string_view prompt, std::string_view negative_prompt, const filesystem::file_reader& reader)
        {
            nlohmann::json json(cfg.sd.img2img);
            if (cfg.sd.img2img.seed_resize_from_h == -1)
            {
                json["seed_resize_from_h"] = cfg.sd.common.seed_resize_from_h;
            }
            if (cfg.sd.img2img.seed_resize_from_w == -1)
            {
                json["seed_resize_from_w"] = cfg.sd.common.seed_resize_from_w;
            }
            nlohmann::json images{ nlohmann::json::array() };
            for (const auto& path : cfg.sd.img2img.init_images)
            {
                images.push_back(image_info_type::from_file(path, cfg, reader).base64_image);
            }
            json["init_images"] = std::move(images);
            json["mask"] = cfg.sd.img2img.mask.empty() ? std::string{} : image_info_type::from_file(cfg.sd.img2img.mask, cfg, reader).base64_image;
            json["latent_mask"] = cfg.sd.img2img.latent_mask.empty() ? std::string{} : image_info_type::from_file(cfg.sd.img2img.latent_mask, cfg, reader).base64_image;
            return json;
        }

        static void apply_generation_options(const config& cfg, nlohmann::json& request)
        {
            if (cfg.sd.generation_options.empty()) return;
            const nlohmann::json options{ nlohmann::json::parse(cfg.sd.generation_options) };
            if (!options.is_object())
            {
                throw std::invalid_argument{ "--sd-generation-options requires a JSON object" };
            }
            request.update(options);
        }

        nlohmann::json make_adetailer_request(const config& cfg)
        {
            nlohmann::json json(nlohmann::json::object());
            nlohmann::json alwayson_scripts{ nlohmann::json::object() };
            nlohmann::json adetailer{ nlohmann::json::object() };
            nlohmann::json object{ nlohmann::json::object() };
            object["ad_model"] = cfg.sd.common.alwayson_scripts.adetailer_parameters.args1.ad_model;
            if (!cfg.sd.common.alwayson_scripts.adetailer_parameters.args1.ad_prompt.empty())
            {
                object["ad_prompt"] = cfg.sd.common.alwayson_scripts.adetailer_parameters.args1.ad_prompt;
            }
            if (!cfg.sd.common.alwayson_scripts.adetailer_parameters.args1.ad_negative_prompt.empty())
            {
                object["ad_negative_prompt"] = cfg.sd.common.alwayson_scripts.adetailer_parameters.args1.ad_negative_prompt;
            }
            adetailer["args"] = { true, false, object };
            alwayson_scripts["ADetailer"] = adetailer;
            json["alwayson_scripts"] = alwayson_scripts;
            return json;
        }

        nlohmann::json make_abg_remover_request()
        {
            nlohmann::json json(nlohmann::json::object());
            json["script_name"] = "abg remover";
            json["script_args"] =
            {
                false,
                false,
                false,
                "#000000",
                false
            };
            return json;
        }

        nlohmann::json make_request(const config& cfg, std::string_view prompt, std::string_view negative_prompt)
        {
            nlohmann::json json(cfg.sd.common);

            json["prompt"] = prompt;
            json["negative_prompt"] = negative_prompt;

            if (!cfg.sd.common.sampler_name.empty())
            {
                json["sampler_name"] = cfg.sd.common.sampler_name;
            }

            if (!cfg.sd.common.sampler_index.empty())
            {
                json["sampler_index"] = cfg.sd.common.sampler_index;
            }

            if (cfg.sd.mode == sd_mode::txt2img)
            {
                json.update(make_txt2img_request(cfg, prompt, negative_prompt));
            }
            else if (cfg.sd.mode == sd_mode::img2img)
            {
                json.update(make_img2img_request(cfg, prompt, negative_prompt));
            }

            if (cfg.sd.common.alwayson_scripts.adetailer_parameters.ad_enable)
            {
                json.update(make_adetailer_request(cfg));
            }

            if (cfg.sd.abg_remover_enable)
            {
                json.update(make_abg_remover_request());
            }

            apply_generation_options(cfg, json);

            return json;
        }

        nlohmann::json make_request(const config& cfg, std::string_view prompt, std::string_view negative_prompt, const filesystem::file_reader& reader)
        {
            nlohmann::json json(cfg.sd.common);
            json["prompt"] = prompt;
            json["negative_prompt"] = negative_prompt;
            if (!cfg.sd.common.sampler_name.empty()) json["sampler_name"] = cfg.sd.common.sampler_name;
            if (!cfg.sd.common.sampler_index.empty()) json["sampler_index"] = cfg.sd.common.sampler_index;
            if (cfg.sd.mode == sd_mode::txt2img)
            {
                json.update(make_txt2img_request(cfg, prompt, negative_prompt, reader));
            }
            else if (cfg.sd.mode == sd_mode::img2img)
            {
                json.update(make_img2img_request(cfg, prompt, negative_prompt, reader));
            }
            if (cfg.sd.common.alwayson_scripts.adetailer_parameters.ad_enable)
            {
                json.update(make_adetailer_request(cfg));
            }
            if (cfg.sd.abg_remover_enable)
            {
                json.update(make_abg_remover_request());
            }
            apply_generation_options(cfg, json);
            return json;
        }

        std::string send_request(const config& cfg, std::string_view prompt, std::string_view negative_prompt)
        {
            tcp transport;
            return send_request(cfg, prompt, negative_prompt,
                [&transport, &cfg](std::string_view host, std::string_view port, const tcp::request_type& request)
                {
                    transport.expires_after(std::chrono::seconds{ cfg.timeout_connect }).connect(host, port);
                    return transport.expires_after(std::chrono::seconds{ cfg.timeout_request }).request(request);
                });
        }

        std::string send_request(const config& cfg, std::string_view prompt, std::string_view negative_prompt, const tcp::request_executor& executor)
        {
            return send_request(cfg, prompt, negative_prompt, executor, [](const std::filesystem::path& path, std::ios_base::openmode mode)
            {
                return filesystem::read_file_to_string(path, mode);
            });
        }

        std::string send_request(const config& cfg, std::string_view prompt, std::string_view negative_prompt, const tcp::request_executor& executor, const filesystem::file_reader& reader)
        {
            const std::string_view host{ cfg.sd.host };
            const std::string_view port{ cfg.sd.port };

            const nlohmann::json json{ make_request(cfg, prompt, negative_prompt, reader) };
            const std::string request_body{ json.dump() };
            HERMIT_LOG(info) << "Send JSON\n```\n" << request_body << "\n```";

            const std::string target{ sd_mode_to_target(cfg.sd.mode, cfg) };
            tcp::request_type request{ tcp::make_post_json_request(host, target, request_body) };
            request.prepare_payload();

            const tcp::response_type response{ executor(host, port, request) };
            tcp::ensure_success(response);
            HERMIT_LOG(trace) << "Receive JSON\n```\n" << response.body() << "\n```";

            nlohmann::json response_json{ nlohmann::json::parse(response.body()) };

            const std::string base64_image_data{ response_json.at("images").at(0).get<std::string>() };

            if (base64_image_data.empty())
            {
                hermit::throw_exception(image_generation_exception{} << error_info::description{ "No image data found in the response" });
            }

            const std::string decoded_image{ string_utils::base64_decode(base64_image_data) };

            return decoded_image;
        }

        std::string send_request(const config& cfg, std::string_view prompt, std::string_view negative_prompt, const public_api::http_executor& executor)
        {
            return send_request(cfg, prompt, negative_prompt, executor, [](const std::filesystem::path& path, std::ios_base::openmode mode)
            {
                return filesystem::read_file_to_string(path, mode);
            });
        }



        std::string send_request(const config& cfg, std::string_view prompt, std::string_view negative_prompt, const public_api::http_executor& executor, const filesystem::file_reader& reader)
        {
            const nlohmann::json json{ make_request(cfg, prompt, negative_prompt, reader) };
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            const public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.sd.host, cfg.sd.port, sd_mode_to_target(cfg.sd.mode, cfg), json.dump(), std::move(options)) };
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            const nlohmann::json response_json{ nlohmann::json::parse(response.body) };
            const std::string base64_image_data{ response_json.at("images").at(0).get<std::string>() };
            if (base64_image_data.empty()) hermit::throw_exception(image_generation_exception{} << error_info::description{ "No image data found in the response" });
            return string_utils::base64_decode(base64_image_data);
        }

        std::string manage_api(const config& cfg, const public_api::http_executor& executor)
        {
            using public_api::http_method;
            const std::string& action{ cfg.sd.api_action };
            std::string target;
            std::string body;
            http_method method{ http_method::post };
            if (action == "progress")
            {
                target = "/sdapi/v1/progress";
                method = http_method::get;
                if (!cfg.sd.api_options.empty())
                {
                    const nlohmann::json options{ nlohmann::json::parse(cfg.sd.api_options) };
                    if (!options.is_object() || (options.size() > 1) || (options.contains("skip_current_image") && !options.at("skip_current_image").is_boolean()))
                    {
                        throw std::invalid_argument{ "sd progress options must be an object containing only boolean skip_current_image" };
                    }
                    if (options.contains("skip_current_image"))
                    {
                        boost::urls::url query;
                        query.set_path(target);
                        query.params().append({ "skip_current_image", options.at("skip_current_image").get<bool>() ? "true" : "false" });
                        target = std::string{ query.encoded_target() };
                    }
                }
            }
            else if (action == "routes")
            {
                target = "/openapi.json";
                method = http_method::get;
                if (!cfg.sd.api_options.empty()) throw std::invalid_argument{ "sd-api routes does not accept --sd-api-options" };
            }
            else if (action == "pending-tasks" || action == "quicksettings-hint" || action == "ping"
                || action == "profile-startup" || action == "sysinfo" || action == "sysinfo-download")
            {
                target = "/internal/" + action;
                method = http_method::get;
                if (!cfg.sd.api_options.empty()) throw std::invalid_argument{ "sd-api " + action + " does not accept --sd-api-options" };
            }
            else if (action == "internal-progress")
            {
                target = "/internal/progress";
                const nlohmann::json options{ nlohmann::json::parse(cfg.sd.api_options) };
                if (!options.is_object()) throw std::invalid_argument{ "sd-api internal-progress requires a JSON object" };
                body = options.dump();
            }
            else if (action == "options")
            {
                target = "/sdapi/v1/options";
                method = http_method::get;
                if (!cfg.sd.api_options.empty())
                {
                    throw std::invalid_argument{ "sd-api options does not accept --sd-api-options" };
                }
            }
            else if (action == "options-set")
            {
                target = "/sdapi/v1/options";
                const nlohmann::json request_options{ nlohmann::json::parse(cfg.sd.api_options) };
                if (!request_options.is_object() || request_options.empty())
                {
                    throw std::invalid_argument{ "sd-api options-set requires a non-empty JSON object in --sd-api-options" };
                }
                body = request_options.dump();
            }
            else if (action == "create-embedding" || action == "create-hypernetwork"
                || action == "train-embedding" || action == "train-hypernetwork")
            {
                const nlohmann::json request_options{ nlohmann::json::parse(cfg.sd.api_options) };
                if (!request_options.is_object() || request_options.empty())
                {
                    throw std::invalid_argument{ "sd-api " + action + " requires a non-empty JSON object in --sd-api-options" };
                }
                const bool is_create{ action.starts_with("create-") };
                const std::string operation{ is_create ? "create/" : "train/" };
                const std::string resource{ action.ends_with("embedding") ? "embedding" : "hypernetwork" };
                target = "/sdapi/v1/" + operation + resource;
                body = request_options.dump();
            }
            else if (action == "cmd-flags" || action == "samplers" || action == "schedulers" || action == "upscalers"
                || action == "latent-upscale-modes" || action == "sd-models" || action == "sd-vae" || action == "hypernetworks"
                || action == "face-restorers" || action == "realesrgan-models" || action == "prompt-styles" || action == "embeddings"
                || action == "memory" || action == "scripts" || action == "script-info" || action == "extensions")
            {
                target = "/sdapi/v1/" + action;
                method = http_method::get;
                if (!cfg.sd.api_options.empty())
                {
                    throw std::invalid_argument{ "sd-api " + action + " does not accept --sd-api-options" };
                }
            }
            else if (action == "refresh-embeddings" || action == "refresh-checkpoints" || action == "refresh-vae")
            {
                target = "/sdapi/v1/" + action;
                if (!cfg.sd.api_options.empty())
                {
                    throw std::invalid_argument{ "sd-api " + action + " does not accept --sd-api-options" };
                }
            }
            else if (action == "unload-checkpoint" || action == "reload-checkpoint"
                || action == "server-kill" || action == "server-restart" || action == "server-stop")
            {
                target = "/sdapi/v1/" + action;
                if (!cfg.sd.api_options.empty())
                {
                    throw std::invalid_argument{ "sd-api " + action + " does not accept --sd-api-options" };
                }
            }
            else if (action == "interrogate")
            {
                target = "/sdapi/v1/interrogate";
                const nlohmann::json options{ nlohmann::json::parse(cfg.sd.api_options) };
                if (!options.is_object() || !options.contains("image") || !options.at("image").is_string()
                    || options.at("image").get_ref<const std::string&>().empty() || !options.contains("model")
                    || !options.at("model").is_string() || (options.at("model") != "clip" && options.at("model") != "deepdanbooru"))
                {
                    throw std::invalid_argument{ "sd interrogate requires --sd-api-options with image and model (clip or deepdanbooru)" };
                }
                body = options.dump();
            }
            else if (action == "extra-single-image" || action == "extra-batch-images" || action == "png-info")
            {
                const nlohmann::json request_options{ nlohmann::json::parse(cfg.sd.api_options) };
                if (!request_options.is_object())
                {
                    throw std::invalid_argument{ "Stable Diffusion API options must be a JSON object" };
                }
                const std::string image_key{ action == "extra-batch-images" ? "imageList" : "image" };
                if (!request_options.contains(image_key) || (action == "extra-batch-images"
                    ? !request_options.at(image_key).is_array() : !request_options.at(image_key).is_string()))
                {
                    throw std::invalid_argument{ "Stable Diffusion API options require " + image_key };
                }
                target = action == "extra-single-image" ? "/sdapi/v1/extra-single-image"
                    : action == "extra-batch-images" ? "/sdapi/v1/extra-batch-images" : "/sdapi/v1/png-info";
                body = request_options.dump();
            }
            else if (action == "interrupt") target = "/sdapi/v1/interrupt";
            else if (action == "skip") target = "/sdapi/v1/skip";
            else throw std::invalid_argument{ "unsupported Stable Diffusion API action: " + action };

            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.sd.host, cfg.sd.port, target, std::move(body), std::move(options)) };
            request.method = method;
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return response.body;
        }

        public_api::http_response_data make_api_request(const config& cfg, const public_api::http_executor& executor)
        {
            return make_api_request(cfg, executor, [](const std::filesystem::path& path, std::ios_base::openmode mode)
            {
                return filesystem::read_file_to_string(path, mode);
            });
        }

        public_api::http_response_data make_api_request(const config& cfg, const public_api::http_executor& executor,
            const filesystem::file_reader& reader)
        {
            using public_api::http_method;
            http_method method;
            const std::string& method_name{ cfg.sd.api_method };
            if (method_name == "GET") method = http_method::get;
            else if (method_name == "POST") method = http_method::post;
            else if (method_name == "PUT") method = http_method::put;
            else if (method_name == "PATCH") method = http_method::patch;
            else if (method_name == "DELETE") method = http_method::delete_;
            else if (method_name == "HEAD") method = http_method::head;
            else if (method_name == "OPTIONS") method = http_method::options;
            else throw std::invalid_argument{ "sd-api request method must be GET, POST, PUT, PATCH, DELETE, HEAD, or OPTIONS" };

            const std::string& path{ cfg.sd.api_path };
            const std::string decoded_path{ boost::urls::pct_string_view{ path }.decode() };
            const bool has_dot_segment{ [&decoded_path]
            {
                std::size_t segment_start{};
                while (segment_start <= decoded_path.size())
                {
                    const std::size_t segment_end{ decoded_path.find('/', segment_start) };
                    const std::string_view segment{ decoded_path.data() + segment_start,
                        (segment_end == std::string::npos ? decoded_path.size() : segment_end) - segment_start };
                    if (segment == "." || segment == "..") return true;
                    if (segment_end == std::string::npos) break;
                    segment_start = segment_end + 1;
                }
                return false;
            }() };
            if (path.empty() || path.front() != '/' || path.find('#') != std::string::npos
                || path.find('\\') != std::string::npos || decoded_path.find('\\') != std::string::npos
                || path.starts_with("//") || decoded_path.starts_with("//") || has_dot_segment
                || std::any_of(path.begin(), path.end(), [](unsigned char character)
                    { return character <= 0x20 || character == 0x7f; }))
            {
                throw std::invalid_argument{ "sd-api request path must be an absolute local path without traversal or fragment" };
            }
            if (!cfg.sd.api_options.empty() && !cfg.sd.api_body_file.empty())
            {
                throw std::invalid_argument{ "sd-api request accepts either --sd-api-options or --sd-api-body-file, not both" };
            }
            std::string body;
            if (!cfg.sd.api_body_file.empty())
            {
                const std::filesystem::path body_path{ filesystem::string_to_path_by_config(cfg.sd.api_body_file, cfg) };
                body = filesystem::read_file_to_string(body_path, std::ios::binary, reader);
            }
            else if (!cfg.sd.api_options.empty())
            {
                const nlohmann::json options{ nlohmann::json::parse(cfg.sd.api_options) };
                if (!options.is_object() && !options.is_array())
                {
                    throw std::invalid_argument{ "sd-api request body must be a JSON object or array" };
                }
                body = options.dump();
            }
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.sd.host, cfg.sd.port, path, std::move(body), std::move(options)) };
            request.method = method;
            if (!cfg.sd.api_content_type.empty())
            {
                if (std::any_of(cfg.sd.api_content_type.begin(), cfg.sd.api_content_type.end(), [](unsigned char character)
                    { return character < 0x20 || character == 0x7f; }))
                {
                    throw std::invalid_argument{ "sd-api request Content-Type contains a control character" };
                }
                request.content_type = cfg.sd.api_content_type;
            }
            if (!cfg.sd.api_headers.empty())
            {
                const nlohmann::json headers{ nlohmann::json::parse(cfg.sd.api_headers) };
                if (!headers.is_object()) throw std::invalid_argument{ "sd-api request headers must be a JSON object of string values" };
                const std::string token_characters{ "!#$%&'*+-.^_`|~" };
                for (const auto& [name, value] : headers.items())
                {
                    const bool valid_name{ !name.empty() && std::all_of(name.begin(), name.end(), [&](unsigned char character)
                    {
                        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
                            || (character >= '0' && character <= '9') || token_characters.find(static_cast<char>(character)) != std::string::npos;
                    }) };
                    std::string normalized_name{ name };
                    std::transform(normalized_name.begin(), normalized_name.end(), normalized_name.begin(), [](unsigned char character)
                    {
                        return static_cast<char>(std::tolower(character));
                    });
                    if (!valid_name || normalized_name == "host" || normalized_name == "content-length"
                        || normalized_name == "transfer-encoding" || normalized_name == "connection" || normalized_name == "content-type"
                        || !value.is_string())
                    {
                        throw std::invalid_argument{ "sd-api request contains an invalid, reserved, or non-string header" };
                    }
                    const std::string header_value{ value.get<std::string>() };
                    if (std::any_of(header_value.begin(), header_value.end(), [](unsigned char character)
                        { return (character < 0x20 && character != '\t') || character == 0x7f; }))
                    {
                        throw std::invalid_argument{ "sd-api request header value contains a control character" };
                    }
                    request.headers.emplace(name, header_value);
                }
            }
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return response;
        }
    } // namespace sd

} // namespace Hermit

