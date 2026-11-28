#ifndef HERMIT_SD_CONFIG_HPP
#define HERMIT_SD_CONFIG_HPP

#include <string>
#include <vector>
#include <boost/describe.hpp>
#include "nlohmann/json.hpp"
#include "Hermit_aux_config.hpp"
#include "Hermit_tg_kc_config.hpp"

namespace Hermit
{
    struct config;
    BOOST_DEFINE_ENUM_CLASS(sd_mode, txt2img, img2img);

    sd_mode string_to_sd_mode(std::string_view name);

    std::string sd_mode_to_target(sd_mode mode, const config& cfg);

    struct sd_common_parameters
    {
        std::string prompt;
        std::string negative_prompt;
        std::vector<std::string> styles;
        int seed{};
        int subseed{};
        double subseed_strength{};
        int seed_resize_from_h{};
        int seed_resize_from_w{};
        std::string sampler_name;
        std::string scheduler;
        int batch_size{};
        int n_iter{};
        int steps{};
        double cfg_scale{};
        int width{};
        int height{};
        bool restore_faces{};
        bool tiling{};
        bool do_not_save_samples{};
        bool do_not_save_grid{};
        int eta{};
        double denoising_strength{};
        int s_min_uncond{};
        int s_churn{};
        int s_tmax{};
        int s_tmin{};
        int s_noise{};
        nlohmann::json override_settings;
        bool override_settings_restore_afterwards{};
        std::string refiner_checkpoint;
        double refiner_switch_at{};
        bool disable_extra_networks{};
        std::string comments;
        std::string force_task_id;
        std::string sampler_index;
        std::string script_name;
        nlohmann::json script_args;
        bool send_images{};
        bool save_images{};
        std::string infotext;
        alwayson_scripts alwayson_scripts;
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE
    (
        sd_common_parameters,
        /*prompt,*/
        /*negative_prompt,*/
        styles,
        seed,
        subseed,
        subseed_strength,
        seed_resize_from_h,
        seed_resize_from_w,
        /*sampler_name,*/
        scheduler,
        batch_size,
        n_iter,
        steps,
        cfg_scale,
        width,
        height,
        restore_faces,
        tiling,
        do_not_save_samples,
        do_not_save_grid,
        eta,
        denoising_strength,
        s_min_uncond,
        s_churn,
        s_tmax,
        s_tmin,
        s_noise,
        override_settings,
        override_settings_restore_afterwards,
        refiner_checkpoint,
        refiner_switch_at,
        disable_extra_networks,
        comments,
        force_task_id,
        /*sampler_index,*/
        /*script_name,*/
        /*script_args,*/
        send_images,
        save_images,
        infotext
        /*alwayson_scripts*/
    );

    struct sd_txt2img_parameters
    {
        std::string target;

        bool enable_hr{};
        std::string firstpass_image;
        int firstphase_width{};
        int firstphase_height{};
        double hr_scale{};
        std::string hr_upscaler;
        int hr_second_pass_steps{};
        int hr_resize_x{};
        int hr_resize_y{};
        std::string hr_checkpoint_name;
        std::string hr_sampler_name;
        std::string hr_scheduler;
        std::string hr_prompt;
        std::string hr_negative_prompt;
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE
    (
        sd_txt2img_parameters,
        enable_hr,
        /*firstpass_image,*/
        firstphase_width,
        firstphase_height,
        hr_scale,
        hr_upscaler,
        hr_second_pass_steps,
        hr_resize_x,
        hr_resize_y,
        hr_checkpoint_name,
        hr_sampler_name,
        hr_scheduler,
        hr_prompt,
        hr_negative_prompt
    );

    struct sd_img2img_parameters
    {
        std::string target;
        std::vector<std::string> init_images;
        int seed_resize_from_h{ -1 };
        int seed_resize_from_w{ -1 };
        int resize_mode{};
        double image_cfg_scale{};
        std::string mask;
        int mask_blur_x{};
        int mask_blur_y{};
        int mask_blur{};
        bool mask_round{};
        int inpainting_fill{};
        bool inpaint_full_res{};
        int inpaint_full_res_padding{};
        int inpainting_mask_invert{};
        double initial_noise_multiplier{};
        std::string latent_mask;
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE
    (
        sd_img2img_parameters,
        target,
        /*init_images,*/
        seed_resize_from_h,
        seed_resize_from_w,
        resize_mode,
        image_cfg_scale,
        /*mask,*/
        mask_blur_x,
        mask_blur_y,
        /*mask_blur,*/
        mask_round,
        inpainting_fill,
        inpaint_full_res,
        inpaint_full_res_padding,
        inpainting_mask_invert,
        initial_noise_multiplier,
        latent_mask
    );

    struct sd_parameters
    {
        std::string host;
        std::string port;
        std::string api_action;
        std::string api_options;
        std::string api_method;
        std::string api_path;
        std::string api_body_file;
        std::string api_content_type;
        std::string api_headers;
        std::string generation_options;

        std::string prompt_file;
        std::string negative_prompt_file;
        std::string output_file;

        sd_mode mode;
        sd_common_parameters common;
        sd_txt2img_parameters txt2img;
        sd_img2img_parameters img2img;
        bool abg_remover_enable{};
    };

}

#endif
