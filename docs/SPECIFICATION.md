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

Hermit アプリと `test` の Release x64 は full LTCG（`/LTCG`）を使用します。Hermit アプリで incremental LTCG（`/LTCG:incremental`）を使うと、更新した HermitLib オブジェクトの差分リンクで LNK1103 が再現しました。`Hermit.vcxproj` に `UseLinkTimeCodeGeneration` を明示して非 incremental LTCG に統一し、PDB と LTCG を維持したまま、異なる翻訳単位の差分リンク2回で成功を確認しています。`test` の Release x64 では `UseLibraryDependencyInputs=false` を明示し、HermitLib の静的ライブラリをリンク入力として使用します。

通常のビルド・テスト検証は差分ビルド（`/t:Build`）を優先します。`/t:Rebuild` は、ツールチェーン・生成先の変更、依存関係の破損、またはクリーン状態の再現が必要な場合に限って実行します。

## コーディング規約

- C++20 と `.editorconfig` の書式を使用し、文字コードは UTF-8、改行は LF、インデントはスペース 4 個とします。
- C++ の型、関数、変数、メンバー、名前空間、列挙子、定数、マクロは Google C++ Style Guide の命名規則に従います。未適用コードは修正箇所で段階的に適用し、命名変更だけを目的に無関係な箇所へ変更を広げません。公開 CLI オプションと同じ INI key は小文字 kebab-case を使い、ドット・アンダースコア・大文字を区切りに用いません。名前を変更するときに旧名の互換 alias は設けず、旧表記を拒否します。C++ 命名規則の段階適用は [ISS-038](ISSUES.md) で追跡します。
- ローカル変数、定数、メンバー初期化は一様初期化 (`{}`) を優先します。単純な初期化宣言も、型推論や代入が必要な場合を除き、可能な限り `=` ではなく `{}` を使用します。
- `=` は再代入、コピー代入、ラムダのキャプチャ、既定引数、マクロ言語の代入など、宣言時の一様初期化が適さない場面に限定します。
- 狭窄変換をコンパイル時に検出できる一様初期化の利点を維持し、暗黙変換が必要な場合は意図を明示します。
- 新規コードだけでなく、既存コードを変更する範囲でも規約違反を増やさず、単純な宣言を変更する場合は `{}` へ統一します。
- Windows API を使用するソースだけが `Windows.h` を参照し、`WIN32_LEAN_AND_MEAN`、`STRICT`、`NOMINMAX` と汎用マクロの undef によって汚染範囲を局所化します。

Release x64 のフルビルド実測値は **5分02.94秒** です。測定条件は Visual Studio 18 Developer 環境、`Hermit.sln`、`/t:Rebuild /m:1`、`Configuration=Release`、`Platform=x64`、`_CL_=/Bt+` です。ログは[`build-profile/release-x64-parser-boundary-2026-11-26.log`](../build-profile/release-x64-parser-boundary-2026-11-26.log)に記録しています。依存関係復元状態、ツールセット、ホスト負荷が変わった場合は再測定します。

parser grammar と visitor のテンプレート境界は分割済みで、c1xx 合計は変更前54.34秒から32.31秒へ約40.6%短縮しました。残るビルド時間の課題は [`build-profile/README.md`](../build-profile/README.md) と [`ISSUES.md`](ISSUES.md) で追跡します。リンク設定やPDB生成は維持します。

## ファイル構成

| ファイル | 概要 |
| --- | --- |
| `Hermit/Hermit.hpp` | 公開型、設定、名前空間 API、テンプレート実装、共通マクロを定義するヘッダー。 |
| `Hermit/errors.hpp` | Hermit の例外階層と Boost エラー情報タグ。HTTP 実装の Boost.Beast 型は公開せず、Boost.System のエラーコードと整数ステータスを使用。 |
| `Hermit/value.hpp` | 透過コンテナ別名、`string_hash`、文字列キー用コンテナ。 |
| `Hermit/platform.hpp` | Windows API の最小インクルードと汎用マクロ隔離。 |
| `Hermit/context.hpp` | 値型、マクロ値参照、実行コンテキスト、トークン数結果型。 |
| `Hermit/llm_types.hpp` | LLM バックエンド共通インターフェースと生成モード。 |
| `Hermit/command_line.hpp` | CLI 解析 API の軽量宣言。Boost.Program_options/Describe は実装境界へ限定。 |
| `Hermit/string_utils.hpp` | 文字列、Base64、エスケープ、トークン処理 API。prompt truncation は token-count callback を受け取れる。 |
| `Hermit/filesystem.hpp` | ファイル入出力、パス解決、画像 MIME 変換 API。読み書きの最終パスと mode を受け取る reader/writer callback を提供する。 |
| `Hermit/tcp.hpp` | TCP 接続、HTTP リクエスト、タイムアウト API。注入 transport を含む HTTP response status の検証境界。応答 body は最大128 MiBに制限する。 |
| `Hermit/types.hpp` | 共通型ヘッダー。例外、値型、コンテキストを束ねる安定した入口。 |
| `Hermit/aux_config.hpp` | PNG パラメーターとサーバープロセス設定型。 |
| `Hermit/llm_config.hpp` | LLM 接続先、backend 名、生成、モデル一覧・embeddings API path、会話、推論設定。 |
| `Hermit/media_config.hpp` | Style-Bert-VITS2 と ComfyUI の設定型。filesystem の `file_reader` を利用した画像／userdata file 読込注入境界と、ComfyUI API action・upload file・userdata path/user ID・metadata folder・node class・WebSocket 接続設定を宣言する。 |
| `Hermit/sd_config.hpp` | Stable Diffusion のモード・共通・txt2img・img2img 設定型、生成 request の追加 JSON fields、API action/options。img2img 専用 seed resize 値は `-1` を既定値とし、未指定時は共通値を使う。 |
| `Hermit/tg_kc_config.hpp` | text-generation-webui、KoboldCpp、OpenAI 互換 LLM server、Ollama native API の設定型・要求応答 adapter。 |
| `Hermit/config.hpp` | 全バックエンド設定を束ねる実行時構成型。ログレベルは Boost.Log 非依存の `log::severity_level` で保持する。 |
| `Hermit/builtin.hpp` | 組み込みマクロ、macro context、file reader / environment / process runner / generated・summary LLM HTTP executor の注入 API。 |
| `Hermit/http.hpp` | URL パラメーター設定テンプレート。 |
| `Hermit/llm.hpp` | LLM の生成、モデル一覧・embedding・model lifecycle・汎用 HTTP streaming API、会話、段落、コードブロック処理 API。 |
| `Hermit/generation.hpp` | Stable Diffusion の生成と progress/interrogate/control/generic HTTP API・raw streaming API、Style-Bert-VITS2、ComfyUI の生成 API。Style-Bert-VITS2 の GET/POST 音声生成、モデル一覧・更新・状態・G2P・WAV取得 API、ComfyUI の queue/control/history/object-info/upload/WebSocket event API・`generated_file_info`・HTTP/file reader/binary storage 注入、SD 画像/body reader 注入境界を公開。 |
| `Hermit/log.hpp` | severity・初期化 API、ログマクロ、差し替え可能なログ stream factory。sink・書式実装ヘッダーは `.cpp` に限定。 |
| `Hermit/log_types.hpp` | Boost.Log に依存しない severity 型。実行時設定が保持するログレベル境界。 |
| `Hermit/runtime.hpp` | phase、反復、seed、プロセス制御、差し替え可能な port connector・process launcher・process terminator・生成物 writer・SD/SB HTTP executor と画像 reader、アプリケーション実行 API。 |
| `Hermit/core.hpp` | コマンド・モード変換、値文字列化、基盤処理の公開宣言。 |
| `Hermit/png.hpp` | PNG tEXt パラメーター抽出 API。 |
| `Hermit/app_types.hpp` | 段落項目とアプリケーション実行モード（model list/info、embeddings、tokenize、汎用 HTTP request/stream、Stable Diffusion の定型 API・汎用 request/stream、ComfyUI queue/control/history/object-info/WebSocket event command を含む）。 |
| `Hermit/lru.hpp` | トークン数 LRU キャッシュの型と API。 |
| `Hermit/lru_impl.hpp` | LRU のテンプレート実体と CBOR ファイル永続化。設定・filesystem・ログに依存する非公開実装境界。reader/writer callback で保存と復元を差し替え可能。 |
| `Hermit/templates.hpp` | 乱数、unwrap、値変換、variant 取得の共通テンプレート。 |
| `Hermit/templates_core.hpp` | parser など軽量な翻訳単位が利用する unwrap／型特性の最小テンプレート。重いログ・変換依存を持たない。 |
| `Hermit/parser_types.hpp` | Boost.Spirit parser の AST 型と Fusion 適応定義。単独・複合代入ASTは演算子を保持し、実験用statement variant ruleで使用する。 |
| `Hermit/parser.hpp` | parser の文法記号、文法テンプレート、評価 API、単独・複合代入で共用する非テンプレート演算子適用境界。`config` は前方宣言で参照する。 |
| `Hermit/parser_grammar_impl.hpp` | Boost.Spirit の属性変換と document grammar 構築の実装定義。`parser.cpp` のみが参照する。 |
| `Hermit/parser_visitor_impl.hpp` | AST 評価 visitor と演算子テンプレートの実装定義。`parser_visitors.cpp` のみが参照する。 |
| `Hermit/parser_evaluation_internal.hpp` | parser と visitor の間の非テンプレート `evaluate_ast` 内部宣言。 |
| `Hermit/parser_internal.hpp` | Spirit grammar rule と symbol の内部宣言。 |
| `Hermit/core.cpp` | 設定値、コンテキスト、LRU キャッシュ、マクロなどの基盤処理。 |
| `Hermit/parser.cpp` | プロンプト・フェーズ・マクロの解析処理。式演算子の評価、実験的prefix/postfix更新を含む。 |
| `Hermit/parser_visitors.cpp` | AST 評価 visitor と実験的 statement 評価の実装（通常ビルドの公開経路から切り離し）。 |
| `Hermit/parser_grammar_symbols.hpp` | Spirit 文法の symbol 型の完全定義。 |
| `Hermit/parser_lexical_rules.hpp` | 字句・基本値 rule の component。 |
| `Hermit/parser_primary_macro_rules.hpp` | primary／macro rule の component。lexical component を参照する。 |
| `Hermit/parser_unary_postfix_rules.hpp` | unary／postfix rule の component。primary／macro component を参照する。 |
| `Hermit/parser_arithmetic_comparison_rules.hpp` | 算術・比較・論理 rule の component。unary／postfix component を参照する。 |
| `Hermit/parser_assignment_document_rules.hpp` | conditional／document と実験的 statement rule の component。Stage 1式statement、Stage 2単独代入、Stage 3複合代入、Stage 4複合文、Stage 5 if/else、Stage 6 while、Stage 7増減式を専用実験flagで構築する。Stage 8は専用flagで全statement構文を統合し、実験構成128/128 testsで検証済み。通常構成ではstatement全体のruleを構築せず、公開grammarは式placeholderのみ。 |
| `Hermit/public.hpp` | 軽量な公開 API 宣言。重い実装ヘッダーに依存しない。 |
| `Hermit/legacy.hpp` | 既存 `Hermit.hpp` の互換用集約ヘッダー。 |
| `Hermit/public_types.hpp` | Beast 型を公開しない LLM／HTTP 型消去境界。request/response、GET/POST/PUT/PATCH/DELETE/HEAD/OPTIONS、Content-Type、binary response metadata、timeout、authorization、追加 request headers、incremental stream callback、wait/storage callback を定義する。 |
| `Hermit/public_http_adapter.hpp` / `.cpp` | 既存 tcp executor と型消去 HTTP 境界の内部 adapter。全 executor 応答の 2xx HTTP status を成功として検証し、request method・headers と逐次 response chunks を伝達する。 |
| `test/test_public_api.cpp` | 軽量公開 API と型消去契約の互換性テスト。 |
| `docs/PARSER_TEMPLATE_BOUNDARY.md` | parser の内部 API、Spirit 文法、評価 visitor のテンプレート境界設計。 |
| `docs/DEVELOPMENT_KNOWLEDGE.md` | Qi属性伝播・AST実験・依存注入テストで得た観測、採否、根拠をID付き表形式で管理する知見台帳。 |
| `docs/REQUIREMENTS.md` | ユーザー承認済みの宣言的要求と受け入れ条件を記録する正本。 |
| `docs/BACKEND_WEB_API_COVERAGE.md` | REQ-005 の公式バックエンド API 操作、実装・テスト対応、版・拡張差分を追跡する台帳。 |
| `readme.md` | 日本語の利用者向け導入・実行例。冒頭リンクから README の言語を切り替える。 |
| `docs/README/en/README.md` | 英語の利用者向け導入・実行例。 |
| `docs/IDENTIFIERS.md` | 要求、課題、parser段階、開発知見で使う固定IDの形式と採番規則。 |
| `build-profile/parser-template-boundary-2026-11-27.csv` / `.svg` | parser と visitor の `/Bt+` frontend/backend 比較値と図。 |
| `Hermit/builtin.cpp` | 組み込みマクロと動的変数の設定。 |
| `Hermit/builtin_expand.cpp` | parser による macro 展開境界。 |
| `Hermit/builtin_registry.cpp` | builtin マクロ登録と引数数検証。 |
| `Hermit/builtin_path.cpp` | `root`、`parent`、`stem`、`extension` のパス操作マクロ。 |
| `Hermit/builtin_environment.cpp` | `getenv`、`setenv` と環境アクセサー境界。 |
| `Hermit/builtin_process.cpp` | `exec` と Boost.Process/Asio のプロセス実行境界。 |
| `Hermit/builtin_input.cpp` | 日時マクロ、対話入力判定、標準入力読み取り。 |
| `Hermit/builtin_llm.cpp` | `generated`／`summary` の LLM 呼び出しマクロ。 |
| `Hermit/png.cpp` | PNG署名・IHDR・chunk長を検証してtEXtメタデータを挿入・抽出。 |
| `Hermit/tcp.cpp` | TCP 接続確認とサーバー待機。 |
| `Hermit/filesystem.cpp` | ファイル読み書き、パス解決、プロンプトファイル処理。 |
| `Hermit/llm.cpp` | Completions/Chat Completions、汎用 HTTP request/stream、Text Generation Web UI moderation/health/Anthropic Messages/current model info/admin model list/load/unload/token encode/decode、LM Studio native chat/Responses/Decisions/Anthropic Messages/Jev System One、vLLM batch chat、モデル・embedding・token count、Ollama/LM Studio model lifecycle、会話履歴。 |
| `Hermit/sd.cpp` | Stable Diffusion の画像生成要求、定型 API および汎用 JSON/raw HTTP request・response streaming 呼び出し。 |
| `Hermit/sb.cpp` | Style-Bert-VITS2 の音声生成要求。 |
| `Hermit/cu.cpp` | ComfyUI の画像アップロード、prompt、queue/control/history、WebSocket `/ws` event stream API。 |
| `Hermit/command_line.cpp` | Boost.Program_options による CLI オプション解析。ComfyUI object-info を含む。 |
| `Hermit/string_utils.cpp` | 文字列変換・分割・URL 関連の共通処理。 |
| `Hermit/string_extra.cpp` | 追加の文字列整形・エンコード処理。 |
| `Hermit/log.cpp` | ログ出力とログレベル制御。 |
| `Hermit/process.cpp` | サーバープロセスの起動・終了。 |
| `Hermit/iteration.cpp` | モード実行、seed、phase、反復制御、ComfyUI WebSocket dispatch、汎用 API binary response と SD/API raw stream の stdout 出力、例外安全な main。 |
| `Hermit/main.cpp` | Windows エントリーポイント。 |
| `test/test.cpp` | Google Test のテストケース。 |
| `test/test_support.hpp` | テスト共通ヘルパー、出力リダイレクタ、Mock 型。 |
| `test/test_builtin.cpp` | builtin の依存性注入テスト。 |
| `test/test_parser.cpp` | parser の AST 構造、公開式評価、演算子優先順位と重複token境界、無効statement構文の拒否、再帰展開・停止条件テスト。実験的statementテストは機能フラグで隔離。 |
| `test/test_filesystem.cpp` | filesystem のパス、拡張子、MIME 変換テスト。 |
| `test/test_llm.cpp` | LLM の純粋な Markdown・項目リスト変換テスト。 |
| `test/test_cu.cpp` | ComfyUI multipart 要求生成、queue/control/history/object-info/models/embeddings/extensions/features/upload-image/upload-mask/view/metadata と WebSocket event stream 契約テスト。 |
| `test/test_sb.cpp` | Style-Bert-VITS2 の URL・transport テスト。 |
| `test/test_sd.cpp` | Stable Diffusion の要求生成・PNG パラメーター・HTTP method/path/header/raw body・binary response・generic response streaming 契約テスト。 |
| `test/test_tcp.cpp` | TCP transport 注入・Content-Type からの text/binary response 判定テスト。 |
| `test/test_llm_backend.cpp` | TG／Kobold backend の要求・応答、Chat 形式、共通モック契約のテスト。 |
| `Hermit/HermitLib.vcxproj` | Hermit 実装を Static Library としてビルドする共通ライブラリプロジェクト。 |

