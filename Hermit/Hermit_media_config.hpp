#ifndef HERMIT_MEDIA_CONFIG_HPP
#define HERMIT_MEDIA_CONFIG_HPP

#include <string>
#include <string_view>
#include <vector>

namespace Hermit
{
    struct config;
    struct image_info_type
    {
        std::string base64_image;
        std::string mime_type;
        static image_info_type from_file(std::string_view path, const config& cfg);
    };
    struct sb_parameters
    {
        std::string host, port, target, text_file, output_file, text, model_name;
        int model_id{};
        std::string speaker_name;
        int speaker_id{};
        double sdp_ratio{}, noise{}, noisew{}, length{};
        std::string language;
        bool auto_split{};
        double split_interval{};
        std::string assist_text;
        double assist_text_weight{};
        std::string style;
        double style_weight{};
        std::string reference_audio_path;
    };
    struct cu_parameters
    {
        std::string host, port, prompt_target, upload_image_target;
        std::string prompt, prompt_file, output_directory;
        std::vector<std::string> upload_images;
        bool preserve_subdirectories{};
    };
}

#endif
