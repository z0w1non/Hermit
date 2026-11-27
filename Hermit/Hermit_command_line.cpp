#include <filesystem>
#include <fstream>

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

            po::options_description general_options("一般設定");
            general_options.add_options()
                ("help,h", "ヘルプを表示して終了する")
                ("backend", po::value<std::string>()->default_value(""), "生成バックエンド: tg, kc, lmstudio, llama-cpp, vllm, ollama, sd, sb, cu")
                ("path.base", po::value(&cfg.base_path)->default_value("."), "相対パスの基準ディレクトリ (既定値: .)")
                ("logging.level", po::value<std::string>()->default_value("info")->notifier(log_level_notifier), "記録する最低ログレベル: trace, debug, info, warning, error, fatal (既定値: info)")
                ("logging.file", po::value(&cfg.log_file)->default_value("log"), "ログファイルのパス。空文字列ならファイルへ記録しない (既定値: log)")
                ("config", po::value(&cfg.config_file)->default_value("config.ini"), "読み込む INI 設定ファイル (既定値: config.ini)")
                ("logging.console", po::bool_switch(&cfg.verbose)->default_value(false), "ログをコンソールにも出力する (既定値: false)")
                ("security.allow-process-exec", po::bool_switch(&cfg.allow_process_exec)->default_value(false), "exec マクロから子プロセスを起動することを許可する (既定値: false)")
                ("iteration.count", po::value(&cfg.number_iterations)->default_value(1), "処理の反復回数。-1 は無限反復 (既定値: 1)")
                ("variable", po::value<std::vector<std::string>>()->multitoken()->notifier(make_unescape_strings_notifier(cfg.user_defined_variables)), "マクロ変数を key=value 形式で定義する。複数指定可")
                ("iteration.phases", po::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>{ "" }, "")->notifier(make_unescape_strings_notifier(cfg.phases)), "反復ごとに処理する phase の値。複数指定可")
                ("random.seed", po::value(&cfg.seed)->default_value(-1), "乱数 seed。-1 は反復ごとに seed を決定する (既定値: -1)")

                ;

            po::options_description png_options("PNG");
            png_options.add_options()
                ("png.file", po::value(&cfg.png.png_file), "png extract で読み込む PNG ファイル")

                ;

            po::options_description process_options("サーバープロセス");
            process_options.add_options()
                ("process.executable", po::value(&cfg.proc.server_executable_file)->default_value(""), "server executable file")
                ("process.arguments", po::value(&cfg.proc.server_arguments), "server arguments")
                ("process.host", po::value(&cfg.proc.server_host)->default_value("localhost"), "起動したサーバーへの接続を待つホスト (既定値: localhost)")
                ("process.port", po::value(&cfg.proc.server_port)->default_value("5000"), "起動したサーバーへの接続を待つポート (既定値: 5000)")
                ("process.max-retries", po::value(&cfg.proc.server_max_retries)->default_value(60), "サーバーへの接続試行回数 (既定値: 60)")
                ("process.wait-ms", po::value(&cfg.proc.server_wait_ms)->default_value(1000), "接続試行の間隔をミリ秒で指定する (既定値: 1000)")

                ;

            po::options_description network_options("ネットワーク");
            network_options.add_options()
                ("network.connect-timeout", po::value(&cfg.timeout_connect)->default_value(10), "TCP 接続確立のタイムアウト秒数 (既定値: 10)")
                ("network.request-timeout", po::value(&cfg.timeout_request)->default_value(0), "要求送信から応答受信完了までのタイムアウト秒数。0 は無制限 (既定値: 0)")

                ;

            po::options_description llm_options("LLM 共通設定");
            llm_options.add_options()
                ("llm.prompt", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.llm.prompt)), "LLM prompt")
                ("llm.prompt-file", po::value(&cfg.llm.prompt_file)->default_value("prompt"), "LLM prompt file path")
                ("llm.output-file", po::value(&cfg.llm.output_file)->default_value("output"), "LLM output file path")
                ("llm.chat-file", po::value(&cfg.llm.chat_file)->default_value(""), "LLM (input / output) chat file path")
                ("llm.generation-prefix", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.llm.generation_prefix)), "LLM generation prefix")
                ("llm.generation-suffix", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.llm.generation_suffix)), "LLM generation suffix")
                ("llm.paragraphs-file", po::value(&cfg.llm.paragraphs_file)->default_value(""), "LLM paragraphs file")
                ("llm.image-file", po::value(&cfg.llm.image_file)->default_value(""), "LLM image file")
                ("llm.host", po::value(&cfg.llm.host)->default_value("localhost"), "LLM host")
                ("llm.port", po::value(&cfg.llm.port)->default_value("5000"), "LLM port")
                ("llm.api-key", po::value(&cfg.llm.api_key)->default_value(""), "LLM API key")
                ("llm.model", po::value(&cfg.llm.model)->default_value(""), "Model identifier for LLM backends that require one")
                ("llm.completions-target", po::value(&cfg.llm.completions_target), "LLM completions target")
                ("llm.token-count-target", po::value(&cfg.llm.token_count_target), "LLM token count target")
                ("llm.chat-completions-target", po::value(&cfg.llm.chat_completions_target), "LLM chat completions target")
                ("llm.models-target", po::value(&cfg.llm.models_target), "LLM model list API target")
                ("llm.embedding-target", po::value(&cfg.llm.embedding_target), "LLM embeddings API target")
                ("llm.embedding-input", po::value<std::vector<std::string>>()->multitoken()->notifier(make_unescape_strings_notifier(cfg.llm.embedding_inputs)), "Embeddings input texts; multiple values create a batch")
                ("llm.embedding-options", po::value(&cfg.llm.embedding_options)->default_value(""), "Additional embeddings request fields as a JSON object")
                ("llm.tokenize-target", po::value(&cfg.llm.tokenize_target), "llama.cpp tokenize API path")
                ("llm.detokenize-target", po::value(&cfg.llm.detokenize_target), "llama.cpp detokenize API path")
                ("llm.tokenize-options", po::value(&cfg.llm.tokenize_options)->default_value(""), "Tokenization options as a JSON object")
                ("llm.token-ids", po::value<std::vector<int>>()->multitoken()->notifier([&cfg](const auto& ids) { cfg.llm.token_ids = ids; }), "Token IDs for detokenize")
                ("llm.running-models-target", po::value(&cfg.llm.running_models_target), "Ollama running-models API path")
                ("llm.model-info-target", po::value(&cfg.llm.model_info_target), "Ollama model-info API path")
                ("llm.model-info-options", po::value(&cfg.llm.model_info_options)->default_value(""), "Ollama model-info request fields as a JSON object")
                ("llm.model-action-target", po::value(&cfg.llm.model_action_target), "Override Ollama pull/push API path")
                ("llm.model-action-options", po::value(&cfg.llm.model_action_options)->default_value(""), "Ollama model action request body as a JSON object")
                ("llm.min-completion-tokens", po::value(&cfg.llm.min_completion_tokens)->default_value(256), "LLM min completion tokens")
                ("llm.max-completion-iterations", po::value(&cfg.llm.max_completion_iterations)->default_value(5), "LLM max completion iterations")
                ("llm.reasoning-prefix", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.llm.reasoning_prefix)), "LLM reasoning prefix")
                ("llm.reasoning-suffix", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.llm.reasoning_suffix)), "LLM reasoning suffix")
                ("llm.code-block-extract", po::bool_switch(&cfg.llm.code_block_extract)->default_value(false), "LLM code block extract switch")

                ("llm.mode", po::value<std::string>()->default_value("completions")->notifier([&cfg](const std::string& value) { cfg.llm.mode = string_to_llm_mode(value); }), "LLM mode (completions | chat-completions)")

                ;

            po::options_description tg_options("Text Generation Web UI (tg)");
            tg_options.add_options()
                ("tg.model", po::value(&cfg.tg.common.model)->default_value("", "TG model"))
                ("tg.num-best-of", po::value(&cfg.tg.completions.best_of)->default_value(1), "TG best of")
                ("tg.echo", po::bool_switch(&cfg.tg.completions.echo)->default_value(false), "TG echo")
                ("tg.frequency-penalty", po::value(&cfg.tg.common.frequency_penalty)->default_value(0.0), "TG frequency penalty")
                ("tg.logprobs", po::value(&cfg.tg.completions.logprobs)->default_value(0.0), "TG log probabilities")
                ("tg.max-tokens", po::value(&cfg.tg.common.max_tokens)->default_value(512), "TG max tokens")
                ("tg.n", po::value(&cfg.tg.completions.n)->default_value(1), "TG number of responses generated for the same prompt")
                ("tg.presence-penalty", po::value(&cfg.tg.common.presence_penalty)->default_value(0.0), "TG presence penalty")
                ("tg.stop", po::value<std::vector<std::string>>()->multitoken()->default_value(default_stop, boost::algorithm::join(default_stop, " "))->notifier(make_unescape_strings_notifier(cfg.tg.common.stop)), "TG stop sequences")
                ("tg.stream", po::bool_switch(&cfg.tg.common.stream)->default_value(false), "TG stream")
                ("tg.suffix", po::value(&cfg.tg.completions.suffix)->default_value(""), "TG suffix")
                ("tg.temperature", po::value(&cfg.tg.common.temperature)->default_value(1.0), "TG temperature")
                ("tg.top-p", po::value(&cfg.tg.common.top_p)->default_value(1.0), "TG top p")
                ("tg.dynatemp-low", po::value(&cfg.tg.common.dynatemp_low)->default_value(0.75, "0.75"), "TG dynatemp low")
                ("tg.dynatemp-high", po::value(&cfg.tg.common.dynatemp_high)->default_value(1.25, "1.25"), "TG dynatemp high")
                ("tg.dynatemp-exponent", po::value(&cfg.tg.common.dynatemp_exponent)->default_value(1.0), "TG dynatemp exponent")
                ("tg.smoothing-factor", po::value(&cfg.tg.common.smoothing_factor)->default_value(0.0), "TG smoothing factor")
                ("tg.smoothing-curve", po::value(&cfg.tg.common.smoothing_curve)->default_value(1.0), "TG smoothing curve")
                ("tg.min-p", po::value(&cfg.tg.common.min_p)->default_value(0.1, "0.1"), "TG min p")
                ("tg.top-k", po::value(&cfg.tg.common.top_k)->default_value(0), "TG top k")
                ("tg.typical-p", po::value(&cfg.tg.common.typical_p)->default_value(1.0), "TG typical p")
                ("tg.xtc-threshold", po::value(&cfg.tg.common.xtc_threshold)->default_value(0.1, "0.1"), "TG Exclude Top Choices (XTC) threshold")
                ("tg.xtc-probability", po::value(&cfg.tg.common.xtc_probability)->default_value(0.0), "TG Exclude Top Choices (XTC) probability")
                ("tg.epsilon-cutoff", po::value(&cfg.tg.common.epsilon_cutoff)->default_value(0), "TG epsilon cutoff")
                ("tg.eta-cutoff", po::value(&cfg.tg.common.eta_cutoff)->default_value(0), "TG eta cutoff")
                ("tg.tfs", po::value(&cfg.tg.common.tfs)->default_value(1.0), "TG tfs")
                ("tg.top-a", po::value(&cfg.tg.common.top_a)->default_value(0.0), "TG top a")
                ("tg.top-n-sigma", po::value(&cfg.tg.common.top_n_sigma)->default_value(1.0), "TG top n sigma")
                ("tg.dry-multiplier", po::value(&cfg.tg.common.dry_multiplier)->default_value(0.0), "TG DRY multiplier")
                ("tg.dry-allowed-length", po::value(&cfg.tg.common.dry_allowed_length)->default_value(2), "TG DRY allowed length")
                ("tg.dry-base", po::value(&cfg.tg.common.dry_base)->default_value(1.75), "TG DRY base")
                ("tg.repetition-penalty", po::value(&cfg.tg.common.repetition_penalty)->default_value(1.2), "TG repetition penalty")
                ("tg.encoder-repetition-penalty", po::value(&cfg.tg.common.encoder_repetition_penalty)->default_value(1.0), "TG encoder repetition penalty")
                ("tg.no-repeat-ngram-size", po::value(&cfg.tg.common.no_repeat_ngram_size)->default_value(0), "TG no repeat ngram size")
                ("tg.repetition-penalty-range", po::value(&cfg.tg.common.repetition_penalty_range)->default_value(0), "TG repetition penalty range")
                ("tg.penalty-alpha", po::value(&cfg.tg.common.penalty_alpha)->default_value(0.9, "0.9"), "TG penalty alpha")
                ("tg.guidance-scale", po::value(&cfg.tg.common.guidance_scale)->default_value(1.0), "TG guidance scale")
                ("tg.mirostat-mode", po::value(&cfg.tg.common.mirostat_mode)->default_value(0), "TG mirostat mode")
                ("tg.mirostat-tau", po::value(&cfg.tg.common.mirostat_tau)->default_value(5), "TG mirostat tau")
                ("tg.mirostat-eta", po::value(&cfg.tg.common.mirostat_eta)->default_value(0.1, "0.1"), "TG mirostat eta")
                ("tg.prompt-lookup-num-tokens", po::value(&cfg.tg.common.prompt_lookup_num_tokens)->default_value(0), "TG prompt lookup num tokens")
                ("tg.max-tokens-second", po::value(&cfg.tg.common.max_tokens_second)->default_value(0), "TG max tokens second")
                ("tg.do-sample", po::bool_switch(&cfg.tg.common.do_sample)->default_value(true), "TG do sample")
                ("tg.dynamic-temperature", po::bool_switch(&cfg.tg.common.dynamic_temperature)->default_value(false), "TG dynamic temperature")
                ("tg.temperature-last", po::bool_switch(&cfg.tg.common.temperature_last)->default_value(false), "TG temperature last")
                ("tg.auto-max-new-tokens", po::bool_switch(&cfg.tg.common.auto_max_new_tokens)->default_value(false), "TG auto max_new tokens")
                ("tg.ban-eos-token", po::bool_switch(&cfg.tg.common.ban_eos_token)->default_value(false), "TG ban eos token")
                ("tg.add-bos-token", po::bool_switch(&cfg.tg.common.add_bos_token)->default_value(true), "TG add Beginning of Sequence Token (BOS) token")
                ("tg.skip-special-tokens", po::bool_switch(&cfg.tg.common.skip_special_tokens)->default_value(true), "TG skip special tokens (bos_token, eos_token, unk_token, pad_token, etc.)")
                ("tg.static-cache", po::bool_switch(&cfg.tg.common.static_cache)->default_value(false), "TG static cache")
                ("tg.truncation-length", po::value(&cfg.tg.common.truncation_length)->default_value(4096), "TG truncation length")
                ("tg.sampler-priority", po::value(&cfg.tg.common.sampler_priority)->multitoken()->default_value(default_sampler_priority, boost::algorithm::join(default_sampler_priority, " ")), "TG sampler priority")
                ("tg.custom-token-bans", po::value(&cfg.tg.common.custom_token_bans)->default_value(""), "TG custom token bans")
                ("tg.negative-prompt", po::value(&cfg.tg.chat_completions.negative_prompt)->default_value(""), "TG negative prompt")
                ("tg.dry-sequence-breakers", po::value<std::string>()->default_value("(\"\\n\", \":\", \"\\\"\", \"*\")")->notifier(make_unescape_string_notifier(cfg.tg.common.dry_sequence_breakers)), "TG dry sequence breakers")
                ("tg.grammar-string", po::value(&cfg.tg.common.grammar_string)->default_value(""), "TG grammar-string")

                ;

            po::options_description kc_options("KoboldCpp (kc)");
            kc_options.add_options()
                ("kc.max-context-length", po::value(&cfg.kc.common.max_context_length)->default_value(4096), "Maximum number of tokens to send to the model. (minimum: 1)")
                ("kc.max-length", po::value(&cfg.kc.common.max_length)->default_value(512), "Number of tokens to generate. (minimum: 1)")
                ("kc.rep-pen", po::value(&cfg.kc.common.rep_pen)->default_value(1.0), "Base repetition penalty value. (minimum: 1.0)")
                ("kc.rep-pen-range", po::value(&cfg.kc.common.rep_pen_range)->default_value(0), "Repetition penalty range. (minimum: 0)")
                ("kc.sampler-order", po::value<std::vector<int>>(&cfg.kc.common.sampler_order)->multitoken(), "Sampler order to be used. If N is the length of this array, then N must be greater than or equal to 6 and the array must be a permutation of the first N non-negative integers.")
                ("kc.sampler-seed", po::value(&cfg.kc.common.sampler_seed)->default_value(1), "RNG seed to use for sampling. If not specified, the global RNG will be used. (minimum: 1, maximum: 999999)")
                ("kc.stop-sequence", po::value<std::vector<std::string>>()->multitoken()->notifier(make_unescape_strings_notifier(cfg.kc.common.stop_sequence)), "An array of string sequences where the API will stop generating further tokens. The returned text WILL contain the stop sequence if trim_stop is false.")
                ("kc.temperature", po::value(&cfg.kc.common.temperature)->default_value(1.0), "Temperature value.")
                ("kc.tfs", po::value(&cfg.kc.common.tfs)->default_value(1.0), "Tail free sampling value. (minimum: 0.0, maximum: 1.0)")
                ("kc.top-a", po::value(&cfg.kc.common.top_a)->default_value(1.0), "Top-a sampling value. (minimum: 0.0)")
                ("kc.top-k", po::value(&cfg.kc.common.top_k)->default_value(0.0), "Top-k sampling value. (minimum: 0.0)")
                ("kc.top-p", po::value(&cfg.kc.common.top_p)->default_value(1.0), "Top-p sampling value. (minimum: 0.0, maximum: 1.0)")
                ("kc.min-p", po::value(&cfg.kc.common.min_p)->default_value(0.1), "Min-p sampling value. (minimum: 0.0, maximum: 1.0)")
                ("kc.typical", po::value(&cfg.kc.common.typical)->default_value(1.0), "Typical sampling value. (minimum: 0.0, maximum: 1.0)")
                ("kc.use-default-badwordsids", po::bool_switch(&cfg.kc.common.use_default_badwordsids)->default_value(false), "If true, prevents the EOS token from being generated (Ban EOS).")
                ("kc-dynatemp_range", po::value(&cfg.kc.common.dynatemp_range)->default_value(0.0), "If not equal to 0, uses dynamic temperature. Dynamic temperature range will be between Temp+Range and Temp-Range. If equal to 0 , uses static temperature. (default: 0, minimum: -5.0, maximum: 5.0)")
                ("kc.smoothing-factor", po::value(&cfg.kc.common.smoothing_factor)->default_value(0.0), "Modifies temperature behavior. If greater than 0 uses smoothing factor. (default: 0.0, minimum: 0.0)")
                ("kc.dynatemp-exponent", po::value(&cfg.kc.common.dynatemp_exponent)->default_value(1.0), "Exponent used in dynatemp. (default: 0.0)")
                ("kc.mirostat", po::value(&cfg.kc.common.mirostat)->default_value(0), "KoboldCpp ONLY. Sets the mirostat mode, 0=disabled, 1=mirostat_v1, 2=mirostat_v2. (minimum: 0, maximum: 2)")
                ("kc.mirostat-tau", po::value(&cfg.kc.common.mirostat_tau)->default_value(0.0), "KoboldCpp ONLY. Mirostat tau value. (minimum: 0.0)")
                ("kc.mirostat-eta", po::value(&cfg.kc.common.mirostat_eta)->default_value(0.0), "KoboldCpp ONLY. Mirostat eta value. (minimum: 0.0)")
                ("kc.genkey", po::value(&cfg.kc.common.genkey)->default_value(""), "KoboldCpp ONLY. A unique genkey set by the user. When checking a polled-streaming request, use this key to be able to fetch pending text even if multiuser is enabled.")
                ("kc.grammar", po::value(&cfg.kc.common.grammar)->default_value(""), "KoboldCpp ONLY. A string containing the GBNF grammar to use.")
                ("kc.grammar-retain-state", po::bool_switch(&cfg.kc.common.grammar_retain_state)->default_value(false), "KoboldCpp ONLY. If true, retains the previous generation's grammar state, otherwise it is reset on new generation.")
                ("kc.memory", po::value(&cfg.kc.completions.memory)->default_value(""), "KoboldCpp ONLY. If set, forcefully appends this string to the beginning of any submitted prompt text. If resulting context exceeds the limit, forcefully overwrites text from the beginning of the main prompt until it can fit. Useful to guarantee full memory insertion even when you cannot determine exact token count.")
                ("kc.images", po::value(&cfg.kc.chat_completions.images)->multitoken(), "KoboldCpp ONLY. If set, takes an array of base64 encoded strings, each one representing an image to be processed.")
                ("kc.trim-stop", po::bool_switch(&cfg.kc.common.trim_stop)->default_value(true), "KoboldCpp ONLY. If true, also removes detected stop_sequences from the output and truncates all text after them. If false, output will also include stop sequence and potentially a few additional characters.")
                ("kc.render-special", po::bool_switch(&cfg.kc.common.render_special)->default_value(false), "KoboldCpp ONLY. If true, prints special tokens as text for GGUF models")
                ("kc.bypass-eos", po::bool_switch(&cfg.kc.common.bypass_eos)->default_value(false), "KoboldCpp ONLY. If true, allows EOS token to be generated, but does not stop generation. Not recommended unless you know what you are doing.")
                ("kc.banned-tokens", po::value<std::vector<std::string>>()->multitoken()->notifier(make_unescape_strings_notifier(cfg.kc.common.banned_tokens)), "An array of string sequences, each entry represents a word or phrase prevented from being generated, either modifying model vocab or by backtracking and regenerating when they appear.")
                ("kc.dry-multiplier", po::value(&cfg.kc.common.dry_multiplier)->default_value(0.0), "KoboldCpp ONLY. DRY multiplier value, 0 to disable. (minimum: 0)")
                ("kc.dry-base", po::value(&cfg.kc.common.dry_base)->default_value(1.75), "KoboldCpp ONLY. DRY base value. (minimum: 0)")
                ("kc.dry-allowed-length", po::value(&cfg.kc.common.dry_allowed_length)->default_value(2), "KoboldCpp ONLY. DRY allowed length value. (minimum: 0)")
                ("kc.dry-penalty-last-n", po::value(&cfg.kc.common.dry_penalty_last_n)->default_value(0), "KoboldCpp ONLY. DRY last n tokens penalized value. (minimum: 0)")
                ("kc.dry-sequence-breakers", po::value<std::vector<std::string>>()->multitoken()->notifier(make_unescape_strings_notifier(cfg.kc.common.dry_sequence_breakers)), "An array of string sequence breakers for DRY.")
                ("kc.xtc-threshold", po::value(&cfg.kc.common.xtc_threshold)->default_value(0.1), "KoboldCpp ONLY. XTC threshold. (minimum: 0)")
                ("kc.xtc-probability", po::value(&cfg.kc.common.xtc_probability)->default_value(0.0), "KoboldCpp ONLY. XTC probability. Set to above 0 to enable XTC. (minimum: 0)")
                ("kc.nsigma", po::value(&cfg.kc.common.nsigma)->default_value(0.0), "KoboldCpp ONLY. Top N-Sigma value. Set to above 0 to enable nsigma. (minimum: 0)")
                ("kc.logprobs", po::bool_switch(&cfg.kc.common.logprobs)->default_value(false), "If true, return up to 5 top logprobs for generated tokens. Incurs performance overhead.")
                ("kc.replace-instruct-placeholders", po::bool_switch(&cfg.kc.common.use_default_badwordsids)->default_value(false), "If true, replaces instruct placeholders {{[INPUT]}} and {{[OUTPUT]}} with backend selected instruct tags.")

