#include <fstream>
#include <filesystem>

#include <boost/describe.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>
#include <boost/nowide/iostream.hpp>
#include "nlohmann/json.hpp"

#include "Hermit_command_line.hpp"
#include "Hermit_config.hpp"
#include "Hermit_filesystem.hpp"
#include "Hermit_string_utils.hpp"
#include "Hermit_log.hpp"

namespace Hermit
{
    namespace command_line
    {
        void parse_user_defined_variables(const std::vector<std::string>& user_defined_variables, context& ctx)
        {
            for (const std::string& key_value_pair : user_defined_variables)
            {
                const std::size_t separator_position{ key_value_pair.find('=') };
                if (separator_position != std::string::npos)
                {
                    const std::string key{ key_value_pair.substr(0, separator_position) };
                    const std::string value{ key_value_pair.substr(separator_position + 1) };
                    if (!key.empty())
                    {
                        ctx.set(key, value);
                        HERMIT_LOG(info) << "Variable set " << key << " = " << value;
                    }
                }
                else
                {
                    HERMIT_LOG(warning) << "Invalid define format: " << key_value_pair << ". Expected key=value";
                }
            }
        }

        std::vector<std::string> split_command_line_args(std::string_view args)
        {
            using separator_type = boost::escaped_list_separator<char>;
            using tokenizer_type = boost::tokenizer<separator_type, std::string_view::const_iterator, std::string>;

            const separator_type separator{ '\0', ' ', '"' };
            const tokenizer_type tokenizer{ args, separator };

            std::vector<std::string> result;
            for (const std::string& token : tokenizer)
            {
                if (!token.empty())
                {
                    result.push_back(token);
                }
            }
            return result;
        }

        boost::program_options::options_description make_options_description(config& cfg)
        {
            namespace po = boost::program_options;

            const std::vector<std::string> default_stop{ "\\n\\n", ":", "***" };
            const std::vector<std::string> default_sampler_priority
            {
                "repetition_penalty",
                "presence_penalty",
                "frequency_penalty",
                "dry",
                "temperature",
                "dynamic_temperature",
                "quadratic_sampling",
                "top_n_sigma",
                "top_k",
                "top_p",
                "typical_p",
                "epsilon_cutoff",
                "eta_cutoff",
                "tfs",
                "top_a",
                "min_p",
                "mirostat",
                "xtc",
                "encoder_repetition_penalty",
                "no_repeat_ngram"
            };

            const auto log_level_notifier
            {
                [&cfg](const std::string& log_level)
                {
                    cfg.log_level = log::string_to_severity_level(log_level);
                }
            };

            const auto command_mode_notifier
            {
                [&cfg](const std::string& value)
                {
                    cfg.command_mode = string_to_command_mode(value);

                    if (cfg.command_mode == command_mode::tg)
                    {
                        cfg.llm.backend = &cfg.tg;
                        if (cfg.llm.completions_target.empty())
                        {
                            cfg.llm.completions_target = "/v1/completions";
                        }
                        if (cfg.llm.token_count_target.empty())
                        {
                            cfg.llm.token_count_target = "/v1/internal/token-count";
                        }
                        if (cfg.llm.chat_completions_target.empty())
                        {
                            cfg.llm.chat_completions_target = "/v1/chat/completions";
                        }
                    }
                    else if (cfg.command_mode == command_mode::kc)
                    {
                        cfg.llm.backend = &cfg.kc;
                        if (cfg.llm.completions_target.empty())
                        {
                            cfg.llm.completions_target = "/api/v1/generate";
                        }
                        if (cfg.llm.token_count_target.empty())
                        {
                            cfg.llm.token_count_target = "/api/extra/tokencount";
                        }
                        if (cfg.llm.chat_completions_target.empty())
                        {
                            cfg.llm.chat_completions_target = "/v1/chat/completions";
                        }
                    }
                }
            };

            const auto make_to_json_notifier
            {
                [](auto& ref)
                {
                    return [&ref](const std::string& str)
                    {
                        ref = nlohmann::json::parse(str);
                    };
                }
            };

            const auto make_unescape_string_notifier
            {
                [](auto& ref)
                {
                    return [&ref](const auto& str)
                    {
                        ref = string_utils::unescape_string(str);
                    };
                }
            };

            const auto make_unescape_strings_notifier
            {
                [](auto& ref)
                {
                    return [&ref](const auto& strings)
                    {
                        ref = string_utils::unescape_strings(strings);
                    };
                }
            };

            po::options_description options_description("Allowed options");
            options_description.add_options()
                ("help,h", "produce help message")
                ("mode", po::value<std::string>()->notifier(command_mode_notifier), "mode (tg | kc | sd | sb | cu | extract-png-parameters | create-process | terminate-process)")
                ("base-path", po::value(&cfg.base_path)->default_value("."), "base path")
                ("log-level", po::value<std::string>()->default_value("info")->notifier(log_level_notifier), "log level (trace|debug|info|warning|error|fatal)")
                ("log-file", po::value(&cfg.log_file)->default_value("log"), "log file path")
                ("config-file,c", po::value(&cfg.config_file)->default_value("config.ini"), "config file path")
                ("verbose,v", po::bool_switch(&cfg.verbose)->default_value(false), "enable verbose output")
                ("number-iterations,N", po::value(&cfg.number_iterations)->default_value(1), "number of iterations (-1 means infinity)")
                ("define,D", po::value<std::vector<std::string>>()->multitoken()->notifier(make_unescape_strings_notifier(cfg.user_defined_variables)), "define variables (key=value)")
                ("phases", po::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>{ "" }, "")->notifier(make_unescape_strings_notifier(cfg.phases)), "phases name list")
                ("seed", po::value(&cfg.seed)->default_value(-1), "seed value")