現在の単体テストは 298 件（29 テストスイート、2026-11-28 の最新全件検証）で、CLI の既定値・共通/バックエンド別オプション反映・`--kc-bypass-eos` と `trim_stop` の独立性・不正モードエラー、例外安全なエントリーポイント、LRU、context の継承・上書き・型変換・optional取得・親値参照、phase 境界、文字列・filesystem の text/binary 入出力、親ディレクトリ作成、prompt 優先順位、書き込み失敗、画像 Base64/MIME と未知拡張子・不存在エラー、ログseverity変換、PNG署名・chunk境界・metadata key不在、Base64不正入力と閾値境界、組み込みマクロの registry、型変換、型不一致時の callback 未呼出、generated/summary の completion/token-count executor 成功と HTTP 失敗伝播、summary の max_tokens context と reasoning 除去・上限切り詰め、JSON エスケープ、コードブロック抽出とファイル名のパストラバーサル防止、パス操作、既定引数の乱数境界、exec マクロの既定拒否・明示許可、依存注入された環境 accessor／random source／process runner／file reader／port resolver・connector・waiter／process launcher・terminator と process runner の終了コード・失敗契約、全 command mode、LLM/SD の API ターゲット解決、LLM 共通バックエンド境界の Mock 契約、HTTP transport と各 request executor の Mock 契約、LLM completion/token-count の runtime HTTP/TCP 注入成功と HTTP status 失敗伝播、SD/LLM/ComfyUI runtime writer failure 伝播、既定 TCP transport を使う LLM/SD/SB/ComfyUI overload の loopback 通信、SB writer 失敗伝播、ComfyUI 履歴 URL/JSON・queue/control/history/users/userdata query/path/header/binary/204・jobs list/detail/cancellation query/path/body・object-info/models/embeddings/extensions/features/image-mask multipart/view/metadata/system-stats/default-workflow/prompt/template list/template file/i18n/web UI API contract・WebSocket text/binary event stream forwarding・storage 注入・サブディレクトリ保持・sanitize・traversal 拒否、TCP JSON 要求のmethod・header・body・128 MiBを超える HTTP 応答 body の拒否・全 backend 注入 executor の非成功 status 例外、Stable Diffusion API の progress/interrogate/control/Extras/PNG-info/options/list/refresh/create/train/memory/script/checkpoint/OpenAPI/internal API contract・任意 JSON method/path request・HTTP method adapter・txt2img/img2img dynamic field forwarding、img2img の複数画像・mask・latent mask の Base64 要求と欠落ファイル失敗、各生成バックエンドの要求・応答解析、LLM のコードブロック・推論除去・項目リスト解析、公開parserの式 AST 評価・組み込みマクロ評価・再帰停止・条件式分岐・論理条件評価・演算子優先順位と重複token境界、マクロ変数・関数引数・ネスト・区切り記号不整合の解析、Qi 属性変換とplaceholder候補のbacktracking、予約語と識別子境界、代入・増減・statement構文の拒否、非テンプレート parse/evaluate 境界、公開モード変換、user-defined variable の区切り・重複キー、設定 stream の値反映・CLI 優先順位・不正値を検証します。実験的な式statementは `HERMIT_ENABLE_EXPERIMENTAL_EXPRESSION_STATEMENTS`、単独代入は `HERMIT_ENABLE_EXPERIMENTAL_ASSIGNMENTS`、複合代入は `HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_ASSIGNMENTS`、複合文は `HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_STATEMENTS`、if/elseは `HERMIT_ENABLE_EXPERIMENTAL_IF_STATEMENTS`、whileは `HERMIT_ENABLE_EXPERIMENTAL_WHILE_STATEMENTS`、前置/後置増減式は `HERMIT_ENABLE_EXPERIMENTAL_INCREMENT_DECREMENT`、全statement統合はStage 1–7 flagと `HERMIT_ENABLE_EXPERIMENTAL_STAGE8_STATEMENTS` を併用した時のみ有効です。Stage 8実験構成は128 tests / 25 suites、当時の既定構成は120 tests / 27 suites、2026-11-28の最新通常構成は310 tests / 29 suitesで検証しています。実験構文は既定構成には含めません。LLM、SD、SB、ComfyUI の既存 runtime overload は loopback mock server を使って既定 TCP transport 経由の通信を検証しています。process runtime は起動 callback の引数・例外と終了 callback の対象パス・終了数を検証し、detach される child process の終了コードは API が公開しません。

Stage 2–7の実験flag `HERMIT_ENABLE_EXPERIMENTAL_ASSIGNMENTS`、`HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_ASSIGNMENTS`、`HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_STATEMENTS`、`HERMIT_ENABLE_EXPERIMENTAL_IF_STATEMENTS`、`HERMIT_ENABLE_EXPERIMENTAL_WHILE_STATEMENTS`、`HERMIT_ENABLE_EXPERIMENTAL_INCREMENT_DECREMENT` を同時に有効にしたRelease x64検証構成では、単独・複合代入、block、if/else、while、前置/後置増減式のテストを含む124件（25 suites）が成功した。whileはplaceholder内で全loop共有の10,000 body実行予算を持ち、10,001回目が必要ならbodyの実行前に停止する。増減式は整数変数のみを更新し、prefixは更新後、postfixは更新前の値を返す。これらの構文は公開grammarには含めない。通常構成もRelease x64 Rebuild後に105件（25 suites）が成功した。

## アプリケーション固有用語

| 用語 | 意味 | 関連実装・設定 |
| --- | --- | --- |
| command / backend | サブコマンドで操作を選択し、生成時は `--backend` で `tg`, `kc`, `lmstudio`, `llama-cpp`, `vllm`, `ollama`, `sd`, `sb`, `cu` を指定する。 | `generate --backend <...>`、`server start|stop`、`png extract` |
| tg / kc / lmstudio / llama-cpp / vllm / ollama | Text Generation Web UI、KoboldCpp、OpenAI 互換 LLM server 3種、Ollama native API のテキスト生成モード。 | `CommandMode`、`LlmBackendParameters` |
| sd / sb / cu | Stable Diffusion 画像生成・`sd-api` 操作 / Style-Bert-VITS2 / ComfyUI の連携モード。 | `command_mode` |
| LLM | 大規模言語モデル。テキスト生成の接続先。 | `--llm-*` |
| Completions | prompt 文字列を送る生成 API。 | `--llm-mode completions` |
| Chat Completions | messages 配列と会話履歴を使う生成 API。 | `--llm-mode chat-completions` |
| prompt | 生成要求としてバックエンドへ渡す入力文。 | `--llm-prompt`, `--sd-prompt` など |
| generation prefix / suffix | 生成前後に自動付加する文字列。 | `--llm-generation-prefix`, `--llm-generation-suffix` |
| phase | 反復ごとに順番に実行する入力段階。 | `--iteration-phases`, `context` |
| paragraph | 段落ファイルから読み込む phase 用の入力単位。 | `--llm-paragraphs-file` |
| macro | prompt 内で展開される変数記法。 | `--variable`, 組み込みマクロ |
| context | マクロ名と値を保持する実行時環境。 | `context`, `cfg.ctx` |
| N | 現在の反復番号（1 始まり）。 | 組み込み変数 `N` |
| token count | prompt のトークン数。上限超過時の分割・継続判定に使う。 | `--llm-token-count-target` |
| LRU cache | 生成済み入力・応答を保存して再利用するキャッシュ。 | `--llm-cache-file` |
| base path | 相対パスを解決する基準ディレクトリ。 | `--path-base` |
| server process mode | サーバー実行ファイルを起動・待機・終了する補助モード。 | `create-process`, `terminate-process` |
| tEXt parameters | PNG の tEXt チャンクに保存された生成パラメーター。 | `extract-png-parameters` |

## ヘッダー依存関係

Release x64 はデバッグ情報と PDB を生成します。プロジェクトごとの `IntDir` と明示的な PDB パスを分離し、並列コンパイル時の中間ファイル・PDB 共有競合を防止します。LNK1101/C1902 が発生した場合は、生成を無効化せず、共有パス・古い生成物・ツールセット不一致を調査します。
テストプロジェクトの Release x64 も同じ設定です。
Hermit アプリの Release x64 リンクは `/LTCG` を使い、更新オブジェクトの差分リンクで LNK1103 を再現した `/LTCG:incremental` を使いません。
ビルドは、Visual Studio 18 の `VsDevCmd.bat -arch=x64 -host_arch=x64` で初期化した環境から実行し、MSPDB DLL のバージョン混在を防ぎます。

ヘッダーは、共通型、設定型、機能 API の順に依存する構成です。実装 `.cpp` は原則として担当機能の API ヘッダーを起点に参照します。

依存関係を整理・変更する際は、実装変更と同じ変更単位で本節の依存先表および上記のファイル構成表を更新します。仕様書とコードの依存関係が一致しない場合は、実装を正として差分を修正します。

依存関係は Make 記法の `ターゲット: 依存先` で表記します。

```make
types.hpp: errors.hpp value.hpp context.hpp
context.hpp: errors.hpp value.hpp boost/variant.hpp
errors.hpp: <filesystem> <exception> <string> boost/system/error_code.hpp boost/current_function.hpp boost/exception/all.hpp boost/stacktrace.hpp
lru.hpp: context.hpp filesystem.hpp boost/multi_index_container.hpp boost/multi_index/hashed_index.hpp boost/multi_index/sequenced_index.hpp
lru_impl.hpp: lru.hpp config.hpp errors.hpp filesystem.hpp log.hpp nlohmann/json.hpp <cstdint> <filesystem> <ios> <vector>
templates.hpp: context.hpp templates_core.hpp errors.hpp log.hpp boost/lexical_cast.hpp boost/variant/get.hpp
llm_types.hpp: nlohmann/json.hpp boost/describe.hpp
llm_config.hpp: llm_types.hpp <string>
tg_kc_config.hpp: llm_types.hpp nlohmann/json.hpp boost/describe.hpp
sd_config.hpp: aux_config.hpp tg_kc_config.hpp nlohmann/json.hpp boost/describe.hpp
media_config.hpp: filesystem.hpp <string> <string_view> <vector>
aux_config.hpp: <string>
app_types.hpp: <string> <string_view> <vector>
config.hpp: context.hpp lru.hpp app_types.hpp llm_config.hpp tg_kc_config.hpp sd_config.hpp media_config.hpp aux_config.hpp log_types.hpp
command_line.hpp: context.hpp <istream> <string> <string_view> <vector>
string_utils.hpp: context.hpp <functional> <string> <string_view> <vector> nlohmann/json.hpp boost/archive/iterators/base64_from_binary.hpp boost/archive/iterators/binary_from_base64.hpp boost/archive/iterators/transform_width.hpp
filesystem.hpp: <filesystem> <functional> <ios> <string> <string_view> <vector>
parser_types.hpp: context.hpp boost/fusion/include/adapt_struct.hpp boost/optional.hpp boost/variant.hpp
core.hpp: config.hpp app_types.hpp context.hpp sd_config.hpp llm_types.hpp
parser.hpp: parser_types.hpp config.hpp templates.hpp boost/spirit/include/qi.hpp
parser_grammar_impl.hpp: parser_internal.hpp
parser_internal.hpp: parser_types.hpp parser_grammar_symbols.hpp parser_lexical_rules.hpp parser_primary_macro_rules.hpp parser_unary_postfix_rules.hpp parser_arithmetic_comparison_rules.hpp parser_assignment_document_rules.hpp
parser_visitor_impl.hpp: parser.hpp errors.hpp templates_core.hpp
parser_evaluation_internal.hpp: parser.hpp
parser.cpp: parser.hpp parser_internal.hpp parser_grammar_impl.hpp parser_evaluation_internal.hpp builtin.hpp errors.hpp log.hpp
parser_visitors.cpp: parser_visitor_impl.hpp parser_evaluation_internal.hpp builtin.hpp config.hpp log.hpp
tcp.hpp: <chrono> <cstdint> <functional> <optional> <string_view> boost/asio/io_context.hpp boost/asio/ip/tcp.hpp boost/beast/core.hpp boost/beast/http.hpp
http.hpp: boost/url.hpp
public_types.hpp: <functional> <chrono> <map> <optional> <string> <string_view>
public_http_adapter.hpp: public_types.hpp tcp.hpp
llm.hpp: config.hpp tcp.hpp public_types.hpp filesystem.hpp nlohmann/json.hpp
config.hpp filesystem.hpp media_config.hpp sd_config.hpp tcp.hpp public_http_adapter.hpp public_types.hpp nlohmann/json.hpp
builtin.hpp: context.hpp public_types.hpp
log_types.hpp: （標準 C++ のみ）
log.hpp: log_types.hpp <filesystem> <functional> <iosfwd> <memory> <optional> <string_view> boost/log/trivial.hpp（`HERMIT_LOGGING_SOURCE_LOCATION` 有効時のみ boost/log/core.hpp 等）
runtime.hpp: config.hpp filesystem.hpp public_http_adapter.hpp tcp.hpp <filesystem> <functional> <string> <string_view> <vector>
platform.hpp: boost/predef.h Windows.h tlhelp32.h
png.hpp: <string> <string_view>
Hermit.hpp: types.hpp llm_types.hpp command_line.hpp string_utils.hpp filesystem.hpp aux_config.hpp llm_config.hpp sd_config.hpp tg_kc_config.hpp config.hpp app_types.hpp media_config.hpp lru.hpp templates.hpp lru_impl.hpp builtin.hpp http.hpp llm.hpp generation.hpp log.hpp runtime.hpp tcp.hpp png.hpp core.hpp parser_types.hpp parser.hpp boost/spirit/include/qi.hpp
```

