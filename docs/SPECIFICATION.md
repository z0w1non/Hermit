# Hermit 技術仕様書

この文書は、現行の `Hermit/Hermit.hpp`、機能別の `Hermit/*.cpp`、`readme.md` から実装済みの振る舞いを整理したものです。コードと README に差がある場合は、実装を現行動作の根拠として記載し、差分を末尾にまとめています。バックエンド固有の全パラメーターは数が多いため、実行時の `--help` と実装中のオプション定義を詳細な一覧として扱います。

## 概要

Hermit は、ローカルまたはネットワーク上の生成 AI サーバーを CLI から利用する Windows 向けアプリケーションです。テキスト、画像、音声の生成、プロンプト内のマクロ評価、反復・段階生成、生成結果のファイル保存を行います。HTTP/HTTPS の JSON 通信、トークン数の問い合わせ、プロセス制御などを実装しています。

実装は名前空間と責務ごとの `.cpp` に分割されています。実装のコンパイルは `Hermit/HermitLib.vcxproj` に集約し、アプリケーションと test は同じ `HermitLib.lib` を参照します。ソリューションでも Hermit と test が HermitLib に依存する順序を明示しています。

`Hermit.vcxproj` はヘッダーを `<ClInclude>` として列挙せず、`$(MSBuildProjectDirectory)` を追加インクルードディレクトリとして使用します。ヘッダーは実装ソースから直接参照され、プロジェクト項目の重複管理を避けます。

Windows API を使用するファイルだけが `Windows.h` をインクルードします。インクルード前に `WIN32_LEAN_AND_MEAN`、`STRICT`、`NOMINMAX` を設定し、インクルード後に `IN`、`OUT`、`NEAR`、`FAR` を解除して、マクロ汚染を翻訳単位内に限定します。Windows API を使用しない分割ファイルには Windows ヘッダーや関連する前処理を置きません。

ビルド要件は Visual Studio 2026 Community の C++ ビルド環境、vcpkg の MSBuild 連携、C++20 です。必要な Installer コンポーネントはルートの `.vsconfig` からインポートします。依存ライブラリはルートの `vcpkg.json` に Boost と Google Test を宣言し、`Hermit/Hermit.vcxproj`、`Hermit/HermitLib.vcxproj`、`test/test.vcxproj` の各プロジェクトで manifest mode を有効にしています。各プロジェクトは `vcpkg.props` と `vcpkg.targets` を標準の MSBuild 拡張としてインポートするため、ビルド時に manifest の依存関係が自動復元され、Boost と Google Test のインクルードパス・リンク設定が反映されます。初めて使う環境では vcpkg の導入と `vcpkg integrate install` による MSBuild 連携設定が必要です。初回の復元にはネットワーク接続が必要です。

Boost または Google Test の C1083 が発生した場合は、依存パッケージの内容を直接確認・編集せず、各 `.vcxproj` の `VcpkgRoot` 解決、`vcpkg.props`（`Microsoft.Cpp.props` 後）、`vcpkg.targets`（`Microsoft.Cpp.targets` 後）の条件付きインポートと、Visual Studio 18 Developer 環境の `where msbuild` 結果を確認します。インポート追加後、Release x64 の `/t:Rebuild` で3プロジェクトのヘッダー解決とリンク成功を確認済みです。

