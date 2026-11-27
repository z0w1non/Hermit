#ifndef HERMIT_GENERATION_HPP
#define HERMIT_GENERATION_HPP

#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"

#include "Hermit_config.hpp"
#include "Hermit_filesystem.hpp"
#include "Hermit_tcp.hpp"
#include "Hermit_public_http_adapter.hpp"
#include "Hermit_public_types.hpp"

namespace Hermit
{
    namespace sd
    {
        std::string make_png_parameters(const sd_parameters&, std::string_view, std::string_view);
        nlohmann::json make_txt2img_request(const config&, std::string_view, std::string_view);
        nlohmann::json make_img2img_request(const config&, std::string_view, std::string_view);
        nlohmann::json make_adetailer_request(const config&);
        nlohmann::json make_abg_remover_request();
        nlohmann::json make_request(const config&, std::string_view, std::string_view);
        nlohmann::json make_request(const config&, std::string_view, std::string_view, const filesystem::file_reader&);
        std::string send_request(const config&, std::string_view, std::string_view);
        std::string send_request(const config&, std::string_view, std::string_view, const tcp::request_executor&);
        std::string send_request(const config&, std::string_view, std::string_view, const tcp::request_executor&, const filesystem::file_reader&);
        std::string send_request(const config&, std::string_view, std::string_view, const public_api::http_executor&);
        std::string send_request(const config&, std::string_view, std::string_view, const public_api::http_executor&, const filesystem::file_reader&);
    }
    namespace sb
    {
        std::string make_request_url(std::string_view, const sb_parameters&, std::string_view);
        std::string make_text_request_url(std::string_view, std::string_view);
        std::string send_request(const config&, std::string_view);
        std::string send_request(const config&, std::string_view, const tcp::request_executor&);
        std::string send_request(const config&, std::string_view, const public_api::http_executor&);
        std::string send_post_request(const config&, std::string_view);
        std::string send_post_request(const config&, std::string_view, const public_api::http_executor&);
        nlohmann::json get_models_info(const config&);
        nlohmann::json get_models_info(const config&, const public_api::http_executor&);
        nlohmann::json refresh_models(const config&);
        nlohmann::json refresh_models(const config&, const public_api::http_executor&);
        nlohmann::json get_status(const config&);
        nlohmann::json get_status(const config&, const public_api::http_executor&);
        nlohmann::json get_phonemes(const config&, std::string_view);
        nlohmann::json get_phonemes(const config&, std::string_view, const public_api::http_executor&);
        std::string get_audio(const config&, std::string_view);
        std::string get_audio(const config&, std::string_view, const public_api::http_executor&);
    }
    namespace cu
    {
        using websocket_event_handler = std::function<bool(std::string_view, bool)>;
        using websocket_executor = std::function<void(std::string_view, std::string_view, std::string_view,
            const websocket_event_handler&)>;
        using websocket_event_writer = std::function<void(std::string_view)>;
        std::string make_websocket_target(const cu_parameters&);
        void receive_events(const config&, const websocket_executor&, const websocket_event_writer&);
        void receive_events(const config&);
        std::string generate_boundary();
        std::string make_image_body(std::string_view, std::string_view, std::string_view, bool);
        std::string make_image_body(std::string_view, std::string_view, std::string_view, const nlohmann::json&);
        std::string make_image_content_type(std::string_view);
        std::string upload_image(const config&, std::string_view, bool overwrite = true);
        std::string upload_image(const config&, std::string_view, bool, const tcp::request_executor&);
        std::string upload_image(const config&, std::string_view, bool, const public_api::http_executor&);
        std::string upload_file(const config&, std::string_view, std::string_view, const nlohmann::json&,
            const public_api::http_executor&, const filesystem::file_reader&);
        void upload_images(const config&, context&);
        std::string manage_api(const config&, const public_api::http_executor&);
        std::string manage_api(const config&, const public_api::http_executor&, const filesystem::file_reader&);
        bool api_returns_binary(const cu_parameters&);
        void send_request(const config&, std::string_view);
        void send_request(const config&, std::string_view, const tcp::request_executor&);
        void send_request(const config&, std::string_view, const tcp::request_executor&, const public_api::binary_storage&);
        void send_request(const config&, std::string_view, const public_api::http_executor&);
        void send_request(const config&, std::string_view, const public_api::http_executor&, const public_api::binary_storage&);
        void send_request(const config&, std::string_view, const public_api::binary_storage&);
        struct generated_file_info
        {
            std::string filename;
            std::string subfolder;
            std::string type;
        };
        std::vector<generated_file_info> receive_generated_file_info(const config&, std::string_view);
        std::vector<generated_file_info> receive_generated_file_info(const config&, std::string_view, const tcp::request_executor&);
        using wait_function = std::function<void(std::chrono::milliseconds)>;
        std::vector<generated_file_info> receive_generated_file_info(const config&, std::string_view, const tcp::request_executor&, const wait_function&);
        std::vector<generated_file_info> receive_generated_file_info(const config&, std::string_view, const public_api::http_executor&, const wait_function&);
        std::vector<generated_file_info> receive_generated_file_info(const config&, std::string_view, const public_api::http_executor&);
        void write_generated_files(const config&, const std::vector<generated_file_info>&);
        void write_generated_files(const config&, const std::vector<generated_file_info>&, const tcp::request_executor&);
        void write_generated_files(const config&, const std::vector<generated_file_info>&, const tcp::request_executor&, const public_api::binary_storage&);
        void write_generated_files(const config&, const std::vector<generated_file_info>&, const public_api::http_executor&);
        void write_generated_files(const config&, const std::vector<generated_file_info>&, const public_api::http_executor&, const public_api::binary_storage&);
    }
}

#endif