実装ファイルの直接依存も次の Make ルールで管理します。

```make
png.cpp: png.hpp errors.hpp
string_utils.cpp: string_utils.hpp config.hpp platform.hpp boost/algorithm/string.hpp
string_extra.cpp: llm.hpp filesystem.hpp tcp.hpp public_http_adapter.hpp media_config.hpp string_utils.hpp value.hpp errors.hpp log.hpp
log.cpp: log.hpp errors.hpp filesystem.hpp value.hpp
process.cpp: runtime.hpp app_types.hpp value.hpp log.hpp platform.hpp command_line.hpp builtin.hpp boost/algorithm/string.hpp boost/asio/io_context.hpp boost/asio/ip/tcp.hpp boost/scope/scope_exit.hpp boost/process/v2/*.hpp
tcp.cpp: tcp.hpp errors.hpp log.hpp boost/beast/version.hpp
sd.cpp: generation.hpp filesystem.hpp tcp.hpp log.hpp errors.hpp string_utils.hpp public_http_adapter.hpp <algorithm> <chrono> <cctype> <stdexcept> <utility> boost/url.hpp
public_http_adapter.cpp: public_http_adapter.hpp errors.hpp <algorithm> <cctype> <limits> <string> <utility>
sb.cpp: generation.hpp http.hpp tcp.hpp log.hpp public_http_adapter.hpp
cu.cpp: generation.hpp filesystem.hpp tcp.hpp http.hpp log.hpp errors.hpp runtime.hpp public_http_adapter.hpp <algorithm> <chrono> <filesystem> <random> <sstream> <iomanip> boost/asio.hpp boost/beast.hpp boost/nowide/iostream.hpp boost/url.hpp
command_line.cpp: command_line.hpp config.hpp filesystem.hpp string_utils.hpp log.hpp boost/describe.hpp boost/algorithm/string.hpp boost/program_options.hpp boost/tokenizer.hpp boost/nowide/iostream.hpp boost/url.hpp nlohmann/json.hpp
filesystem.cpp: filesystem.hpp config.hpp builtin.hpp string_utils.hpp errors.hpp log.hpp runtime.hpp boost/nowide/fstream.hpp boost/range/algorithm.hpp
iteration.cpp: runtime.hpp config.hpp filesystem.hpp builtin.hpp llm.hpp generation.hpp public_http_adapter.hpp command_line.hpp png.hpp log.hpp lru.hpp boost/exception/diagnostic_information.hpp boost/nowide/args.hpp boost/nowide/iostream.hpp <chrono> <iostream> <random>
llm.cpp: llm.hpp public_http_adapter.hpp config.hpp filesystem.hpp builtin.hpp string_utils.hpp tcp.hpp log.hpp errors.hpp runtime.hpp templates.hpp boost/algorithm/string.hpp boost/beast/core.hpp boost/beast/http.hpp boost/date_time/posix_time/posix_time.hpp boost/nowide/iostream.hpp boost/url.hpp nlohmann/json.hpp <chrono> <cstdint> <filesystem> <fstream> <iomanip> <regex> <sstream> <utility>
builtin.cpp: builtin.hpp config.hpp filesystem.hpp string_utils.hpp llm.hpp parser.hpp templates.hpp errors.hpp boost/asio/error.hpp boost/asio/io_context.hpp boost/asio/read.hpp boost/asio/readable_pipe.hpp boost/date_time/posix_time/posix_time.hpp boost/lexical_cast.hpp boost/nowide/cstdlib.hpp boost/nowide/iostream.hpp boost/process/v2/environment.hpp boost/process/v2/process.hpp boost/process/v2/stdio.hpp
builtin_path.cpp: builtin.hpp templates.hpp
builtin_environment.cpp: builtin.hpp templates.hpp boost/nowide/cstdlib.hpp
builtin_process.cpp: builtin.hpp errors.hpp string_utils.hpp templates.hpp boost/asio/error.hpp boost/asio/io_context.hpp boost/asio/read.hpp boost/asio/readable_pipe.hpp boost/process/v2/environment.hpp boost/process/v2/process.hpp boost/process/v2/stdio.hpp
builtin_input.cpp: builtin.hpp boost/date_time/posix_time/posix_time.hpp boost/nowide/iostream.hpp boost/predef.hpp
builtin_llm.cpp: builtin.hpp llm.hpp string_utils.hpp templates.hpp
core.cpp: core.hpp templates.hpp lru_impl.hpp
main.cpp: runtime.hpp
```

### 残存する統合ヘッダー依存

| 実装 | 現状 | 追加分割を保留する理由 |
| --- | --- | --- |
| `builtin.cpp` | 機能別ヘッダーと Boost の直接依存 | 組み込みマクロ、parser 評価、LLM 呼び出し、プロセス実行を直接参照し、`Hermit.hpp` 依存を除去済み。 |
| `core.cpp` | `core.hpp`, `templates.hpp`, `lru_impl.hpp` | 基盤 API、コンテキスト、値変換、モード変換を実装。LRU のテンプレート実体と JSON/filesystem 永続化は専用実装ヘッダーへ分離済み。 |
| `parser.cpp` | 標準ヘッダー + `parser.hpp` + `parser_internal.hpp` + `parser_grammar_impl.hpp` + `parser_evaluation_internal.hpp` + builtin/errors/log API | Spirit grammar 構築と非テンプレート AST 評価境界を参照する。評価 visitor 実装は別翻訳単位へ分離。 |
| `llm.cpp` | `llm.hpp`, `config.hpp`, `filesystem.hpp`, `builtin.hpp`, `string_utils.hpp`, `tcp.hpp`, `log.hpp`, `errors.hpp`, `runtime.hpp`, `templates.hpp`, `boost/url.hpp` | LLM、filesystem、TCP、macro、token-count、会話履歴を直接参照する実装。URL path segment encoding に Boost.URL を使用。 |

上記は、単一ヘッダー置換によるコンパイル短縮の利点より内部 API 再設計の影響が大きいため、Release x64 ビルドと単体テストが成功する現構成を維持する。

依存方向は、原則として `共通型 → 設定型 → 機能 API → 実装` とし、機能ヘッダーから実装 `.cpp` をインクルードしません。`Hermit.hpp` は互換性・テスト用の集約入口であり、新規の実装では担当機能のヘッダーを優先します。

すべての分割ヘッダーは専用の include guard を持ち、多重インクルードによる再定義を防ぎます。Windows 固有 API は `platform.hpp` に限定し、他のヘッダーへ Windows SDK のマクロを漏らしません。

## 起動コマンドと設定

CLI は位置引数のサブコマンドで操作を選択し、生成・モデル一覧取得で `--backend` を指定します。

```text
Hermit generate --backend <tg|kc|lmstudio|llama-cpp|vllm|ollama|sd|sb|cu> [options]
Hermit server <start|stop> [options]
Hermit png extract [options]
Hermit models list --backend <lmstudio|llama-cpp|vllm|ollama|tg> [options]
Hermit models openai-list --backend tg [options]
Hermit models show --backend ollama --llm-model <model> [options]
Hermit models running --backend ollama [options]
Hermit models version --backend ollama [options]
Hermit models <create|copy|delete|pull|push> --backend ollama --llm-model-action-options <json> [options]
Hermit models <load|unload|download|download-status> --backend lmstudio --llm-model-action-options <json> [options]
Hermit responses create --backend lmstudio --llm-api-options <json> [options]
Hermit lmstudio-chat create --backend lmstudio --llm-api-options <json> [options]
Hermit decisions create --backend lmstudio --llm-api-options <json> [options]
Hermit messages create --backend <tg|lmstudio> --llm-api-options <json> [options]
Hermit systemone create --backend lmstudio --llm-api-options <json> [options]
Hermit batch-chat create --backend vllm --llm-api-options <json> [options]
Hermit moderations create --backend tg --llm-api-options <json> [options]
Hermit health check --backend tg [options]
Hermit generation stop --backend tg [options]
Hermit logits get --backend tg --llm-api-options <json> [options]
Hermit chat-prompt create --backend tg --llm-api-options <json> [options]
Hermit loras list --backend tg [options]
Hermit loras <load|unload> --backend tg [--llm-model-action-options <json>] [options]
Hermit models info --backend tg [options]
Hermit models <info|model|max-length|max-context-length|soft-prompt|soft-prompts-list|version|true-max-context-length|performance|preloadstory> --backend kc [options]
Hermit models <load|unload> --backend tg [--llm-model-action-options <json>] [options]
Hermit comfy <queue|queue-update|history|history-item|history-update|interrupt|free|events|object-info|models|models-folder|embeddings|extensions|features|upload-image|upload-mask|view|view-metadata|system-stats|default-workflow|prompt-status|workflow-templates|template-file|i18n|web-ui|users|user-create|userdata-list|userdata-list-v2|userdata-get|userdata-put|userdata-delete|userdata-move|jobs|job|job-cancel|jobs-cancel> [options]
Hermit sd-api <progress|interrogate|interrupt|skip|extra-single-image|extra-batch-images|png-info|options|options-set|create-embedding|create-hypernetwork|train-embedding|train-hypernetwork|cmd-flags|samplers|schedulers|upscalers|latent-upscale-modes|sd-models|sd-vae|hypernetworks|face-restorers|realesrgan-models|prompt-styles|embeddings|refresh-embeddings|refresh-checkpoints|refresh-vae|memory|scripts|script-info|extensions|unload-checkpoint|reload-checkpoint|server-kill|server-restart|server-stop> [options]
Hermit embeddings create --backend <lmstudio|llama-cpp|vllm|ollama> --llm-model <model> [options]
Hermit tokenize <encode|decode> --backend <llama-cpp|tg> [options]
```

`--mode` とドット区切りの旧オプション名は廃止しました。設定オプションは `--llm-api-options value` のように小文字 kebab-case、INI キーは `llm-api-options = value` とし、両方で同じ名前を使います。短縮形は `-h`（`--help`）のみです。CLI 値は設定ファイルより優先されます。

