#ifndef HERMIT_GENERATION_HPP
#define HERMIT_GENERATION_HPP

#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <functional>
#include "nlohmann/json.hpp"
#include "Hermit_config.hpp"
#include "Hermit_tcp.hpp"
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
        std::string send_request(const config&, std::string_view, std::string_view);
        std::string send_request(const config&, std::string_view, std::string_view, const tcp::request_executor&);
        std::string send_request(const config&, std::string_view, std::string_view, const public_api::http_executor&);
    }
    namespace sb
    {
        std::string make_request_url(std::string_view, const sb_parameters&, std::string_view);
        std::string send_request(const config&, std::string_view);
        std::string send_request(const config&, std::string_view, const tcp::request_executor&);
        std::string send_request(const config&, std::string_view, const public_api::http_executor&);
    }
    namespace cu
    {
        std::string generate_boundary();
        std::string make_image_body(std::string_view, std::string_view, std::string_view, bool);
        std::string make_image_content_type(std::string_view);
        std::string upload_image(const config&, std::string_view, bool overwrite = true);
        std::string upload_image(const config&, std::string_view, bool, const tcp::request_executor&);
        std::string upload_image(const config&, std::string_view, bool, const public_api::http_executor&);
        void upload_images(const config&, context&);
        void send_request(const config&, std::string_view);
        void send_request(const config&, std::string_view, const tcp::request_executor&);
        struct generated_file_info;
        std::vector<generated_file_info> receive_generated_file_info(const config&, std::string_view);
        std::vector<generated_file_info> receive_generated_file_info(const config&, std::string_view, const tcp::request_executor&);
        using wait_function = std::function<void(std::chrono::milliseconds)>;
        std::vector<generated_file_info> receive_generated_file_info(const config&, std::string_view, const tcp::request_executor&, const wait_function&);
        std::vector<generated_file_info> receive_generated_file_info(const config&, std::string_view, const public_api::http_executor&, const wait_function&);
        std::vector<generated_file_info> receive_generated_file_info(const config&, std::string_view, const public_api::http_executor&);
        void write_generated_files(const config&, const std::vector<generated_file_info>&);
        void write_generated_files(const config&, const std::vector<generated_file_info>&, const tcp::request_executor&);
        void write_generated_files(const config&, const std::vector<generated_file_info>&, const public_api::http_executor&);
    }
}

#endif