ビルド高速化のため、実装は機能単位の翻訳単位に分割し、両プロジェクトで MSVC のマルチプロセッサーコンパイル（`MultiProcessorCompilation`）を有効にしています。HermitLib は `/MP` と `/Z7`（翻訳単位へデバッグ情報を埋め込み）を使用し、並列 CL 実行時の共有 PDB マネージャー競合を避けつつ、リンク時の PDB 生成を維持します。MSBuild 実行時は `/m` を指定するとプロジェクト間の並列ビルドも利用できます。各プロジェクトの中間生成物は `$(SolutionDir)$(Platform)\$(Configuration)\obj\$(MSBuildProjectName)\`、リンク時 PDB はプロジェクト別の明示パスへ分離します。LTCG を有効にする `test` の Release x64 では `UseLibraryDependencyInputs=false` を明示し、HermitLib の静的ライブラリをリンク入力として使用します。

通常のビルド・テスト検証は差分ビルド（`/t:Build`）を優先します。`/t:Rebuild` は、ツールチェーン・生成先の変更、依存関係の破損、またはクリーン状態の再現が必要な場合に限って実行します。

## コーディング規約

- C++20 と `.editorconfig` の書式を使用し、文字コードは UTF-8、改行は LF、インデントはスペース 4 個とします。
- ローカル変数、定数、メンバー初期化は一様初期化 (`{}`) を優先します。単純な初期化宣言も、型推論や代入が必要な場合を除き、可能な限り `=` ではなく `{}` を使用します。
- `=` は再代入、コピー代入、ラムダのキャプチャ、既定引数、マクロ言語の代入など、宣言時の一様初期化が適さない場面に限定します。
- 狭窄変換をコンパイル時に検出できる一様初期化の利点を維持し、暗黙変換が必要な場合は意図を明示します。
- 新規コードだけでなく、既存コードを変更する範囲でも規約違反を増やさず、単純な宣言を変更する場合は `{}` へ統一します。
- Windows API を使用するソースだけが `Windows.h` を参照し、`WIN32_LEAN_AND_MEAN`、`STRICT`、`NOMINMAX` と汎用マクロの undef によって汚染範囲を局所化します。

Release x64 のフルビルド実測値は約 **5分00秒（300秒、10秒精度）** です。測定条件は Visual Studio 18 Developer 環境、`Hermit.sln`、`/t:Rebuild /m`、`Configuration=Release`、`Platform=x64`、依存関係復元を除くコンパイル・リンクです。ビルド構成やツールセット、ホスト負荷が変わった場合は再測定してこの値を更新します。

結論として、リンク設定の変更や PDB 無効化ではなく、`Hermit.hpp`、Boost.Spirit、Boost.Log、Boost.Beast、Boost.Program_options、nlohmann/json を公開ヘッダーから実装境界へ限定することが最優先です。次回測定も同じ条件で行い、依存整理後の CL 時間と再コンパイル翻訳単位数を比較します。

## ファイル構成

| ファイル | 概要 |
| --- | --- |
| `Hermit/Hermit.hpp` | 公開型、設定、名前空間 API、テンプレート実装、共通マクロを定義するヘッダー。 |
| `Hermit/Hermit_errors.hpp` | Hermit の例外階層と Boost エラー情報タグ。HTTP 実装の Boost.Beast 型は公開せず、Boost.System のエラーコードと整数ステータスを使用。 |
| `Hermit/Hermit_value.hpp` | 透過コンテナ別名、`string_hash`、文字列キー用コンテナ。 |
| `Hermit/Hermit_platform.hpp` | Windows API の最小インクルードと汎用マクロ隔離。 |
| `Hermit/Hermit_context.hpp` | 値型、マクロ値参照、実行コンテキスト、トークン数結果型。 |
| `Hermit/Hermit_llm_types.hpp` | LLM バックエンド共通インターフェースと生成モード。 |
| `Hermit/Hermit_command_line.hpp` | CLI 解析 API の軽量宣言。Boost.Program_options/Describe は実装境界へ限定。 |
| `Hermit/Hermit_string_utils.hpp` | 文字列、Base64、エスケープ、トークン処理 API。 |
| `Hermit/Hermit_filesystem.hpp` | ファイル入出力、パス解決、画像 MIME 変換 API。 |
| `Hermit/Hermit_tcp.hpp` | TCP 接続、HTTP リクエスト、タイムアウト API。 |
| `Hermit/Hermit_types.hpp` | 共通型ヘッダー。例外、値型、コンテキストを束ねる安定した入口。 |
| `Hermit/Hermit_aux_config.hpp` | PNG パラメーターとサーバープロセス設定型。 |
| `Hermit/Hermit_llm_config.hpp` | LLM 接続先、生成、会話、推論設定。 |
| `Hermit/Hermit_media_config.hpp` | Style-Bert-VITS2 と ComfyUI の設定型。 |
| `Hermit/Hermit_sd_config.hpp` | Stable Diffusion のモード・共通・txt2img・img2img 設定型。 |
| `Hermit/Hermit_tg_kc_config.hpp` | text-generation-webui と KoboldCpp の設定型。 |
| `Hermit/Hermit_config.hpp` | 全バックエンド設定を束ねる実行時構成型。 |
| `Hermit/Hermit_builtin.hpp` | 組み込みマクロとマクロコンテキスト API。 |
| `Hermit/Hermit_http.hpp` | URL パラメーター設定テンプレート。 |
| `Hermit/Hermit_llm.hpp` | LLM の生成、会話、段落、コードブロック処理 API。 |
| `Hermit/Hermit_generation.hpp` | Stable Diffusion、Style-Bert-VITS2、ComfyUI の生成 API。 |
| `Hermit/Hermit_log.hpp` | 軽量な severity・初期化 API とログマクロ。sink・書式実装ヘッダーは `.cpp` に限定。 |
| `Hermit/Hermit_runtime.hpp` | phase、反復、seed、プロセス制御、アプリケーション実行 API。 |
| `Hermit/Hermit_core.hpp` | コマンド・モード変換、値文字列化、基盤処理の公開宣言。 |
| `Hermit/Hermit_png.hpp` | PNG tEXt パラメーター抽出 API。 |
| `Hermit/Hermit_app_types.hpp` | 段落項目とアプリケーション実行モード。 |
| `Hermit/Hermit_lru.hpp` | トークン数 LRU キャッシュの型と API。 |
| `Hermit/Hermit_lru_impl.hpp` | LRU のテンプレート実体と JSON ファイル永続化。設定・filesystem・ログに依存する非公開実装境界。 |
| `Hermit/Hermit_templates.hpp` | 乱数、unwrap、値変換、variant 取得の共通テンプレート。 |
| `Hermit/Hermit_templates_core.hpp` | parser など軽量な翻訳単位が利用する unwrap／型特性の最小テンプレート。重いログ・変換依存を持たない。 |
| `Hermit/Hermit_parser_types.hpp` | Boost.Spirit parser の AST 型と Fusion 適応定義。 |
| `Hermit/Hermit_parser.hpp` | parser の文法記号、文法テンプレート、評価 API、非テンプレート代入境界。`config` は前方宣言で参照する。 |
| `Hermit/Hermit_parser_impl.hpp` | AST・Boost.Spirit 文法・評価 visitor の実装定義。parser.cpp 専用。visitor 翻訳単位では `HERMIT_PARSER_VISITORS_ONLY` により文法コンストラクター本体を除外し、文法型は parser.cpp で明示的に実体化する。 |
| `Hermit/Hermit_core.cpp` | 設定値、コンテキスト、LRU キャッシュ、マクロなどの基盤処理。 |
| `Hermit/Hermit_parser.cpp` | プロンプト・フェーズ・マクロの解析処理。 |
| `Hermit/Hermit_parser_visitors.cpp` | AST 評価 visitor と代入 visitor の実装。 |
| `Hermit/Hermit_parser_grammar_symbols.hpp` | Spirit 文法の symbol 型の完全定義。 |
| `Hermit/Hermit_parser_lexical_rules.hpp` | 字句・基本値 rule の component。 |
| `Hermit/Hermit_parser_primary_macro_rules.hpp` | primary／macro rule の component。lexical component を参照する。 |
| `Hermit/Hermit_parser_unary_postfix_rules.hpp` | unary／postfix rule の component。primary／macro component を参照する。 |
| `Hermit/Hermit_parser_arithmetic_comparison_rules.hpp` | 算術・比較・論理 rule の component。unary／postfix component を参照する。 |
| `Hermit/Hermit_parser_assignment_document_rules.hpp` | assignment／conditional／document rule の component。arithmetic/comparison component を参照する。 |
| `Hermit/Hermit_public.hpp` | 軽量な公開 API 宣言。重い実装ヘッダーに依存しない。 |
| `Hermit/Hermit_legacy.hpp` | 既存 `Hermit.hpp` の互換用集約ヘッダー。 |
| `Hermit/Hermit_public_types.hpp` | Beast 型を公開しない LLM／HTTP 型消去境界。request/response、HTTP method、binary response、timeout、authorization、wait/storage callback を定義する。 |
| `Hermit/Hermit_public_http_adapter.hpp` / `.cpp` | 既存 tcp executor と型消去 HTTP 境界の内部 adapter。 |
| `test/test_public_api.cpp` | 軽量公開 API と型消去契約の互換性テスト。 |
| `docs/PARSER_TEMPLATE_BOUNDARY.md` | parser の内部 API、Spirit 文法、評価 visitor のテンプレート境界設計。 |
| `Hermit/Hermit_builtin.cpp` | 組み込みマクロと動的変数の設定。 |
| `Hermit/Hermit_builtin_expand.cpp` | parser による macro 展開境界。 |
| `Hermit/Hermit_builtin_registry.cpp` | builtin マクロ登録と引数数検証。 |
| `Hermit/Hermit_builtin_path.cpp` | `root`、`parent`、`stem`、`extension` のパス操作マクロ。 |
| `Hermit/Hermit_builtin_environment.cpp` | `getenv`、`setenv` と環境アクセサー境界。 |
| `Hermit/Hermit_builtin_process.cpp` | `exec` と Boost.Process/Asio のプロセス実行境界。 |
| `Hermit/Hermit_builtin_input.cpp` | 日時マクロ、対話入力判定、標準入力読み取り。 |
| `Hermit/Hermit_builtin_llm.cpp` | `generated`／`summary` の LLM 呼び出しマクロ。 |
| `Hermit/Hermit_png.cpp` | PNG の tEXt チャンクから生成パラメーターを抽出。 |
| `Hermit/Hermit_tcp.cpp` | TCP 接続確認とサーバー待機。 |
| `Hermit/Hermit_filesystem.cpp` | ファイル読み書き、パス解決、プロンプトファイル処理。 |
| `Hermit/Hermit_llm.cpp` | Completions/Chat Completions、トークン数、会話履歴。 |
| `Hermit/Hermit_sd.cpp` | Stable Diffusion の画像生成要求。 |
| `Hermit/Hermit_sb.cpp` | Style-Bert-VITS2 の音声生成要求。 |
| `Hermit/Hermit_cu.cpp` | ComfyUI の画像アップロードと prompt API。 |
| `Hermit/Hermit_command_line.cpp` | Boost.Program_options による CLI オプション解析。 |
| `Hermit/Hermit_string_utils.cpp` | 文字列変換・分割・URL 関連の共通処理。 |
| `Hermit/Hermit_string_extra.cpp` | 追加の文字列整形・エンコード処理。 |
| `Hermit/Hermit_log.cpp` | ログ出力とログレベル制御。 |
| `Hermit/Hermit_process.cpp` | サーバープロセスの起動・終了。 |
| `Hermit/Hermit_iteration.cpp` | モード実行、seed、phase、反復制御、例外安全な main。 |
| `Hermit/main.cpp` | Windows エントリーポイント。 |
| `test/test.cpp` | Google Test のテストケース。 |
| `test/test_support.hpp` | テスト共通ヘルパー、出力リダイレクタ、Mock 型。 |
| `test/test_builtin.cpp` | builtin の依存性注入テスト。 |
| `test/test_parser.cpp` | parser の再帰展開・代入・停止条件テスト。 |
| `test/test_filesystem.cpp` | filesystem のパス、拡張子、MIME 変換テスト。 |
| `test/test_llm.cpp` | LLM の純粋な Markdown・項目リスト変換テスト。 |
| `test/test_cu.cpp` | ComfyUI multipart 要求生成テスト。 |
| `test/test_sb.cpp` | Style-Bert-VITS2 の URL・transport テスト。 |
| `test/test_sd.cpp` | Stable Diffusion の要求生成・PNG パラメーター・transport テスト。 |
| `test/test_tcp.cpp` | TCP transport 注入テスト。 |
| `test/test_llm_backend.cpp` | TG／Kobold backend の要求・応答、Chat 形式、共通モック契約のテスト。 |
| `Hermit/HermitLib.vcxproj` | Hermit 実装を Static Library としてビルドする共通ライブラリプロジェクト。 |

現在の単体テストは 75 件（22 テストスイート）で、CLI の既定値・共通/バックエンド別オプション反映・`--kc-bypass-eos` と `trim_stop` の独立性・不正モードエラー、例外安全なエントリーポイント、LRU、context、文字列・filesystem の正常系と未知拡張子エラー、組み込みマクロの registry、型変換、JSON エスケープ、コードブロック抽出、パス操作、既定引数の乱数境界、環境 accessor／random source／process runner 注入、全 command mode、LLM/SD の API ターゲット解決、LLM 共通バックエンド境界の Mock 契約、テンプレートパラメータで注入した HTTP transport と LLM/SD/SB request executor の Mock 契約、LLM 各バックエンドの token-count/completions/chat completions JSON 要求・応答解析、構文不正・必須フィールド欠落応答のエラー、Stable Diffusion の txt2img 要求とオプショナルスクリプト、Style-Bert-VITS2 の model/speaker 名・ID フォールバックを含む条件付き URL、ComfyUI の multipart 境界、LLM のコードブロック・推論除去・項目リスト解析、parser の AST 評価・組み込みマクロ評価・代入・再帰停止、非テンプレート parse/evaluate 境界、公開モード変換を検証します。

## アプリケーション固有用語

| 用語 | 意味 | 関連実装・設定 |
| --- | --- | --- |
| mode | 実行するバックエンドまたは補助処理の選択値。 | `--mode`（`tg`, `kc`, `sd`, `sb`, `cu` など） |
| tg / kc | text-generation-webui / KoboldCpp のテキスト生成モード。 | `command_mode` |
| sd / sb / cu | Stable Diffusion / Style-Bert-VITS2 / ComfyUI の連携モード。 | `command_mode` |
| LLM | 大規模言語モデル。テキスト生成の接続先。 | `--llm-*` |
| Completions | prompt 文字列を送る生成 API。 | `--llm-mode completions` |
| Chat Completions | messages 配列と会話履歴を使う生成 API。 | `--llm-mode chat-completions` |
| prompt | 生成要求としてバックエンドへ渡す入力文。 | `--llm-prompt`, `--sd-prompt` など |
| generation prefix / suffix | 生成前後に自動付加する文字列。 | `--llm-generation-prefix`, `--llm-generation-suffix` |
| phase | 反復ごとに順番に実行する入力段階。 | `--phases`, `context` |
| paragraph | 段落ファイルから読み込む phase 用の入力単位。 | `--llm-paragraphs-file` |
| macro | prompt 内で展開される変数記法。 | `--define`, 組み込みマクロ |
| context | マクロ名と値を保持する実行時環境。 | `context`, `cfg.ctx` |
| N | 現在の反復番号（1 始まり）。 | 組み込み変数 `N` |
| token count | prompt のトークン数。上限超過時の分割・継続判定に使う。 | `--llm-token-count-target` |
| LRU cache | 生成済み入力・応答を保存して再利用するキャッシュ。 | `--llm-cache-file` |
| base path | 相対パスを解決する基準ディレクトリ。 | `--base-path` |
| server process mode | サーバー実行ファイルを起動・待機・終了する補助モード。 | `create-process`, `terminate-process` |
| tEXt parameters | PNG の tEXt チャンクに保存された生成パラメーター。 | `extract-png-parameters` |

## ヘッダー依存関係

Release x64 はデバッグ情報と PDB を生成します。プロジェクトごとの `IntDir` と明示的な PDB パスを分離し、並列コンパイル時の中間ファイル・PDB 共有競合を防止します。LNK1101/C1902 が発生した場合は、生成を無効化せず、共有パス・古い生成物・ツールセット不一致を調査します。
テストプロジェクトの Release x64 も同じ設定です。
ビルドは、Visual Studio 18 の `VsDevCmd.bat -arch=x64 -host_arch=x64` で初期化した環境から実行し、MSPDB DLL のバージョン混在を防ぎます。

ヘッダーは、共通型、設定型、機能 API の順に依存する構成です。実装 `.cpp` は原則として担当機能の API ヘッダーを起点に参照します。

依存関係を整理・変更する際は、実装変更と同じ変更単位で本節の依存先表および上記のファイル構成表を更新します。仕様書とコードの依存関係が一致しない場合は、実装を正として差分を修正します。

依存関係は Make 記法の `ターゲット: 依存先` で表記します。

```make
Hermit_types.hpp: Hermit_errors.hpp Hermit_value.hpp Hermit_context.hpp
Hermit_context.hpp: Hermit_errors.hpp Hermit_value.hpp boost/variant.hpp
Hermit_errors.hpp: <filesystem> <exception> <string> boost/system/error_code.hpp boost/current_function.hpp boost/exception/all.hpp boost/stacktrace.hpp
Hermit_lru.hpp: Hermit_context.hpp boost/multi_index_container.hpp boost/multi_index/hashed_index.hpp boost/multi_index/sequenced_index.hpp
Hermit_lru_impl.hpp: Hermit_lru.hpp Hermit_config.hpp Hermit_filesystem.hpp Hermit_log.hpp nlohmann/json.hpp boost/nowide/fstream.hpp
Hermit_templates.hpp: Hermit_context.hpp Hermit_templates_core.hpp Hermit_errors.hpp Hermit_log.hpp boost/lexical_cast.hpp boost/variant/get.hpp
Hermit_llm_types.hpp: nlohmann/json.hpp boost/describe.hpp
Hermit_llm_config.hpp: Hermit_llm_types.hpp
Hermit_tg_kc_config.hpp: Hermit_llm_types.hpp nlohmann/json.hpp boost/describe.hpp
Hermit_sd_config.hpp: Hermit_aux_config.hpp Hermit_tg_kc_config.hpp nlohmann/json.hpp boost/describe.hpp
Hermit_media_config.hpp: <string> <string_view> <vector>
Hermit_aux_config.hpp: <string>
Hermit_app_types.hpp: <string> <string_view> <vector>
Hermit_config.hpp: Hermit_context.hpp Hermit_lru.hpp Hermit_app_types.hpp Hermit_llm_config.hpp Hermit_tg_kc_config.hpp Hermit_sd_config.hpp Hermit_media_config.hpp Hermit_aux_config.hpp boost/log/trivial.hpp
Hermit_command_line.hpp: Hermit_context.hpp <istream> <string> <string_view> <vector>
Hermit_string_utils.hpp: Hermit_context.hpp Hermit_platform.hpp nlohmann/json.hpp boost/archive/iterators/base64_from_binary.hpp boost/archive/iterators/binary_from_base64.hpp boost/archive/iterators/transform_width.hpp
Hermit_filesystem.hpp: Hermit_config.hpp <filesystem> <string> <string_view> <vector>
Hermit_parser_types.hpp: Hermit_context.hpp boost/fusion/include/adapt_struct.hpp boost/variant.hpp
Hermit_core.hpp: Hermit_config.hpp Hermit_app_types.hpp Hermit_context.hpp Hermit_sd_config.hpp Hermit_llm_types.hpp
Hermit_parser.hpp: Hermit_parser_types.hpp Hermit_config.hpp Hermit_templates.hpp boost/spirit/include/qi.hpp
Hermit_parser_impl.hpp: Hermit_parser.hpp Hermit_errors.hpp Hermit_templates_core.hpp
Hermit_parser_visitors.cpp: Hermit_parser_impl.hpp Hermit_log.hpp
Hermit_tcp.hpp: boost/asio/io_context.hpp boost/beast/core.hpp boost/beast/http.hpp
Hermit_http.hpp: boost/url.hpp
Hermit_llm.hpp: Hermit_config.hpp Hermit_llm_types.hpp Hermit_context.hpp nlohmann/json.hpp
Hermit_generation.hpp: Hermit_config.hpp Hermit_media_config.hpp Hermit_sd_config.hpp nlohmann/json.hpp
Hermit_builtin.hpp: Hermit_context.hpp Hermit_config.hpp
Hermit_log.hpp: <filesystem> <optional> <string_view> boost/log/trivial.hpp（`HERMIT_LOGGING_SOURCE_LOCATION` 有効時のみ boost/log/core.hpp 等）
Hermit_runtime.hpp: Hermit_config.hpp
Hermit_platform.hpp: boost/predef.h Windows.h tlhelp32.h
Hermit_png.hpp: <string> <string_view>
Hermit.hpp: Hermit_types.hpp Hermit_llm_types.hpp Hermit_command_line.hpp Hermit_string_utils.hpp Hermit_filesystem.hpp Hermit_aux_config.hpp Hermit_llm_config.hpp Hermit_sd_config.hpp Hermit_tg_kc_config.hpp Hermit_config.hpp Hermit_app_types.hpp Hermit_media_config.hpp Hermit_lru.hpp Hermit_templates.hpp Hermit_lru_impl.hpp Hermit_builtin.hpp Hermit_http.hpp Hermit_llm.hpp Hermit_generation.hpp Hermit_log.hpp Hermit_runtime.hpp Hermit_tcp.hpp Hermit_png.hpp Hermit_core.hpp Hermit_parser_types.hpp Hermit_parser.hpp boost/spirit/include/qi.hpp
```

実装ファイルの直接依存も次の Make ルールで管理します。

```make
Hermit_png.cpp: Hermit_png.hpp Hermit_errors.hpp
Hermit_string_utils.cpp: Hermit_string_utils.hpp Hermit_config.hpp Hermit_platform.hpp boost/algorithm/string.hpp
Hermit_string_extra.cpp: Hermit_llm.hpp Hermit_filesystem.hpp Hermit_tcp.hpp Hermit_media_config.hpp Hermit_value.hpp Hermit_errors.hpp Hermit_log.hpp
Hermit_log.cpp: Hermit_log.hpp Hermit_errors.hpp Hermit_filesystem.hpp Hermit_value.hpp
Hermit_process.cpp: Hermit_runtime.hpp Hermit_app_types.hpp Hermit_value.hpp Hermit_log.hpp Hermit_platform.hpp Hermit_command_line.hpp Hermit_builtin.hpp boost/algorithm/string.hpp boost/asio/io_context.hpp boost/asio/ip/tcp.hpp boost/scope/scope_exit.hpp boost/process/v2/*.hpp
Hermit_tcp.cpp: Hermit_tcp.hpp Hermit_errors.hpp Hermit_log.hpp boost/beast/version.hpp
Hermit_sd.cpp: Hermit_generation.hpp Hermit_filesystem.hpp Hermit_tcp.hpp Hermit_log.hpp Hermit_errors.hpp Hermit_string_utils.hpp
Hermit_sb.cpp: Hermit_generation.hpp Hermit_http.hpp Hermit_tcp.hpp Hermit_log.hpp
Hermit_cu.cpp: Hermit_generation.hpp Hermit_filesystem.hpp Hermit_tcp.hpp Hermit_http.hpp Hermit_log.hpp Hermit_errors.hpp <random> <sstream> <iomanip>
Hermit_command_line.cpp: Hermit_command_line.hpp Hermit_config.hpp Hermit_filesystem.hpp Hermit_string_utils.hpp Hermit_log.hpp boost/describe.hpp boost/algorithm/string.hpp boost/program_options.hpp boost/tokenizer.hpp boost/nowide/iostream.hpp nlohmann/json.hpp
Hermit_filesystem.cpp: Hermit_filesystem.hpp Hermit_config.hpp Hermit_builtin.hpp Hermit_string_utils.hpp Hermit_errors.hpp Hermit_log.hpp Hermit_runtime.hpp boost/nowide/fstream.hpp boost/range/algorithm.hpp
Hermit_iteration.cpp: Hermit_runtime.hpp Hermit_config.hpp Hermit_filesystem.hpp Hermit_builtin.hpp Hermit_llm.hpp Hermit_generation.hpp Hermit_command_line.hpp Hermit_png.hpp Hermit_log.hpp Hermit_lru.hpp boost/exception/diagnostic_information.hpp boost/nowide/args.hpp boost/nowide/iostream.hpp <random>
Hermit_llm.cpp: Hermit_llm.hpp Hermit_config.hpp Hermit_filesystem.hpp Hermit_builtin.hpp Hermit_string_utils.hpp Hermit_tcp.hpp Hermit_log.hpp Hermit_errors.hpp Hermit_runtime.hpp Hermit_templates.hpp boost/algorithm/string.hpp boost/beast/core.hpp boost/beast/http.hpp boost/date_time/posix_time/posix_time.hpp boost/nowide/iostream.hpp nlohmann/json.hpp <chrono> <cstdint> <filesystem> <fstream> <iomanip> <regex> <sstream>
Hermit_builtin.cpp: Hermit_builtin.hpp Hermit_config.hpp Hermit_filesystem.hpp Hermit_string_utils.hpp Hermit_llm.hpp Hermit_parser.hpp Hermit_templates.hpp Hermit_errors.hpp boost/asio/error.hpp boost/asio/io_context.hpp boost/asio/read.hpp boost/asio/readable_pipe.hpp boost/date_time/posix_time/posix_time.hpp boost/lexical_cast.hpp boost/nowide/cstdlib.hpp boost/nowide/iostream.hpp boost/process/v2/environment.hpp boost/process/v2/process.hpp boost/process/v2/stdio.hpp
Hermit_builtin_path.cpp: Hermit_builtin.hpp Hermit_templates.hpp
Hermit_builtin_environment.cpp: Hermit_builtin.hpp Hermit_templates.hpp boost/nowide/cstdlib.hpp
Hermit_builtin_process.cpp: Hermit_builtin.hpp Hermit_errors.hpp Hermit_string_utils.hpp Hermit_templates.hpp boost/asio/error.hpp boost/asio/io_context.hpp boost/asio/read.hpp boost/asio/readable_pipe.hpp boost/process/v2/environment.hpp boost/process/v2/process.hpp boost/process/v2/stdio.hpp
Hermit_builtin_input.cpp: Hermit_builtin.hpp boost/date_time/posix_time/posix_time.hpp boost/nowide/iostream.hpp boost/predef.hpp
Hermit_builtin_llm.cpp: Hermit_builtin.hpp Hermit_llm.hpp Hermit_string_utils.hpp Hermit_templates.hpp
Hermit_core.cpp: Hermit_core.hpp Hermit_templates.hpp Hermit_lru_impl.hpp
main.cpp: Hermit_runtime.hpp
```

### 残存する統合ヘッダー依存

| 実装 | 現状 | 追加分割を保留する理由 |
| --- | --- | --- |
| `Hermit_builtin.cpp` | 機能別ヘッダーと Boost の直接依存 | 組み込みマクロ、parser 評価、LLM 呼び出し、プロセス実行を直接参照し、`Hermit.hpp` 依存を除去済み。 |
| `Hermit_core.cpp` | `Hermit_core.hpp`, `Hermit_templates.hpp`, `Hermit_lru_impl.hpp` | 基盤 API、コンテキスト、値変換、モード変換を実装。LRU のテンプレート実体と JSON/filesystem 永続化は専用実装ヘッダーへ分離済み。 |
| `Hermit_parser.cpp` | `Hermit_parser.hpp` + `Hermit_parser_impl.hpp` | Boost.Spirit 文法テンプレートと AST 評価 visitor を最小の parser 境界から参照する。 |
| `Hermit_llm.cpp` | `Hermit_llm.hpp`, `Hermit_config.hpp`, `Hermit_filesystem.hpp`, `Hermit_builtin.hpp`, `Hermit_string_utils.hpp`, `Hermit_tcp.hpp`, `Hermit_log.hpp`, `Hermit_errors.hpp`, `Hermit_runtime.hpp`, `Hermit_templates.hpp` | LLM、filesystem、TCP、macro、token-count、会話履歴を直接参照する実装。 |

上記は、単一ヘッダー置換によるコンパイル短縮の利点より内部 API 再設計の影響が大きいため、Release x64 ビルドと単体テストが成功する現構成を維持する。

依存方向は、原則として `共通型 → 設定型 → 機能 API → 実装` とし、機能ヘッダーから実装 `.cpp` をインクルードしません。`Hermit.hpp` は互換性・テスト用の集約入口であり、新規の実装では担当機能のヘッダーを優先します。

すべての分割ヘッダーは専用の include guard を持ち、多重インクルードによる再定義を防ぎます。Windows 固有 API は `Hermit_platform.hpp` に限定し、他のヘッダーへ Windows SDK のマクロを漏らしません。

## 起動とモード

`--mode` は必須です。実装が受け付ける値は次の通りです。

| 値 | 動作 |
| --- | --- |
| `tg` | oobabooga/text-generation-webui とのテキスト生成 |
| `kc` | KoboldCpp とのテキスト生成 |
| `sd` | AUTOMATIC1111 Stable Diffusion WebUI との画像生成 |
| `sb` | Style-Bert-VITS2 との音声生成 |
| `cu` | ComfyUI の prompt API への送信、画像アップロード |
| `extract-png-parameters` | PNG ファイルから生成パラメーターを抽出して標準出力へ出力 |
| `create-process` | サーバープロセスを非同期起動し、指定 TCP ポートへの接続を待機 |
| `terminate-process` | 実行ファイル名またはフルパスに一致するプロセスを終了 |

`tg` と `kc` では `--llm-mode completions`（既定）または `chat-completions` を選べます。Completions はプロンプト文字列を API に送り、トークン数を確認しながら複数回に分けて生成を継続できます。Chat Completions は OpenAI 互換の messages 形式を使い、会話 JSON を保存します。画像入力は Chat Completions の画像 URL コンテンツとして送信します。

### 共通オプション

| オプション | 実装上の既定値・動作 |
| --- | --- |
| `--help`, `-h` | ヘルプを表示して終了 |
| `--mode` | 上記のいずれか。既定値なし |
| `--base-path` | `.`。相対パスの基準 |
| `--log-level` | `info`。`trace`, `debug`, `info`, `warning`, `error`, `fatal` |
| `--log-file` | `log`。空文字ならファイルログなし |
| `--config-file`, `-c` | `config.ini` |
| `--verbose`, `-v` | 既定 `false`。true の場合はログをコンソールにも出力。false の場合、生成結果を標準出力へ出力 |
| `--number-iterations`, `-N` | `1`。`-1` は無限反復 |
| `--define`, `-D` | `key=value` 形式のマクロ変数を複数指定 |
| `--phases` | 既定は空文字列 1 件。複数指定可 |
| `--seed` | `-1` は反復ごとに乱数で seed を決定 |
| `--timeout-connect` | 接続確立の上限秒数。既定 `10` |
| `--timeout-request` | 要求送信から応答受信完了までの上限秒数。既定 `0`（タイムアウトなし） |

設定ファイルは INI 形式です。コマンドラインで指定した値が設定ファイルの値より優先されます。起動後に `--define` の値をマクロ変数として登録します。

### 実装済みコマンドラインオプション一覧

以下の表は `Hermit/Hermit_command_line.cpp` の `command_line::make_options_description()` に登録されたオプションから作成しています。短縮形の `—` は短縮形がありません。各表の「デフォルト値」は実装の `default_value` を反映し、明示的な既定値がない場合は `—` とします。

#### 共通・制御

| オプション名 | 省略形 | デフォルト値 | 機能 |
| --- | --- | --- | --- |
| `--help` | `-h` | — | ヘルプを表示する。 |
| `--mode` | — | — | 実行モードを選択する。 |
| `--base-path` | — | `.` | 相対パスの基準ディレクトリを指定する。 |
| `--log-level` | — | `info` | 記録する最低ログレベルを指定する。 |
| `--log-file` | — | `log` | ログファイルを指定する。 |
| `--config-file` | `-c` | `config.ini` | 設定ファイルを指定する。 |
| `--verbose` | `-v` | `false` | ログのコンソール出力を有効にする。 |
| `--number-iterations` | `-N` | `1` | 処理の反復回数を指定する。 |
| `--define` | `-D` | — | マクロ変数を key=value 形式で定義する。 |
| `--phases` | — | 空文字列 1 件 | 実行する phase の値を指定する。 |
| `--seed` | — | `-1` | 乱数 seed を指定する。 |
| `--timeout-connect` | — | `10` | 接続確立のタイムアウト秒数を指定する。 |
| `--timeout-request` | — | `0` | 要求送信から応答完了までのタイムアウト秒数を指定する。 |

#### PNG / サーバープロセス

| オプション名 | 省略形 | 機能 |
| --- | --- | --- |
| `--png-file` | — | パラメーター抽出対象の PNG ファイルを指定する。 |
| `--server-executable-file` | — | 起動・終了するサーバー実行ファイルを指定する。 |
| `--server-arguments` | — | サーバー起動時の引数を指定する。 |
| `--server-host` | — | 起動後の接続待機先ホストを指定する。 |
| `--server-port` | — | 起動後の接続待機先ポートを指定する。 |
| `--server-max-retries` | — | 接続待機の最大試行回数を指定する。 |
| `--server-wait-ms` | — | 接続試行間の待ち時間をミリ秒で指定する。 |

#### LLM 共通

| オプション名 | 省略形 | 機能 |
| --- | --- | --- |
| `--llm-prompt` | — | LLM に渡すプロンプト文字列を指定する。 |
| `--llm-prompt-file` | — | プロンプトファイルを指定する。 |
| `--llm-output-file` | — | 生成結果の出力先を指定する。 |
| `--llm-chat-file` | — | 会話履歴 JSON の入出力先を指定する。 |
| `--llm-generation-prefix` | — | 生成前にプロンプトへ加える接頭文字列を指定する。 |
| `--llm-generation-suffix` | — | 生成結果へ加える接尾文字列を指定する。 |
| `--llm-paragraphs-file` | — | phase を読み込む段落ファイルを指定する。 |
| `--llm-image-file` | — | LLM に送る画像ファイルを指定する。 |
| `--llm-host` | — | LLM サーバーのホストを指定する。 |
| `--llm-port` | — | LLM サーバーのポートを指定する。 |
| `--llm-api-key` | — | Bearer 認証に使う API key を指定する。 |
| `--llm-completions-target` | — | Completions API のパスを指定する。 |
| `--llm-token-count-target` | — | トークン数 API のパスを指定する。 |
| `--llm-chat-completions-target` | — | Chat Completions API のパスを指定する。 |
| `--llm-min-completion-tokens` | — | 継続生成を終了する最小追加トークン数を指定する。 |
| `--llm-max-completion-iterations` | — | Completions の最大継続回数を指定する。 |
| `--llm-reasoning-prefix` | — | 除去対象 reasoning の開始文字列を指定する。 |
| `--llm-reasoning-suffix` | — | 除去対象 reasoning の終了文字列を指定する。 |
| `--llm-code-block-extract` | — | コードブロック抽出を有効にする。 |
| `--llm-mode` | — | Completions または Chat Completions を選択する。 |

#### Text-Generation-webui (`tg`)

| オプション名 | 省略形 | 機能 |
| --- | --- | --- |
| `--tg-model` | — | Text-Generation-webui 向けの設定項目を指定する（tg-model）。 |
| `--tg-num-best-of` | — | Text-Generation-webui 向けの設定項目を指定する（best of）。 |
| `--tg-echo` | — | Text-Generation-webui 向けの設定項目を指定する（echo）。 |
| `--tg-frequency-penalty` | — | Text-Generation-webui 向けの設定項目を指定する（frequency penalty）。 |
| `--tg-logprobs` | — | Text-Generation-webui 向けの設定項目を指定する（presence penalty）。 |
| `--tg-max-tokens` | — | Text-Generation-webui 向けの設定項目を指定する（max tokens）。 |
| `--tg-n` | — | Text-Generation-webui 向けの設定項目を指定する（number of responses generated for the same prompt）。 |
| `--tg-presence-penalty` | — | Text-Generation-webui 向けの設定項目を指定する（presence penalty）。 |
| `--tg-stop` | — | Text-Generation-webui 向けの設定項目を指定する（stop sequences）。 |
| `--tg-stream` | — | Text-Generation-webui 向けの設定項目を指定する（stream）。 |
| `--tg-suffix` | — | Text-Generation-webui 向けの設定項目を指定する（suffix）。 |
| `--tg-temperature` | — | Text-Generation-webui 向けの設定項目を指定する（temperature）。 |
| `--tg-top-p` | — | Text-Generation-webui 向けの設定項目を指定する（top p）。 |
| `--tg-dynatemp-low` | — | Text-Generation-webui 向けの設定項目を指定する（dynatemp low）。 |
| `--tg-dynatemp-high` | — | Text-Generation-webui 向けの設定項目を指定する（dynatemp high）。 |
| `--tg-dynatemp-exponent` | — | Text-Generation-webui 向けの設定項目を指定する（dynatemp exponent）。 |
| `--tg-smoothing-factor` | — | Text-Generation-webui 向けの設定項目を指定する（smoothing factor）。 |
| `--tg-smoothing-curve` | — | Text-Generation-webui 向けの設定項目を指定する（smoothing curve）。 |
| `--tg-min-p` | — | Text-Generation-webui 向けの設定項目を指定する（min p）。 |
| `--tg-top-k` | — | Text-Generation-webui 向けの設定項目を指定する（top k）。 |
| `--tg-typical-p` | — | Text-Generation-webui 向けの設定項目を指定する（typical p）。 |
| `--tg-xtc-threshold` | — | Text-Generation-webui 向けの設定項目を指定する（Exclude Top Choices (XTC) threshold）。 |
| `--tg-xtc-probability` | — | Text-Generation-webui 向けの設定項目を指定する（Exclude Top Choices (XTC) probability）。 |
| `--tg-epsilon-cutoff` | — | Text-Generation-webui 向けの設定項目を指定する（epsilon cutoff）。 |
| `--tg-eta-cutoff` | — | Text-Generation-webui 向けの設定項目を指定する（eta cutoff）。 |
| `--tg-tfs` | — | Text-Generation-webui 向けの設定項目を指定する（tfs）。 |
| `--tg-top-a` | — | Text-Generation-webui 向けの設定項目を指定する（top a）。 |
| `--tg-top-n-sigma` | — | Text-Generation-webui 向けの設定項目を指定する（top n sigma）。 |
| `--tg-dry-multiplier` | — | Text-Generation-webui 向けの設定項目を指定する（DRY multiplier）。 |
| `--tg-dry-allowed-length` | — | Text-Generation-webui 向けの設定項目を指定する（DRY allowed length）。 |
| `--tg-dry-base` | — | Text-Generation-webui 向けの設定項目を指定する（DRY base）。 |
| `--tg-repetition-penalty` | — | Text-Generation-webui 向けの設定項目を指定する（repetition penalty）。 |
| `--tg-encoder-repetition-penalty` | — | Text-Generation-webui 向けの設定項目を指定する（encoder repetition penalty）。 |
| `--tg-no-repeat-ngram-size` | — | Text-Generation-webui 向けの設定項目を指定する（no repeat ngram size）。 |
| `--tg-repetition-penalty-range` | — | Text-Generation-webui 向けの設定項目を指定する（repetition penalty range）。 |
| `--tg-penalty-alpha` | — | Text-Generation-webui 向けの設定項目を指定する（penalty alpha）。 |
| `--tg-guidance-scale` | — | Text-Generation-webui 向けの設定項目を指定する（guidance scale）。 |
| `--tg-mirostat-mode` | — | Text-Generation-webui 向けの設定項目を指定する（mirostat mode）。 |
| `--tg-mirostat-tau` | — | Text-Generation-webui 向けの設定項目を指定する（mirostat tau）。 |
| `--tg-mirostat-eta` | — | Text-Generation-webui 向けの設定項目を指定する（mirostat eta）。 |
| `--tg-prompt-lookup-num-tokens` | — | Text-Generation-webui 向けの設定項目を指定する（prompt lookup num tokens）。 |
| `--tg-max-tokens-second` | — | Text-Generation-webui 向けの設定項目を指定する（max tokens second）。 |
| `--tg-do-sample` | — | Text-Generation-webui 向けの設定項目を指定する（do sample）。 |
| `--tg-dynamic-temperature` | — | Text-Generation-webui 向けの設定項目を指定する（dynamic temperature）。 |
| `--tg-temperature-last` | — | Text-Generation-webui 向けの設定項目を指定する（temperature last）。 |
| `--tg-auto-max-new-tokens` | — | Text-Generation-webui 向けの設定項目を指定する（auto max_new tokens）。 |
| `--tg-ban-eos-token` | — | Text-Generation-webui 向けの設定項目を指定する（ban eos token）。 |
| `--tg-add-bos-token` | — | Text-Generation-webui 向けの設定項目を指定する（add Beginning of Sequence Token (BOS) token）。 |
| `--tg-skip-special-tokens` | — | Text-Generation-webui 向けの設定項目を指定する（skip special tokens (bos_token, eos_token, unk_token, pad_token, etc.)）。 |
| `--tg-static-cache` | — | Text-Generation-webui 向けの設定項目を指定する（static cache）。 |
| `--tg-truncation-length` | — | Text-Generation-webui 向けの設定項目を指定する（truncation length）。 |
| `--tg-sampler-priority` | — | Text-Generation-webui 向けの設定項目を指定する（sampler priority）。 |
| `--tg-custom-token-bans` | — | Text-Generation-webui 向けの設定項目を指定する（custom token bans）。 |
| `--tg-negative-prompt` | — | Text-Generation-webui 向けの設定項目を指定する（negative prompt）。 |
| `--tg-dry-sequence-breakers` | — | Text-Generation-webui 向けの設定項目を指定する（dry sequence breakers）。 |
| `--tg-grammar-string` | — | Text-Generation-webui 向けの設定項目を指定する（grammar-string）。 |

#### KoboldCpp (`kc`)

| オプション名 | 省略形 | 機能 |
| --- | --- | --- |
| `--kc-max-context-length` | — | KoboldCpp 向けの設定項目を指定する（Maximum number of tokens to send to the model. (minimum: 1)）。 |
| `--kc-max-length` | — | KoboldCpp 向けの設定項目を指定する（Number of tokens to generate. (minimum: 1)）。 |
| `--kc-rep-pen` | — | KoboldCpp 向けの設定項目を指定する（Base repetition penalty value. (minimum: 1.0)）。 |
| `--kc-rep-pen-range` | — | KoboldCpp 向けの設定項目を指定する（Repetition penalty range. (minimum: 0)）。 |
| `--kc-sampler-order` | — | KoboldCpp 向けの設定項目を指定する（Sampler order to be used. If N is the length of this array, then N must be greater than or equal to 6 and the array must be a permutation of the first N non-negative integers.）。 |
| `--kc-sampler-seed` | — | KoboldCpp 向けの設定項目を指定する（RNG seed to use for sampling. If not specified, the global RNG will be used. (minimum: 1, maximum: 999999)）。 |
| `--kc-stop-sequence` | — | KoboldCpp 向けの設定項目を指定する（An array of string sequences where the API will stop generating further tokens. The returned text WILL contain the stop sequence if trim_stop is false.）。 |
| `--kc-temperature` | — | KoboldCpp 向けの設定項目を指定する（Temperature value.）。 |
| `--kc-tfs` | — | KoboldCpp 向けの設定項目を指定する（Tail free sampling value. (minimum: 0.0, maximum: 1.0)）。 |
| `--kc-top-a` | — | KoboldCpp 向けの設定項目を指定する（Top-a sampling value. (minimum: 0.0)）。 |
| `--kc-top-k` | — | KoboldCpp 向けの設定項目を指定する（Top-k sampling value. (minimum: 0.0)）。 |
| `--kc-top-p` | — | KoboldCpp 向けの設定項目を指定する（Top-p sampling value. (minimum: 0.0, maximum: 1.0)）。 |
| `--kc-min-p` | — | KoboldCpp 向けの設定項目を指定する（Min-p sampling value. (minimum: 0.0, maximum: 1.0)）。 |
| `--kc-typical` | — | KoboldCpp 向けの設定項目を指定する（Typical sampling value. (minimum: 0.0, maximum: 1.0)）。 |
| `--kc-use-default-badwordsids` | — | KoboldCpp 向けの設定項目を指定する（If true, prevents the EOS token from being generated (Ban EOS).）。 |
| `--kc-dynatemp_range` | — | KoboldCpp 向けの設定項目を指定する（If not equal to 0, uses dynamic temperature. Dynamic temperature range will be between Temp+Range and Temp-Range. If equal to 0 , uses static temperature. (default: 0, minimum: -5.0, maximum: 5.0)）。 |
| `--kc-smoothing-factor` | — | KoboldCpp 向けの設定項目を指定する（Modifies temperature behavior. If greater than 0 uses smoothing factor. (default: 0.0, minimum: 0.0)）。 |
| `--kc-dynatemp-exponent` | — | KoboldCpp 向けの設定項目を指定する（Exponent used in dynatemp. (default: 0.0)）。 |
| `--kc-mirostat` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. Sets the mirostat mode, 0=disabled, 1=mirostat_v1, 2=mirostat_v2. (minimum: 0, maximum: 2)）。 |
| `--kc-mirostat-tau` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. Mirostat tau value. (minimum: 0.0)）。 |
| `--kc-mirostat-eta` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. Mirostat eta value. (minimum: 0.0)）。 |
| `--kc-genkey` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. A unique genkey set by the user. When checking a polled-streaming request, use this key to be able to fetch pending text even if multiuser is enabled.）。 |
| `--kc-grammar` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. A string containing the GBNF grammar to use.）。 |
| `--kc-grammar-retain-state` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. If true, retains the previous generation's grammar state, otherwise it is reset on new generation.）。 |
| `--kc-memory` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. If set, forcefully appends this string to the beginning of any submitted prompt text. If resulting context exceeds the limit, forcefully overwrites text from the beginning of the main prompt until it can fit. Useful to guarantee full memory insertion even when you cannot determine exact token count.）。 |
| `--kc-images` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. If set, takes an array of base64 encoded strings, each one representing an image to be processed.）。 |
| `--kc-trim-stop` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. If true, also removes detected stop_sequences from the output and truncates all text after them. If false, output will also include stop sequence and potentially a few additional characters.）。 |
| `--kc-render-special` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. If true, prints special tokens as text for GGUF models）。 |
| `--kc-bypass-eos` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. If true, allows EOS token to be generated, but does not stop generation. Not recommended unless you know what you are doing.）。 |
| `--kc-banned-tokens` | — | KoboldCpp 向けの設定項目を指定する（An array of string sequences, each entry represents a word or phrase prevented from being generated, either modifying model vocab or by backtracking and regenerating when they appear.）。 |
| `--kc-dry-multiplier` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. DRY multiplier value, 0 to disable. (minimum: 0)）。 |
| `--kc-dry-base` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. DRY base value. (minimum: 0)）。 |
| `--kc-dry-allowed-length` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. DRY allowed length value. (minimum: 0)）。 |
| `--kc-dry-penalty-last-n` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. DRY last n tokens penalized value. (minimum: 0)）。 |
| `--kc-dry-sequence-breakers` | — | KoboldCpp 向けの設定項目を指定する（An array of string sequence breakers for DRY.）。 |
| `--kc-xtc-threshold` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. XTC threshold. (minimum: 0)）。 |
| `--kc-xtc-probability` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. XTC probability. Set to above 0 to enable XTC. (minimum: 0)）。 |
| `--kc-nsigma` | — | KoboldCpp 向けの設定項目を指定する（KoboldCpp ONLY. Top N-Sigma value. Set to above 0 to enable nsigma. (minimum: 0)）。 |
| `--kc-logprobs` | — | KoboldCpp 向けの設定項目を指定する（If true, return up to 5 top logprobs for generated tokens. Incurs performance overhead.）。 |
| `--kc-replace-instruct-placeholders` | — | KoboldCpp 向けの設定項目を指定する（If true, replaces instruct placeholders {{[INPUT]}} and {{[OUTPUT]}} with backend selected instruct tags.）。 |

#### Stable Diffusion (`sd`)

| オプション名 | 省略形 | 機能 |
| --- | --- | --- |
| `--sd-host` | — | Stable Diffusion 向けの設定項目を指定する（host）。 |
| `--sd-port` | — | Stable Diffusion 向けの設定項目を指定する（port）。 |
| `--sd-prompt-file` | — | Stable Diffusion 向けの設定項目を指定する（prompt file）。 |
| `--sd-negative-prompt-file` | — | Stable Diffusion 向けの設定項目を指定する（negative prompt file）。 |
| `--sd-output-file` | — | Stable Diffusion 向けの設定項目を指定する（output PNG file）。 |
| `--sd-prompt` | — | Stable Diffusion 向けの設定項目を指定する（prompt）。 |
| `--sd-negative-prompt` | — | Stable Diffusion 向けの設定項目を指定する（negative prompt）。 |
| `--sd-styles` | — | Stable Diffusion 向けの設定項目を指定する（styles）。 |
| `--sd-seed` | — | Stable Diffusion 向けの設定項目を指定する（seed）。 |
| `--sd-subseed` | — | Stable Diffusion 向けの設定項目を指定する（subseed）。 |
| `--sd-subseed-strength` | — | Stable Diffusion 向けの設定項目を指定する（subseed strength）。 |
| `--sd-seed-resize-from-h` | — | Stable Diffusion 向けの設定項目を指定する（seed resize from height）。 |
| `--sd-seed-resize-from-w` | — | Stable Diffusion 向けの設定項目を指定する（seed resize from width）。 |
| `--sd-sampler-name` | — | Stable Diffusion 向けの設定項目を指定する（sampler name）。 |
| `--sd-scheduler` | — | Stable Diffusion 向けの設定項目を指定する（scheduler）。 |
| `--sd-batch_size` | — | Stable Diffusion 向けの設定項目を指定する（batch size）。 |
| `--sd-n-iter` | — | Stable Diffusion 向けの設定項目を指定する（n iter）。 |
| `--sd-steps` | — | Stable Diffusion 向けの設定項目を指定する（steps）。 |
| `--sd-cfg-scale` | — | Stable Diffusion 向けの設定項目を指定する（cfg scale）。 |
| `--sd-width` | — | Stable Diffusion 向けの設定項目を指定する（image width）。 |
| `--sd-height` | — | Stable Diffusion 向けの設定項目を指定する（image height）。 |
| `--sd-restore-faces` | — | Stable Diffusion 向けの設定項目を指定する（restore faces）。 |
| `--sd-tiling` | — | Stable Diffusion 向けの設定項目を指定する（tiling）。 |
| `--sd-do-not-save-samples` | — | Stable Diffusion 向けの設定項目を指定する（do not save samples）。 |
| `--sd-do-not-save-grid` | — | Stable Diffusion 向けの設定項目を指定する（do not save grid）。 |
| `--sd-eta` | — | Stable Diffusion 向けの設定項目を指定する（eta）。 |
| `--sd-denoising-strength` | — | Stable Diffusion 向けの設定項目を指定する（denoising strength）。 |
| `--sd-s-min-uncond` | — | Stable Diffusion 向けの設定項目を指定する（s min uncond）。 |
| `--sd-s-churn` | — | Stable Diffusion 向けの設定項目を指定する（s churn）。 |
| `--sd-s-tmax` | — | Stable Diffusion 向けの設定項目を指定する（s tmax）。 |
| `--sd-s-tmin` | — | Stable Diffusion 向けの設定項目を指定する（s tmin）。 |
| `--sd-s-noise` | — | Stable Diffusion 向けの設定項目を指定する（s noise）。 |
| `--sd-override-settings` | — | Stable Diffusion 向けの設定項目を指定する（override settings）。 |
| `--sd-override-settings-restore-afterwards` | — | Stable Diffusion 向けの設定項目を指定する（override settings restore afterwards）。 |
| `--sd-refiner-checkpoint` | — | Stable Diffusion 向けの設定項目を指定する（refiner checkpoint）。 |
| `--sd-refiner-switch-at` | — | Stable Diffusion 向けの設定項目を指定する（refiner switch at）。 |
| `--sd-disable-extra-networks` | — | Stable Diffusion 向けの設定項目を指定する（disable extra networks）。 |
| `--sd-comments` | — | Stable Diffusion 向けの設定項目を指定する（comments）。 |
| `--sd-force-task-id` | — | Stable Diffusion 向けの設定項目を指定する（force task id）。 |
| `--sd-sampler-index` | — | Stable Diffusion 向けの設定項目を指定する（sampler index）。 |
| `--sd-script-name` | — | Stable Diffusion 向けの設定項目を指定する（script name）。 |
| `--sd-script-args` | — | Stable Diffusion 向けの設定項目を指定する（script_args）。 |
| `--sd-send-images` | — | Stable Diffusion 向けの設定項目を指定する（send images）。 |
| `--sd-save-images` | — | Stable Diffusion 向けの設定項目を指定する（save images）。 |
| `--sd-ad-enable` | — | Stable Diffusion 向けの設定項目を指定する（ADetailer enable）。 |
| `--sd-ad-model` | — | Stable Diffusion 向けの設定項目を指定する（ADetailer model）。 |
| `--sd-ad-prompt` | — | Stable Diffusion 向けの設定項目を指定する（ADetailer prompt）。 |
| `--sd-ad-negative-prompt` | — | Stable Diffusion 向けの設定項目を指定する（ADetailer negative prompt）。 |
| `--sd-infotext` | — | Stable Diffusion 向けの設定項目を指定する（infotext）。 |
| `--sd-mode` | — | Stable Diffusion 向けの設定項目を指定する（mode (txt2img \\| img2img)）。 |
| `--sd-txt2img-target` | — | Stable Diffusion 向けの設定項目を指定する（txt2img target）。 |
| `--sd-enable-hr` | — | Stable Diffusion 向けの設定項目を指定する（enable hr）。 |
| `--sd-firstpass-image` | — | Stable Diffusion 向けの設定項目を指定する（firstpass image）。 |
| `--sd-firstphase-width` | — | Stable Diffusion 向けの設定項目を指定する（firstphase width）。 |
| `--sd-firstphase-height` | — | Stable Diffusion 向けの設定項目を指定する（firstphase height）。 |
| `--sd-hr-scale` | — | Stable Diffusion 向けの設定項目を指定する（hr scale）。 |
| `--sd-hr-upscaler` | — | Stable Diffusion 向けの設定項目を指定する（hr upscaler）。 |
| `--sd-hr-second-pass-steps` | — | Stable Diffusion 向けの設定項目を指定する（hr second pass steps）。 |
| `--sd-hr-resize-x` | — | Stable Diffusion 向けの設定項目を指定する（hr resize x）。 |
| `--sd-hr-resize-y` | — | Stable Diffusion 向けの設定項目を指定する（hr resize y）。 |
| `--sd-hr-checkpoint-name` | — | Stable Diffusion 向けの設定項目を指定する（hr checkpoint name）。 |
| `--sd-img2img-target` | — | Stable Diffusion 向けの設定項目を指定する（img2img target）。 |
| `--sd-init-images` | — | Stable Diffusion 向けの設定項目を指定する（img2img init_images (Base64 encoded images)）。 |
| `--sd-resize-mode` | — | Stable Diffusion 向けの設定項目を指定する（img2img resize_mode [0 - 3] (0: Just resize, 1: Crop and resize, 2: Resize and fill, 3: Just resize)）。 |
| `--sd-image-cfg-scale` | — | Stable Diffusion 向けの設定項目を指定する（img2img image_cfg_scale）。 |
| `--sd-mask` | — | Stable Diffusion 向けの設定項目を指定する（img2img mask (Base64 encoded image)）。 |
| `--sd-mask-blur-x` | — | Stable Diffusion 向けの設定項目を指定する（img2img mask_blur_x）。 |
| `--sd-mask-blur-y` | — | Stable Diffusion 向けの設定項目を指定する（img2img mask_blur_y）。 |
| `--sd-mask-blur` | — | Stable Diffusion 向けの設定項目を指定する（img2img mask_blur）。 |
| `--sd-mask-round` | — | Stable Diffusion 向けの設定項目を指定する（img2img mask_round）。 |
| `--sd-inpainting-fill` | — | Stable Diffusion 向けの設定項目を指定する（img2img inpainting_fill）。 |
| `--sd-inpaint-full-res` | — | Stable Diffusion 向けの設定項目を指定する（img2img inpaint_full_res）。 |
| `--sd-inpaint-full-res-padding` | — | Stable Diffusion 向けの設定項目を指定する（img2img inpaint_full_res_padding）。 |
| `--sd-inpainting-mask-invert` | — | Stable Diffusion 向けの設定項目を指定する（img2img inpainting_mask_invert）。 |
| `--sd-initial-noise-multiplier` | — | Stable Diffusion 向けの設定項目を指定する（img2img initial_noise_multiplier）。 |
| `--sd-latent-mask` | — | Stable Diffusion 向けの設定項目を指定する（img2img latent_mask (Base64 encoded image)）。 |
| `--sd-abg-remover-enable` | — | Stable Diffusion 向けの設定項目を指定する（ABG Remover enable）。 |

#### Style-Bert-VITS2 (`sb`)

| オプション名 | 省略形 | 機能 |
| --- | --- | --- |
| `--sb-host` | — | Style-Bert-VITS2 向けの設定項目を指定する（host）。 |
| `--sb-port` | — | Style-Bert-VITS2 向けの設定項目を指定する（port）。 |
| `--sb-target` | — | Style-Bert-VITS2 向けの設定項目を指定する（voide target）。 |
| `--sb-text-file` | — | Style-Bert-VITS2 向けの設定項目を指定する（text file）。 |
| `--sb-output-file` | — | Style-Bert-VITS2 向けの設定項目を指定する（output WAV）。 |
| `--sb-text` | — | Style-Bert-VITS2 向けの設定項目を指定する（text）。 |
| `--sb-model-name` | — | Style-Bert-VITS2 向けの設定項目を指定する（model name）。 |
| `--sb-model-id` | — | Style-Bert-VITS2 向けの設定項目を指定する（model id）。 |
| `--sb-speaker-name` | — | Style-Bert-VITS2 向けの設定項目を指定する（speaker name）。 |
| `--sb-speaker-id` | — | Style-Bert-VITS2 向けの設定項目を指定する（speaker id）。 |
| `--sb-sdp-ratio` | — | Style-Bert-VITS2 向けの設定項目を指定する（sdp ratio）。 |
| `--sb-noise` | — | Style-Bert-VITS2 向けの設定項目を指定する（noise）。 |
| `--sb-noisew` | — | Style-Bert-VITS2 向けの設定項目を指定する（noisew）。 |
| `--sb-length` | — | Style-Bert-VITS2 向けの設定項目を指定する（length）。 |
| `--sb-language` | — | Style-Bert-VITS2 向けの設定項目を指定する（language）。 |
| `--sb-auto-split` | — | Style-Bert-VITS2 向けの設定項目を指定する（auto split）。 |
| `--sb-split-interval` | — | Style-Bert-VITS2 向けの設定項目を指定する（split interval）。 |
| `--sb-assist-text` | — | Style-Bert-VITS2 向けの設定項目を指定する（assist text）。 |
| `--sb-assist-text-weight` | — | Style-Bert-VITS2 向けの設定項目を指定する（assist text weight）。 |
| `--sb-style` | — | Style-Bert-VITS2 向けの設定項目を指定する（style）。 |
| `--sb-style-weight` | — | Style-Bert-VITS2 向けの設定項目を指定する（style weight）。 |
| `--sb-reference-audio-path` | — | Style-Bert-VITS2 向けの設定項目を指定する（reference audio path）。 |

#### ComfyUI (`cu`)

| オプション名 | 省略形 | 機能 |
| --- | --- | --- |
| `--cu-host` | — | ComfyUI 向けの設定項目を指定する（host）。 |
| `--cu-port` | — | ComfyUI 向けの設定項目を指定する（port）。 |
| `--cu-prompt-target` | — | ComfyUI 向けの設定項目を指定する（prompt target）。 |
| `--cu-upload-image-target` | — | ComfyUI 向けの設定項目を指定する（upload image target）。 |
| `--cu-prompt` | — | ComfyUI 向けの設定項目を指定する（prompt）。 |
| `--cu-prompt-file` | — | ComfyUI 向けの設定項目を指定する（prompt file）。 |
| `--cu-output-directory` | — | ComfyUI 向けの設定項目を指定する（output directory）。 |
| `--cu-upload-images` | — | ComfyUI 向けの設定項目を指定する（upload images (macro_name=local_path)）。 |
| `--cu-preserve-subdirectories` | — | ComfyUI 向けの設定項目を指定する（preserve server side sub-directories）。 |

コードでは `--sd-seed-resize-from-h` と `--sd-seed-resize-from-w` が共通項目と img2img 項目の両方で登録されています。README の画像生成例にある `--sd-step` は登録名ではなく、実装上のオプション名は `--sd-steps` です。
パス区切りは `/` を使用します。相対パスは `--base-path` を基準にします。テキストファイルを指定する箇所では拡張子を省略でき、その場合 `.txt` が補われます。出力先の親ディレクトリは自動作成されます。バイナリ出力は拡張子補完の対象外です。

### テキスト生成オプション

主な共通 LLM オプションは次の通りです。詳細な sampler、停止文字列、生成長等の設定は `--tg-*` / `--kc-*` オプションです。

| オプション | 実装上の既定値・動作 |
| --- | --- |
| `--llm-prompt` | 空。値を設定すると prompt file の内容を使わない |
| `--llm-prompt-file` | `prompt` |
| `--llm-output-file` | `output`。生成結果を追記 |
| `--llm-chat-file` | 空。空の場合は日時と UUID を含むチャット JSON ファイル名を自動生成 |
| `--llm-generation-prefix`, `--llm-generation-suffix` | 空 |
| `--llm-paragraphs-file` | 空。指定すると段落リストを phase に読み込む |
| `--llm-image-file` | 空。Chat Completions で画像を送信 |
| `--llm-host`, `--llm-port` | `localhost`, `5000` |
| `--llm-api-key` | 空。設定時は Bearer Authorization ヘッダーを付ける |
| `--llm-completions-target` | `tg`: `/v1/completions`; `kc`: `/api/v1/generate` |
| `--llm-token-count-target` | `tg`: `/v1/internal/token-count`; `kc`: `/api/extra/tokencount` |
| `--llm-chat-completions-target` | `/v1/chat/completions` |
| `--llm-mode` | `completions`。`completions` または `chat-completions` |
| `--llm-min-completion-tokens` | `256`。この追加生成量に達するか、最大継続回数に達すると終了 |
| `--llm-max-completion-iterations` | `5` |
| `--llm-reasoning-prefix`, `--llm-reasoning-suffix` | 空。両方の間にある reasoning 部分を結果から除去 |
| `--llm-code-block-extract` | false。true の場合、応答の Markdown コードブロックを個別出力 |

Completions のプロンプトは、テンプレート展開と generation prefix の追加後にトークン数を問い合わせます。残りコンテキストと backend の最大生成数を考慮して生成し、応答を次の要求の末尾に加えて継続します。現行実装では `completions()` が generation suffix を追加し、通常のファイル出力処理も suffix を追加するため、通常の `tg` / `kc` 出力では suffix が二度付加されます。`generated()` など `completions()` を直接呼ぶマクロでは一度です。

Chat Completions はプロンプトを user message として追加し、必要に応じて画像も含めます。応答後に assistant message を追加し、messages JSON をチャットファイルに保存します。既存チャットファイルが読めない場合は空の会話から開始します。プロンプトにはサーバー側のチャットテンプレートを使うための本文を渡します。

### 画像・音声・ComfyUI

- `sd` は `--sd-*` で指定した txt2img または img2img パラメーターを JSON API に送り、画像をバイナリ保存します。主な設定は host `localhost`、port `7860`、mode `txt2img`、output `{{datetime}}.png` です。
- `sb` は `--sb-*` で指定したテキスト、モデル、話者、言語等を Style-Bert-VITS2 API に送り、音声をバイナリ保存します。既定の port は `5001`、target は `/voice`、出力名は `{{datetime}}.wav` です。
- `cu` は `--cu-*` で指定した ComfyUI prompt JSON を送信します。既定 host は `localhost`、port は `8188`、target は `/prompt`。画像のアップロード指定、サーバー側サブディレクトリ保持も設定できます。
- 各 mode の prompt/text の直接指定が空の場合、対応するファイルから読み込みます。`sd` の正負 prompt と `sb` の text は送信前にマクロ展開されます。

## マクロ言語

マクロは `{{...}}` で囲みます。文字列中に変数 `{{name}}`、関数 `{{function(arg, ...)}}` を記述できます。各ファイル内容とファイルパス等、コード上で展開を呼び出す箇所で評価されます。式は再帰下降パーサーで評価され、最大再帰深度は 32 です。反復・条件分岐の文構文はありません。

変数の値は bool, int, char, double, string です。未定義変数への `=` は宣言と初期化になります。変数の型変更は `=` のみで可能です。評価スコープは同一ファイルと `generated` が呼び出すサブプロンプトに限定されます。`--define` で起動時に変数を渡せます。末尾の `;` は式の値を破棄し、文字列展開を空にします。マクロ評価でエラーが発生した場合、そのマクロ全体を空文字にし警告を記録します。

使用可能な演算子:

- 代入: `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `<<=`, `>>=`, `&=`, `^=`, `|=`
- 比較: `==`, `!=`, `<`, `>`, `<=`, `>=`
- 二項: `<<`, `>>`, `+`, `-`, `*`, `/`, `%`, `|`, `^`, `&`
- 前置: `++`, `--`, `+`, `-`, `!`, `~`
- 後置: `++`, `--`

ポインターや参照は提供しません。

### 組み込み変数

| 変数 | 内容 |
| --- | --- |
| `{{N}}` | 1 から始まる現在の反復番号 |
| `{{date}}` | ローカル日付 `yyyyMMdd` |
| `{{time}}` | ローカル時刻 `HHmmss` |
| `{{datetime}}` | ローカル日時 `yyyyMMddHHmmss` |
| `{{stdin}}` | 非対話入力時の標準入力全体。対話端末では空 |
| `{{phase}}` | 現在の phase |
| `{{prev_phase}}` / `{{next_phase}}` | 前後の phase。存在しない側は設定されない |

### 組み込み関数

| 関数 | 仕様 |
| --- | --- |
| `file(path)` | テキストファイル全体を読む |
| `head(text, max_tokens)` / `tail(text, max_tokens)` | トークナイザーで数えた先頭 / 末尾トークン数に切り詰める |
| `head_tail(text, head_tokens, tail_tokens)` | 先頭と末尾を残し、中間を `...` にする |
| `json_literal(text)` | JSON 文字列の内容として使えるようエスケープ |
| `getenv(key)` | 環境変数。未設定なら空文字 |
| `setenv(key, value)` | 環境変数を設定し、空文字を返す |
| `generated(prompt)` | completions API でサブプロンプトを実行 |
| `summary(prompt, target, max_tokens)` | `target` と `max_tokens` 変数をサブプロンプトに設定し生成。結果を max_tokens 以下に切り詰める |
| `random(min?, max?)` | 整数乱数。既定範囲は 0 から UINT32_MAX |
| `choice(value, ...)` | 引数からランダムに一つ選ぶ |
| `exec(exe, arg, ...)` | 子プロセスを実行し標準出力を返す。終了コードは `{{exit_code}}` に設定 |
| `code_block(markdown, name)` | Markdown から指定名のコードブロック本文を返す。なければ空 |
| `root(path)`, `parent(path)`, `stem(path)`, `extension(path)` | パスのルート、親、拡張子を除く名前、拡張子を返す |

`head` / `tail` と prompt の切り詰めには LLM backend の token count API を利用します。トークン数キャッシュの容量は 1000 件で、テキスト生成モード間の実行中に利用し、ファイルへ読み書きして再利用します。

## phase と段落ファイル

`--phases` に複数の値を渡すと、各反復で順番に各 phase の生成を行います。現在・前・次の値は上記の組み込み変数で参照します。`--llm-paragraphs-file` を指定すると `--phases` の値を段落リストから生成します。リスト項目の見出しと、直後にインデントされた子リストの説明を結合して phase にします。

段落ファイルを使わない場合の phase は `--phases` の値です。ソースには Chat Completions 用の phase と prefix の初期化関数がありますが、現在の起動処理からは呼び出されていません。そのため `{{user}}`, `{{char}}` や `{{phase}}: ` が自動設定されるとは限りません。

## コードブロック抽出

`--llm-code-block-extract` が有効なとき、生成 Markdown の名前付きコードブロックをファイルとして出力します。ブロック名をファイル名にし、空白を `_` に置換し、`:*?"<>|#` を除去します。ファイル名の拡張子がなければ `.txt` を補完します。ブロック名が `stdout` の場合はファイルにせず標準出力へ出します。

## ファイルとログ

ログは `--log-level` 以上の severity を記録します。`--verbose` が有効ならログをコンソールへ出力し、`--log-file` が空でなければファイルにも追記します。ログファイルのパスは初期化時にマクロ展開され、その後は同じファイルに書き続けます。

テキスト生成結果は出力ファイルに追記し、親ディレクトリがなければ作成します。バイナリ画像・音声結果は対応する出力ファイルへ書き込みます。標準出力は `--verbose` が false の場合にテキスト生成結果を出力するほか、PNG パラメーター抽出や `stdout` コードブロックで使用します。

## サーバープロセス制御

`create-process` は `--server-executable-file` と `--server-arguments` で実行ファイルを非同期起動します。起動後、`--server-host` (`localhost`)、`--server-port` (`5000`) に TCP 接続できるまで、`--server-max-retries` (`60`) 回、`--server-wait-ms` (`1000`) 間隔で待機します。

`terminate-process` は `--server-executable-file` に指定したフルパスまたはファイル名に一致するプロセスを終了します。現在の実装でプロセス列挙・終了処理が実装されるのは Windows です。

## 課題

課題は [ISSUES.md](ISSUES.md) で優先度順・解決順序順に管理します。課題表は優先度、課題、影響、状態、解決策の候補、完了条件に加え、根拠、前提・依存、次アクション、検証結果、最終更新を管理対象とします。未収集の情報は「未収集」と明記し、課題の解決時はコード・テスト・検証結果と合わせて `ISSUES.md` を更新します。
現時点では仕様上の不具合と断定していない、将来の設計・検証で判断する事項です。各項目には検討の出発点となる改善案を関連付けます。

| 検討事項 | 判断材料 | 改善案 |
| --- | --- | --- |
| 実ネットワーク、外部 LLM、常駐プロセスに依存する経路の検証方法。 | 単体テストへ導入すると環境依存・非決定性が増す一方、通信・認証・サーバー互換性を完全には検証できない。 | HTTP/TCP、LLM、プロセス境界を依存注入可能にし、決定的なスタブによる統合寄りテストを追加する。外部サービスを使う検証は別の統合テスト手順に分離する。 |
| README と技術仕様書の重複範囲。 | 同じ公開仕様を両文書へ記載すると既定値・オプション名の不整合が生じる。 | 実装仕様・既定値・依存関係・マクロ詳細は本仕様書へ集約し、README は導入・実行例・利用者向け要約と参照リンクに限定する。 |
| 技術資料の見出し構造を継続的に保つ方法。 | 章追加時に同じレベルの見出しが誤った親章へ置かれる可能性がある。 | 見出し変更後に全見出しを走査し、`#` → `##` → `###` の親子関係と章順を確認する。必要に応じて目次生成または Markdown lint を導入する。 |

### 依存性注入後のテスト計画

`send_http_get` は transport インスタンスをテンプレート経由で注入できるため、既に Mock transport の接続・タイムアウト・要求・応答テストを実装しています。その他の外部サービス境界は、次の順序で注入可能化とテスト移行を行います。

| 対象 | 現在の状態 | 依存性注入後に実装可能なテスト | 変更すべき既存テスト |
| --- | --- | --- | --- |
| `tcp::send_http_get` | transport 注入済み | 接続、要求 target、タイムアウト、正常応答、HTTP 非 200 応答、送受信エラー | 既存の実ネットワーク待機テストがあれば Mock transport 呼び出しへ置換する。 |
| LLM completions / chat completions | 関数内部で `tcp` を生成 | リクエスト JSON、Authorization、正常応答、必須フィールド欠落、HTTP エラー、タイムアウト | 現在の純粋な backend parameter テストに Mock transport 呼び出し検証を追加し、外部サーバー依存テストは置換する。 |
| Stable Diffusion `send_request` | 関数内部で `tcp` を生成 | txt2img/img2img request body、画像応答の Base64 decode、空画像、HTTP エラー | 現在の `make_request` テストを transport 注入版の送受信テストへ拡張する。 |
| Style-Bert-VITS2 `send_request` | 関数内部で `tcp` を生成 | URL、query parameter、音声応答、HTTP エラー | URL 生成テストは維持し、send_request の実ネットワーク想定は Mock 呼び出しへ変更する。 |
| ComfyUI upload/prompt/history | 一部が `tcp::send_http_get`、POST は内部生成 | upload 応答、prompt/history JSON、生成ファイル情報、サブディレクトリ、HTTP エラー | multipart 生成テストに Mock transport の送受信と一時出力テストを追加する。 |
| process launcher | Windows API / Boost.Process を直接使用 | 起動引数、待機、終了コード、起動失敗、終了対象なし | 実プロセスを起動するテストを追加せず、launcher をテンプレート注入可能にして Mock へ置換する。 |

## README との役割分担

README は利用者向けの導入、実行例、バックエンド接続手順を扱います。実装の全オプション、既定値、マクロ言語、ファイル構成、依存関係、既知の注意点は本仕様書を正本とします。両文書を変更する場合は、実装の現在の挙動と照合して記述を同期します。

## 参照元

- 実装・CLI オプション・マクロ・バックエンド: [`Hermit/Hermit.hpp`](Hermit/Hermit.hpp)
- 利用例・バックエンド設定例・概要: [`readme.md`](readme.md)
- ビルド構成: [`Hermit.sln`](Hermit.sln)、`Hermit/Hermit.vcxproj`、`test/test.vcxproj`