| 値 | 動作 |
| --- | --- |
| `generate --backend tg` | text-generation-webui とのテキスト生成 |
| `generate --backend kc` | KoboldCpp とのテキスト生成 |
| `generate --backend lmstudio` | LM Studio の OpenAI 互換 Completions / Chat Completions |
| `models load|unload|download|download-status --backend lmstudio` | LM Studio native REST のモデル管理・download status |
| `generate --backend llama-cpp` | llama.cpp server の OpenAI 互換 Completions / Chat Completions |
| `generate --backend vllm` | vLLM の OpenAI 互換 Completions / Chat Completions |
| `generate --backend ollama` | Ollama native `/api/generate` / `/api/chat` |
| `generate --backend sd` | AUTOMATIC1111 Stable Diffusion WebUI との画像生成 |
| `sd-api progress` | A1111 の `GET /sdapi/v1/progress`。`--sd-api-options` で `skip_current_image` query を指定 |
| `sd-api routes` | A1111 FastAPI の `GET /openapi.json` から実行中 API schema を取得 |
| `sd-api request` | 任意 A1111 route に HTTP method/path/body/header を指定。JSON object/array body は `--sd-api-options`、raw/binary body は `--sd-api-body-file`、Content-Type は `--sd-api-content-type`、追加 headers は JSON map の `--sd-api-headers`。非 text/JSON/XML 応答は bytes を stdout へ出力 |
| `sd-api stream` | 同じ任意 HTTP request 設定を使用し、応答 body chunk を解析せず stdout へ逐次出力。SSE 等に対応し WebSocket は対象外 |
| `sd-api pending-tasks`, `internal-progress` | A1111 `GET /internal/pending-tasks` と `POST /internal/progress`。後者の request は `--sd-api-options` |
| `sd-api quicksettings-hint`, `ping`, `profile-startup`, `sysinfo`, `sysinfo-download` | A1111 の UI 内部 API。download は応答 bytes を stdout に出力 |
| `sd-api interrogate` | A1111 の `POST /sdapi/v1/interrogate`。`--sd-api-options` に Base64 `image` と `clip` / `deepdanbooru` の `model` を指定 |
| `sd-api interrupt` | A1111 の `POST /sdapi/v1/interrupt` で実行中処理を停止 |
| `sd-api skip` | A1111 の `POST /sdapi/v1/skip` で現在の処理を skip |
| `sd-api extra-single-image` | A1111 の `POST /sdapi/v1/extra-single-image` で画像を処理。`--sd-api-options` に Base64 `image` と任意の upscaler fields を指定 |
| `sd-api extra-batch-images` | A1111 の `POST /sdapi/v1/extra-batch-images` で画像一覧を処理。`--sd-api-options` に `{name,data}` 配列の `imageList` と任意の upscaler fields を指定 |
| `sd-api png-info` | A1111 の `POST /sdapi/v1/png-info` で PNG 情報を解析。`--sd-api-options` に Base64 `image` を指定 |
| `sd-api options` | A1111 の `GET /sdapi/v1/options` で WebUI 設定を取得 |
| `sd-api options-set` | A1111 の `POST /sdapi/v1/options` で設定を更新。`--sd-api-options` に非空 JSON object を指定 |
| `sd-api create-embedding`, `create-hypernetwork`, `train-embedding`, `train-hypernetwork` | A1111 の各 POST API へ `--sd-api-options` の非空 JSON object を転送。request fields はサーバー版・設定に依存 |
| `sd-api cmd-flags` | A1111 の command-line flags JSON を取得 |
| `sd-api samplers`, `schedulers`, `upscalers`, `latent-upscale-modes` | A1111 の sampler/scheduler/upscaler 選択肢を JSON で取得 |
| `sd-api sd-models`, `sd-vae`, `hypernetworks`, `face-restorers`, `realesrgan-models`, `prompt-styles`, `embeddings` | A1111 のモデル・VAE・hypernetwork・restorer・style・embedding 情報を JSON で取得 |
| `sd-api refresh-embeddings`, `refresh-checkpoints`, `refresh-vae` | A1111 の embedding/checkpoint/VAE 一覧を再読込。body は指定しない |
| `sd-api memory`, `scripts`, `script-info`, `extensions` | A1111 の memory、txt2img/img2img script 名、script API metadata、extension metadata を取得 |
| `sd-api unload-checkpoint`, `reload-checkpoint` | A1111 の現在の model weights を unload、または current model を device へ reload。body は指定しない |
| `sd-api server-kill`, `server-restart`, `server-stop` | A1111 の server control API。server 起動時に `--api-server-stop` が必要。body は指定しない |
| `generate --backend sb` | Style-Bert-VITS2 との音声生成 |
| `generate --backend cu` | ComfyUI の prompt API への送信、画像アップロード |
| `comfy queue` | ComfyUI の `GET /queue` で実行中・待機中の queue を取得 |
| `comfy queue-update` | ComfyUI の `POST /queue` で pending queue を clear または prompt ID 指定で削除 |
| `comfy history` | ComfyUI の `GET /history` で履歴を取得。`--cu-api-options` で `max_items` / `offset` query を指定 |
| `comfy history-item` | ComfyUI の `GET /history/{prompt_id}` で指定 prompt の履歴を取得 |
| `comfy history-update` | ComfyUI の `POST /history` で履歴全消去または prompt ID 指定削除 |
| `comfy interrupt` | ComfyUI の `POST /interrupt` で実行中処理を停止 |
| `comfy free` | ComfyUI の `POST /free` で指定モデルを unload、memory を解放 |
| `comfy events` | ComfyUI の WebSocket `/ws` からイベントを受信。text frame は JSON のまま、binary frame は Base64 JSON envelope で出力 |
| `comfy object-info` | ComfyUI の `GET /object_info` または `GET /object_info/{node_class}` で node schema を取得 |
| `comfy models` | ComfyUI の `GET /models` で model folder type を取得 |
| `comfy models-folder` | ComfyUI の `GET /models/{folder}` で指定 folder の model list を取得 |
| `comfy embeddings` | ComfyUI の `GET /embeddings` で embedding 一覧を取得 |
| `comfy extensions` | ComfyUI の `GET /extensions` で web extension 一覧を取得 |
| `comfy features` | ComfyUI の `GET /features` で server capability を取得 |
| `comfy upload-image` | ComfyUI の `POST /upload/image` へ multipart で画像をアップロード |
| `comfy upload-mask` | ComfyUI の `POST /upload/mask` へ元画像参照付き multipart mask をアップロード |
| `comfy view` | ComfyUI の `GET /view` から画像 bytes を取得して標準出力へ出力 |
| `comfy view-metadata` | ComfyUI の `GET /view_metadata/{folder_name}` から safetensors metadata を取得 |
| `comfy system-stats` | ComfyUI の `GET /system_stats` から system/device 状態を取得 |
| `comfy default-workflow` | ComfyUI の `GET /default_workflow` から既定 workflow JSON を取得 |
| `comfy prompt-status` | ComfyUI の `GET /prompt` から現在の queue status を取得 |
| `comfy workflow-templates` | ComfyUI の `GET /workflow_templates` から custom node workflow template 一覧を取得 |
| `comfy template-file` | ComfyUI の `/api/workflow_templates/{module}/...` から `--cu-template-path` の template file を取得 |
| `comfy i18n` | ComfyUI の `GET /i18n` から custom node 翻訳を取得 |
| `comfy web-ui` | ComfyUI の `GET /` から frontend HTML を標準出力へ出力 |
| `comfy users` | ComfyUI の `GET /users` で storage 状態・ユーザー情報を取得。response は multi-user 設定により異なる |
| `comfy user-create` | ComfyUI の `POST /users` で `--cu-api-options` の username を使って user を作成 |
| `comfy userdata-list` | ComfyUI の `GET /userdata` で `dir` 配下のファイル一覧を取得 |
| `comfy userdata-list-v2` | ComfyUI の `GET /v2/userdata` で directory/file の一覧を取得 |
| `comfy userdata-get` | ComfyUI の `GET /userdata/{file}` から raw file bytes を取得し標準出力へ出力 |
| `comfy userdata-put` | ComfyUI の `POST /userdata/{file}` へ `--cu-upload-file` の raw bytes を送信 |
| `comfy userdata-delete` | ComfyUI の `DELETE /userdata/{file}` でファイルを削除 |
| `comfy userdata-move` | ComfyUI の `POST /userdata/{file}/move/{dest}` でファイルを移動 |
| `comfy jobs` | ComfyUI の `GET /api/jobs` で job 一覧を取得。`--cu-api-options` に status/workflow_id/sort_by/sort_order/limit/offset を指定 |
| `comfy job` | ComfyUI の `GET /api/jobs/{job_id}` で `--cu-prompt-id` の job を取得 |
| `comfy job-cancel` | ComfyUI の `POST /api/jobs/{job_id}/cancel` で `--cu-prompt-id` の job を取消 |
| `comfy jobs-cancel` | ComfyUI の `POST /api/jobs/cancel` で `--cu-api-options` の `job_ids` 配列にある job を取消 |
| `models list --backend lmstudio|llama-cpp|vllm|ollama|tg` | 指定した LLM server のモデル一覧 API を呼び出し、JSON を標準出力へ出力。TG は admin key を使う |
| `models openai-list --backend tg` | TG の OpenAI 互換 `/v1/models` を API key で呼び出し、model list schema を検証して JSON を出力 |
| `models show --backend ollama` | Ollama の `/api/show` で指定モデルの詳細を取得して JSON を出力 |
| `models running --backend ollama` | Ollama の `/api/ps` で稼働中モデル一覧を取得して JSON を出力 |
| `models version --backend ollama` | Ollama の `/api/version` でサーバーバージョンを取得して JSON を出力 |
| `models info --backend tg` | Text Generation Web UI `/v1/internal/model/info` から現在のモデル名と LoRA 名を取得し、JSON を出力 |
| `models info|model|max-length|max-context-length|soft-prompt|soft-prompts-list|version|true-max-context-length|performance|preloadstory --backend kc` | KoboldCpp のモデル・設定・バージョン・性能情報・preloaded story を取得して出力 |
| `models load --backend tg` | Text Generation Web UI `/v1/internal/model/load` へ `model_name` と任意の loader/template field を送り、`"OK"` を受け取る |
| `models unload --backend tg` | Text Generation Web UI `/v1/internal/model/unload` を呼び、`"OK"` を受け取る |
| `models create|copy|delete|pull|push --backend ollama` | Ollama の model lifecycle API を呼び、JSON/NDJSON 応答を標準出力へ出力 |
| `models load|unload|download|download-status --backend lmstudio` | LM Studio native REST のモデル管理・download status API を呼び、JSON 応答を標準出力へ出力 |
| `responses create --backend lmstudio` | LM Studio `POST /v1/responses` へ model/input を含む JSON を送り、通常は response object、`stream:true` では受信 SSE bytes を逐次標準出力へ出力 |
| `lmstudio-chat create --backend lmstudio` | LM Studio native `POST /api/v1/chat` へ model/input を含む JSON を送り、通常は chat response object、`stream:true` では named SSE events を逐次標準出力へ出力 |
| `decisions create --backend lmstudio` | LM Studio `POST /v1/decisions` へ model/input/questions JSON を送り、answers を含む response JSON を標準出力へ出力 |
| `messages create --backend tg|lmstudio` | Anthropic `POST /v1/messages` へ Messages JSON を送り、message JSON または `stream:true` の SSE events を標準出力へ出力。既定 port は TG 5000、LM Studio 1234。TG は model 省略時にロード中の model を使い、LM Studio では model が必須。API key は `x-api-key` header で送信 |
| `systemone create --backend lmstudio` | LM Studio Jev-compatible `POST /v1/systemone` へ model/state/questions JSON を送り、question ID 対応の answers JSON を標準出力へ出力 |
| `batch-chat create --backend vllm` | vLLM `POST /v1/chat/completions/batch` へ複数 conversation を送り、conversation ごとの choices JSON を標準出力へ出力 |
| `moderations create --backend tg` | Text Generation Web UI `POST /v1/moderations` へ input を送り、moderation results JSON を標準出力へ出力 |
| `health check --backend tg` | Text Generation Web UI `GET /v1/internal/health` を呼び、health JSON を標準出力へ出力 |
| `generation stop --backend tg` | Text Generation Web UI `POST /v1/internal/stop-generation` を呼び、JSON string `"OK"` を受け取る |
| `logits get --backend tg` | Text Generation Web UI `POST /v1/internal/logits` へ prompt/options を送り、logits JSON を出力 |
| `chat-prompt create --backend tg` | Text Generation Web UI `POST /v1/internal/chat-prompt` へ messages/options を送り、整形済み prompt JSON を出力 |
| `loras list --backend tg` | Text Generation Web UI の admin API `GET /v1/internal/lora/list` から LoRA 名一覧を取得 |
| `loras load --backend tg` | admin API `POST /v1/internal/lora/load` に `lora_names` 配列を送り、`"OK"` を受け取る |
| `loras unload --backend tg` | admin API `POST /v1/internal/lora/unload` を呼び、`"OK"` を受け取る |
| `backend-api request|stream --backend <tg|kc|lmstudio|llama-cpp|vllm|ollama>` | 任意 backend HTTP route へ method/path と JSON または raw body、Content-Type、追加 header を送る。`request` は応答全体、`stream` は受信 chunk を標準出力へ逐次出力。KC は websearch、multiplayer、server-side data API もこの経路で利用できる |
| `embeddings create --backend tg|lmstudio|llama-cpp|vllm|ollama` | 指定した LLM server の embeddings API を呼び出し、応答 JSON を標準出力へ出力 |
| `tokenize encode --backend llama-cpp` | llama.cpp の `/tokenize` へテキストと tokenizer option を送り、token JSON を標準出力へ出力 |
| `tokenize decode --backend llama-cpp` | llama.cpp の `/detokenize` へ token ID を送り、テキスト JSON を標準出力へ出力 |
| `tokenize encode --backend tg` | Text Generation Web UI の `/v1/internal/encode` へ text を送り、token ID と長さの JSON を標準出力へ出力 |
| `tokenize decode --backend tg` | Text Generation Web UI の `/v1/internal/decode` へ token ID を送り、text の JSON を標準出力へ出力 |
| `tokenize encode|decode --backend kc` | KoboldCpp の `/api/extra/tokenize` または `/api/extra/detokenize` へ prompt / token ID を送り、応答 JSON を出力 |
| `grammar convert --backend kc` | `schema` object を KoboldCpp `/api/extra/json_to_grammar` へ送り、生成 grammar を JSON で出力 |
| `generation stream --backend kc` | generation JSON を `/api/extra/generate/stream` へ送り、SSE event を逐次標準出力へ出力 |
| `generation check|abort|logprobs --backend kc` | generation check/abort または `/api/extra/last_logprobs` を呼び出す。check/logprobs は `--llm-api-options` に `genkey` を指定すると対象 request を選択する |
| `server start` | サーバープロセスを起動し、指定 TCP ポートへの接続を待機 |
| `server stop` | 実行ファイル名またはフルパスに一致するプロセスを終了 |
| `png extract` | PNG ファイルから生成パラメーターを抽出して標準出力へ出力 |

テキスト生成バックエンドでは `--llm-mode completions`（既定）または `chat-completions` を選べます。Completions はプロンプト文字列を API に送り、トークン数を確認しながら複数回に分けて生成を継続できます。Text Generation Web UI は `/v1/internal/token-count` に `{text}` を送り、`length` を読みます。OpenAI-compatible backends は標準 completions/chat request JSON を使い、token count を `usage.prompt_tokens` から読みます。Ollama は native generate/chat JSON を使い、token count は `prompt_eval_count` から読みます。Chat Completions は会話 JSON を保存します。画像入力は既存の chat message 形式で送信します。専用 adapter のない HTTP operation は `backend-api request` で呼び出せます。SSE 等の HTTP streaming response は `backend-api stream` で raw chunk を逐次転送します。対応 route と request field は [バックエンド API 対応台帳](BACKEND_WEB_API_COVERAGE.md) を参照してください。WebSocket は別途対応します。

### 共通オプション

| オプション | 実装上の既定値・動作 |
| --- | --- |
| `--help`, `-h` | ヘルプを表示して終了 |
| `generate --backend` | `tg`, `kc`, `lmstudio`, `llama-cpp`, `vllm`, `ollama`, `sd`, `sb`, `cu` のいずれか。生成コマンドで必須 |
| `--path-base` | `.`。相対パスの基準 |
| `--logging-level` | `info`。`trace`, `debug`, `info`, `warning`, `error`, `fatal` |
| `--logging-file` | `log`。空文字ならファイルログなし |
| `--config` | `config.ini` |
| `--logging-console` | 既定 `false`。true の場合はログをコンソールにも出力。false の場合、生成結果を標準出力へ出力 |
| `--iteration-count` | `1`。`-1` は無限反復 |
| `--variable` | `key=value` 形式のマクロ変数を複数指定 |
| `--iteration-phases` | 既定は空文字列 1 件。複数指定可 |
| `--random-seed` | `-1` は反復ごとに乱数で seed を決定 |
| `--network-connect-timeout` | 接続確立の上限秒数。既定 `10` |
| `--network-request-timeout` | 要求送信から応答受信完了までの上限秒数。既定 `0`（タイムアウトなし） |

