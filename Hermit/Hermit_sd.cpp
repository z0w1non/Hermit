#include "Hermit_generation.hpp"
#include "Hermit_filesystem.hpp"
#include "Hermit_tcp.hpp"
#include "Hermit_log.hpp"
#include "Hermit_errors.hpp"
#include "Hermit_string_utils.hpp"
#include "Hermit_public_http_adapter.hpp"
#include <chrono>

namespace Hermit
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
                Hermit::throw_exception(image_generation_exception{} << error_info::description{ "No image data found in the response" });
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
            if (base64_image_data.empty()) Hermit::throw_exception(image_generation_exception{} << error_info::description{ "No image data found in the response" });
            return string_utils::base64_decode(base64_image_data);
        }
    } // namespace sd

} // namespace Hermit

