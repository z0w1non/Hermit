#ifndef HERMIT_TG_KC_CONFIG_HPP
#define HERMIT_TG_KC_CONFIG_HPP

#include <string>
#include <vector>

#include <boost/describe.hpp>

#include "nlohmann/json.hpp"
#include "llm_types.hpp"

namespace hermit
{
    struct tg_common_parameters
    {
        std::string model;
        int max_tokens{};
        double temperature{};
        double top_p{};
        int top_k{};
        int seed{};
        bool stream{};
        std::vector<std::string> stop;
        std::string user;
        std::string preset;

        double frequency_penalty{};
        double presence_penalty{};
        double repetition_penalty{};
        double encoder_repetition_penalty{};
        int no_repeat_ngram_size{};
        int repetition_penalty_range{};
        double penalty_alpha{};

        bool dynamic_temperature{};
        double dynatemp_low{};
        double dynatemp_high{};
        double dynatemp_exponent{};
        double smoothing_factor{};
        double smoothing_curve{};
        double min_p{};
        double typical_p{};
        double xtc_threshold{};
        double xtc_probability{};
        double epsilon_cutoff{};
        double eta_cutoff{};
        double tfs{};
        double top_a{};
        double top_n_sigma{};

        double dry_multiplier{};
        int dry_allowed_length{};
        double dry_base{};
        std::string dry_sequence_breakers;

        int mirostat_mode{};
        double mirostat_tau{};
        double mirostat_eta{};

        bool do_sample{};
        bool temperature_last{};
        bool auto_max_new_tokens{};
        bool ban_eos_token{};
        bool add_bos_token{};
        bool skip_special_tokens{};
        bool static_cache{};
        int truncation_length{};
        int prompt_lookup_num_tokens{};
        int max_tokens_second{};
        double guidance_scale{};

        std::vector<std::string> sampler_priority;
        std::string custom_token_bans;
        std::string grammar_string;
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
        tg_common_parameters,
        model, max_tokens, temperature, top_p, top_k, seed, stream, stop, user, preset,
        frequency_penalty, presence_penalty, repetition_penalty, encoder_repetition_penalty, no_repeat_ngram_size, repetition_penalty_range, penalty_alpha,
        dynamic_temperature, dynatemp_low, dynatemp_high, dynatemp_exponent, smoothing_factor, smoothing_curve, min_p, typical_p, xtc_threshold, xtc_probability, epsilon_cutoff, eta_cutoff, tfs, top_a, top_n_sigma,
        dry_multiplier, dry_allowed_length, dry_base, dry_sequence_breakers,
        mirostat_mode, mirostat_tau, mirostat_eta,
        do_sample, temperature_last, auto_max_new_tokens, ban_eos_token, add_bos_token, skip_special_tokens, static_cache, truncation_length, prompt_lookup_num_tokens, max_tokens_second, guidance_scale,
        sampler_priority, custom_token_bans, grammar_string
    );

    struct tg_completions_parameters
    {
        std::string suffix;
        bool echo{};
        int best_of{};
        int n{};
        double logprobs{};
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
        tg_completions_parameters,
        suffix, echo, best_of, n, logprobs
    );

    struct tg_chat_completions_parameters
    {
        std::string negative_prompt;
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
        tg_chat_completions_parameters,
        negative_prompt
    );

    struct tg_parameters
        : LlmBackendParameters
    {
        bool openai_compatible{};
        tg_common_parameters common{};
        tg_completions_parameters completions{};
        tg_chat_completions_parameters chat_completions{};

        nlohmann::json get_request_for_completions(std::string_view prompt, int max_tokens) const override;
        std::string parse_response_for_completions(const std::string& response) const override;
        nlohmann::json get_request_for_token_count(std::string_view prompt) const override;
        int parse_response_for_token_count(const std::string& response) const override;
        nlohmann::json get_request_for_chat_completions(const nlohmann::json& messages) const override;
        std::string parse_response_for_chat_completions(const std::string& response) const override;
        int get_max_tokens() const override { return common.max_tokens; }
        int get_truncation_length() const override { return common.truncation_length; }
    };

    struct kc_common_parameters
    {
        int max_context_length{};
        int max_length{};

        double temperature{};
        double tfs{};
        double top_a{};
        double top_k{};
        double top_p{};
        double min_p{};
        double typical{};

        double dynatemp_range{};
        double smoothing_factor{};
        double dynatemp_exponent{};