設定ファイルは INI 形式です。コマンドラインで指定した値が設定ファイルの値より優先されます。起動後に `--variable` の値をマクロ変数として登録します。

### 実装済みコマンドラインオプション一覧

以下の表は `Hermit/command_line.cpp` の `command_line::make_options_description()` に登録されたオプションから作成しています。短縮形の `—` は短縮形がありません。共通オプション表は実装上の既定値を記載し、バックエンド別の表は送信する API フィールドとその用途を示します。全項目の現在の既定値は `Hermit --help` でも確認できます。

#### 共通・制御

| オプション名 | 省略形 | デフォルト値 | 機能 |
| --- | --- | --- | --- |
| `--help` | `-h` | — | ヘルプを表示する。 |
| `--backend` | — | — | `generate`、`models`、`lmstudio-chat create`、`responses create`、`decisions create`、`messages create`、`systemone create`、`batch-chat create`、`moderations create`、`health check`、`embeddings create`、`tokenize` の backend を選択する。 |
| `--path-base` | — | `.` | 相対パスの基準ディレクトリを指定する。 |
| `--logging-level` | — | `info` | 記録する最低ログレベルを指定する。 |
| `--logging-file` | — | `log` | ログファイルを指定する。 |
| `--config` | — | `config.ini` | 設定ファイルを指定する。 |
| `--logging-console` | — | `false` | ログのコンソール出力を有効にする。 |
| `--security-allow-process-exec` | — | `false` | `exec(...)` マクロから子プロセスを起動する機能を明示的に許可する。 |
| `--iteration-count` | — | `1` | 処理の反復回数を指定する。 |
| `--variable` | — | — | マクロ変数を key=value 形式で定義する。 |
| `--iteration-phases` | — | 空文字列 1 件 | 実行する phase の値を指定する。 |
| `--random-seed` | — | `-1` | 乱数 seed を指定する。 |
| `--network-connect-timeout` | — | `10` | 接続確立のタイムアウト秒数を指定する。 |
| `--network-request-timeout` | — | `0` | 要求送信から応答完了までのタイムアウト秒数を指定する。 |

#### PNG / サーバープロセス

| オプション名 | 省略形 | 機能 |
| --- | --- | --- |
| `--png-file` | — | パラメーター抽出対象の PNG ファイルを指定する。 |
| `--process-executable` | — | 起動・終了するサーバー実行ファイルを指定する。 |
| `--process-arguments` | — | サーバー起動時の引数を指定する。 |
| `--process-host` | — | 起動後の接続待機先ホストを指定する。 |
| `--process-port` | — | 起動後の接続待機先ポートを指定する。 |
| `--process-max-retries` | — | 接続待機の最大試行回数を指定する。 |
| `--process-wait-ms` | — | 接続試行間の待ち時間をミリ秒で指定する。 |

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
| `--llm-admin-key` | — | Text Generation Web UI の admin route 認証に使う key を指定する。TG の `models list` は未指定時に `--llm-api-key` を使う。 |
| `--llm-api-method` | `GET` | `backend-api request|stream` の HTTP method。GET/POST/PUT/PATCH/DELETE/HEAD/OPTIONS を指定する。 |
| `--llm-api-path` | `/` | `backend-api request|stream` の `/` 始まりの route path。authority、fragment、traversal、空白を含められない。 |
| `--llm-api-options` | — | `backend-api request|stream` や endpoint 固有コマンドの JSON body。 |
| `--llm-api-body-file` | — | `backend-api request|stream` の raw/binary body file。`--llm-api-options` と併用できない。 |
| `--llm-api-content-type` | — | `backend-api request|stream` の body Content-Type。multipart では boundary を含める。 |
| `--llm-api-headers` | — | `backend-api request|stream` の追加 HTTP header を JSON string map で指定する。Host、Content-Length、Transfer-Encoding、Connection、Content-Type は指定できない。 |
| `--llm-model` | — | OpenAI-compatible または Ollama backend の model ID/name を指定する。 |
| `--llm-completions-target` | — | Completions API のパスを指定する。 |
| `--llm-token-count-target` | — | トークン数 API のパスを指定する。 |
| `--llm-chat-completions-target` | — | Chat Completions API のパスを指定する。 |
| `--llm-models-target` | — | `models list` で呼び出すモデル一覧 API のパスを指定する。 |
| `--llm-embedding-target` | — | `embeddings create` で呼び出す embeddings API のパスを指定する。 |
| `--llm-embedding-input` | — | 埋め込み対象のテキストを複数指定する。省略時は `--llm-prompt` を使う。 |
| `--llm-embedding-options` | — | backend 固有の追加 request field を JSON object で指定する。`input` と `model` は指定できない。 |
| `--llm-tokenize-target` | — | llama.cpp `/tokenize` の接続先 path を上書きする。 |
| `--llm-detokenize-target` | — | llama.cpp `/detokenize` の接続先 path を上書きする。 |
| `--llm-tokenize-options` | — | `/tokenize` に送る `add_special`、`parse_special`、`with_pieces` などを JSON object で指定する。 |
| `--llm-token-ids` | — | `tokenize decode` に渡す整数 token ID を複数指定する。 |
| `--llm-json-to-grammar-options` | — | KoboldCpp `grammar convert` に渡す `schema` object を含む JSON request body を指定する。 |
| `--llm-running-models-target` | — | Ollama の稼働中モデル一覧 path を上書きする。 |
| `--llm-model-info-target` | — | Ollama のモデル詳細 API path を上書きする。 |
| `--llm-model-info-options` | — | `/api/show` の追加 field を JSON object で指定する。`model` は予約 field。 |
| `--llm-model-action-target` | — | Ollama pull/push API の path を上書きする。 |
| `--llm-model-action-options` | — | Ollama model lifecycle、LM Studio load/unload/download、TG model load の request body を JSON object で指定する。LM Studio download-status は `job_id` のみを指定する。 |
| `--llm-min-completion-tokens` | — | 継続生成を終了する最小追加トークン数を指定する。 |
| `--llm-max-completion-iterations` | — | Completions の最大継続回数を指定する。 |
| `--llm-reasoning-prefix` | — | 除去対象 reasoning の開始文字列を指定する。 |
| `--llm-reasoning-suffix` | — | 除去対象 reasoning の終了文字列を指定する。 |
| `--llm-code-block-extract` | — | コードブロック抽出とファイル名のパストラバーサル防止を有効にする。 |
| `--llm-mode` | — | Completions または Chat Completions を選択する。 |

#### Text-Generation-webui (`tg`)

| オプション名 | 省略形 | 機能 |
| --- | --- | --- |
| `--tg-model` | — | バックエンド設定の「model」に対応する値を指定する。 |
| `--tg-num-best-of` | — | バックエンド設定の「best of」に対応する値を指定する。 |
| `--tg-echo` | — | バックエンド設定の「echo」に対応する値を指定する。 |
| `--tg-frequency-penalty` | — | バックエンド設定の「frequency penalty」に対応する値を指定する。 |
| `--tg-logprobs` | — | API に送る log probabilities の設定値を指定する。 |
| `--tg-max-tokens` | — | バックエンド設定の「max tokens」に対応する値を指定する。 |
| `--tg-n` | — | バックエンド設定の「number of responses generated for the same prompt」に対応する値を指定する。 |
| `--tg-presence-penalty` | — | バックエンド設定の「presence penalty」に対応する値を指定する。 |
| `--tg-stop` | — | バックエンド設定の「stop sequences」に対応する値を指定する。 |
| `--tg-stream` | — | バックエンド設定の「stream」に対応する値を指定する。 |
| `--tg-suffix` | — | バックエンド設定の「suffix」に対応する値を指定する。 |
| `--tg-temperature` | — | バックエンド設定の「temperature」に対応する値を指定する。 |
| `--tg-top-p` | — | バックエンド設定の「top p」に対応する値を指定する。 |
| `--tg-dynatemp-low` | — | バックエンド設定の「dynatemp low」に対応する値を指定する。 |
| `--tg-dynatemp-high` | — | バックエンド設定の「dynatemp high」に対応する値を指定する。 |
| `--tg-dynatemp-exponent` | — | バックエンド設定の「dynatemp exponent」に対応する値を指定する。 |
| `--tg-smoothing-factor` | — | バックエンド設定の「smoothing factor」に対応する値を指定する。 |
| `--tg-smoothing-curve` | — | バックエンド設定の「smoothing curve」に対応する値を指定する。 |
| `--tg-min-p` | — | バックエンド設定の「min p」に対応する値を指定する。 |
| `--tg-top-k` | — | バックエンド設定の「top k」に対応する値を指定する。 |
| `--tg-typical-p` | — | バックエンド設定の「typical p」に対応する値を指定する。 |
| `--tg-xtc-threshold` | — | バックエンド設定の「Exclude Top Choices (XTC) threshold」に対応する値を指定する。 |
| `--tg-xtc-probability` | — | バックエンド設定の「Exclude Top Choices (XTC) probability」に対応する値を指定する。 |
| `--tg-epsilon-cutoff` | — | バックエンド設定の「epsilon cutoff」に対応する値を指定する。 |
| `--tg-eta-cutoff` | — | バックエンド設定の「eta cutoff」に対応する値を指定する。 |
| `--tg-tfs` | — | バックエンド設定の「tfs」に対応する値を指定する。 |
| `--tg-top-a` | — | バックエンド設定の「top a」に対応する値を指定する。 |
| `--tg-top-n-sigma` | — | バックエンド設定の「top n sigma」に対応する値を指定する。 |
| `--tg-dry-multiplier` | — | バックエンド設定の「DRY multiplier」に対応する値を指定する。 |
| `--tg-dry-allowed-length` | — | バックエンド設定の「DRY allowed length」に対応する値を指定する。 |
| `--tg-dry-base` | — | バックエンド設定の「DRY base」に対応する値を指定する。 |
| `--tg-repetition-penalty` | — | バックエンド設定の「repetition penalty」に対応する値を指定する。 |
| `--tg-encoder-repetition-penalty` | — | バックエンド設定の「encoder repetition penalty」に対応する値を指定する。 |
| `--tg-no-repeat-ngram-size` | — | バックエンド設定の「no repeat ngram size」に対応する値を指定する。 |
| `--tg-repetition-penalty-range` | — | バックエンド設定の「repetition penalty range」に対応する値を指定する。 |
| `--tg-penalty-alpha` | — | バックエンド設定の「penalty alpha」に対応する値を指定する。 |
| `--tg-guidance-scale` | — | バックエンド設定の「guidance scale」に対応する値を指定する。 |
| `--tg-mirostat-mode` | — | バックエンド設定の「mirostat mode」に対応する値を指定する。 |
| `--tg-mirostat-tau` | — | バックエンド設定の「mirostat tau」に対応する値を指定する。 |
| `--tg-mirostat-eta` | — | バックエンド設定の「mirostat eta」に対応する値を指定する。 |
| `--tg-prompt-lookup-num-tokens` | — | バックエンド設定の「prompt lookup num tokens」に対応する値を指定する。 |
| `--tg-max-tokens-second` | — | バックエンド設定の「max tokens second」に対応する値を指定する。 |
| `--tg-do-sample` | — | バックエンド設定の「do sample」に対応する値を指定する。 |
| `--tg-dynamic-temperature` | — | バックエンド設定の「dynamic temperature」に対応する値を指定する。 |
| `--tg-temperature-last` | — | バックエンド設定の「temperature last」に対応する値を指定する。 |
| `--tg-auto-max-new-tokens` | — | バックエンド設定の「auto max_new tokens」に対応する値を指定する。 |
| `--tg-ban-eos-token` | — | バックエンド設定の「ban eos token」に対応する値を指定する。 |
| `--tg-add-bos-token` | — | バックエンド設定の「add Beginning of Sequence Token (BOS) token」に対応する値を指定する。 |
| `--tg-skip-special-tokens` | — | バックエンド設定の「skip special tokens (bos_token, eos_token, unk_token, pad_token, etc.)」に対応する値を指定する。 |
| `--tg-static-cache` | — | バックエンド設定の「static cache」に対応する値を指定する。 |
| `--tg-truncation-length` | — | バックエンド設定の「truncation length」に対応する値を指定する。 |
| `--tg-sampler-priority` | — | バックエンド設定の「sampler priority」に対応する値を指定する。 |
| `--tg-custom-token-bans` | — | バックエンド設定の「custom token bans」に対応する値を指定する。 |
| `--tg-negative-prompt` | — | バックエンド設定の「negative prompt」に対応する値を指定する。 |
| `--tg-dry-sequence-breakers` | — | バックエンド設定の「dry sequence breakers」に対応する値を指定する。 |
| `--tg-grammar-string` | — | バックエンド設定の「grammar-string」に対応する値を指定する。 |

#### KoboldCpp (`kc`)