                ("png-file", po::value(&cfg.png.png_file), "for extract-png-parametesrs")

                ("server-executable-file", po::value(&cfg.proc.server_executable_file)->default_value(""), "server executable file")
                ("server-arguments", po::value(&cfg.proc.server_arguments), "server arguments")
                ("server-host", po::value(&cfg.proc.server_host)->default_value("localhost"), "server ip")
                ("server-port", po::value(&cfg.proc.server_port)->default_value("5000"), "server port")
                ("server-max-retries", po::value(&cfg.proc.server_max_retries)->default_value(60), "server max retries")
                ("server-wait-ms", po::value(&cfg.proc.server_wait_ms)->default_value(1000), "server wait ms")

                ("timeout-connect", po::value(&cfg.timeout_connect)->default_value(10), "Time limit for establishing the connection (handshake completion)")
                ("timeout-request", po::value(&cfg.timeout_request)->default_value(0), "Time limit from sending the request to completing the receipt of the response.")

                ("llm-prompt", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.llm.prompt)), "LLM prompt")
                ("llm-prompt-file", po::value(&cfg.llm.prompt_file)->default_value("prompt"), "LLM prompt file path")
                ("llm-output-file", po::value(&cfg.llm.output_file)->default_value("output"), "LLM output file path")
                ("llm-chat-file", po::value(&cfg.llm.chat_file)->default_value(""), "LLM (input / output) chat file path")
                ("llm-generation-prefix", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.llm.generation_prefix)), "LLM generation prefix")
                ("llm-generation-suffix", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.llm.generation_suffix)), "LLM generation suffix")
                ("llm-paragraphs-file", po::value(&cfg.llm.paragraphs_file)->default_value(""), "LLM paragraphs file")
                ("llm-image-file", po::value(&cfg.llm.image_file)->default_value(""), "LLM image file")
                ("llm-host", po::value(&cfg.llm.host)->default_value("localhost"), "LLM host")
                ("llm-port", po::value(&cfg.llm.port)->default_value("5000"), "LLM port")
                ("llm-api-key", po::value(&cfg.llm.api_key)->default_value(""), "LLM API key")
                ("llm-completions-target", po::value(&cfg.llm.completions_target), "LLM completions target")
                ("llm-token-count-target", po::value(&cfg.llm.token_count_target), "LLM token count target")
                ("llm-chat-completions-target", po::value(&cfg.llm.chat_completions_target), "LLM chat completions target")
                ("llm-min-completion-tokens", po::value(&cfg.llm.min_completion_tokens)->default_value(256), "LLM min completion tokens")
                ("llm-max-completion-iterations", po::value(&cfg.llm.max_completion_iterations)->default_value(5), "LLM max completion iterations")
                ("llm-reasoning-prefix", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.llm.reasoning_prefix)), "LLM reasoning prefix")
                ("llm-reasoning-suffix", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.llm.reasoning_suffix)), "LLM reasoning suffix")
                ("llm-code-block-extract", po::bool_switch(&cfg.llm.code_block_extract)->default_value(false), "LLM code block extract switch")

                ("llm-mode", po::value<std::string>()->default_value("completions")->notifier([&cfg](const std::string& value) { cfg.llm.mode = string_to_llm_mode(value); }), "LLM mode (completions | chat-completions)")

                ("tg-model", po::value(&cfg.tg.common.model)->default_value("", "TG model"))
                ("tg-num-best-of", po::value(&cfg.tg.completions.best_of)->default_value(1), "TG best of")
                ("tg-echo", po::bool_switch(&cfg.tg.completions.echo)->default_value(false), "TG echo")
                ("tg-frequency-penalty", po::value(&cfg.tg.common.frequency_penalty)->default_value(0.0), "TG frequency penalty")
                ("tg-logprobs", po::value(&cfg.tg.completions.logprobs)->default_value(0.0), "TG presence penalty")
                ("tg-max-tokens", po::value(&cfg.tg.common.max_tokens)->default_value(512), "TG max tokens")
                ("tg-n", po::value(&cfg.tg.completions.n)->default_value(1), "TG number of responses generated for the same prompt")
                ("tg-presence-penalty", po::value(&cfg.tg.common.presence_penalty)->default_value(0.0), "TG presence penalty")
                ("tg-stop", po::value<std::vector<std::string>>()->multitoken()->default_value(default_stop, boost::algorithm::join(default_stop, " "))->notifier(make_unescape_strings_notifier(cfg.tg.common.stop)), "TG stop sequences")
                ("tg-stream", po::bool_switch(&cfg.tg.common.stream)->default_value(false), "TG stream")
                ("tg-suffix", po::value(&cfg.tg.completions.suffix)->default_value(""), "TG suffix")
                ("tg-temperature", po::value(&cfg.tg.common.temperature)->default_value(1.0), "TG temperature")
                ("tg-top-p", po::value(&cfg.tg.common.top_p)->default_value(1.0), "TG top p")
                ("tg-dynatemp-low", po::value(&cfg.tg.common.dynatemp_low)->default_value(0.75, "0.75"), "TG dynatemp low")
                ("tg-dynatemp-high", po::value(&cfg.tg.common.dynatemp_high)->default_value(1.25, "1.25"), "TG dynatemp high")
                ("tg-dynatemp-exponent", po::value(&cfg.tg.common.dynatemp_exponent)->default_value(1.0), "TG dynatemp exponent")
                ("tg-smoothing-factor", po::value(&cfg.tg.common.smoothing_factor)->default_value(0.0), "TG smoothing factor")
                ("tg-smoothing-curve", po::value(&cfg.tg.common.smoothing_curve)->default_value(1.0), "TG smoothing curve")
                ("tg-min-p", po::value(&cfg.tg.common.min_p)->default_value(0.1, "0.1"), "TG min p")
                ("tg-top-k", po::value(&cfg.tg.common.top_k)->default_value(0), "TG top k")
                ("tg-typical-p", po::value(&cfg.tg.common.typical_p)->default_value(1.0), "TG typical p")
                ("tg-xtc-threshold", po::value(&cfg.tg.common.xtc_threshold)->default_value(0.1, "0.1"), "TG Exclude Top Choices (XTC) threshold")
                ("tg-xtc-probability", po::value(&cfg.tg.common.xtc_probability)->default_value(0.0), "TG Exclude Top Choices (XTC) probability")
                ("tg-epsilon-cutoff", po::value(&cfg.tg.common.epsilon_cutoff)->default_value(0), "TG epsilon cutoff")
                ("tg-eta-cutoff", po::value(&cfg.tg.common.eta_cutoff)->default_value(0), "TG eta cutoff")
                ("tg-tfs", po::value(&cfg.tg.common.tfs)->default_value(1.0), "TG tfs")
                ("tg-top-a", po::value(&cfg.tg.common.top_a)->default_value(0.0), "TG top a")
                ("tg-top-n-sigma", po::value(&cfg.tg.common.top_n_sigma)->default_value(1.0), "TG top n sigma")
                ("tg-dry-multiplier", po::value(&cfg.tg.common.dry_multiplier)->default_value(0.0), "TG DRY multiplier")
                ("tg-dry-allowed-length", po::value(&cfg.tg.common.dry_allowed_length)->default_value(2), "TG DRY allowed length")
                ("tg-dry-base", po::value(&cfg.tg.common.dry_base)->default_value(1.75), "TG DRY base")
                ("tg-repetition-penalty", po::value(&cfg.tg.common.repetition_penalty)->default_value(1.2), "TG repetition penalty")
                ("tg-encoder-repetition-penalty", po::value(&cfg.tg.common.encoder_repetition_penalty)->default_value(1.0), "TG encoder repetition penalty")
                ("tg-no-repeat-ngram-size", po::value(&cfg.tg.common.no_repeat_ngram_size)->default_value(0), "TG no repeat ngram size")
                ("tg-repetition-penalty-range", po::value(&cfg.tg.common.repetition_penalty_range)->default_value(0), "TG repetition penalty range")
                ("tg-penalty-alpha", po::value(&cfg.tg.common.penalty_alpha)->default_value(0.9, "0.9"), "TG penalty alpha")
                ("tg-guidance-scale", po::value(&cfg.tg.common.guidance_scale)->default_value(1.0), "TG guidance scale")
                ("tg-mirostat-mode", po::value(&cfg.tg.common.mirostat_mode)->default_value(0), "TG mirostat mode")
                ("tg-mirostat-tau", po::value(&cfg.tg.common.mirostat_tau)->default_value(5), "TG mirostat tau")
                ("tg-mirostat-eta", po::value(&cfg.tg.common.mirostat_eta)->default_value(0.1, "0.1"), "TG mirostat eta")
                ("tg-prompt-lookup-num-tokens", po::value(&cfg.tg.common.prompt_lookup_num_tokens)->default_value(0), "TG prompt lookup num tokens")
                ("tg-max-tokens-second", po::value(&cfg.tg.common.max_tokens_second)->default_value(0), "TG max tokens second")
                ("tg-do-sample", po::bool_switch(&cfg.tg.common.do_sample)->default_value(true), "TG do sample")
                ("tg-dynamic-temperature", po::bool_switch(&cfg.tg.common.dynamic_temperature)->default_value(false), "TG dynamic temperature")
                ("tg-temperature-last", po::bool_switch(&cfg.tg.common.temperature_last)->default_value(false), "TG temperature last")
                ("tg-auto-max-new-tokens", po::bool_switch(&cfg.tg.common.auto_max_new_tokens)->default_value(false), "TG auto max_new tokens")
                ("tg-ban-eos-token", po::bool_switch(&cfg.tg.common.ban_eos_token)->default_value(false), "TG ban eos token")
                ("tg-add-bos-token", po::bool_switch(&cfg.tg.common.add_bos_token)->default_value(true), "TG add Beginning of Sequence Token (BOS) token")
                ("tg-skip-special-tokens", po::bool_switch(&cfg.tg.common.skip_special_tokens)->default_value(true), "TG skip special tokens (bos_token, eos_token, unk_token, pad_token, etc.)")
                ("tg-static-cache", po::bool_switch(&cfg.tg.common.static_cache)->default_value(false), "TG static cache")
                ("tg-truncation-length", po::value(&cfg.tg.common.truncation_length)->default_value(4096), "TG truncation length")
                ("tg-sampler-priority", po::value(&cfg.tg.common.sampler_priority)->multitoken()->default_value(default_sampler_priority, boost::algorithm::join(default_sampler_priority, " ")), "TG sampler priority")
                ("tg-custom-token-bans", po::value(&cfg.tg.common.custom_token_bans)->default_value(""), "TG custom token bans")
                ("tg-negative-prompt", po::value(&cfg.tg.chat_completions.negative_prompt)->default_value(""), "TG negative prompt")
                ("tg-dry-sequence-breakers", po::value<std::string>()->default_value("(\"\\n\", \":\", \"\\\"\", \"*\")")->notifier(make_unescape_string_notifier(cfg.tg.common.dry_sequence_breakers)), "TG dry sequence breakers")
                ("tg-grammar-string", po::value(&cfg.tg.common.grammar_string)->default_value(""), "TG grammar-string")

                ("kc-max-context-length", po::value(&cfg.kc.common.max_context_length)->default_value(4096), "Maximum number of tokens to send to the model. (minimum: 1)")
                ("kc-max-length", po::value(&cfg.kc.common.max_length)->default_value(512), "Number of tokens to generate. (minimum: 1)")
                ("kc-rep-pen", po::value(&cfg.kc.common.rep_pen)->default_value(1.0), "Base repetition penalty value. (minimum: 1.0)")
                ("kc-rep-pen-range", po::value(&cfg.kc.common.rep_pen_range)->default_value(0), "Repetition penalty range. (minimum: 0)")
                ("kc-sampler-order", po::value<std::vector<int>>(&cfg.kc.common.sampler_order)->multitoken(), "Sampler order to be used. If N is the length of this array, then N must be greater than or equal to 6 and the array must be a permutation of the first N non-negative integers.")
                ("kc-sampler-seed", po::value(&cfg.kc.common.sampler_seed)->default_value(1), "RNG seed to use for sampling. If not specified, the global RNG will be used. (minimum: 1, maximum: 999999)")
                ("kc-stop-sequence", po::value<std::vector<std::string>>()->multitoken()->notifier(make_unescape_strings_notifier(cfg.kc.common.stop_sequence)), "An array of string sequences where the API will stop generating further tokens. The returned text WILL contain the stop sequence if trim_stop is false.")
                ("kc-temperature", po::value(&cfg.kc.common.temperature)->default_value(1.0), "Temperature value.")
                ("kc-tfs", po::value(&cfg.kc.common.tfs)->default_value(1.0), "Tail free sampling value. (minimum: 0.0, maximum: 1.0)")
                ("kc-top-a", po::value(&cfg.kc.common.top_a)->default_value(1.0), "Top-a sampling value. (minimum: 0.0)")
                ("kc-top-k", po::value(&cfg.kc.common.top_k)->default_value(0.0), "Top-k sampling value. (minimum: 0.0)")
                ("kc-top-p", po::value(&cfg.kc.common.top_p)->default_value(1.0), "Top-p sampling value. (minimum: 0.0, maximum: 1.0)")
                ("kc-min-p", po::value(&cfg.kc.common.min_p)->default_value(0.1), "Min-p sampling value. (minimum: 0.0, maximum: 1.0)")
                ("kc-typical", po::value(&cfg.kc.common.typical)->default_value(1.0), "Typical sampling value. (minimum: 0.0, maximum: 1.0)")
                ("kc-use-default-badwordsids", po::bool_switch(&cfg.kc.common.use_default_badwordsids)->default_value(false), "If true, prevents the EOS token from being generated (Ban EOS).")
                ("kc-dynatemp_range", po::value(&cfg.kc.common.dynatemp_range)->default_value(0.0), "If not equal to 0, uses dynamic temperature. Dynamic temperature range will be between Temp+Range and Temp-Range. If equal to 0 , uses static temperature. (default: 0, minimum: -5.0, maximum: 5.0)")
                ("kc-smoothing-factor", po::value(&cfg.kc.common.smoothing_factor)->default_value(0.0), "Modifies temperature behavior. If greater than 0 uses smoothing factor. (default: 0.0, minimum: 0.0)")
                ("kc-dynatemp-exponent", po::value(&cfg.kc.common.dynatemp_exponent)->default_value(1.0), "Exponent used in dynatemp. (default: 0.0)")
                ("kc-mirostat", po::value(&cfg.kc.common.mirostat)->default_value(0), "KoboldCpp ONLY. Sets the mirostat mode, 0=disabled, 1=mirostat_v1, 2=mirostat_v2. (minimum: 0, maximum: 2)")
                ("kc-mirostat-tau", po::value(&cfg.kc.common.mirostat_tau)->default_value(0.0), "KoboldCpp ONLY. Mirostat tau value. (minimum: 0.0)")
                ("kc-mirostat-eta", po::value(&cfg.kc.common.mirostat_eta)->default_value(0.0), "KoboldCpp ONLY. Mirostat eta value. (minimum: 0.0)")
                ("kc-genkey", po::value(&cfg.kc.common.genkey)->default_value(""), "KoboldCpp ONLY. A unique genkey set by the user. When checking a polled-streaming request, use this key to be able to fetch pending text even if multiuser is enabled.")
                ("kc-grammar", po::value(&cfg.kc.common.grammar)->default_value(""), "KoboldCpp ONLY. A string containing the GBNF grammar to use.")
                ("kc-grammar-retain-state", po::bool_switch(&cfg.kc.common.grammar_retain_state)->default_value(false), "KoboldCpp ONLY. If true, retains the previous generation's grammar state, otherwise it is reset on new generation.")
                ("kc-memory", po::value(&cfg.kc.completions.memory)->default_value(""), "KoboldCpp ONLY. If set, forcefully appends this string to the beginning of any submitted prompt text. If resulting context exceeds the limit, forcefully overwrites text from the beginning of the main prompt until it can fit. Useful to guarantee full memory insertion even when you cannot determine exact token count.")
                ("kc-images", po::value(&cfg.kc.chat_completions.images)->multitoken(), "KoboldCpp ONLY. If set, takes an array of base64 encoded strings, each one representing an image to be processed.")
                ("kc-trim-stop", po::bool_switch(&cfg.kc.common.trim_stop)->default_value(true), "KoboldCpp ONLY. If true, also removes detected stop_sequences from the output and truncates all text after them. If false, output will also include stop sequence and potentially a few additional characters.")
                ("kc-render-special", po::bool_switch(&cfg.kc.common.render_special)->default_value(false), "KoboldCpp ONLY. If true, prints special tokens as text for GGUF models")
                ("kc-bypass-eos", po::bool_switch(&cfg.kc.common.bypass_eos)->default_value(false), "KoboldCpp ONLY. If true, allows EOS token to be generated, but does not stop generation. Not recommended unless you know what you are doing.")
                ("kc-banned-tokens", po::value<std::vector<std::string>>()->multitoken()->notifier(make_unescape_strings_notifier(cfg.kc.common.banned_tokens)), "An array of string sequences, each entry represents a word or phrase prevented from being generated, either modifying model vocab or by backtracking and regenerating when they appear.")
                ("kc-dry-multiplier", po::value(&cfg.kc.common.dry_multiplier)->default_value(0.0), "KoboldCpp ONLY. DRY multiplier value, 0 to disable. (minimum: 0)")
                ("kc-dry-base", po::value(&cfg.kc.common.dry_base)->default_value(1.75), "KoboldCpp ONLY. DRY base value. (minimum: 0)")
                ("kc-dry-allowed-length", po::value(&cfg.kc.common.dry_allowed_length)->default_value(2), "KoboldCpp ONLY. DRY allowed length value. (minimum: 0)")
                ("kc-dry-penalty-last-n", po::value(&cfg.kc.common.dry_penalty_last_n)->default_value(0), "KoboldCpp ONLY. DRY last n tokens penalized value. (minimum: 0)")
                ("kc-dry-sequence-breakers", po::value<std::vector<std::string>>()->multitoken()->notifier(make_unescape_strings_notifier(cfg.kc.common.dry_sequence_breakers)), "An array of string sequence breakers for DRY.")
                ("kc-xtc-threshold", po::value(&cfg.kc.common.xtc_threshold)->default_value(0.1), "KoboldCpp ONLY. XTC threshold. (minimum: 0)")
                ("kc-xtc-probability", po::value(&cfg.kc.common.xtc_probability)->default_value(0.0), "KoboldCpp ONLY. XTC probability. Set to above 0 to enable XTC. (minimum: 0)")
                ("kc-nsigma", po::value(&cfg.kc.common.nsigma)->default_value(0.0), "KoboldCpp ONLY. Top N-Sigma value. Set to above 0 to enable nsigma. (minimum: 0)")
                ("kc-logprobs", po::bool_switch(&cfg.kc.common.logprobs)->default_value(false), "If true, return up to 5 top logprobs for generated tokens. Incurs performance overhead.")
                ("kc-replace-instruct-placeholders", po::bool_switch(&cfg.kc.common.use_default_badwordsids)->default_value(false), "If true, replaces instruct placeholders {{[INPUT]}} and {{[OUTPUT]}} with backend selected instruct tags.")

                ("sd-host", po::value(&cfg.sd.host)->default_value("localhost"), "SD host")
                ("sd-port", po::value(&cfg.sd.port)->default_value("7860"), "SD port")
                ("sd-prompt-file", po::value(&cfg.sd.prompt_file)->default_value("prompt"), "SD prompt file")
                ("sd-negative-prompt-file", po::value(&cfg.sd.negative_prompt_file)->default_value("negative_prompt"), "SD negative prompt file")
                ("sd-output-file", po::value(&cfg.sd.output_file)->default_value("{{datetime}}.png"), "SD output PNG file")
                ("sd-prompt", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.sd.common.prompt)), "SD prompt")
                ("sd-negative-prompt", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.sd.common.negative_prompt)), "SD negative prompt")
                ("sd-styles", po::value(&cfg.sd.common.styles), "SD styles")
                ("sd-seed", po::value(&cfg.sd.common.seed)->default_value(-1), "SD seed")
                ("sd-subseed", po::value(&cfg.sd.common.subseed)->default_value(-1), "SD subseed")
                ("sd-subseed-strength", po::value(&cfg.sd.common.subseed_strength)->default_value(0), "SD subseed strength")
                ("sd-seed-resize-from-h", po::value(&cfg.sd.common.seed_resize_from_h)->default_value(-1), "SD seed resize from height")
                ("sd-seed-resize-from-w", po::value(&cfg.sd.common.seed_resize_from_w)->default_value(-1), "SD seed resize from width")
                ("sd-sampler-name", po::value(&cfg.sd.common.sampler_name)->default_value("Euler a"), "SD sampler name")
                ("sd-scheduler", po::value(&cfg.sd.common.scheduler)->default_value("Automatic"), "SD scheduler")
                ("sd-batch_size", po::value(&cfg.sd.common.batch_size)->default_value(1), "SD batch size")
                ("sd-n-iter", po::value(&cfg.sd.common.n_iter)->default_value(1), "SD n iter")
                ("sd-steps", po::value(&cfg.sd.common.steps)->default_value(30), "SD steps")
                ("sd-cfg-scale", po::value(&cfg.sd.common.cfg_scale)->default_value(7), "SD cfg scale")
                ("sd-width", po::value(&cfg.sd.common.width)->default_value(1024), "SD image width")
                ("sd-height", po::value(&cfg.sd.common.height)->default_value(1024), "SD image height")
                ("sd-restore-faces", po::bool_switch(&cfg.sd.common.restore_faces)->default_value(false), "SD restore faces")
                ("sd-tiling", po::bool_switch(&cfg.sd.common.tiling)->default_value(false), "SD tiling")
                ("sd-do-not-save-samples", po::bool_switch(&cfg.sd.common.do_not_save_samples)->default_value(false), "SD do not save samples")
                ("sd-do-not-save-grid", po::bool_switch(&cfg.sd.common.do_not_save_grid)->default_value(false), "SD do not save grid")
                ("sd-eta", po::value(&cfg.sd.common.eta)->default_value(0), "SD eta")
                ("sd-denoising-strength", po::value(&cfg.sd.common.denoising_strength)->default_value(0.75, "0.75"), "SD denoising strength")
                ("sd-s-min-uncond", po::value(&cfg.sd.common.s_min_uncond)->default_value(0), "SD s min uncond")
                ("sd-s-churn", po::value(&cfg.sd.common.s_churn)->default_value(0), "SD s churn")
                ("sd-s-tmax", po::value(&cfg.sd.common.s_tmax)->default_value(0), "SD s tmax")
                ("sd-s-tmin", po::value(&cfg.sd.common.s_tmin)->default_value(0), "SD s tmin")
                ("sd-s-noise", po::value(&cfg.sd.common.s_noise)->default_value(1), "SD s noise")
                ("sd-override-settings", po::value<std::string>()->notifier(make_to_json_notifier(cfg.sd.common.override_settings)), "SD override settings")
                ("sd-override-settings-restore-afterwards", po::bool_switch(&cfg.sd.common.override_settings_restore_afterwards)->default_value(true), "SD override settings restore afterwards")
                ("sd-refiner-checkpoint", po::value(&cfg.sd.common.refiner_checkpoint)->default_value(""), "SD refiner checkpoint")
                ("sd-refiner-switch-at", po::value(&cfg.sd.common.refiner_switch_at)->default_value(0.8, "0.8"), "SD refiner switch at")
                ("sd-disable-extra-networks", po::bool_switch(&cfg.sd.common.disable_extra_networks)->default_value(false), "SD disable extra networks")
                ("sd-comments", po::value(&cfg.sd.common.comments)->default_value(""), "SD comments")
                ("sd-force-task-id", po::value(&cfg.sd.common.force_task_id)->default_value(""), "SD force task id")
                ("sd-sampler-index", po::value(&cfg.sd.common.sampler_index)->default_value(""), "SD sampler index")
                ("sd-script-name", po::value(&cfg.sd.common.script_name)->default_value(""), "SD script name")
                ("sd-script-args", po::value<std::string>()->notifier(make_to_json_notifier(cfg.sd.common.script_args)), "SD script_args")
                ("sd-send-images", po::bool_switch(&cfg.sd.common.send_images)->default_value(true), "SD send images")
                ("sd-save-images", po::bool_switch(&cfg.sd.common.save_images)->default_value(false), "SD save images")
                ("sd-ad-enable", po::bool_switch(&cfg.sd.common.alwayson_scripts.adetailer_parameters.ad_enable)->default_value(false), "SD ADetailer enable")
                ("sd-ad-model", po::value(&cfg.sd.common.alwayson_scripts.adetailer_parameters.args1.ad_model)->default_value("face_yolov8n.pt"), "SD ADetailer model")
                ("sd-ad-prompt", po::value(&cfg.sd.common.alwayson_scripts.adetailer_parameters.args1.ad_prompt)->default_value(""), "SD ADetailer prompt")
                ("sd-ad-negative-prompt", po::value(&cfg.sd.common.alwayson_scripts.adetailer_parameters.args1.ad_negative_prompt)->default_value(""), "SD ADetailer negative prompt")
                ("sd-infotext", po::value(&cfg.sd.common.infotext)->default_value(""), "SD infotext")

                ("sd-mode", po::value<std::string>()->default_value("txt2img")->notifier([&cfg](const std::string& value) { cfg.sd.mode = string_to_sd_mode(value); }), "SD mode (txt2img | img2img)")

                ("sd-txt2img-target", po::value(&cfg.sd.txt2img.target)->default_value("/sdapi/v1/txt2img"), "SD txt2img target")
                ("sd-enable-hr", po::bool_switch(&cfg.sd.txt2img.enable_hr)->default_value(false), "SD enable hr")
                ("sd-firstpass-image", po::value(&cfg.sd.txt2img.firstpass_image)->default_value(""), "SD firstpass image")
                ("sd-firstphase-width", po::value(&cfg.sd.txt2img.firstphase_width)->default_value(0), "SD firstphase width")
                ("sd-firstphase-height", po::value(&cfg.sd.txt2img.firstphase_height)->default_value(0), "SD firstphase height")
                ("sd-hr-scale", po::value(&cfg.sd.txt2img.hr_scale)->default_value(0), "SD hr scale")
                ("sd-hr-upscaler", po::value(&cfg.sd.txt2img.hr_upscaler)->default_value("SwinIR_4x"), "SD hr upscaler")
                ("sd-hr-second-pass-steps", po::value(&cfg.sd.txt2img.hr_second_pass_steps)->default_value(0), "SD hr second pass steps")
                ("sd-hr-resize-x", po::value(&cfg.sd.txt2img.hr_resize_x)->default_value(0), "SD hr resize x")
                ("sd-hr-resize-y", po::value(&cfg.sd.txt2img.hr_resize_y)->default_value(0), "SD hr resize y")
                ("sd-hr-checkpoint-name", po::value(&cfg.sd.txt2img.hr_checkpoint_name)->default_value(""), "SD hr checkpoint name")
                //("sd-hr-prompt", po::value(&cfg.sd_txt2img_params.hr_prompt)->default_value(""), "SD hr prompt")
                //("sd-hr-negative-prompt", po::value(&cfg.sd_txt2img_params.hr_negative_prompt)->default_value(""), "SD hr negative prompt")

                ("sd-img2img-target", po::value(&cfg.sd.img2img.target)->default_value("/sdapi/v1/img2img"), "SD img2img target")
                ("sd-init-images", po::value(&cfg.sd.img2img.init_images)->multitoken(), "SD img2img init_images (Base64 encoded images)")
                ("sd-seed-resize-from-h", po::value(&cfg.sd.img2img.seed_resize_from_h)->default_value(-1), "SD img2img seed_resize_from_h")
                ("sd-seed-resize-from-w", po::value(&cfg.sd.img2img.seed_resize_from_w)->default_value(-1), "SD img2img seed_resize_from_w")
                ("sd-resize-mode", po::value(&cfg.sd.img2img.resize_mode)->default_value(0), "SD img2img resize_mode [0 - 3] (0: Just resize, 1: Crop and resize, 2: Resize and fill, 3: Just resize)")
                ("sd-image-cfg-scale", po::value(&cfg.sd.img2img.image_cfg_scale)->default_value(1.0), "SD img2img image_cfg_scale")
                ("sd-mask", po::value(&cfg.sd.img2img.mask)->default_value(""), "SD img2img mask (Base64 encoded image)")
                ("sd-mask-blur-x", po::value(&cfg.sd.img2img.mask_blur_x)->default_value(4), "SD img2img mask_blur_x")
                ("sd-mask-blur-y", po::value(&cfg.sd.img2img.mask_blur_y)->default_value(4), "SD img2img mask_blur_y")
                ("sd-mask-blur", po::value(&cfg.sd.img2img.mask_blur)->default_value(4), "SD img2img mask_blur")
                ("sd-mask-round", po::bool_switch(&cfg.sd.img2img.mask_round)->default_value(true), "SD img2img mask_round")
                ("sd-inpainting-fill", po::value(&cfg.sd.img2img.inpainting_fill)->default_value(0), "SD img2img inpainting_fill")
                ("sd-inpaint-full-res", po::bool_switch(&cfg.sd.img2img.inpaint_full_res)->default_value(true), "SD img2img inpaint_full_res")
                ("sd-inpaint-full-res-padding", po::value(&cfg.sd.img2img.inpaint_full_res_padding)->default_value(0), "SD img2img inpaint_full_res_padding")
                ("sd-inpainting-mask-invert", po::value(&cfg.sd.img2img.inpainting_mask_invert)->default_value(0), "SD img2img inpainting_mask_invert")
                ("sd-initial-noise-multiplier", po::value(&cfg.sd.img2img.initial_noise_multiplier)->default_value(1.0), "SD img2img initial_noise_multiplier")
                ("sd-latent-mask", po::value(&cfg.sd.img2img.latent_mask)->default_value(""), "SD img2img latent_mask (Base64 encoded image)")

                ("sd-abg-remover-enable", po::bool_switch(&cfg.sd.abg_remover_enable)->default_value(false), "SD ABG Remover enable")

                ("sb-host", po::value(&cfg.sb.host)->default_value("localhost"), "SB host")
                ("sb-port", po::value(&cfg.sb.port)->default_value("5001"), "SB port")
                ("sb-target", po::value(&cfg.sb.target)->default_value("/voice"), "SB voide target")
                ("sb-text-file", po::value(&cfg.sb.text_file)->default_value("text"), "SB text file")
                ("sb-output-file", po::value(&cfg.sb.output_file)->default_value("{{datetime}}.wav"), "SB output WAV")
                ("sb-text", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.sb.text)), "SB text")
                ("sb-model-name", po::value(&cfg.sb.model_name)->default_value(""), "SB model name")
                ("sb-model-id", po::value(&cfg.sb.model_id)->default_value(0), "SB model id")
                ("sb-speaker-name", po::value(&cfg.sb.speaker_name)->default_value(""), "SB speaker name")
                ("sb-speaker-id", po::value(&cfg.sb.speaker_id)->default_value(0), "SB speaker id")
                ("sb-sdp-ratio", po::value(&cfg.sb.sdp_ratio)->default_value(0.2, "0.2"), "SB sdp ratio")
                ("sb-noise", po::value(&cfg.sb.noise)->default_value(0.6, "0.6"), "SB noise")
                ("sb-noisew", po::value(&cfg.sb.noisew)->default_value(0.8, "0.8"), "SB noisew")
                ("sb-length", po::value(&cfg.sb.length)->default_value(1), "SB length")
                ("sb-language", po::value(&cfg.sb.language)->default_value(""), "SB language")
                ("sb-auto-split", po::bool_switch(&cfg.sb.auto_split)->default_value(true), "SB auto split")
                ("sb-split-interval", po::value(&cfg.sb.split_interval)->default_value(0.5, "0.5"), "SB split interval")
                ("sb-assist-text", po::value(&cfg.sb.assist_text)->default_value(""), "SB assist text")
                ("sb-assist-text-weight", po::value(&cfg.sb.assist_text_weight)->default_value(1), "SB assist text weight")
                ("sb-style", po::value(&cfg.sb.style)->default_value(""), "SB style")
                ("sb-style-weight", po::value(&cfg.sb.style_weight)->default_value(1), "SB style weight")
                ("sb-reference-audio-path", po::value(&cfg.sb.reference_audio_path)->default_value(""), "SB reference audio path")

                ("cu-host", po::value(&cfg.cu.host)->default_value("localhost"), "Comfy UI host")
                ("cu-port", po::value(&cfg.cu.port)->default_value("8188"), "Comfy UI port")
                ("cu-prompt-target", po::value(&cfg.cu.prompt_target)->default_value("/prompt"), "Comfy UI prompt target")
                ("cu-upload-image-target", po::value(&cfg.cu.upload_image_target)->default_value("/upload/image"), "Comfy UI upload image target")
                ("cu-prompt", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.cu.prompt)), "Comfy UI prompt")
                ("cu-prompt-file", po::value(&cfg.cu.prompt_file)->default_value("prompt.json"), "Comfy UI prompt file")
                ("cu-output-directory", po::value(&cfg.cu.output_directory)->default_value("output"), "Comfy UI output directory")
                ("cu-upload-images", po::value(&cfg.cu.upload_images)->multitoken(), "Comfy UI upload images (macro_name=local_path)")
                ("cu-preserve-subdirectories", po::bool_switch(&cfg.cu.preserve_subdirectories)->default_value(false), "Comfy UI preserve server side sub-directories")
                ;

            return options_description;
        }

        void parse_command_line(const boost::program_options::options_description& options_description, int argc, char** argv, boost::program_options::variables_map& vm)
        {
            boost::program_options::store(boost::program_options::parse_command_line(argc, argv, options_description), vm);
        }

        void parse_config_stream(const boost::program_options::options_description& options_description, std::istream& config_stream, boost::program_options::variables_map& vm)
        {
            boost::program_options::store(boost::program_options::parse_config_file(config_stream, options_description), vm);
        }

        parse_result parse(int argc, char** argv, config& cfg)
        {
            const boost::program_options::options_description options_description{ make_options_description(cfg) };
            boost::program_options::variables_map vm;

            try
            {
                parse_command_line(options_description, argc, argv, vm);

                const auto config_file_iter{ vm.find("config-file") };
                const auto base_path_iter{ vm.find("base-path") };
                if (config_file_iter != vm.end() && base_path_iter != vm.end())
                {
                    const std::string config_file{ config_file_iter->second.as<std::string>() };
                    const std::string base_path{ base_path_iter->second.as<std::string>() };
                    if (!config_file.empty())
                    {
                        std::ifstream config_stream{ filesystem::string_to_path_by_base_path(config_file, base_path) };
                        if (config_stream.is_open())
                        {
                            parse_config_stream(options_description, config_stream, vm);
                        }
                    }
                }

                boost::program_options::notify(vm);
            }
            catch (boost::program_options::error& error)
            {
                boost::nowide::cerr << error.what() << '\n';
                return parse_result::program_options_error;
            }

            if (vm.find("help") != vm.end())
            {
                boost::nowide::cout << options_description << '\n';
                return parse_result::help;
            }

            return parse_result::success;
        }

        void after_parse(config& cfg)
        {
            std::optional<std::filesystem::path> log_file_path;
            if (!cfg.log_file.empty())
            {
                log_file_path = filesystem::string_to_path_by_config(filesystem::complement_extension(cfg.log_file, ".txt"), cfg);
            }
            log::init_log(cfg.verbose, log_file_path, cfg.log_level);

            parse_user_defined_variables(cfg.user_defined_variables, cfg.ctx);
        }
    } // namespace command_line

} // namespace Hermit

