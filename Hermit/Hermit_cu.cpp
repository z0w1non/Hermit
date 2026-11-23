#include "Hermit_generation.hpp"
#include "Hermit_filesystem.hpp"
#include "Hermit_tcp.hpp"
#include "Hermit_http.hpp"
#include "Hermit_log.hpp"
#include "Hermit_public_http_adapter.hpp"
#include "Hermit_errors.hpp"

#include <chrono>
#include <thread>
#include <random>
#include <sstream>
#include <iomanip>

namespace Hermit
{
    namespace cu
    {
        std::string generate_boundary()
        {
            static thread_local std::mt19937_64 generator{ std::random_device{}() };
            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            oss << std::setw(sizeof(std::uint64_t) * 2) << generator()
                << std::setw(sizeof(std::uint64_t) * 2) << generator()
                << std::setw(sizeof(std::uint64_t) * 2) << generator()
                << std::setw(sizeof(std::uint64_t) * 2) << generator();
            return oss.str();
        }

        std::string make_image_body(std::string_view image_data, std::string_view filename, std::string_view boundary, bool overwrite)
        {
            std::string body;
            body.reserve(154 + image_data.size() + boundary.size() + filename.size());

            body += "--";
            body += boundary;
            body += "\r\nContent-Disposition: form-data; name=\"image\"; filename=\"";
            body += filename;
            body += "\"\r\nContent-Type: image/png\r\n\r\n";
            body += image_data;
            body += "\r\n";

            if (overwrite)
            {
                body += "--";
                body += boundary;
                body += "\r\nContent-Disposition: form-data; name=\"overwrite\"\r\n\r\ntrue\r\n";
            }

            body += "--";
            body += boundary;
            body += "--\r\n";

            return body;
        }

        std::string make_image_content_type(std::string_view boundary)
        {
            std::string content_type;
            content_type.reserve(30 + boundary.size());
            content_type += "multipart/form-data; boundary=";
            content_type += boundary;
            return content_type;
        }

        std::string upload_image(const config& cfg, std::string_view image_path, bool overwrite)
        {
            tcp transport;
            return upload_image(cfg, image_path, overwrite,
                [&transport, &cfg](std::string_view host, std::string_view port, const tcp::request_type& request)
                {
                    transport.expires_after(std::chrono::seconds{ cfg.timeout_connect }).connect(host, port);
                    return transport.expires_after(std::chrono::seconds{ cfg.timeout_request }).request(request);
                });
        }

        std::string upload_image(const config& cfg, std::string_view image_path, bool overwrite, const tcp::request_executor& executor)
        {
            const std::string_view host{ cfg.cu.host };
            const std::string_view port{ cfg.cu.port };
            const std::string_view target{ cfg.cu.upload_image_target };

            const std::string image_data{ filesystem::read_binary_file_to_string(image_path, cfg) };
            const std::string filename{ std::filesystem::path{ image_path }.filename().string() };
            const std::string boundary{ generate_boundary() };

            const std::string body{ make_image_body(image_data, filename, boundary, overwrite) };
            const std::string content_type{ make_image_content_type(boundary) };

            tcp::request_type request{ tcp::make_post_json_request(host, target, body) };
            request.set(boost::beast::http::field::content_type, content_type);
            request.prepare_payload();

            const tcp::response_type response{ executor(host, port, request) };
            nlohmann::json response_json{ nlohmann::json::parse(response.body()) };

            return response_json.at("name").get<std::string>();
        }

