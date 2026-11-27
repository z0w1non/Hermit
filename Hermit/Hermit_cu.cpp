#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <boost/asio/connect.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/nowide/iostream.hpp>

#include "Hermit_generation.hpp"
#include "Hermit_filesystem.hpp"
#include "Hermit_tcp.hpp"
#include "Hermit_http.hpp"
#include "Hermit_log.hpp"
#include "Hermit_public_http_adapter.hpp"
#include "Hermit_errors.hpp"
#include "Hermit_runtime.hpp"
#include "Hermit_string_utils.hpp"

namespace Hermit
{
    namespace cu
    {
        namespace
        {
            bool is_safe_userdata_path(std::string_view path, bool allow_empty = false)
            {
                if (path.empty()) return allow_empty;
                if (path.front() == '/' || path.front() == '\\' || path.find('\\') != std::string_view::npos
                    || path.find(':') != std::string_view::npos || path.find('%') != std::string_view::npos)
                {
                    return false;
                }
                std::size_t segment_start{};
                while (segment_start <= path.size())
                {
                    const std::size_t separator{ path.find('/', segment_start) };
                    const std::string_view segment{ path.substr(segment_start,
                        separator == std::string_view::npos ? path.size() - segment_start : separator - segment_start) };
                    if (segment.empty() || segment == "." || segment == "..") return false;
                    if (separator == std::string_view::npos) break;
                    segment_start = separator + 1;
                }
                return true;
            }

            bool is_safe_comfy_user_id(std::string_view user_id)
            {
                return !user_id.empty() && std::all_of(user_id.begin(), user_id.end(), [](unsigned char character)
                {
                    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
                        || (character >= '0' && character <= '9') || character == '-' || character == '_';
                });
            }
        }

        std::string make_websocket_target(const cu_parameters& cfg)
        {
            boost::urls::url url{};
            url.set_path("/ws");
            url_params_setter{ url }.set_if(!cfg.client_id.empty(), "clientId", cfg.client_id);
            const boost::urls::pct_string_view target{ url.encoded_target() };
            return std::string{ target.data(), target.size() };
        }

        void receive_events(const config& cfg, const websocket_executor& executor, const websocket_event_writer& writer)
        {
            if (!executor || !writer)
            {
                throw std::invalid_argument{ "ComfyUI WebSocket executor and writer are required" };
            }
            std::size_t event_count{};
            const websocket_event_handler handler{ [&cfg, &writer, &event_count](std::string_view message, bool binary)
            {
                if (binary)
                {
                    const nlohmann::json event{ { "type", "binary" }, { "data", string_utils::base64_encode(message) } };
                    writer(event.dump());
                }
                else
                {
                    writer(message);
                }
                ++event_count;
                return cfg.cu.event_count == 0 || event_count < static_cast<std::size_t>(cfg.cu.event_count);
            } };
            executor(cfg.cu.host, cfg.cu.port, make_websocket_target(cfg.cu), handler);
        }

        void receive_events(const config& cfg)
        {
            const websocket_executor executor{ [&cfg](std::string_view host, std::string_view port, std::string_view target,
                const websocket_event_handler& handler)
            {
                namespace asio = boost::asio;
                namespace beast = boost::beast;
                namespace websocket = boost::beast::websocket;
                using tcp = asio::ip::tcp;

                asio::io_context context;
                tcp::resolver resolver{ context };
                websocket::stream<beast::tcp_stream> socket{ context };
                const tcp::resolver::results_type endpoints{ resolver.resolve(std::string{ host }, std::string{ port }) };
                beast::get_lowest_layer(socket).expires_after(std::chrono::seconds{ cfg.timeout_connect });
                beast::get_lowest_layer(socket).connect(endpoints);
                beast::get_lowest_layer(socket).expires_never();
                socket.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
                const std::string host_header{ std::string{ host } + ":" + std::string{ port } };
                socket.handshake(host_header, target);

                beast::flat_buffer buffer;
                bool keep_reading{ true };
                try
                {
                    while (keep_reading)
                    {
                        socket.read(buffer);
                        const std::string message{ beast::buffers_to_string(buffer.data()) };
                        keep_reading = handler(message, socket.got_binary());
                        buffer.consume(buffer.size());
                    }
                }
                catch (const boost::system::system_error& error)
                {
                    if (error.code() != websocket::error::closed) throw;
                }
                boost::system::error_code ignored;
                socket.close(websocket::close_code::normal, ignored);
            } };
            const websocket_event_writer writer{ [](std::string_view message)
            {
                boost::nowide::cout << message << '\n' << std::flush;
            } };
            receive_events(cfg, executor, writer);
        }

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
            nlohmann::json fields{ nlohmann::json::object() };
            if (overwrite) fields["overwrite"] = "true";
            return make_image_body(image_data, filename, boundary, fields);
        }