| オプション名 | 省略形 | 機能 |
| --- | --- | --- |
| `--kc-max-context-length` | — | バックエンド設定の「Maximum number of tokens to send to the model. (minimum: 1)」に対応する値を指定する。 |
| `--kc-max-length` | — | バックエンド設定の「Number of tokens to generate. (minimum: 1)」に対応する値を指定する。 |
| `--kc-rep-pen` | — | バックエンド設定の「Base repetition penalty value. (minimum: 1.0)」に対応する値を指定する。 |
| `--kc-rep-pen-range` | — | バックエンド設定の「Repetition penalty range. (minimum: 0)」に対応する値を指定する。 |
| `--kc-sampler-order` | — | バックエンド設定の「Sampler order to be used. If N is the length of this array, then N must be greater than or equal to 6 and the array must be a permutation of the first N non-negative integers.」に対応する値を指定する。 |
| `--kc-sampler-seed` | — | バックエンド設定の「RNG seed to use for sampling. If not specified, the global RNG will be used. (minimum: 1, maximum: 999999)」に対応する値を指定する。 |
| `--kc-stop-sequence` | — | バックエンド設定の「An array of string sequences where the API will stop generating further tokens. The returned text WILL contain the stop sequence if trim_stop is false.」に対応する値を指定する。 |
| `--kc-temperature` | — | バックエンド設定の「Temperature value.」に対応する値を指定する。 |
| `--kc-tfs` | — | バックエンド設定の「Tail free sampling value. (minimum: 0.0, maximum: 1.0)」に対応する値を指定する。 |
| `--kc-top-a` | — | バックエンド設定の「Top-a sampling value. (minimum: 0.0)」に対応する値を指定する。 |
| `--kc-top-k` | — | バックエンド設定の「Top-k sampling value. (minimum: 0.0)」に対応する値を指定する。 |
| `--kc-top-p` | — | バックエンド設定の「Top-p sampling value. (minimum: 0.0, maximum: 1.0)」に対応する値を指定する。 |
| `--kc-min-p` | — | バックエンド設定の「Min-p sampling value. (minimum: 0.0, maximum: 1.0)」に対応する値を指定する。 |
| `--kc-typical` | — | バックエンド設定の「Typical sampling value. (minimum: 0.0, maximum: 1.0)」に対応する値を指定する。 |
| `--kc-use-default-badwordsids` | — | バックエンド設定の「If true, prevents the EOS token from being generated (Ban EOS).」に対応する値を指定する。 |
| `--kc-dynatemp_range` | — | バックエンド設定の「If not equal to 0, uses dynamic temperature. Dynamic temperature range will be between Temp+Range and Temp-Range. If equal to 0 , uses static temperature. (default: 0, minimum: -5.0, maximum: 5.0)」に対応する値を指定する。 |
| `--kc-smoothing-factor` | — | バックエンド設定の「Modifies temperature behavior. If greater than 0 uses smoothing factor. (default: 0.0, minimum: 0.0)」に対応する値を指定する。 |
| `--kc-dynatemp-exponent` | — | バックエンド設定の「Exponent used in dynatemp. (default: 0.0)」に対応する値を指定する。 |
| `--kc-mirostat` | — | バックエンド設定の「KoboldCpp ONLY. Sets the mirostat mode, 0=disabled, 1=mirostat_v1, 2=mirostat_v2. (minimum: 0, maximum: 2)」に対応する値を指定する。 |
| `--kc-mirostat-tau` | — | バックエンド設定の「KoboldCpp ONLY. Mirostat tau value. (minimum: 0.0)」に対応する値を指定する。 |
| `--kc-mirostat-eta` | — | バックエンド設定の「KoboldCpp ONLY. Mirostat eta value. (minimum: 0.0)」に対応する値を指定する。 |
| `--kc-genkey` | — | バックエンド設定の「KoboldCpp ONLY. A unique genkey set by the user. When checking a polled-streaming request, use this key to be able to fetch pending text even if multiuser is enabled.」に対応する値を指定する。 |
| `--kc-grammar` | — | バックエンド設定の「KoboldCpp ONLY. A string containing the GBNF grammar to use.」に対応する値を指定する。 |
| `--kc-grammar-retain-state` | — | バックエンド設定の「KoboldCpp ONLY. If true, retains the previous generation's grammar state, otherwise it is reset on new generation.」に対応する値を指定する。 |
| `--kc-memory` | — | バックエンド設定の「KoboldCpp ONLY. If set, forcefully appends this string to the beginning of any submitted prompt text. If resulting context exceeds the limit, forcefully overwrites text from the beginning of the main prompt until it can fit. Useful to guarantee full memory insertion even when you cannot determine exact token count.」に対応する値を指定する。 |
| `--kc-images` | — | バックエンド設定の「KoboldCpp ONLY. If set, takes an array of base64 encoded strings, each one representing an image to be processed.」に対応する値を指定する。 |
| `--kc-trim-stop` | — | バックエンド設定の「KoboldCpp ONLY. If true, also removes detected stop_sequences from the output and truncates all text after them. If false, output will also include stop sequence and potentially a few additional characters.」に対応する値を指定する。 |
| `--kc-render-special` | — | バックエンド設定の「KoboldCpp ONLY. If true, prints special tokens as text for GGUF models」に対応する値を指定する。 |
| `--kc-bypass-eos` | — | バックエンド設定の「KoboldCpp ONLY. If true, allows EOS token to be generated, but does not stop generation. Not recommended unless you know what you are doing.」に対応する値を指定する。 |
| `--kc-banned-tokens` | — | バックエンド設定の「An array of string sequences, each entry represents a word or phrase prevented from being generated, either modifying model vocab or by backtracking and regenerating when they appear.」に対応する値を指定する。 |
| `--kc-dry-multiplier` | — | バックエンド設定の「KoboldCpp ONLY. DRY multiplier value, 0 to disable. (minimum: 0)」に対応する値を指定する。 |
| `--kc-dry-base` | — | バックエンド設定の「KoboldCpp ONLY. DRY base value. (minimum: 0)」に対応する値を指定する。 |
| `--kc-dry-allowed-length` | — | バックエンド設定の「KoboldCpp ONLY. DRY allowed length value. (minimum: 0)」に対応する値を指定する。 |
| `--kc-dry-penalty-last-n` | — | バックエンド設定の「KoboldCpp ONLY. DRY last n tokens penalized value. (minimum: 0)」に対応する値を指定する。 |
| `--kc-dry-sequence-breakers` | — | バックエンド設定の「An array of string sequence breakers for DRY.」に対応する値を指定する。 |
| `--kc-xtc-threshold` | — | バックエンド設定の「KoboldCpp ONLY. XTC threshold. (minimum: 0)」に対応する値を指定する。 |
| `--kc-xtc-probability` | — | バックエンド設定の「KoboldCpp ONLY. XTC probability. Set to above 0 to enable XTC. (minimum: 0)」に対応する値を指定する。 |
| `--kc-nsigma` | — | バックエンド設定の「KoboldCpp ONLY. Top N-Sigma value. Set to above 0 to enable nsigma. (minimum: 0)」に対応する値を指定する。 |
| `--kc-logprobs` | — | バックエンド設定の「If true, return up to 5 top logprobs for generated tokens. Incurs performance overhead.」に対応する値を指定する。 |
| `--kc-replace-instruct-placeholders` | — | バックエンド設定の「If true, replaces instruct placeholders {{[INPUT]}} and {{[OUTPUT]}} with backend selected instruct tags.」に対応する値を指定する。 |

#### Text Generation Web UI / LM Studio / llama.cpp / vLLM / Ollama

TG (`tg`) は既定 port 5000 の OpenAI-compatible API を使います。生成時は既定 stop sequence を送らず、`--tg-stop` が明示された場合だけ stop 値を送ります。token count は `/v1/completions` に `max_tokens: 0` と `echo: true` を付けて問い合わせ、`usage.prompt_tokens` を読みます。TG の `embeddings create` は `/v1/embeddings` に入力と `encoding_format` を送り、モデルはサーバー起動時の `OPENEDAI_EMBEDDING_MODEL` / `OPENEDAI_EMBEDDING_DEVICE` で選択するため `--llm-model` は不要です。

LM Studio (`lmstudio`)、llama.cpp server (`llama-cpp`)、vLLM (`vllm`) は OpenAI 互換 Completions / Chat Completions / Embeddings API を利用できます。既定 port はそれぞれ 1234 / 8080 / 8000 です。`--llm-model` にサーバーが返す model ID を指定してください。TG の既定 stop sequence は送信せず、`--tg-stop` が明示された場合にだけ stop 値を送ります。token count は `/v1/completions` に `max_tokens: 0` と `echo: true` を付けて問い合わせ、`usage.prompt_tokens` を読みます。`embeddings create` は `/v1/embeddings` に `--llm-embedding-input` のテキスト配列（省略時は `--llm-prompt`）を送り、返された JSON を標準出力へ出します。`--llm-embedding-options` は encoding format や server 固有 field を JSON object で指定します。対象サーバーで embedding 対応モデルを読み込んでください。

Ollama (`ollama`) は native `/api/generate`、`/api/chat`、`/api/embed` を使い、既定 port は 11434 です。`--llm-model` に model name/tag を指定します。token count は `/api/generate` を `stream: false` と `num_predict: 0` で呼び、`prompt_eval_count` を読みます。`embeddings create` は `/api/embed` に入力テキスト配列を送り、返された JSON を標準出力へ出します。`--llm-embedding-options` で `truncate`、`dimensions`、`keep_alive`、`options` などの native field を指定できます。各 backend の request/response schema は `test/test_llm_backend.cpp` の固定 JSON 契約で検証します。vLLM Responses lifecycle、Ollama の一部管理 API などは未対応で、各 endpoint は [対応台帳](BACKEND_WEB_API_COVERAGE.md) の課題として追跡します。Anthropic Messages compatibility は `messages create --backend tg|lmstudio` で JSON/SSE を送信します。Jev System One は `systemone create --backend lmstudio` で question ID 対応 JSON を送信します。

#### Stable Diffusion (`sd`)

| オプション名 | 省略形 | 機能 |
| --- | --- | --- |
| `--sd-host` | — | バックエンド設定の「host」に対応する値を指定する。 |
| `--sd-port` | — | バックエンド設定の「port」に対応する値を指定する。 |
| `--sd-api-options` | — | `sd-api progress` の query option、`internal-progress` の body、または `sd-api` の画像・interrogate・options・create/train request fields を JSON object で指定する。汎用 `request` は JSON object/array body を指定する。create/train fields は WebUI の版・設定に応じて指定する。 |
| `--sd-api-method` | `GET` | `sd-api request|stream` の HTTP method。GET/POST/PUT/PATCH/DELETE/HEAD/OPTIONS を指定する。 |
| `--sd-api-path` | `/openapi.json` | `sd-api request|stream` の `/` 始まりの path。authority、fragment、traversal、空白および制御文字を含められない。 |
| `--sd-api-body-file` | — | `sd-api request|stream` の raw/binary body file。`--sd-api-options` とは併用できない。相対 path は設定 base path から解決する。 |
| `--sd-api-content-type` | — | `sd-api request|stream` body の Content-Type。multipart/form-data では boundary を含める。 |
| `--sd-api-headers` | — | `sd-api request|stream` の追加 HTTP headers を JSON string map で指定する。Host、Content-Length、Transfer-Encoding、Connection、Content-Type は指定できない。 |
| `--sd-generation-options` | — | `generate --backend sd` の txt2img/img2img request に追加する server-specific JSON object。既存 field を含む場合は設定値を上書きする。 |
| `--sd-prompt-file` | — | バックエンド設定の「prompt file」に対応する値を指定する。 |
| `--sd-negative-prompt-file` | — | バックエンド設定の「negative prompt file」に対応する値を指定する。 |
| `--sd-output-file` | — | バックエンド設定の「output PNG file」に対応する値を指定する。 |
| `--sd-prompt` | — | バックエンド設定の「prompt」に対応する値を指定する。 |
| `--sd-negative-prompt` | — | バックエンド設定の「negative prompt」に対応する値を指定する。 |
| `--sd-styles` | — | バックエンド設定の「styles」に対応する値を指定する。 |
| `--sd-seed` | — | バックエンド設定の「seed」に対応する値を指定する。 |
| `--sd-subseed` | — | バックエンド設定の「subseed」に対応する値を指定する。 |
| `--sd-subseed-strength` | — | バックエンド設定の「subseed strength」に対応する値を指定する。 |
| `--sd-seed-resize-from-h` | — | バックエンド設定の「seed resize from height」に対応する値を指定する。 |
| `--sd-seed-resize-from-w` | — | バックエンド設定の「seed resize from width」に対応する値を指定する。 |
| `--sd-sampler-name` | — | バックエンド設定の「sampler name」に対応する値を指定する。 |
| `--sd-scheduler` | — | バックエンド設定の「scheduler」に対応する値を指定する。 |
| `--sd-batch_size` | — | バックエンド設定の「batch size」に対応する値を指定する。 |
| `--sd-n-iter` | — | バックエンド設定の「n iter」に対応する値を指定する。 |
| `--sd-steps` | — | バックエンド設定の「steps」に対応する値を指定する。 |
| `--sd-cfg-scale` | — | バックエンド設定の「cfg scale」に対応する値を指定する。 |
| `--sd-width` | — | バックエンド設定の「image width」に対応する値を指定する。 |
| `--sd-height` | — | バックエンド設定の「image height」に対応する値を指定する。 |
| `--sd-restore-faces` | — | バックエンド設定の「restore faces」に対応する値を指定する。 |
| `--sd-tiling` | — | バックエンド設定の「tiling」に対応する値を指定する。 |
| `--sd-do-not-save-samples` | — | バックエンド設定の「do not save samples」に対応する値を指定する。 |
| `--sd-do-not-save-grid` | — | バックエンド設定の「do not save grid」に対応する値を指定する。 |
| `--sd-eta` | — | バックエンド設定の「eta」に対応する値を指定する。 |
| `--sd-denoising-strength` | — | バックエンド設定の「denoising strength」に対応する値を指定する。 |
| `--sd-s-min-uncond` | — | バックエンド設定の「s min uncond」に対応する値を指定する。 |
| `--sd-s-churn` | — | バックエンド設定の「s churn」に対応する値を指定する。 |
| `--sd-s-tmax` | — | バックエンド設定の「s tmax」に対応する値を指定する。 |
| `--sd-s-tmin` | — | バックエンド設定の「s tmin」に対応する値を指定する。 |
| `--sd-s-noise` | — | バックエンド設定の「s noise」に対応する値を指定する。 |
| `--sd-override-settings` | — | バックエンド設定の「override settings」に対応する値を指定する。 |
| `--sd-override-settings-restore-afterwards` | — | バックエンド設定の「override settings restore afterwards」に対応する値を指定する。 |
| `--sd-refiner-checkpoint` | — | バックエンド設定の「refiner checkpoint」に対応する値を指定する。 |
| `--sd-refiner-switch-at` | — | バックエンド設定の「refiner switch at」に対応する値を指定する。 |
| `--sd-disable-extra-networks` | — | バックエンド設定の「disable extra networks」に対応する値を指定する。 |
| `--sd-comments` | — | バックエンド設定の「comments」に対応する値を指定する。 |
| `--sd-force-task-id` | — | バックエンド設定の「force task id」に対応する値を指定する。 |
| `--sd-sampler-index` | — | バックエンド設定の「sampler index」に対応する値を指定する。 |
| `--sd-script-name` | — | バックエンド設定の「script name」に対応する値を指定する。 |
| `--sd-script-args` | — | バックエンド設定の「script_args」に対応する値を指定する。 |
| `--sd-send-images` | — | バックエンド設定の「send images」に対応する値を指定する。 |
| `--sd-save-images` | — | バックエンド設定の「save images」に対応する値を指定する。 |
| `--sd-ad-enable` | — | バックエンド設定の「ADetailer enable」に対応する値を指定する。 |
| `--sd-ad-model` | — | バックエンド設定の「ADetailer model」に対応する値を指定する。 |
| `--sd-ad-prompt` | — | バックエンド設定の「ADetailer prompt」に対応する値を指定する。 |
| `--sd-ad-negative-prompt` | — | バックエンド設定の「ADetailer negative prompt」に対応する値を指定する。 |
| `--sd-infotext` | — | バックエンド設定の「infotext」に対応する値を指定する。 |
| `--sd-mode` | — | バックエンド設定の「mode (txt2img \\| img2img)」に対応する値を指定する。 |
| `--sd-txt2img-target` | — | バックエンド設定の「txt2img target」に対応する値を指定する。 |
| `--sd-enable-hr` | — | バックエンド設定の「enable hr」に対応する値を指定する。 |
| `--sd-firstpass-image` | — | バックエンド設定の「firstpass image」に対応する値を指定する。 |
| `--sd-firstphase-width` | — | バックエンド設定の「firstphase width」に対応する値を指定する。 |
| `--sd-firstphase-height` | — | バックエンド設定の「firstphase height」に対応する値を指定する。 |
| `--sd-hr-scale` | — | バックエンド設定の「hr scale」に対応する値を指定する。 |
| `--sd-hr-upscaler` | — | バックエンド設定の「hr upscaler」に対応する値を指定する。 |
| `--sd-hr-second-pass-steps` | — | バックエンド設定の「hr second pass steps」に対応する値を指定する。 |
| `--sd-hr-resize-x` | — | バックエンド設定の「hr resize x」に対応する値を指定する。 |
| `--sd-hr-resize-y` | — | バックエンド設定の「hr resize y」に対応する値を指定する。 |
| `--sd-hr-checkpoint-name` | — | バックエンド設定の「hr checkpoint name」に対応する値を指定する。 |
| `--sd-img2img-target` | — | バックエンド設定の「img2img target」に対応する値を指定する。 |
| `--sd-init-images` | — | バックエンド設定の「img2img init_images (Base64 encoded images)」に対応する値を指定する。 |
| `--sd-img2img-seed-resize-from-h` | — | img2img 専用の元画像高さ。`-1` の場合は共通オプション `--sd-seed-resize-from-h` の値を使う。 |
| `--sd-img2img-seed-resize-from-w` | — | img2img 専用の元画像幅。`-1` の場合は共通オプション `--sd-seed-resize-from-w` の値を使う。 |
| `--sd-resize-mode` | — | バックエンド設定の「img2img resize_mode [0 - 3] (0: Just resize, 1: Crop and resize, 2: Resize and fill, 3: Just resize)」に対応する値を指定する。 |
| `--sd-image-cfg-scale` | — | バックエンド設定の「img2img image_cfg_scale」に対応する値を指定する。 |
| `--sd-mask` | — | バックエンド設定の「img2img mask (Base64 encoded image)」に対応する値を指定する。 |
| `--sd-mask-blur-x` | — | バックエンド設定の「img2img mask_blur_x」に対応する値を指定する。 |
| `--sd-mask-blur-y` | — | バックエンド設定の「img2img mask_blur_y」に対応する値を指定する。 |
| `--sd-mask-blur` | — | バックエンド設定の「img2img mask_blur」に対応する値を指定する。 |
| `--sd-mask-round` | — | バックエンド設定の「img2img mask_round」に対応する値を指定する。 |
| `--sd-inpainting-fill` | — | バックエンド設定の「img2img inpainting_fill」に対応する値を指定する。 |
| `--sd-inpaint-full-res` | — | バックエンド設定の「img2img inpaint_full_res」に対応する値を指定する。 |
| `--sd-inpaint-full-res-padding` | — | バックエンド設定の「img2img inpaint_full_res_padding」に対応する値を指定する。 |
| `--sd-inpainting-mask-invert` | — | バックエンド設定の「img2img inpainting_mask_invert」に対応する値を指定する。 |
| `--sd-initial-noise-multiplier` | — | バックエンド設定の「img2img initial_noise_multiplier」に対応する値を指定する。 |
| `--sd-latent-mask` | — | バックエンド設定の「img2img latent_mask (Base64 encoded image)」に対応する値を指定する。 |
| `--sd-abg-remover-enable` | — | バックエンド設定の「ABG Remover enable」に対応する値を指定する。 |