        int mirostat{};
        double mirostat_tau{};
        double mirostat_eta{};

        double dry_multiplier{};
        double dry_base{};
        int dry_allowed_length{};
        int dry_penalty_last_n{};
        std::vector<std::string> dry_sequence_breakers;

        double xtc_threshold{};
        double xtc_probability{};
        double nsigma{};

        double rep_pen{};
        int rep_pen_range{};
        bool use_default_badwordsids{};
        std::vector<std::string> banned_tokens;

        std::vector<int> sampler_order;
        int sampler_seed{};
        std::vector<std::string> stop_sequence;

        std::string genkey;
        std::string grammar;
        bool grammar_retain_state{};
        bool trim_stop{};
        bool render_special{};
        bool bypass_eos{};
        bool logprobs{};
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE
    (
        kc_common_parameters,
        max_context_length, max_length,
        temperature, tfs, top_a, top_k, top_p, min_p, typical,
        dynatemp_range, smoothing_factor, dynatemp_exponent,
        mirostat, mirostat_tau, mirostat_eta,
        dry_multiplier, dry_base, dry_allowed_length, dry_penalty_last_n, dry_sequence_breakers,
        xtc_threshold, xtc_probability, nsigma,
        rep_pen, rep_pen_range, use_default_badwordsids, banned_tokens,
        sampler_order, sampler_seed, stop_sequence,
        genkey, grammar, grammar_retain_state, trim_stop, render_special, bypass_eos, logprobs
    );

    struct kc_completions_parameters
    {
        std::string prompt;
        std::string memory;
        bool replace_instruct_placeholders{};
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE
    (
        kc_completions_parameters,
        prompt, memory, replace_instruct_placeholders
    );

    struct kc_chat_completions_parameters
    {
        std::vector<std::string> images;
    };

    struct kc_parameters
        : LlmBackendParameters
    {
        bool ollama_compatible{};
        std::string model;
        kc_common_parameters common{};
        kc_completions_parameters completions{};
        kc_chat_completions_parameters chat_completions{};

        nlohmann::json get_request_for_completions(std::string_view prompt, int max_tokens) const override;
        std::string parse_response_for_completions(const std::string& response) const override;
        nlohmann::json get_request_for_token_count(std::string_view prompt) const override;
        int parse_response_for_token_count(const std::string& response) const override;
        nlohmann::json get_request_for_chat_completions(const nlohmann::json& messages) const override;
        std::string parse_response_for_chat_completions(const std::string& response) const override;
        int get_max_tokens() const override { return common.max_length; }
        int get_truncation_length() const override { return common.max_context_length; }
    };

    struct adetailer_parameters
    {
        bool ad_enable{};
        bool skip_img2img{};

        struct arg
        {
            std::string ad_model;
            std::string ad_model_classes;
            bool ad_tab_enable{};
            std::string ad_prompt;
            std::string ad_negative_prompt;
            double ad_confidence{};
            std::string ad_mask_filter_method;
            int ad_mask_k{};
            double ad_mask_min_ratio{};
            double ad_mask_max_ratio{};
            int ad_dilate_erode{};
            int ad_x_offset{};
            int ad_y_offset{};
            std::string ad_mask_merge_invert;
            int ad_mask_blur{};
            double ad_denoising_strength{};
            bool ad_inpaint_only_masked{};
            int ad_inpaint_only_masked_padding{};
            bool ad_use_inpaint_width_height{};
            int ad_inpaint_width{};
            int ad_inpaint_height{};
            bool ad_use_steps{};
            int ad_steps{};
            bool ad_use_cfg_scale{};
            double ad_cfg_scale{};
            bool ad_use_checkpoint{};
            std::string ad_checkpoint;
            bool ad_use_vae{};
            std::string ad_vae;
            bool ad_use_sampler{};
            std::string ad_sampler;
            std::string ad_scheduler;
            bool ad_use_noise_multiplier{};
            double ad_noise_multiplier{};
            bool ad_use_clip_skip{};
            int ad_clip_skip{};
            bool ad_restore_face{};
            std::string ad_controlnet_model;
            std::string ad_controlnet_module;
            std::string ad_controlnet_weight;
            double ad_controlnet_guidance_start{};
            double ad_controlnet_guidance_end{};
        };

        arg args1;
    };

    struct alwayson_scripts
    {
        adetailer_parameters adetailer_parameters;
    };

    int send_token_count_request(const config& cfg, std::string_view prompt);
}

#endif