;

            po::options_description sd_options("Stable Diffusion WebUI (sd)");
            sd_options.add_options()
                ("sd.host", po::value(&cfg.sd.host)->default_value("localhost"), "SD host")
                ("sd.port", po::value(&cfg.sd.port)->default_value("7860"), "SD port")
                ("sd.api-options", po::value(&cfg.sd.api_options)->default_value(""), "Stable Diffusion API request options as a JSON object")
                ("sd.prompt-file", po::value(&cfg.sd.prompt_file)->default_value("prompt"), "SD prompt file")
                ("sd.negative-prompt-file", po::value(&cfg.sd.negative_prompt_file)->default_value("negative_prompt"), "SD negative prompt file")
                ("sd.output-file", po::value(&cfg.sd.output_file)->default_value("{{datetime}}.png"), "SD output PNG file")
                ("sd.prompt", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.sd.common.prompt)), "SD prompt")
                ("sd.negative-prompt", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.sd.common.negative_prompt)), "SD negative prompt")
                ("sd.styles", po::value(&cfg.sd.common.styles), "SD styles")
                ("sd.seed", po::value(&cfg.sd.common.seed)->default_value(-1), "SD seed")
                ("sd.subseed", po::value(&cfg.sd.common.subseed)->default_value(-1), "SD subseed")
                ("sd.subseed-strength", po::value(&cfg.sd.common.subseed_strength)->default_value(0), "SD subseed strength")
                ("sd.seed-resize-from-h", po::value(&cfg.sd.common.seed_resize_from_h)->default_value(-1), "SD seed resize from height")
                ("sd.seed-resize-from-w", po::value(&cfg.sd.common.seed_resize_from_w)->default_value(-1), "SD seed resize from width")
                ("sd.sampler-name", po::value(&cfg.sd.common.sampler_name)->default_value("Euler a"), "SD sampler name")
                ("sd.scheduler", po::value(&cfg.sd.common.scheduler)->default_value("Automatic"), "SD scheduler")
                ("sd-batch_size", po::value(&cfg.sd.common.batch_size)->default_value(1), "SD batch size")
                ("sd.n-iter", po::value(&cfg.sd.common.n_iter)->default_value(1), "SD n iter")
                ("sd.steps", po::value(&cfg.sd.common.steps)->default_value(30), "SD steps")
                ("sd.cfg-scale", po::value(&cfg.sd.common.cfg_scale)->default_value(7), "SD cfg scale")
                ("sd.width", po::value(&cfg.sd.common.width)->default_value(1024), "SD image width")
                ("sd.height", po::value(&cfg.sd.common.height)->default_value(1024), "SD image height")
                ("sd.restore-faces", po::bool_switch(&cfg.sd.common.restore_faces)->default_value(false), "SD restore faces")
                ("sd.tiling", po::bool_switch(&cfg.sd.common.tiling)->default_value(false), "SD tiling")
                ("sd.do-not-save-samples", po::bool_switch(&cfg.sd.common.do_not_save_samples)->default_value(false), "SD do not save samples")
                ("sd.do-not-save-grid", po::bool_switch(&cfg.sd.common.do_not_save_grid)->default_value(false), "SD do not save grid")
                ("sd.eta", po::value(&cfg.sd.common.eta)->default_value(0), "SD eta")
                ("sd.denoising-strength", po::value(&cfg.sd.common.denoising_strength)->default_value(0.75, "0.75"), "SD denoising strength")
                ("sd.s-min-uncond", po::value(&cfg.sd.common.s_min_uncond)->default_value(0), "SD s min uncond")
                ("sd.s-churn", po::value(&cfg.sd.common.s_churn)->default_value(0), "SD s churn")
                ("sd.s-tmax", po::value(&cfg.sd.common.s_tmax)->default_value(0), "SD s tmax")
                ("sd.s-tmin", po::value(&cfg.sd.common.s_tmin)->default_value(0), "SD s tmin")
                ("sd.s-noise", po::value(&cfg.sd.common.s_noise)->default_value(1), "SD s noise")
                ("sd.override-settings", po::value<std::string>()->notifier(make_to_json_notifier(cfg.sd.common.override_settings)), "SD override settings")
                ("sd.override-settings-restore-afterwards", po::bool_switch(&cfg.sd.common.override_settings_restore_afterwards)->default_value(true), "SD override settings restore afterwards")
                ("sd.refiner-checkpoint", po::value(&cfg.sd.common.refiner_checkpoint)->default_value(""), "SD refiner checkpoint")
                ("sd.refiner-switch-at", po::value(&cfg.sd.common.refiner_switch_at)->default_value(0.8, "0.8"), "SD refiner switch at")
                ("sd.disable-extra-networks", po::bool_switch(&cfg.sd.common.disable_extra_networks)->default_value(false), "SD disable extra networks")
                ("sd.comments", po::value(&cfg.sd.common.comments)->default_value(""), "SD comments")
                ("sd.force-task-id", po::value(&cfg.sd.common.force_task_id)->default_value(""), "SD force task id")
                ("sd.sampler-index", po::value(&cfg.sd.common.sampler_index)->default_value(""), "SD sampler index")
                ("sd.script-name", po::value(&cfg.sd.common.script_name)->default_value(""), "SD script name")
                ("sd.script-args", po::value<std::string>()->notifier(make_to_json_notifier(cfg.sd.common.script_args)), "SD script_args")
                ("sd.send-images", po::bool_switch(&cfg.sd.common.send_images)->default_value(true), "SD send images")
                ("sd.save-images", po::bool_switch(&cfg.sd.common.save_images)->default_value(false), "SD save images")
                ("sd.ad-enable", po::bool_switch(&cfg.sd.common.alwayson_scripts.adetailer_parameters.ad_enable)->default_value(false), "SD ADetailer enable")
                ("sd.ad-model", po::value(&cfg.sd.common.alwayson_scripts.adetailer_parameters.args1.ad_model)->default_value("face_yolov8n.pt"), "SD ADetailer model")
                ("sd.ad-prompt", po::value(&cfg.sd.common.alwayson_scripts.adetailer_parameters.args1.ad_prompt)->default_value(""), "SD ADetailer prompt")
                ("sd.ad-negative-prompt", po::value(&cfg.sd.common.alwayson_scripts.adetailer_parameters.args1.ad_negative_prompt)->default_value(""), "SD ADetailer negative prompt")
                ("sd.infotext", po::value(&cfg.sd.common.infotext)->default_value(""), "SD infotext")

                ("sd.mode", po::value<std::string>()->default_value("txt2img")->notifier([&cfg](const std::string& value) { cfg.sd.mode = string_to_sd_mode(value); }), "SD mode (txt2img | img2img)")

                ("sd.txt2img-target", po::value(&cfg.sd.txt2img.target)->default_value("/sdapi/v1/txt2img"), "SD txt2img target")
                ("sd.enable-hr", po::bool_switch(&cfg.sd.txt2img.enable_hr)->default_value(false), "SD enable hr")
                ("sd.firstpass-image", po::value(&cfg.sd.txt2img.firstpass_image)->default_value(""), "SD firstpass image")
                ("sd.firstphase-width", po::value(&cfg.sd.txt2img.firstphase_width)->default_value(0), "SD firstphase width")
                ("sd.firstphase-height", po::value(&cfg.sd.txt2img.firstphase_height)->default_value(0), "SD firstphase height")
                ("sd.hr-scale", po::value(&cfg.sd.txt2img.hr_scale)->default_value(0), "SD hr scale")
                ("sd.hr-upscaler", po::value(&cfg.sd.txt2img.hr_upscaler)->default_value("SwinIR_4x"), "SD hr upscaler")
                ("sd.hr-second-pass-steps", po::value(&cfg.sd.txt2img.hr_second_pass_steps)->default_value(0), "SD hr second pass steps")
                ("sd.hr-resize-x", po::value(&cfg.sd.txt2img.hr_resize_x)->default_value(0), "SD hr resize x")
                ("sd.hr-resize-y", po::value(&cfg.sd.txt2img.hr_resize_y)->default_value(0), "SD hr resize y")
                ("sd.hr-checkpoint-name", po::value(&cfg.sd.txt2img.hr_checkpoint_name)->default_value(""), "SD hr checkpoint name")
                //("sd.hr-prompt", po::value(&cfg.sd_txt2img_params.hr_prompt)->default_value(""), "SD hr prompt")
                //("sd.hr-negative-prompt", po::value(&cfg.sd_txt2img_params.hr_negative_prompt)->default_value(""), "SD hr negative prompt")

                ("sd.img2img-target", po::value(&cfg.sd.img2img.target)->default_value("/sdapi/v1/img2img"), "SD img2img target")
                ("sd.init-images", po::value(&cfg.sd.img2img.init_images)->multitoken(), "SD img2img init_images (Base64 encoded images)")
                ("sd.img2img-seed-resize-from-h", po::value(&cfg.sd.img2img.seed_resize_from_h)->default_value(-1), "img2img seed resize source height; -1 uses --sd.seed-resize-from-h")
                ("sd.img2img-seed-resize-from-w", po::value(&cfg.sd.img2img.seed_resize_from_w)->default_value(-1), "img2img seed resize source width; -1 uses --sd.seed-resize-from-w")
                ("sd.resize-mode", po::value(&cfg.sd.img2img.resize_mode)->default_value(0), "SD img2img resize_mode [0 - 3] (0: Just resize, 1: Crop and resize, 2: Resize and fill, 3: Just resize)")
                ("sd.image-cfg-scale", po::value(&cfg.sd.img2img.image_cfg_scale)->default_value(1.0), "SD img2img image_cfg_scale")
                ("sd.mask", po::value(&cfg.sd.img2img.mask)->default_value(""), "SD img2img mask (Base64 encoded image)")
                ("sd.mask-blur-x", po::value(&cfg.sd.img2img.mask_blur_x)->default_value(4), "SD img2img mask_blur_x")
                ("sd.mask-blur-y", po::value(&cfg.sd.img2img.mask_blur_y)->default_value(4), "SD img2img mask_blur_y")
                ("sd.mask-blur", po::value(&cfg.sd.img2img.mask_blur)->default_value(4), "SD img2img mask_blur")
                ("sd.mask-round", po::bool_switch(&cfg.sd.img2img.mask_round)->default_value(true), "SD img2img mask_round")
                ("sd.inpainting-fill", po::value(&cfg.sd.img2img.inpainting_fill)->default_value(0), "SD img2img inpainting_fill")
                ("sd.inpaint-full-res", po::bool_switch(&cfg.sd.img2img.inpaint_full_res)->default_value(true), "SD img2img inpaint_full_res")
                ("sd.inpaint-full-res-padding", po::value(&cfg.sd.img2img.inpaint_full_res_padding)->default_value(0), "SD img2img inpaint_full_res_padding")
                ("sd.inpainting-mask-invert", po::value(&cfg.sd.img2img.inpainting_mask_invert)->default_value(0), "SD img2img inpainting_mask_invert")
                ("sd.initial-noise-multiplier", po::value(&cfg.sd.img2img.initial_noise_multiplier)->default_value(1.0), "SD img2img initial_noise_multiplier")
                ("sd.latent-mask", po::value(&cfg.sd.img2img.latent_mask)->default_value(""), "SD img2img latent_mask (Base64 encoded image)")

                ("sd.abg-remover-enable", po::bool_switch(&cfg.sd.abg_remover_enable)->default_value(false), "SD ABG Remover enable")

                ;

            po::options_description sb_options("Style-Bert-VITS2 (sb)");
            sb_options.add_options()
                ("sb.host", po::value(&cfg.sb.host)->default_value("localhost"), "SB host")
                ("sb.port", po::value(&cfg.sb.port)->default_value("5001"), "SB port")
                ("sb.target", po::value(&cfg.sb.target)->default_value("/voice"), "SB voice API endpoint path")
                ("sb.text-file", po::value(&cfg.sb.text_file)->default_value("text"), "SB text file")
                ("sb.output-file", po::value(&cfg.sb.output_file)->default_value("{{datetime}}.wav"), "SB output WAV")
                ("sb.text", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.sb.text)), "SB text")
                ("sb.model-name", po::value(&cfg.sb.model_name)->default_value(""), "SB model name")
                ("sb.model-id", po::value(&cfg.sb.model_id)->default_value(0), "SB model id")
                ("sb.speaker-name", po::value(&cfg.sb.speaker_name)->default_value(""), "SB speaker name")
                ("sb.speaker-id", po::value(&cfg.sb.speaker_id)->default_value(0), "SB speaker id")
                ("sb.sdp-ratio", po::value(&cfg.sb.sdp_ratio)->default_value(0.2, "0.2"), "SB sdp ratio")
                ("sb.noise", po::value(&cfg.sb.noise)->default_value(0.6, "0.6"), "SB noise")
                ("sb.noisew", po::value(&cfg.sb.noisew)->default_value(0.8, "0.8"), "SB noisew")
                ("sb.length", po::value(&cfg.sb.length)->default_value(1), "SB length")
                ("sb.language", po::value(&cfg.sb.language)->default_value(""), "SB language")
                ("sb.auto-split", po::bool_switch(&cfg.sb.auto_split)->default_value(true), "SB auto split")
                ("sb.split-interval", po::value(&cfg.sb.split_interval)->default_value(0.5, "0.5"), "SB split interval")
                ("sb.assist-text", po::value(&cfg.sb.assist_text)->default_value(""), "SB assist text")
                ("sb.assist-text-weight", po::value(&cfg.sb.assist_text_weight)->default_value(1), "SB assist text weight")
                ("sb.style", po::value(&cfg.sb.style)->default_value(""), "SB style")
                ("sb.style-weight", po::value(&cfg.sb.style_weight)->default_value(1), "SB style weight")
                ("sb.reference-audio-path", po::value(&cfg.sb.reference_audio_path)->default_value(""), "SB reference audio path")

                ;

            po::options_description cu_options("ComfyUI (cu)");
            cu_options.add_options()
                ("cu.host", po::value(&cfg.cu.host)->default_value("localhost"), "Comfy UI host")
                ("cu.port", po::value(&cfg.cu.port)->default_value("8188"), "Comfy UI port")
                ("cu.prompt-target", po::value(&cfg.cu.prompt_target)->default_value("/prompt"), "Comfy UI prompt target")
                ("cu.upload-image-target", po::value(&cfg.cu.upload_image_target)->default_value("/upload/image"), "Comfy UI upload image target")
                ("cu.api-options", po::value(&cfg.cu.api_options)->default_value(""), "ComfyUI API query or request fields as a JSON object")
                ("cu.prompt-id", po::value(&cfg.cu.api_prompt_id)->default_value(""), "ComfyUI prompt/job ID for history and job operations")
                ("cu.node-class", po::value(&cfg.cu.api_node_class)->default_value(""), "ComfyUI node class for object-info")
                ("cu.model-folder", po::value(&cfg.cu.api_model_folder)->default_value(""), "ComfyUI model folder for models-folder")
                ("cu.metadata-folder", po::value(&cfg.cu.api_metadata_folder)->default_value(""), "ComfyUI model folder for view-metadata")
                ("cu.user-id", po::value(&cfg.cu.api_user_id)->default_value(""), "ComfyUI user ID sent in the comfy-user header")
                ("cu.userdata-path", po::value(&cfg.cu.userdata_path)->default_value(""), "Relative ComfyUI userdata path")
                ("cu.userdata-destination", po::value(&cfg.cu.userdata_destination)->default_value(""), "Destination path for comfy userdata-move")
                ("cu.template-path", po::value(&cfg.cu.api_template_path)->default_value(""), "ComfyUI custom-node workflow template path module/relative-file")
                ("cu.upload-file", po::value(&cfg.cu.upload_file)->default_value(""), "Local file for comfy upload-image/upload-mask/userdata-put")
                ("cu.client-id", po::value(&cfg.cu.client_id)->default_value(""), "ComfyUI WebSocket client ID")
                ("cu.event-count", po::value(&cfg.cu.event_count)->default_value(0), "ComfyUI WebSocket event limit (0 means no limit)")
                ("cu.prompt", po::value<std::string>()->default_value("")->notifier(make_unescape_string_notifier(cfg.cu.prompt)), "Comfy UI prompt")
                ("cu.prompt-file", po::value(&cfg.cu.prompt_file)->default_value("prompt.json"), "Comfy UI prompt file")
                ("cu.output-directory", po::value(&cfg.cu.output_directory)->default_value("output"), "Comfy UI output directory")
                ("cu.upload-images", po::value(&cfg.cu.upload_images)->multitoken(), "Comfy UI upload images (macro_name=local_path)")
                ("cu.preserve-subdirectories", po::bool_switch(&cfg.cu.preserve_subdirectories)->default_value(false), "Comfy UI preserve server side sub-directories")
                ;

            po::options_description options_description("Hermit CLI");
            options_description.add(general_options).add(png_options).add(process_options).add(network_options)
                .add(llm_options).add(tg_options).add(kc_options).add(sd_options).add(sb_options).add(cu_options);

            return options_description;
        }

        void parse_command_line(const boost::program_options::options_description& options_description, int argc, char** argv, boost::program_options::variables_map& vm)
        {
            namespace po = boost::program_options;

            po::options_description hidden_options{ "" };
            hidden_options.add_options()
                ("command", po::value<std::string>())
                ("action", po::value<std::string>());

            po::options_description all_options{ "Hermit command line" };
            all_options.add(options_description).add(hidden_options);

            po::positional_options_description positional_options;
            positional_options.add("command", 1).add("action", 1);

            po::store(po::command_line_parser(argc, argv)
                .options(all_options)
                .positional(positional_options)
                .run(), vm);
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

                const auto config_file_iter{ vm.find("config") };
                const auto base_path_iter{ vm.find("path.base") };
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
                boost::nowide::cout
                    << "使い方:\n"
                    << "  Hermit generate --backend <tg|kc|lmstudio|llama-cpp|vllm|ollama|sd|sb|cu> [options]\n"
                    << "  Hermit server <start|stop> [options]\n"
                    << "  Hermit models list --backend <lmstudio|llama-cpp|vllm|ollama> [options]\n"
                    << "  Hermit models show --backend ollama --llm.model <model> [options]\n"
                    << "  Hermit models running --backend ollama [options]\n"
                    << "  Hermit models version --backend ollama [options]\n"
                    << "  Hermit comfy <queue|queue-update|history|history-item|history-update|interrupt|free|events|object-info|models|models-folder|embeddings|extensions|features|upload-image|upload-mask|view|view-metadata|system-stats|default-workflow|users|user-create|userdata-list|userdata-list-v2|userdata-get|userdata-put|userdata-delete|userdata-move|jobs|job|job-cancel|jobs-cancel> [options]\n"
                    << "  Hermit sd-api <progress|interrogate|interrupt|skip|extra-single-image|extra-batch-images|png-info|options|options-set|create-embedding|create-hypernetwork|train-embedding|train-hypernetwork|cmd-flags|samplers|schedulers|upscalers|latent-upscale-modes|sd-models|sd-vae|hypernetworks|face-restorers|realesrgan-models|prompt-styles|embeddings|memory|scripts|script-info|extensions|refresh-embeddings|refresh-checkpoints|refresh-vae|unload-checkpoint|reload-checkpoint> [options]\n"
                    << "  Hermit models <create|copy|delete|pull|push> --backend ollama --llm.model-action-options <json> [options]\n"
                    << "  Hermit embeddings create --backend <lmstudio|llama-cpp|vllm|ollama> --llm.model <model> [options]\n"
                    << "  Hermit tokenize encode --backend llama-cpp [options]\n"
                    << "  Hermit tokenize decode --backend llama-cpp --llm.token-ids <id...> [options]\n"
                    << "  Hermit png extract [options]\n\n"
                    << "設定ファイルの値よりコマンドラインの値が優先されます。\n\n"
                    << options_description << '\n';
                return parse_result::help;
            }

            const auto command_iterator{ vm.find("command") };
            const auto action_iterator{ vm.find("action") };
            if (command_iterator == vm.end())
            {
                boost::nowide::cerr << "コマンドを指定してください。詳しくは --help を参照してください。\n";
                return parse_result::program_options_error;
            }

            const std::string command{ command_iterator->second.as<std::string>() };
            if (command == "generate")
            {
                const auto backend_iterator{ vm.find("backend") };
                if (action_iterator != vm.end() || backend_iterator == vm.end() || backend_iterator->second.as<std::string>().empty())
                {
                    boost::nowide::cerr << "generate には --backend <tg|kc|lmstudio|llama-cpp|vllm|ollama|sd|sb|cu> が必要です。\n";
                    return parse_result::program_options_error;
                }
                const std::string backend{ backend_iterator->second.as<std::string>() };
                if (backend != "tg" && backend != "kc" && backend != "lmstudio" && backend != "llama-cpp"
                    && backend != "vllm" && backend != "ollama" && backend != "sd" && backend != "sb" && backend != "cu")
                {
                    boost::nowide::cerr << "未対応の生成バックエンドです: " << backend << '\n';
                    return parse_result::program_options_error;
                }
                try
                {
                    cfg.llm.backend_name = backend;
                    cfg.command_mode = string_to_command_mode(backend);
                    cfg.tg.openai_compatible = backend == "lmstudio" || backend == "llama-cpp" || backend == "vllm";
                    cfg.kc.ollama_compatible = backend == "ollama";
                    if (!cfg.llm.model.empty())
                    {
                        cfg.tg.common.model = cfg.llm.model;
                        cfg.kc.model = cfg.llm.model;
                    }
                    if (cfg.tg.openai_compatible && vm.at("tg.stop").defaulted())
                    {
                        cfg.tg.common.stop.clear();
                    }

                    if (backend == "tg" || backend == "lmstudio" || backend == "llama-cpp" || backend == "vllm")
                    {
                        cfg.llm.backend = &cfg.tg;
                        if (cfg.llm.completions_target.empty()) cfg.llm.completions_target = "/v1/completions";
                        if (cfg.llm.chat_completions_target.empty()) cfg.llm.chat_completions_target = "/v1/chat/completions";
                        if (cfg.llm.token_count_target.empty()) cfg.llm.token_count_target = "/v1/completions";
                        if (backend == "lmstudio" && vm.at("llm.port").defaulted()) cfg.llm.port = "1234";
                        if (backend == "llama-cpp" && vm.at("llm.port").defaulted()) cfg.llm.port = "8080";
                        if (backend == "vllm" && vm.at("llm.port").defaulted()) cfg.llm.port = "8000";
                    }
                    else if (backend == "kc" || backend == "ollama")
                    {
                        cfg.llm.backend = &cfg.kc;
                        if (backend == "kc")
                        {
                            if (cfg.llm.completions_target.empty()) cfg.llm.completions_target = "/api/v1/generate";
                            if (cfg.llm.token_count_target.empty()) cfg.llm.token_count_target = "/api/extra/tokencount";
                            if (cfg.llm.chat_completions_target.empty()) cfg.llm.chat_completions_target = "/v1/chat/completions";
                        }
                        else
                        {
                            if (cfg.llm.completions_target.empty()) cfg.llm.completions_target = "/api/generate";
                            if (cfg.llm.token_count_target.empty()) cfg.llm.token_count_target = "/api/generate";
                            if (cfg.llm.chat_completions_target.empty()) cfg.llm.chat_completions_target = "/api/chat";
                            if (vm.at("llm.port").defaulted()) cfg.llm.port = "11434";
                        }
                    }
                }
                catch (const std::exception& error)
                {
                    boost::nowide::cerr << error.what() << '\n';
                    return parse_result::program_options_error;
                }
            }
            else if (command == "server" && action_iterator != vm.end())
            {
                const std::string action{ action_iterator->second.as<std::string>() };
                if (action == "start") cfg.command_mode = command_mode::create_process;
                else if (action == "stop") cfg.command_mode = command_mode::terminate_process;
                else
                {
                    boost::nowide::cerr << "server の操作は start または stop です。\n";
                    return parse_result::program_options_error;
                }
            }
            else if (command == "models" && action_iterator != vm.end())
            {
                const std::string action{ action_iterator->second.as<std::string>() };
                const auto backend_iterator{ vm.find("backend") };
                if (backend_iterator == vm.end())
                {
                    boost::nowide::cerr << "models 操作には --backend が必要です。\n";
                    return parse_result::program_options_error;
                }
                cfg.llm.backend_name = backend_iterator->second.as<std::string>();
                if (action == "list" && cfg.llm.backend_name == "lmstudio")
                {
                    cfg.llm.port = vm.at("llm.port").defaulted() ? "1234" : cfg.llm.port;
                    if (cfg.llm.models_target.empty()) cfg.llm.models_target = "/api/v1/models";
                    cfg.command_mode = command_mode::list_models;
                }
                else if (action == "list" && cfg.llm.backend_name == "llama-cpp")
                {
                    cfg.llm.port = vm.at("llm.port").defaulted() ? "8080" : cfg.llm.port;
                    if (cfg.llm.models_target.empty()) cfg.llm.models_target = "/v1/models";
                    cfg.command_mode = command_mode::list_models;
                }
                else if (action == "list" && cfg.llm.backend_name == "vllm")
                {
                    cfg.llm.port = vm.at("llm.port").defaulted() ? "8000" : cfg.llm.port;
                    if (cfg.llm.models_target.empty()) cfg.llm.models_target = "/v1/models";
                    cfg.command_mode = command_mode::list_models;
                }
                else if (cfg.llm.backend_name == "ollama" && action == "list")
                {
                    cfg.llm.port = vm.at("llm.port").defaulted() ? "11434" : cfg.llm.port;
                    if (cfg.llm.models_target.empty()) cfg.llm.models_target = "/api/tags";
                    cfg.command_mode = command_mode::list_models;
                }
                else if (cfg.llm.backend_name == "ollama" && action == "show")
                {
                    if (cfg.llm.model.empty())
                    {
                        boost::nowide::cerr << "models show には --llm.model が必要です。\n";
                        return parse_result::program_options_error;
                    }
                    cfg.llm.port = vm.at("llm.port").defaulted() ? "11434" : cfg.llm.port;
                    if (cfg.llm.model_info_target.empty()) cfg.llm.model_info_target = "/api/show";
                    cfg.command_mode = command_mode::show_model;
                }
                else if (cfg.llm.backend_name == "ollama" && action == "running")
                {
                    cfg.llm.port = vm.at("llm.port").defaulted() ? "11434" : cfg.llm.port;
                    if (cfg.llm.running_models_target.empty()) cfg.llm.running_models_target = "/api/ps";
                    cfg.command_mode = command_mode::list_running_models;
                }
                else if (cfg.llm.backend_name == "ollama" && action == "version")
                {
                    cfg.llm.port = vm.at("llm.port").defaulted() ? "11434" : cfg.llm.port;
                    cfg.command_mode = command_mode::get_ollama_version;
                }
                else if (cfg.llm.backend_name == "ollama" && (action == "create" || action == "copy" || action == "delete"
                    || action == "pull" || action == "push"))
                {
                    if (cfg.llm.model_action_options.empty())
                    {
                        boost::nowide::cerr << "models " << action << " には --llm.model-action-options が必要です。\n";
                        return parse_result::program_options_error;
                    }
                    cfg.llm.port = vm.at("llm.port").defaulted() ? "11434" : cfg.llm.port;
                    cfg.llm.model_action = action;
                    cfg.command_mode = command_mode::manage_ollama_model;
                }
                else
                {
                    boost::nowide::cerr << "models " << action << " はこの backend に対応していません: " << cfg.llm.backend_name << '\n';
                    return parse_result::program_options_error;
                }
            }
            else if (command == "comfy" && action_iterator != vm.end())
            {
                const std::string action{ action_iterator->second.as<std::string>() };
                if (action != "queue" && action != "queue-update" && action != "history" && action != "history-item"
                    && action != "history-update" && action != "interrupt" && action != "free" && action != "events"
                    && action != "object-info" && action != "models" && action != "models-folder"
                    && action != "embeddings" && action != "extensions" && action != "features"
                    && action != "upload-image" && action != "upload-mask" && action != "view" && action != "view-metadata"
                    && action != "system-stats" && action != "default-workflow"
                    && action != "prompt-status"
                    && action != "workflow-templates" && action != "i18n" && action != "web-ui"
                    && action != "template-file"
                    && action != "users" && action != "user-create"
                    && action != "userdata-list" && action != "userdata-list-v2" && action != "userdata-get"
                    && action != "userdata-put" && action != "userdata-delete" && action != "userdata-move"
                    && action != "jobs" && action != "job" && action != "job-cancel" && action != "jobs-cancel")
                {
                    boost::nowide::cerr << "未対応の ComfyUI API 操作です: " << action << '\n';
                    return parse_result::program_options_error;
                }
                if (action == "queue-update" && cfg.cu.api_options.empty())
                {
                    boost::nowide::cerr << "comfy queue-update には --cu.api-options が必要です。\n";
                    return parse_result::program_options_error;
                }
                if (action == "history-update" && cfg.cu.api_options.empty())
                {
                    boost::nowide::cerr << "comfy history-update には --cu.api-options が必要です。\n";
                    return parse_result::program_options_error;
                }
                if (action == "history-item" && cfg.cu.api_prompt_id.empty())
                {
                    boost::nowide::cerr << "comfy history-item には --cu.prompt-id が必要です。\n";
                    return parse_result::program_options_error;
                }
                if ((action == "job" || action == "job-cancel") && cfg.cu.api_prompt_id.empty())
                {
                    boost::nowide::cerr << "comfy " << action << " には --cu.prompt-id が必要です。\n";
                    return parse_result::program_options_error;
                }
                if (action == "userdata-list" && cfg.cu.api_options.empty())
                {
                    boost::nowide::cerr << "comfy userdata-list には --cu.api-options dir が必要です。\n";
                    return parse_result::program_options_error;
                }
                if ((action == "userdata-get" || action == "userdata-put" || action == "userdata-delete" || action == "userdata-move")
                    && cfg.cu.userdata_path.empty())
                {
                    boost::nowide::cerr << "comfy " << action << " には --cu.userdata-path が必要です。\n";
                    return parse_result::program_options_error;
                }
                if (action == "userdata-put" && cfg.cu.upload_file.empty())
                {
                    boost::nowide::cerr << "comfy userdata-put には --cu.upload-file が必要です。\n";
                    return parse_result::program_options_error;
                }
                if (action == "userdata-move" && cfg.cu.userdata_destination.empty())
                {
                    boost::nowide::cerr << "comfy userdata-move には --cu.userdata-destination が必要です。\n";
                    return parse_result::program_options_error;
                }
                if (action == "events" && cfg.cu.event_count < 0)
                {
                    boost::nowide::cerr << "comfy events の --cu.event-count は 0 以上で指定してください。\n";
                    return parse_result::program_options_error;
                }
                if ((action == "upload-image" || action == "upload-mask") && cfg.cu.upload_file.empty())
                {
                    boost::nowide::cerr << "comfy " << action << " には --cu.upload-file が必要です。\n";
                    return parse_result::program_options_error;
                }
                if (action == "view-metadata" && cfg.cu.api_metadata_folder.empty())
                {
                    boost::nowide::cerr << "comfy view-metadata には --cu.metadata-folder が必要です。\n";
                    return parse_result::program_options_error;
                }
                if (action == "template-file" && cfg.cu.api_template_path.empty())
                {
                    boost::nowide::cerr << "comfy template-file には --cu.template-path が必要です。\n";
                    return parse_result::program_options_error;
                }
                cfg.cu.api_action = action;
                cfg.command_mode = command_mode::manage_comfyui_api;
            }
            else if (command == "sd-api" && action_iterator != vm.end())
            {
                const std::string action{ action_iterator->second.as<std::string>() };
                if (action != "progress" && action != "interrogate" && action != "interrupt" && action != "skip"
                    && action != "extra-single-image" && action != "extra-batch-images" && action != "png-info"
                    && action != "options" && action != "options-set" && action != "cmd-flags" && action != "samplers"
                    && action != "create-embedding" && action != "create-hypernetwork" && action != "train-embedding" && action != "train-hypernetwork"
                    && action != "memory" && action != "scripts" && action != "script-info" && action != "extensions"
                    && action != "schedulers" && action != "upscalers" && action != "latent-upscale-modes"
                    && action != "sd-models" && action != "sd-vae" && action != "hypernetworks"
                    && action != "face-restorers" && action != "realesrgan-models" && action != "prompt-styles" && action != "embeddings"
                    && action != "refresh-embeddings" && action != "refresh-checkpoints" && action != "refresh-vae"
                    && action != "unload-checkpoint" && action != "reload-checkpoint")
                {
                    boost::nowide::cerr << "未対応の Stable Diffusion API 操作です: " << action << '\n';
                    return parse_result::program_options_error;
                }
                if ((action == "interrogate" || action == "extra-single-image" || action == "extra-batch-images"
                    || action == "png-info" || action == "options-set" || action == "create-embedding"
                    || action == "create-hypernetwork" || action == "train-embedding" || action == "train-hypernetwork")
                    && cfg.sd.api_options.empty())
                {
                    boost::nowide::cerr << "sd-api " << action << " には --sd.api-options が必要です。\n";
                    return parse_result::program_options_error;
                }
                if ((action == "interrupt" || action == "skip" || action == "cmd-flags" || action == "samplers"
                    || action == "schedulers" || action == "upscalers" || action == "latent-upscale-modes"
                    || action == "sd-models" || action == "sd-vae" || action == "hypernetworks"
                    || action == "face-restorers" || action == "realesrgan-models" || action == "prompt-styles" || action == "embeddings"
                    || action == "memory" || action == "scripts" || action == "script-info" || action == "extensions"
                    || action == "refresh-embeddings" || action == "refresh-checkpoints" || action == "refresh-vae"
                    || action == "unload-checkpoint" || action == "reload-checkpoint")
                    && !cfg.sd.api_options.empty())
                {
                    boost::nowide::cerr << "sd-api " << action << " は --sd.api-options を受け取りません。\n";
                    return parse_result::program_options_error;
                }
                if (action == "options" && !cfg.sd.api_options.empty())
                {
                    boost::nowide::cerr << "sd-api options は --sd.api-options を受け取りません。値の変更には options-set を使ってください。\n";
                    return parse_result::program_options_error;
                }
                cfg.sd.api_action = action;
                cfg.command_mode = command_mode::manage_sd_api;
            }
            else if (command == "embeddings" && action_iterator != vm.end()
                && action_iterator->second.as<std::string>() == "create")
            {
                const auto backend_iterator{ vm.find("backend") };
                if (backend_iterator == vm.end())
                {
                    boost::nowide::cerr << "embeddings create には --backend <lmstudio|llama-cpp|vllm|ollama> が必要です。\n";
                    return parse_result::program_options_error;
                }
                cfg.llm.backend_name = backend_iterator->second.as<std::string>();
                if (cfg.llm.model.empty())
                {
                    boost::nowide::cerr << "embeddings create には --llm.model が必要です。\n";
                    return parse_result::program_options_error;
                }
                if (cfg.llm.backend_name == "lmstudio")
                {
                    cfg.llm.port = vm.at("llm.port").defaulted() ? "1234" : cfg.llm.port;
                    if (cfg.llm.embedding_target.empty()) cfg.llm.embedding_target = "/v1/embeddings";
                }
                else if (cfg.llm.backend_name == "llama-cpp")
                {
                    cfg.llm.port = vm.at("llm.port").defaulted() ? "8080" : cfg.llm.port;
                    if (cfg.llm.embedding_target.empty()) cfg.llm.embedding_target = "/v1/embeddings";
                }
                else if (cfg.llm.backend_name == "vllm")
                {
                    cfg.llm.port = vm.at("llm.port").defaulted() ? "8000" : cfg.llm.port;
                    if (cfg.llm.embedding_target.empty()) cfg.llm.embedding_target = "/v1/embeddings";
                }
                else if (cfg.llm.backend_name == "ollama")
                {
                    cfg.llm.port = vm.at("llm.port").defaulted() ? "11434" : cfg.llm.port;
                    if (cfg.llm.embedding_target.empty()) cfg.llm.embedding_target = "/api/embed";
                }
                else
                {
                    boost::nowide::cerr << "embeddings create はこの backend に対応していません: " << cfg.llm.backend_name << '\n';
                    return parse_result::program_options_error;
                }
                cfg.command_mode = command_mode::create_embeddings;
            }
            else if (command == "tokenize" && action_iterator != vm.end())
            {
                const auto backend_iterator{ vm.find("backend") };
                const std::string action{ action_iterator->second.as<std::string>() };
                if (backend_iterator == vm.end() || backend_iterator->second.as<std::string>() != "llama-cpp")
                {
                    boost::nowide::cerr << "tokenize には --backend llama-cpp が必要です。\n";
                    return parse_result::program_options_error;
                }
                cfg.llm.backend_name = "llama-cpp";
                cfg.llm.port = vm.at("llm.port").defaulted() ? "8080" : cfg.llm.port;
                if (action == "encode")
                {
                    if (cfg.llm.tokenize_target.empty()) cfg.llm.tokenize_target = "/tokenize";
                    cfg.command_mode = command_mode::tokenize;
                }
                else if (action == "decode")
                {
                    if (cfg.llm.token_ids.empty())
                    {
                        boost::nowide::cerr << "tokenize decode には --llm.token-ids が必要です。\n";
                        return parse_result::program_options_error;
                    }
                    if (cfg.llm.detokenize_target.empty()) cfg.llm.detokenize_target = "/detokenize";
                    cfg.command_mode = command_mode::detokenize;
                }
                else
                {
                    boost::nowide::cerr << "tokenize の操作は encode または decode です。\n";
                    return parse_result::program_options_error;
                }
            }
            else if (command == "png" && action_iterator != vm.end()
                && action_iterator->second.as<std::string>() == "extract")
            {
                cfg.command_mode = command_mode::extract_png_parameters;
            }
            else
            {
                boost::nowide::cerr << "未対応のコマンドです: " << command << '\n';
                return parse_result::program_options_error;
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