        std::string make_image_body(std::string_view image_data, std::string_view filename, std::string_view boundary,
            const nlohmann::json& fields)
        {
            if (!fields.is_object()) throw std::invalid_argument{ "ComfyUI upload fields must be a JSON object" };
            std::string escaped_filename;
            escaped_filename.reserve(filename.size());
            for (const char character : filename)
            {
                if (character == '\r' || character == '\n' || character == '\0'
                    || static_cast<unsigned char>(character) < 0x20 || static_cast<unsigned char>(character) == 0x7F)
                {
                    throw std::invalid_argument{ "ComfyUI upload filename contains a control character" };
                }
                if (character == '\\' || character == '"') escaped_filename += '\\';
                escaped_filename += character;
            }

            std::string body;
            body.reserve(154 + image_data.size() + boundary.size() + filename.size());
            body += "--";
            body += boundary;
            body += "\r\nContent-Disposition: form-data; name=\"image\"; filename=\"";
            body += escaped_filename;
            body += "\"\r\nContent-Type: image/png\r\n\r\n";
            body += image_data;
            body += "\r\n";

            const std::vector<std::string> field_names{ "overwrite", "type", "subfolder", "original_ref" };
            for (const std::string& field_name : field_names)
            {
                if (!fields.contains(field_name)) continue;
                const nlohmann::json& value{ fields.at(field_name) };
                std::string field_value;
                if (field_name == "original_ref")
                {
                    if (!value.is_object()) throw std::invalid_argument{ "ComfyUI original_ref must be a JSON object" };
                    for (const auto& [reference_key, reference_value] : value.items())
                    {
                        if ((reference_key != "filename" && reference_key != "type" && reference_key != "subfolder")
                            || !reference_value.is_string())
                        {
                            throw std::invalid_argument{ "ComfyUI original_ref contains an unsupported or non-string field" };
                        }
                    }
                    field_value = value.dump();
                }
                else if (field_name == "overwrite")
                {
                    if (value.is_boolean()) field_value = value.get<bool>() ? "true" : "false";
                    else if (value.is_string()) field_value = value.get<std::string>();
                    else throw std::invalid_argument{ "ComfyUI upload overwrite must be a boolean or string" };
                }
                else
                {
                    if (!value.is_string()) throw std::invalid_argument{ "ComfyUI upload type and subfolder must be strings" };
                    field_value = value.get<std::string>();
                }
                body += "--";
                body += boundary;
                body += "\r\nContent-Disposition: form-data; name=\"";
                body += field_name;
                body += "\"\r\n\r\n";
                body += field_value;
                body += "\r\n";
            }

            for (const auto& [field_name, value] : fields.items())
            {
                if (std::find(field_names.begin(), field_names.end(), field_name) == field_names.end())
                {
                    throw std::invalid_argument{ "Unsupported ComfyUI upload field: " + field_name };
                }
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
            tcp::ensure_success(response);
            nlohmann::json response_json{ nlohmann::json::parse(response.body()) };

            return response_json.at("name").get<std::string>();
        }

        std::string upload_image(const config& cfg, std::string_view image_path, bool overwrite, const public_api::http_executor& executor)
        {
            const nlohmann::json fields{ { "overwrite", overwrite } };
            const filesystem::file_reader reader{ [](const std::filesystem::path& path, std::ios_base::openmode mode)
            {
                return filesystem::read_file_to_string(path, mode);
            } };
            const nlohmann::json response_json{ nlohmann::json::parse(
                upload_file(cfg, image_path, cfg.cu.upload_image_target, fields, executor, reader)) };
            return response_json.at("name").get<std::string>();
        }

        std::string upload_file(const config& cfg, std::string_view image_path, std::string_view target,
            const nlohmann::json& fields, const public_api::http_executor& executor, const filesystem::file_reader& reader)
        {
            if (!executor || !reader) throw std::invalid_argument{ "ComfyUI upload executor and file reader are required" };
            const std::string image_data{ filesystem::read_binary_file_to_string(image_path, cfg, reader) };
            const std::string filename{ std::filesystem::path{ image_path }.filename().string() };
            const std::string boundary{ generate_boundary() };
            const std::string body{ make_image_body(image_data, filename, boundary, fields) };
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.cu.host, cfg.cu.port, target, body, std::move(options)) };
            request.content_type = make_image_content_type(boundary);
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return response.body;
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