#### Style-Bert-VITS2 (`sb`)

| オプション名 | 省略形 | 機能 |
| --- | --- | --- |
| `--sb-host` | — | バックエンド設定の「host」に対応する値を指定する。 |
| `--sb-port` | — | バックエンド設定の「port」に対応する値を指定する。 |
| `--sb-target` | — | バックエンド設定の「voice endpoint target」に対応する値を指定する。 |
| `--sb-text-file` | — | バックエンド設定の「text file」に対応する値を指定する。 |
| `--sb-output-file` | — | バックエンド設定の「output WAV」に対応する値を指定する。 |
| `--sb-text` | — | バックエンド設定の「text」に対応する値を指定する。 |
| `--sb-model-name` | — | バックエンド設定の「model name」に対応する値を指定する。 |
| `--sb-model-id` | — | バックエンド設定の「model id」に対応する値を指定する。 |
| `--sb-speaker-name` | — | バックエンド設定の「speaker name」に対応する値を指定する。 |
| `--sb-speaker-id` | — | バックエンド設定の「speaker id」に対応する値を指定する。 |
| `--sb-sdp-ratio` | — | バックエンド設定の「sdp ratio」に対応する値を指定する。 |
| `--sb-noise` | — | バックエンド設定の「noise」に対応する値を指定する。 |
| `--sb-noisew` | — | バックエンド設定の「noisew」に対応する値を指定する。 |
| `--sb-length` | — | バックエンド設定の「length」に対応する値を指定する。 |
| `--sb-language` | — | バックエンド設定の「language」に対応する値を指定する。 |
| `--sb-auto-split` | — | バックエンド設定の「auto split」に対応する値を指定する。 |
| `--sb-split-interval` | — | バックエンド設定の「split interval」に対応する値を指定する。 |
| `--sb-assist-text` | — | バックエンド設定の「assist text」に対応する値を指定する。 |
| `--sb-assist-text-weight` | — | バックエンド設定の「assist text weight」に対応する値を指定する。 |
| `--sb-style` | — | バックエンド設定の「style」に対応する値を指定する。 |
| `--sb-style-weight` | — | バックエンド設定の「style weight」に対応する値を指定する。 |
| `--sb-reference-audio-path` | — | バックエンド設定の「reference audio path」に対応する値を指定する。 |

#### ComfyUI (`cu`)

| オプション名 | 省略形 | 機能 |
| --- | --- | --- |
| `--cu-host` | — | バックエンド設定の「host」に対応する値を指定する。 |
| `--cu-port` | — | バックエンド設定の「port」に対応する値を指定する。 |
| `--cu-prompt-target` | — | バックエンド設定の「prompt target」に対応する値を指定する。 |
| `--cu-upload-image-target` | — | バックエンド設定の「upload image target」に対応する値を指定する。 |
| `--cu-api-options` | — | ComfyUI API の query または request fields を JSON object で指定する。 |
| `--cu-prompt-id` | — | `comfy history-item` の prompt ID、または `comfy job` / `job-cancel` の job ID を指定する。 |
| `--cu-user-id` | — | ComfyUI の `comfy-user` request header に設定する user ID を指定する。 |
| `--cu-userdata-path` | — | userdata 操作の relative remote path を指定する。slash で階層を区切り、絶対パスと traversal は拒否される。 |
| `--cu-userdata-destination` | — | `comfy userdata-move` の relative destination path を指定する。 |
| `--cu-template-path` | — | `comfy template-file` で取得する custom node template の `module/relative-file` path を指定する。path traversal は拒否される。 |
| `--cu-node-class` | — | `comfy object-info` で schema を取得する node class を指定する。未指定時は全 node class を取得する。 |
| `--cu-model-folder` | — | `comfy models-folder` で model list を取得する folder を指定する。 |
| `--cu-metadata-folder` | — | `comfy view-metadata` で metadata を取得する model folder を指定する。 |
| `--cu-upload-file` | — | `comfy upload-image` / `upload-mask` または `userdata-put` で送信するローカルファイルを指定する。 |
| `--cu-client-id` | — | ComfyUI WebSocket `/ws` 接続の clientId query を指定する。 |
| `--cu-event-count` | — | `comfy events` が終了するまでに受信する frame 数。0 は無制限。 |
| `--cu-prompt` | — | バックエンド設定の「prompt」に対応する値を指定する。 |
| `--cu-prompt-file` | — | バックエンド設定の「prompt file」に対応する値を指定する。 |
| `--cu-output-directory` | — | バックエンド設定の「output directory」に対応する値を指定する。 |
| `--cu-upload-images` | — | バックエンド設定の「upload images (macro_name=local_path)」に対応する値を指定する。 |
| `--cu-preserve-subdirectories` | — | バックエンド設定の「preserve server side sub-directories」に対応する値を指定する。 |

`--sd-seed-resize-from-h/w` は txt2img と img2img で共通の値です。img2img だけに異なる値を使う場合は `--sd-img2img-seed-resize-from-h/w` を指定します。img2img 専用値の既定値 `-1` は共通値を引き継ぎます。画像生成のステップ数オプション名は `--sd-steps` です。
パス区切りは `/` を使用します。相対パスは `--path-base` を基準にします。テキストファイルを指定する箇所では拡張子を省略でき、その場合 `.txt` が補われます。出力先の親ディレクトリは自動作成されます。バイナリ出力は拡張子補完の対象外です。

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
| `--llm-api-key` | 空。Text Generation Web UI token count など API key 対応 route で Bearer Authorization を付ける |
| `--llm-admin-key` | 空。TG `models list` では `--llm-api-key` に fallback |
| `--llm-completions-target` | `tg` / LM Studio / llama.cpp / vLLM: `/v1/completions`; `kc`: `/api/v1/generate`; Ollama: `/api/generate` |
| `--llm-token-count-target` | `tg`: `/v1/internal/token-count`; `kc`: `/api/extra/tokencount`; OpenAI-compatible: `/v1/completions`; Ollama: `/api/generate` |
| `--llm-chat-completions-target` | `tg` / `kc` / LM Studio / llama.cpp / vLLM: `/v1/chat/completions`; Ollama: `/api/chat` |
| `--llm-mode` | `completions`。`completions` または `chat-completions` |
| `--llm-min-completion-tokens` | `256`。この追加生成量に達するか、最大継続回数に達すると終了 |
| `--llm-max-completion-iterations` | `5` |
| `--llm-reasoning-prefix`, `--llm-reasoning-suffix` | 空。両方の間にある reasoning 部分を結果から除去 |
| `--llm-code-block-extract` | false。true の場合、応答の Markdown コードブロックを個別出力 |

Completions のプロンプトは、テンプレート展開と generation prefix の追加後にトークン数を問い合わせます。残りコンテキストと backend の最大生成数を考慮して生成し、応答を次の要求の末尾に加えて継続します。`completions()` が generation suffix を追加し、ファイル出力側では追加しません。通常の `tg` / `kc` 出力と `generated()` などのマクロで suffix は一度だけ付加されます。

Chat Completions はプロンプトを user message として追加し、必要に応じて画像も含めます。応答後に assistant message を追加し、messages JSON をチャットファイルに保存します。既存チャットファイルが読めない場合は空の会話から開始します。プロンプトにはサーバー側のチャットテンプレートを使うための本文を渡します。

### 画像・音声・ComfyUI

- `sd` は `--sd-*` で指定した txt2img または img2img パラメーターを JSON API に送り、画像をバイナリ保存します。主な設定は host `localhost`、port `7860`、mode `txt2img`、output `{{datetime}}.png` です。
- `sb` は `--sb-*` で指定したテキスト、モデル、話者、言語等を Style-Bert-VITS2 API に送り、音声をバイナリ保存します。既定の port は `5001`、target は `/voice`、出力名は `{{datetime}}.wav` です。
- `cu` は `--cu-*` で指定した ComfyUI prompt JSON を送信します。既定 host は `localhost`、port は `8188`、target は `/prompt`。画像のアップロード指定、サーバー側サブディレクトリ保持も設定できます。
- 各 mode の prompt/text の直接指定が空の場合、対応するファイルから読み込みます。`sd` の正負 prompt と `sb` の text は送信前にマクロ展開されます。

## マクロ言語

マクロは `{{...}}` で囲みます。文字列中に変数 `{{name}}`、関数 `{{function(arg, ...)}}` を記述できます。各ファイル内容とファイルパス等、コード上で展開を呼び出す箇所で評価されます。式は Boost.Spirit Qi の AST と visitor で解析・評価され、最大再帰深度は 32 です。通常ビルドでは式placeholderだけを公開し、statement placeholder、単独代入、制御文、セミコロン付き式文は無効です。AST・rule・visitorのコードは将来の再開に備えて残しますが、公開parse経路から切り離し、内部statement parse APIも既定で失敗させます。

### サポート範囲

| 機能 | 状態 | 記法・備考 |
| --- | --- | --- |
| マクロ展開 | 対応 | `{{ expression }}`。通常のテキスト中に複数記述可能 |
| マクロ変数参照 | 対応 | `{{name}}` |
| 組み込み関数呼び出し | 対応 | `{{function(arg1, arg2)}}` |
| リテラル | 対応 | bool、整数、浮動小数点数、文字、文字列 |
| 代入・複合代入・前置/後置増減 | 無効 | 公開placeholder式では使用不可。内部実装コードは保持 |
| 比較・算術・論理式 | 対応 | 比較、シフト、四則演算、ビット演算、論理演算、条件式 |
| 式の連結 | 対応 | カンマ区切りの式列。評価結果は最後の式 |
| 式 | 対応 | 評価結果を文字列化して placeholder の展開結果にする |
| ステートメント | 無効 | 公開placeholder grammarから除外。AST/rule/visitorはコードに保持 |
| `if`／`else`／`while`／複合文 | 無効 | 通常ビルドでは解析・評価されない |
| ポインター・参照 | 未対応 | マクロ言語の値型に含まれない |