        std::string upload_image(const config& cfg, std::string_view image_path, bool overwrite, const public_api::http_executor& executor)
        {
            const std::string image_data{ filesystem::read_binary_file_to_string(image_path, cfg) };
            const std::string filename{ std::filesystem::path{ image_path }.filename().string() };
            const std::string boundary{ generate_boundary() };
            const std::string body{ make_image_body(image_data, filename, boundary, overwrite) };
            public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.cu.host, cfg.cu.port, cfg.cu.upload_image_target, body) };
            request.content_type = make_image_content_type(boundary);
            const nlohmann::json response_json{ nlohmann::json::parse(executor(request).body) };
            return response_json.at("name").get<std::string>();
        }

        void upload_images(const config& cfg, context& ctx)
        {
            for (const std::string& key_value_pair : cfg.cu.upload_images)
            {
                const std::size_t separator_position{ key_value_pair.find('=') };
                if (separator_position != std::string::npos)
                {
                    const std::string variable_name{ key_value_pair.substr(0, separator_position) };
                    const std::string local_relative_path{ key_value_pair.substr(separator_position + 1) };
                    if (!variable_name.empty())
                    {
                        const std::string server_path{ upload_image(cfg, local_relative_path) };
                        ctx.set(variable_name, server_path);
                        HERMIT_LOG(info) << "Successfully uploaded. (" << variable_name << "=" << server_path << ")";
                    }
                }
                else
                {
                    HERMIT_LOG(warning) << "Invalid upload images format: " << key_value_pair << ". Expected variable_name=local_path";
                }
            }
        }

        struct generated_file_info
        {
            std::string filename;
            std::string subfolder;
            std::string type;
        };

        void send_request(const config& cfg, std::string_view prompt)
        {
            tcp transport;
            send_request(cfg, prompt,
                [&transport, &cfg](std::string_view host, std::string_view port, const tcp::request_type& request)
                {
                    transport.expires_after(std::chrono::seconds{ cfg.timeout_connect }).connect(host, port);
                    return transport.expires_after(std::chrono::seconds{ cfg.timeout_request }).request(request);
                });
        }

        void send_request(const config& cfg, std::string_view prompt, const tcp::request_executor& executor)
        {
            const std::string_view host{ cfg.cu.host };
            const std::string_view port{ cfg.cu.port };
            const std::string_view target{ cfg.cu.prompt_target };

            nlohmann::json json;
            nlohmann::json prompt_json{ nlohmann::json::parse(prompt) };
            json["prompt"] = prompt_json;

            const std::string request_body{ json.dump() };
            HERMIT_LOG(info) << "Send JSON\n```\n" << request_body << "\n```";

            tcp::request_type request{ tcp::make_post_json_request(host, target, request_body) };
            request.prepare_payload();

            const tcp::response_type response{ executor(host, port, request) };
            HERMIT_LOG(info) << "Response: " << response.body();

            nlohmann::json response_json{ nlohmann::json::parse(response.body()) };
            const std::string prompt_id{ response_json.at("prompt_id").get<std::string>() };
            HERMIT_LOG(info) << "Queued successfully. Prompt ID: " << prompt_id;

            const std::vector<generated_file_info> target_files{ receive_generated_file_info(cfg, prompt_id, executor) };
            HERMIT_LOG(info) << "Generation complete";

            write_generated_files(cfg, target_files, executor);
        }

        std::vector<generated_file_info> receive_generated_file_info(const config& cfg, std::string_view prompt_id)
        {
            tcp transport;
            return receive_generated_file_info(cfg, prompt_id,
                [&transport, &cfg](std::string_view host, std::string_view port, const tcp::request_type& request)
                {
                    transport.expires_after(std::chrono::seconds{ cfg.timeout_connect }).connect(host, port);
                    return transport.expires_after(std::chrono::seconds{ cfg.timeout_request }).request(request);
                },
                [](std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); });
        }

        std::vector<generated_file_info> receive_generated_file_info(const config& cfg, std::string_view prompt_id, const tcp::request_executor& executor)
        {
            return receive_generated_file_info(cfg, prompt_id, executor,
                [](std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); });
        }

        std::vector<generated_file_info> receive_generated_file_info(const config& cfg, std::string_view prompt_id, const tcp::request_executor& executor, const wait_function& wait)
        {
            std::vector<generated_file_info> target_files;

            boost::urls::url url;
            url.set_path("/history");
            url.path().append(prompt_id);

            while (true)
            {
                wait(std::chrono::milliseconds{ 1000 });

                const tcp::request_type request{ tcp::make_get_json_request(cfg.cu.host, url.encoded_path()) };
                const tcp::response_type history_response{ executor(cfg.cu.host, cfg.cu.port, request) };

                nlohmann::json history_json{ nlohmann::json::parse(history_response.body()) };

                if (!history_json.is_object())
                {
                    continue;
                }

                try
                {
                    const nlohmann::json& prompt_response_obj{ history_json.at(prompt_id) };

                    try
                    {
                        const nlohmann::json& status_object{ prompt_response_obj.at("status") };
                        const std::string status_str{ status_object.at("status_str").get<std::string>() };
                        if (status_str == "error")
                        {
                            Hermit::throw_exception(comfy_ui_generation_exception{} << error_info::description{ "ComfyUI generation failed on server" });
                        }
                    }
                    catch (const nlohmann::json::out_of_range&)
                    {
                        ;
                    }

                    target_files.clear();

                    const nlohmann::json& outputs_object{ prompt_response_obj.at("outputs") };
                    for (const auto& [key, value] : outputs_object.items())
                    {
                        if (!value.is_object())
                        {
                            continue;
                        }

                        for (const auto& [prop_key, file_list] : value.items())
                        {
                            if (!file_list.is_array())
                            {
                                continue;
                            }

                            for (const nlohmann::json& file_item : file_list)
                            {
                                if (!file_item.is_object()
                                    || !file_item.contains("filename")
                                    || !file_item.contains("type"))
                                {
                                    continue;
                                }

                                target_files.emplace_back
                                (
                                    file_item.at("filename").get<std::string>(),
                                    file_item.value("subfolder", ""),
                                    file_item.at("type").get<std::string>()
                                );
                            }
                        }
                    }

                    if (!target_files.empty())
                    {
                        break;
                    }
                }
                catch (const nlohmann::json::out_of_range&)
                {
                    continue;
                }
            }

            return target_files;
        }

        std::vector<generated_file_info> receive_generated_file_info(const config& cfg, std::string_view prompt_id, const public_api::http_executor& executor, const wait_function& wait)
        {
            return receive_generated_file_info(cfg, prompt_id, public_api::make_tcp_executor(executor), wait);
        }

        std::vector<generated_file_info> receive_generated_file_info(const config& cfg, std::string_view prompt_id, const public_api::http_executor& executor)
        {
            return receive_generated_file_info(cfg, prompt_id, executor,
                [](std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); });
        }

        void write_generated_files(const config& cfg, const std::vector<generated_file_info>& target_files)
        {
            tcp transport;
            write_generated_files(cfg, target_files,
                [&transport, &cfg](std::string_view host, std::string_view port, const tcp::request_type& request)
                {
                    transport.expires_after(std::chrono::seconds{ cfg.timeout_connect }).connect(host, port);
                    return transport.expires_after(std::chrono::seconds{ cfg.timeout_request }).request(request);
                });
        }

        void write_generated_files(const config& cfg, const std::vector<generated_file_info>& target_files, const tcp::request_executor& executor)
        {
            for (const generated_file_info& file_info : target_files)
            {
                std::filesystem::path relative_file_path{ cfg.cu.output_directory };
                if (cfg.cu.preserve_subdirectories)
                {
                    relative_file_path /= file_info.subfolder;
                }
                relative_file_path /= file_info.filename;

                boost::urls::url target;
                url_params_setter{ target }
                    ("filename", file_info.filename)
                    ("subfolder", file_info.subfolder)
                    ("type", file_info.type);

                const tcp::request_type request{ tcp::make_get_json_request(cfg.cu.host, target.encoded_query()) };
                const tcp::response_type view_response{ executor(cfg.cu.host, cfg.cu.port, request) };

                filesystem::write_file(cfg, view_response.body(), relative_file_path.string(), std::ios::binary);
            }
        }

        void write_generated_files(const config& cfg, const std::vector<generated_file_info>& target_files, const public_api::http_executor& executor)
        {
            write_generated_files(cfg, target_files, public_api::make_tcp_executor(executor));
        }
    } // namespace cu

} // namespace Hermit