        std::string manage_api(const config& cfg, const public_api::http_executor& executor)
        {
            const filesystem::file_reader reader{ [](const std::filesystem::path& path, std::ios_base::openmode mode)
            {
                return filesystem::read_file_to_string(path, mode);
            } };
            return manage_api(cfg, executor, reader);
        }

        std::string manage_api(const config& cfg, const public_api::http_executor& executor, const filesystem::file_reader& reader)
        {
            public_api::http_method method{ public_api::http_method::post };
            std::string target;
            std::string raw_body;
            nlohmann::json body{ nlohmann::json::object() };
            if (!cfg.cu.api_options.empty())
            {
                body = nlohmann::json::parse(cfg.cu.api_options);
                if (!body.is_object())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "--cu.api-options must be a JSON object" });
                }
            }

            if (cfg.cu.api_action == "system-stats" || cfg.cu.api_action == "default-workflow"
                || cfg.cu.api_action == "prompt-status" || cfg.cu.api_action == "workflow-templates"
                || cfg.cu.api_action == "i18n" || cfg.cu.api_action == "web-ui")
            {
                if (!cfg.cu.api_options.empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy system-stats and default-workflow do not accept --cu.api-options" });
                }
                method = public_api::http_method::get;
                if (cfg.cu.api_action == "system-stats") target = "/system_stats";
                else if (cfg.cu.api_action == "default-workflow") target = "/default_workflow";
                else if (cfg.cu.api_action == "prompt-status") target = "/prompt";
                else if (cfg.cu.api_action == "workflow-templates") target = "/workflow_templates";
                else if (cfg.cu.api_action == "i18n") target = "/i18n";
                else target = "/";
            }
            else if (cfg.cu.api_action == "template-file")
            {
                const std::string_view path{ cfg.cu.api_template_path };
                const std::size_t separator{ path.find('/') };
                if (!is_safe_userdata_path(path) || separator == std::string_view::npos || separator + 1 >= path.size())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy template-file requires --cu.template-path module/relative-file without traversal" });
                }
                boost::urls::url url{};
                url.set_path("/");
                url.segments().push_back("api");
                url.segments().push_back("workflow_templates");
                std::size_t segment_start{};
                while (segment_start < path.size())
                {
                    const std::size_t segment_end{ path.find('/', segment_start) };
                    url.segments().push_back(path.substr(segment_start,
                        segment_end == std::string_view::npos ? path.size() - segment_start : segment_end - segment_start));
                    if (segment_end == std::string_view::npos) break;
                    segment_start = segment_end + 1;
                }
                method = public_api::http_method::get;
                target = url.encoded_path();
            }
            else if (cfg.cu.api_action == "users")
            {
                if (!body.empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy users does not accept --cu.api-options" });
                }
                method = public_api::http_method::get;
                target = "/users";
            }
            else if (cfg.cu.api_action == "user-create")
            {
                if (!body.contains("username") || body.size() != 1 || !body.at("username").is_string()
                    || body.at("username").get<std::string>().empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy user-create requires --cu.api-options username as a non-empty string" });
                }
                target = "/users";
            }
            else if (cfg.cu.api_action == "userdata-list" || cfg.cu.api_action == "userdata-list-v2")
            {
                boost::urls::url url{};
                const bool legacy{ cfg.cu.api_action == "userdata-list" };
                url.set_path(legacy ? "/userdata" : "/v2/userdata");
                const std::vector<std::string> allowed_fields{ "dir", "recurse", "full_info", "split", "path" };
                url_params_setter params{ url };
                for (const auto& [key, value] : body.items())
                {
                    const bool is_path{ legacy ? key == "dir" : key == "path" };
                    if ((legacy ? key == "path" : key != "path")
                        || std::find(allowed_fields.begin(), allowed_fields.end(), key) == allowed_fields.end()
                        || (is_path ? !value.is_string() : (!value.is_string() && !value.is_boolean())))
                    {
                        Hermit::throw_exception(command_line_exception{} << error_info::description{
                            "comfy userdata listing requires string dir/path and string or boolean recurse/full_info/split fields" });
                    }
                    if (value.is_string()) params(key, value.get<std::string>());
                    else params(key, value.get<bool>());
                }
                if (legacy && (!body.contains("dir") || body.at("dir").get<std::string>().empty()))
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy userdata-list requires --cu.api-options dir" });
                }
                if (body.contains(legacy ? "dir" : "path")
                    && !is_safe_userdata_path(body.at(legacy ? "dir" : "path").get<std::string>(), !legacy))
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "ComfyUI userdata paths must be relative slash-separated paths without traversal" });
                }
                method = public_api::http_method::get;
                target = url.encoded_target();
            }
            else if (cfg.cu.api_action == "userdata-get" || cfg.cu.api_action == "userdata-put"
                || cfg.cu.api_action == "userdata-delete" || cfg.cu.api_action == "userdata-move")
            {
                const bool move{ cfg.cu.api_action == "userdata-move" };
                if (!is_safe_userdata_path(cfg.cu.userdata_path))
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "ComfyUI userdata operations require a relative --cu.userdata-path without traversal" });
                }
                if (move && !is_safe_userdata_path(cfg.cu.userdata_destination))
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy userdata-move requires a safe relative --cu.userdata-destination" });
                }
                const bool query_supported{ cfg.cu.api_action == "userdata-put" || move };
                const std::vector<std::string> allowed_fields{ "full_info", "overwrite" };
                for (const auto& [key, value] : body.items())
                {
                    if (!query_supported || std::find(allowed_fields.begin(), allowed_fields.end(), key) == allowed_fields.end()
                        || (!value.is_string() && !value.is_boolean()))
                    {
                        Hermit::throw_exception(command_line_exception{} << error_info::description{
                            "ComfyUI userdata file options accept only string/boolean full_info and overwrite fields on put/move" });
                    }
                }
                boost::urls::url url{};
                url.set_path("/");
                url.segments().push_back("userdata");
                url.segments().push_back(cfg.cu.userdata_path);
                if (move)
                {
                    url.segments().push_back("move");
                    url.segments().push_back(cfg.cu.userdata_destination);
                }
                url_params_setter params{ url };
                for (const auto& [key, value] : body.items())
                {
                    if (value.is_string()) params(key, value.get<std::string>());
                    else params(key, value.get<bool>());
                }
                target = url.encoded_target();
                if (cfg.cu.api_action == "userdata-get") method = public_api::http_method::get;
                else if (cfg.cu.api_action == "userdata-delete") method = public_api::http_method::delete_;
                else
                {
                    method = public_api::http_method::post;
                    if (cfg.cu.api_action == "userdata-put")
                    {
                        if (cfg.cu.upload_file.empty() || !reader)
                        {
                            Hermit::throw_exception(command_line_exception{} << error_info::description{
                                "comfy userdata-put requires --cu.upload-file" });
                        }
                        raw_body = filesystem::read_binary_file_to_string(cfg.cu.upload_file, cfg, reader);
                    }
                }
            }
            else if (cfg.cu.api_action == "jobs")
            {
                static const std::vector<std::string> allowed_fields{ "status", "workflow_id", "sort_by", "sort_order", "limit", "offset" };
                boost::urls::url url{};
                url.set_path("/api/jobs");
                url_params_setter params{ url };
                for (const auto& [key, value] : body.items())
                {
                    if (std::find(allowed_fields.begin(), allowed_fields.end(), key) == allowed_fields.end()
                        || ((key == "limit" || key == "offset") ? !value.is_number_integer() : !value.is_string()))
                    {
                        Hermit::throw_exception(command_line_exception{} << error_info::description{
                            "comfy jobs accepts status/workflow_id/sort_by/sort_order strings and integer limit/offset fields" });
                    }
                    if (value.is_string()) params(key, value.get<std::string>());
                    else params(key, value.get<std::int64_t>());
                }
                method = public_api::http_method::get;
                target = url.encoded_target();
            }
            else if (cfg.cu.api_action == "job" || cfg.cu.api_action == "job-cancel")
            {
                if (cfg.cu.api_prompt_id.empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy job operations require --cu.prompt-id" });
                }
                if (!body.empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy job operations do not accept --cu.api-options" });
                }
                boost::urls::url url{};
                url.set_path("/");
                url.segments().push_back("api");
                url.segments().push_back("jobs");
                url.segments().push_back(cfg.cu.api_prompt_id);
                if (cfg.cu.api_action == "job-cancel")
                {
                    url.segments().push_back("cancel");
                    method = public_api::http_method::post;
                }
                else method = public_api::http_method::get;
                target = url.encoded_path();
            }
            else if (cfg.cu.api_action == "jobs-cancel")
            {
                if (!body.contains("job_ids") || body.size() != 1 || !body.at("job_ids").is_array()
                    || std::any_of(body.at("job_ids").begin(), body.at("job_ids").end(), [](const nlohmann::json& id)
                        { return !id.is_string() || id.get<std::string>().empty(); }))
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy jobs-cancel requires --cu.api-options job_ids as an array of non-empty IDs" });
                }
                target = "/api/jobs/cancel";
            }
            else if (cfg.cu.api_action == "view" || cfg.cu.api_action == "view-metadata")
            {
                const bool metadata{ cfg.cu.api_action == "view-metadata" };
                const std::vector<std::string> allowed_fields{ "filename", "type", "subfolder", "preview", "channel" };
                if (!body.contains("filename") || !body.at("filename").is_string()
                    || body.at("filename").get<std::string>().empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy view operations require --cu.api-options filename" });
                }
                if (metadata && body.size() != 1)
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy view-metadata accepts only filename in --cu.api-options" });
                }
                boost::urls::url url{};
                url.set_path("/");
                if (metadata)
                {
                    if (cfg.cu.api_metadata_folder.empty() || cfg.cu.api_metadata_folder.find("..") != std::string::npos
                        || cfg.cu.api_metadata_folder.find('\\') != std::string::npos
                        || cfg.cu.api_metadata_folder.find('/') != std::string::npos)
                    {
                        Hermit::throw_exception(command_line_exception{} << error_info::description{
                            "comfy view-metadata folder must be a single safe path segment" });
                    }
                    url.segments().push_back("view_metadata");
                    url.segments().push_back(cfg.cu.api_metadata_folder);
                }
                else
                {
                    url.segments().push_back("view");
                    for (const auto& [key, value] : body.items())
                    {
                        if (std::find(allowed_fields.begin(), allowed_fields.end(), key) == allowed_fields.end()
                            || !value.is_string())
                        {
                            Hermit::throw_exception(command_line_exception{} << error_info::description{
                                "comfy view options accept string filename/type/subfolder/preview/channel fields" });
                        }
                    }
                }
                url_params_setter params{ url };
                for (const auto& [key, value] : body.items()) params(key, value.get<std::string>());
                method = public_api::http_method::get;
                target = url.encoded_target();
            }
            else if (cfg.cu.api_action == "upload-image" || cfg.cu.api_action == "upload-mask")
            {
                if (cfg.cu.upload_file.empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "ComfyUI upload requires --cu.upload-file" });
                }
                const bool mask{ cfg.cu.api_action == "upload-mask" };
                if (mask)
                {
                    if (!body.contains("original_ref") || !body.at("original_ref").is_object()
                        || !body.at("original_ref").contains("filename")
                        || !body.at("original_ref").at("filename").is_string()
                        || body.at("original_ref").at("filename").get<std::string>().empty())
                    {
                        Hermit::throw_exception(command_line_exception{} << error_info::description{
                            "comfy upload-mask requires --cu.api-options original_ref.filename" });
                    }
                    const std::string original_filename{ body.at("original_ref").at("filename").get<std::string>() };
                    if (original_filename.find("..") != std::string::npos
                        || original_filename.find('/') != std::string::npos
                        || original_filename.find('\\') != std::string::npos)
                    {
                        Hermit::throw_exception(command_line_exception{} << error_info::description{
                            "comfy upload-mask original_ref.filename must be a filename without path traversal" });
                    }
                    if (body.contains("original_ref") && body.at("original_ref").contains("subfolder")
                        && (!body.at("original_ref").at("subfolder").is_string()
                            || body.at("original_ref").at("subfolder").get<std::string>().find("..") != std::string::npos
                            || body.at("original_ref").at("subfolder").get<std::string>().find('\\') != std::string::npos
                            || (!body.at("original_ref").at("subfolder").get<std::string>().empty()
                                && body.at("original_ref").at("subfolder").get<std::string>().front() == '/')))
                    {
                        Hermit::throw_exception(command_line_exception{} << error_info::description{
                            "comfy upload-mask original_ref.subfolder must not contain path traversal" });
                    }
                }
                else if (body.contains("original_ref"))
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy upload-image does not accept original_ref" });
                }
                if (!mask && !body.contains("overwrite")) body["overwrite"] = true;
                target = mask ? "/upload/mask" : cfg.cu.upload_image_target;
                return upload_file(cfg, cfg.cu.upload_file, target, body, executor, reader);
            }
            else if (cfg.cu.api_action == "queue")
            {
                if (!cfg.cu.api_options.empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy queue does not accept --cu.api-options" });
                }
                method = public_api::http_method::get;
                target = "/queue";
            }
            else if (cfg.cu.api_action == "object-info")
            {
                if (!cfg.cu.api_options.empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy object-info does not accept --cu.api-options" });
                }
                boost::urls::url url{};
                url.set_path("/");
                url.segments().push_back("object_info");
                if (!cfg.cu.api_node_class.empty()) url.segments().push_back(cfg.cu.api_node_class);
                method = public_api::http_method::get;
                target = url.encoded_path();
            }
            else if (cfg.cu.api_action == "models" || cfg.cu.api_action == "models-folder"
                || cfg.cu.api_action == "embeddings" || cfg.cu.api_action == "extensions" || cfg.cu.api_action == "features")
            {
                if (!cfg.cu.api_options.empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "ComfyUI listing actions do not accept --cu.api-options" });
                }
                boost::urls::url url{};
                url.set_path("/");
                if (cfg.cu.api_action == "models")
                {
                    if (!cfg.cu.api_model_folder.empty())
                    {
                        Hermit::throw_exception(command_line_exception{} << error_info::description{
                            "comfy models does not accept --cu.model-folder; use models-folder" });
                    }
                    url.segments().push_back("models");
                }
                else if (cfg.cu.api_action == "models-folder")
                {
                    if (cfg.cu.api_model_folder.empty())
                    {
                        Hermit::throw_exception(command_line_exception{} << error_info::description{
                            "comfy models-folder requires --cu.model-folder" });
                    }
                    url.segments().push_back("models");
                    url.segments().push_back(cfg.cu.api_model_folder);
                }
                else if (cfg.cu.api_action == "embeddings") url.segments().push_back("embeddings");
                else if (cfg.cu.api_action == "extensions") url.segments().push_back("extensions");
                else url.segments().push_back("features");
                method = public_api::http_method::get;
                target = url.encoded_path();
            }
            else if (cfg.cu.api_action == "history")
            {
                for (const auto& [key, value] : body.items())
                {
                    if ((key != "max_items" && key != "offset") || !value.is_number_integer())
                    {
                        Hermit::throw_exception(command_line_exception{} << error_info::description{
                            "comfy history options accept only integer max_items and offset fields" });
                    }
                }
                boost::urls::url url{};
                url.set_path("/history");
                url_params_setter params{ url };
                if (body.contains("max_items")) params("max_items", body.at("max_items").get<std::int64_t>());
                if (body.contains("offset")) params("offset", body.at("offset").get<std::int64_t>());
                method = public_api::http_method::get;
                target = url.encoded_target();
            }
            else if (cfg.cu.api_action == "history-item")
            {
                if (cfg.cu.api_prompt_id.empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy history-item requires --cu.prompt-id" });
                }
                if (!cfg.cu.api_options.empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy history-item does not accept --cu.api-options" });
                }
                boost::urls::url url{};
                url.set_path("/");
                url.segments().push_back("history");
                url.segments().push_back(cfg.cu.api_prompt_id);
                method = public_api::http_method::get;
                target = url.encoded_path();
            }
            else if (cfg.cu.api_action == "history-update")
            {
                if (body.empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy history-update requires --cu.api-options" });
                }
                if (body.contains("clear") && !body.at("clear").is_boolean())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy history-update clear must be a boolean" });
                }
                if (body.contains("delete"))
                {
                    const nlohmann::json& ids{ body.at("delete") };
                    if (!ids.is_array() || std::any_of(ids.begin(), ids.end(), [](const nlohmann::json& id)
                        { return !id.is_string() || id.get<std::string>().empty(); }))
                    {
                        Hermit::throw_exception(command_line_exception{} << error_info::description{
                            "comfy history-update delete must be an array of non-empty prompt IDs" });
                    }
                }
                if (!body.contains("clear") && !body.contains("delete"))
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy history-update requires clear or delete" });
                }
                target = "/history";
            }
            else if (cfg.cu.api_action == "queue-update")
            {
                target = "/queue";
                if (body.empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "comfy queue-update requires --cu.api-options" });
                }
            }
            else if (cfg.cu.api_action == "interrupt") target = "/interrupt";
            else if (cfg.cu.api_action == "free") target = "/free";
            else
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "unsupported ComfyUI API action: " + cfg.cu.api_action });
            }

            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.cu.host, cfg.cu.port, target,
                method == public_api::http_method::get || method == public_api::http_method::delete_
                    || cfg.cu.api_action == "userdata-move" ? ""
                    : cfg.cu.api_action == "userdata-put" ? std::move(raw_body) : body.dump(), std::move(options)) };
            request.method = method;
            if (!cfg.cu.api_user_id.empty())
            {
                if (!is_safe_comfy_user_id(cfg.cu.api_user_id))
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "--cu.user-id must contain only letters, digits, hyphens, and underscores" });
                }
                request.headers.emplace("comfy-user", cfg.cu.api_user_id);
            }
            if (cfg.cu.api_action == "userdata-put") request.content_type = "application/octet-stream";
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return response.body;
        }

        bool api_returns_binary(const cu_parameters& parameters)
        {
            return parameters.api_action == "view" || parameters.api_action == "userdata-get";
        }

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

        static void send_request_impl(const config& cfg, std::string_view prompt, const tcp::request_executor& executor,
            const public_api::binary_storage* storage)
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
            tcp::ensure_success(response);
            HERMIT_LOG(info) << "Response: " << response.body();

            nlohmann::json response_json{ nlohmann::json::parse(response.body()) };
            const std::string prompt_id{ response_json.at("prompt_id").get<std::string>() };
            HERMIT_LOG(info) << "Queued successfully. Prompt ID: " << prompt_id;

            const std::vector<generated_file_info> target_files{ receive_generated_file_info(cfg, prompt_id, executor) };
            HERMIT_LOG(info) << "Generation complete";

            if (storage)
            {
                write_generated_files(cfg, target_files, executor, *storage);
            }
            else
            {
                write_generated_files(cfg, target_files, executor);
            }
        }

        void send_request(const config& cfg, std::string_view prompt, const tcp::request_executor& executor)
        {
            send_request_impl(cfg, prompt, executor, nullptr);
        }

        void send_request(const config& cfg, std::string_view prompt, const tcp::request_executor& executor,
            const public_api::binary_storage& storage)
        {
            send_request_impl(cfg, prompt, executor, &storage);
        }

        void send_request(const config& cfg, std::string_view prompt, const public_api::http_executor& executor)
        {
            send_request(cfg, prompt, public_api::make_tcp_executor(executor));
        }

        void send_request(const config& cfg, std::string_view prompt, const public_api::http_executor& executor,
            const public_api::binary_storage& storage)
        {
            send_request(cfg, prompt, public_api::make_tcp_executor(executor), storage);
        }

        void send_request(const config& cfg, std::string_view prompt, const public_api::binary_storage& storage)
        {
            tcp transport;
            send_request(cfg, prompt,
                [&transport, &cfg](std::string_view host, std::string_view port, const tcp::request_type& request)
                {
                    transport.expires_after(std::chrono::seconds{ cfg.timeout_connect }).connect(host, port);
                    return transport.expires_after(std::chrono::seconds{ cfg.timeout_request }).request(request);
                }, storage);
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

            boost::urls::url url{};
            url.set_path("/history");
            url.segments().push_back(prompt_id);

            while (true)
            {
                wait(std::chrono::milliseconds{ 1000 });

                const tcp::request_type request{ tcp::make_get_json_request(cfg.cu.host, url.encoded_path()) };
                const tcp::response_type history_response{ executor(cfg.cu.host, cfg.cu.port, request) };
                tcp::ensure_success(history_response);

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
            write_generated_files(cfg, target_files, executor,
                [&cfg](std::string_view path, std::string_view data)
                {
                    filesystem::write_file(cfg, data, path, std::ios::binary);
                });
        }

        void write_generated_files(const config& cfg, const std::vector<generated_file_info>& target_files,
            const tcp::request_executor& executor, const public_api::binary_storage& storage)
        {
            for (const generated_file_info& file_info : target_files)
            {
                std::filesystem::path relative_file_path{ cfg.cu.output_directory };
                if (cfg.cu.preserve_subdirectories)
                {
                    const std::filesystem::path subfolder{ file_info.subfolder };
                    const std::filesystem::path normalized_subfolder{ subfolder.lexically_normal() };
                    if (subfolder.is_absolute() || subfolder.has_root_name() || subfolder.has_root_directory()
                        || std::any_of(normalized_subfolder.begin(), normalized_subfolder.end(),
                            [](const std::filesystem::path& component) { return component == ".."; }))
                    {
                        Hermit::throw_exception(comfy_ui_generation_exception{} << error_info::description{ "ComfyUI output subfolder escapes the configured output directory" });
                    }
                    relative_file_path /= normalized_subfolder;
                }
                const std::string filename{ sanitize_as_filename(std::filesystem::path{ file_info.filename }.filename().string()) };
                if (filename.empty())
                {
                    Hermit::throw_exception(comfy_ui_generation_exception{} << error_info::description{ "ComfyUI output filename is empty after sanitization" });
                }
                relative_file_path /= filename;

                boost::urls::url target;
                target.set_path("/");
                url_params_setter{ target }
                    ("filename", file_info.filename)
                    ("subfolder", file_info.subfolder)
                    ("type", file_info.type);

                const tcp::request_type request{ tcp::make_get_json_request(cfg.cu.host, target.encoded_target()) };
                const tcp::response_type view_response{ executor(cfg.cu.host, cfg.cu.port, request) };
                tcp::ensure_success(view_response);

                storage(relative_file_path.string(), view_response.body());
            }
        }

        void write_generated_files(const config& cfg, const std::vector<generated_file_info>& target_files, const public_api::http_executor& executor)
        {
            write_generated_files(cfg, target_files, public_api::make_tcp_executor(executor));
        }

        void write_generated_files(const config& cfg, const std::vector<generated_file_info>& target_files,
            const public_api::http_executor& executor, const public_api::binary_storage& storage)
        {
            write_generated_files(cfg, target_files, public_api::make_tcp_executor(executor), storage);
        }
    } // namespace cu

} // namespace Hermit