公開placeholderは式だけを受け付け、式の結果を文字列展開します。statement syntax（単独代入、`;` 付き式、複合文、`if`／`else`、`while`）、代入演算子、前置/後置の増減演算子はparseできません。無効化した内部コードの再開状況は [`docs/ISSUES.md`](ISSUES.md) で管理します。

### 基本構文

```ebnf
placeholder          = "{{", expression, "}}" ;
expression           = conditional-expression, { ",", conditional-expression } ;
conditional-expression = logical-or-expression
                       | logical-or-expression, "?", expression, ":", conditional-expression ;
logical-or-expression = logical-and-expression, { "||", logical-and-expression } ;
logical-and-expression = bitwise-or-expression, { "&&", bitwise-or-expression } ;
bitwise-or-expression = bitwise-xor-expression, { "|", bitwise-xor-expression } ;
bitwise-xor-expression = bitwise-and-expression, { "^", bitwise-and-expression } ;
bitwise-and-expression = equality-expression, { "&", equality-expression } ;
equality-expression = relational-expression, { ("==" | "!="), relational-expression } ;
relational-expression = shift-expression, { ("<" | ">" | "<=" | ">="), shift-expression } ;
shift-expression      = additive-expression, { ("<<" | ">>"), additive-expression } ;
additive-expression   = multiplicative-expression, { ("+" | "-"), multiplicative-expression } ;
multiplicative-expression = unary-expression, { ("*" | "/" | "%"), unary-expression } ;
unary-expression      = ("+" | "-" | "!" | "~"), unary-expression
                      | primary-expression ;
primary-expression    = literal | name, [ "(", [ argument-list ], ")" ]
                      | "(", expression, ")" ;
argument-list         = conditional-expression, { ",", conditional-expression } ;
literal               = boolean | integer | floating-point | character | string ;
name                  = identifier ;
```

この EBNF は通常ビルドで公開する式構文を示します。引数リスト内のカンマは引数を区切り、placeholder 本文や括弧内の式にあるカンマは式列として評価します。代入・statement 用の内部 grammar は公開構文に含みません。

空白は構文上の区切りとして扱われます。文字列リテラルは `"..."`、文字リテラルは `'...'` で記述し、エスケープを使用できます。式の評価結果は文字列化して展開します。公開placeholderではstatement syntax、代入演算子、前置/後置増減演算子を無効化しています。予約語 `if`、`else`、`while`、`true`、`false` は識別子として使用できません。予約語で始まる長い識別子（例: `iffy`、`elsewhere`）は使用できます。実装状況と検証結果は [`docs/ISSUES.md`](ISSUES.md) を参照してください。

変数の値は bool, int, char, double, string です。`context::erase` は現在のスコープの値を消去し、親スコープに同名値があればそれを再び参照します。公開placeholderからの変数代入は無効です。`--variable` で起動時に変数を渡せます。マクロ評価でエラーが発生した場合、そのマクロ全体を空文字にし警告を記録します。

組み込み `file` は設定に基づいてテキストファイルを読み込みます。内部の reader callable は macro 引数、設定を受け取る形で差し替え可能で、通常経路は従来の filesystem 実装を使用します。注入境界のテストでは実ファイルを必要とせず、read error の伝播も確認できます。

使用可能な演算子:

- 比較: `==`, `!=`, `<`, `>`, `<=`, `>=`
- 二項: `<<`, `>>`, `+`, `-`, `*`, `/`, `%`, `|`, `^`, `&`
- 前置: `+`, `-`, `!`, `~`

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
| `{{prev_phase}}` / `{{next_phase}}` | 前後の phase。存在しない側は設定されず、phase切替時に前回値を消去 |

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
| `exec(exe, arg, ...)` | `--security-allow-process-exec` が有効な場合に子プロセスを実行し標準出力を返す。既定では拒否し、許可時は終了コードを `{{exit_code}}` に設定 |
| `code_block(markdown, name)` | Markdown から指定名のコードブロック本文を返す。なければ空 |
| `root(path)`, `parent(path)`, `stem(path)`, `extension(path)` | パスのルート、親、拡張子を除く名前、拡張子を返す |

`head` / `tail` と prompt の切り詰めには LLM backend の token count API を利用します。トークン数キャッシュの容量は 1000 件で、テキスト生成モード間の実行中に利用し、ファイルへ読み書きして再利用します。

## phase と段落ファイル

`--iteration-phases` に複数の値を渡すと、各反復で順番に各 phase の生成を行います。現在・前・次の値は上記の組み込み変数で参照します。`--llm-paragraphs-file` を指定すると `--iteration-phases` の値を段落リストから生成します。リスト項目の見出しと、直後にインデントされた子リストの説明を結合して phase にします。

段落ファイルを使わない場合の phase は `--iteration-phases` の値です。ソースには Chat Completions 用の phase と prefix の初期化関数がありますが、現在の起動処理からは呼び出されていません。そのため `{{user}}`, `{{char}}` や `{{phase}}: ` が自動設定されるとは限りません。

## コードブロック抽出とファイル名のパストラバーサル防止

`--llm-code-block-extract` が有効なとき、生成 Markdown の名前付きコードブロックをファイルとして出力します。ブロック名をファイル名にし、空白を `_` に置換し、パス区切り文字と `:*?"<>|#` を除去します。単独の `.`／`..` は空名にして、出力先ディレクトリ外へ書き込めないようにします。ファイル名の拡張子がなければ `.txt` を補完します。ブロック名が `stdout` の場合はファイルにせず標準出力へ出します。

## ファイルとログ

ログは `--logging-level` 以上の severity を記録します。`--logging-console` が有効ならログをコンソールへ出力し、`--logging-file` が空でなければファイルにも追記します。ログファイルのパスは初期化時にマクロ展開され、その後は同じファイルに書き続けます。

テキスト生成結果は出力ファイルに追記し、親ディレクトリがなければ作成します。バイナリ画像・音声結果は対応する出力ファイルへ書き込みます。標準出力は `--logging-console` が false の場合にテキスト生成結果を出力するほか、PNG パラメーター抽出や `stdout` コードブロックで使用します。

## サーバープロセス制御

`create-process` は `--process-executable` と `--process-arguments` で実行ファイルを非同期起動します。起動後、`--process-host` (`localhost`)、`--process-port` (`5000`) に TCP 接続できるまで、`--process-max-retries` (`60`) 回、`--process-wait-ms` (`1000`) 間隔で待機します。

`terminate-process` は `--process-executable` に指定したフルパスまたはファイル名に一致するプロセスを終了します。現在の実装でプロセス列挙・終了処理が実装されるのは Windows です。

## 課題

要求の正本は [REQUIREMENTS.md](REQUIREMENTS.md) です。ユーザーが直接記載するか明示的に更新を指示した要求から、作業計画と受け入れ確認を導きます。
課題は [ISSUES.md](ISSUES.md) で `ISS-NNN` の固定IDを付け、優先度順・解決順序順に管理します。課題表はID、優先度、課題、影響、状態、解決策の候補、完了条件に加え、根拠、前提・依存、次アクション、検証結果、最終更新を管理対象とします。未収集の情報は「未収集」と明記し、課題の解決時はコード・テスト・検証結果と合わせて `ISSUES.md` を更新します。
未解決課題の詳細はISSUES.mdを正本とし、優先度順の作業要約を次に示します。

| ID | 優先度 | 影響 | 状態・次アクション |
| --- | --- | --- | --- |
| [ISS-005](ISSUES.md) | 高 | 重い翻訳単位が差分開発を遅らせる。 | 対応中。重い依存と再解析範囲を同条件で測定する。 |
| [ISS-009](ISSUES.md) | 高 | Beast/Log 公開ヘッダー依存が大きい。 | 対応中。設定 severity は Boost.Log 非依存化済み。既存 executor の互換性を保つ HTTP 境界とログ facade を整理・測定する。 |
| [ISS-011](ISSUES.md) | 高 | builtin の外部境界に注入テストが不足していた。 | 解決済み。environment、file、process、random、generated/summary API 境界の正常系・失敗系を注入テストした。 |
| [ISS-012](ISSUES.md) | 高 | Boost.Log sink の global state 統合を単体検証できない。 | 対応中。sink 登録・formatter を分離して検証する。 |
| [ISS-013](ISSUES.md) | 高 | builtin 公開関数の正常系・境界値・失敗系テストが不足していた。 | 解決済み。registry の各関数と主要な注入境界のテストを追加し、Release x64 の全テストが成功。 |
| [ISS-017](ISSUES.md) | 中 | I/O 実装依存が失敗系検証を制限する。 | 対応中。ISS-012 と共有する残存ログ境界を整理する。 |
| [ISS-026](ISSUES.md) | 低 | 集約ヘッダー依存が再コンパイル範囲を広げる。 | 継続管理。互換用途以外の依存を走査する。 |
| [ISS-027](ISSUES.md) | 低 | Release x64 フルビルドは約5分。 | 継続管理。`/Bt+` 基準値を更新する。 |
| [ISS-032](ISSUES.md) | 低 | sandbox loopback test の残留 process が test.exe をロックした。 | 解決済み。PID 37232 を特定・終了して既定名 relink 成功。loopback test に時間上限と process 回収を必須化。 |
| [ISS-028](ISSUES.md) | 低 | test.pdb の別プロセス保持時に C1041 が起きる。 | 継続管理。再発時に保持プロセスと PID を記録する。 |
| [ISS-029](ISSUES.md) | 高 | Hermit app 差分リンクで LNK1103 が発生していた。 | 解決済み。full LTCG の差分リンク成功を維持・監視する。 |
| [ISS-030](ISSUES.md) | 高 | 対象バックエンド全 Web API の対応が未完了。 | 進行中。LM Studio Anthropic Messages JSON/SSE、Jev System One JSON、vLLM batch chat JSON、TG moderation/health/token count/encode/decode/current model info/admin model list/load/unload/logits/chat-prompt/stop-generation/LoRA list/load/unload に加え、KC generation streaming/check/abort/last_logprobs、preloadstory、tokenize/detokenize/JSON grammar、7 optional POST routes、audio/transcribe・TTS・embeddings/music/speakers、OpenAI互換 text/audio/image、Anthropic/Ollama、A1111/ComfyUI/XTTS と admin/MCP と docs/metadata/UI GET、legacy request/A1111 catalog および documented JSON/multipart aliases、A1111 progress/catalog、ComfyUI metadata、XTTS settings/legacy voice routes を契約テスト付きで追加。loopback TCP streaming test の partial request read/server close race を修正。2026-11-28 VS18 Release x64 Build 成功（警告0・エラー0）、対象 test 100回反復成功、全305/305 tests成功（TG-004/005 model/billing routes を含む）。TG OpenAI-compatible model list 用の専用 command と schema validation を追加。TG embeddings 専用 CLI 対応と optional server-side model 選択に加え、audio transcription multipart と image generation JSON route forwarding tests を追加し、Anthropic Messages JSON/SSE の TG backend 対応も追加。VS18 Release x64 Build成功（警告0・エラー0、1分49.92秒）、全305/305 tests成功（3.379秒）。汎用 `backend-api stream` と TG-001/002 SSE の raw chunk 転送契約を追加。VS18 Release x64 Build 成功（警告0・エラー0、2分14.89秒）、全306/306 tests成功（29 suites、3.410秒）。公式 `modules/api/script.py` の固定 route decorators を2026-11-28に再照合し、TG-001〜016 の16 groupsですべて追跡していることを確認。import 先・optional feature・拡張 routes、実 server確認と他 backend 未対応操作は引き続き追跡。TG-001 `/v1/completions` SSE request/auth/chunk/`[DONE]` test を追加。VS18 Release x64 Build 成功（警告0・エラー0、53.29秒）、全307/307 tests成功（29 suites、3.438秒）。公式操作一覧は [BACKEND_WEB_API_COVERAGE.md](BACKEND_WEB_API_COVERAGE.md) で追跡する。 |
| [ISS-031](ISSUES.md) | 中 | CLI ヘルプとオプション資料に誤記があり、SD の seed resize 名が重複していた。 | 解決済み。共通名を維持し、img2img 専用名を分離。ヘルプ・資料と回帰テストを更新した。 |
| [ISS-033](ISSUES.md) | 中 | CLI がフラットな `--mode` とハイフン連結オプションに依存する。 | 解決済み（当時の構文）。サブコマンドと `--namespace.property` を導入後、ドット区切り構文は [ISS-038](ISSUES.md) で小文字 kebab-case へ移行済み。 |

未解決課題と未対応範囲は [ISSUES.md](ISSUES.md) で管理します。Qi属性伝播、AST実験、依存注入テストで得た再利用可能な知見とその証拠は [DEVELOPMENT_KNOWLEDGE.md](DEVELOPMENT_KNOWLEDGE.md) に表形式で記録します。構文段階と公開方針は [MACRO_PARSER_REDEVELOPMENT.md](MACRO_PARSER_REDEVELOPMENT.md) で管理します。

ユーザー承認済みの宣言的要求は [REQUIREMENTS.md](REQUIREMENTS.md) を正本として管理します。要求の記載・変更権限、要求から計画を作る手順はルートの `AGENTS.md` に従います。
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
| ComfyUI API | 注入 HTTP executor を使用 | queue/history/object-info/model/upload/view/metadata route、query encoding、binary response、HTTP エラー | `test/test_cu.cpp` で route・payload・storage 契約を検証する。 |
| process launcher | Windows API / Boost.Process を直接使用 | 起動引数、待機、終了コード、起動失敗、終了対象なし | 実プロセスを起動するテストを追加せず、launcher をテンプレート注入可能にして Mock へ置換する。 |

## README との役割分担

日本語 README はルートの `readme.md`、英語 README は `docs/README/en/README.md` に置き、各 README 冒頭のリンクで言語を切り替えます。README は利用者向けの導入、実行例、バックエンド接続手順を扱います。実装の全オプション、既定値、マクロ言語、ファイル構成、依存関係、既知の注意点は本仕様書を正本とします。両言語の README を変更する場合は、実装の現在の挙動と照合して記述を同期します。

## 参照元

- 実装・CLI オプション・マクロ・バックエンド: [`Hermit/Hermit.hpp`](../Hermit/Hermit.hpp)
- 利用例・バックエンド設定例・概要: [`readme.md`](../readme.md)
- English guide: [`docs/README/en/README.md`](README/en/README.md)
- ビルド構成: [`Hermit.sln`](../Hermit.sln)、`Hermit/Hermit.vcxproj`、`test/test.vcxproj`
