# Hermit

言語: 日本語 | [English](docs/README/en/README.md)

Hermit は、Windows 上で動作する C++20 の CLI アプリケーションです。テキスト・画像・音声の生成バックエンドをコマンドラインから利用し、プロンプトのテンプレート展開、反復実行、生成結果の保存を行います。

対象バックエンドは [text-generation-webui](https://github.com/oobabooga/text-generation-webui)、[KoboldCpp](https://github.com/LostRuins/koboldcpp)、[AUTOMATIC1111 Stable Diffusion WebUI](https://github.com/AUTOMATIC1111/stable-diffusion-webui)、[Style-Bert-VITS2](https://github.com/litagin02/Style-Bert-VITS2)、[ComfyUI](https://github.com/Comfy-Org/ComfyUI)、[LM Studio](https://lmstudio.ai/)、[llama.cpp server](https://github.com/ggml-org/llama.cpp/tree/master/tools/server)、[vLLM](https://github.com/vllm-project/vllm)、[Ollama](https://ollama.com/) です。Hermit が各バックエンドの全 API を扱えるわけではありません。対応操作と未対応範囲は [バックエンド API 対応表](docs/BACKEND_WEB_API_COVERAGE.md) で管理しています。

## 主な機能

- `tg` / `kc`、OpenAI 互換 API を持つ `lmstudio` / `llama-cpp` / `vllm`、Ollama 独自 API の `ollama` でテキストを生成し、Completions または Chat Completions の処理を選択できます。
- `sd` モードで Stable Diffusion WebUI に txt2img / img2img リクエストを送り、画像を保存できます。
- `sd-api` コマンドで Stable Diffusion WebUI の progress、interrogate、Extras、options、モデル一覧・更新、embedding/hypernetwork 作成・学習、memory/script/extension 情報、checkpoint 管理、任意 server control API と runtime OpenAPI route inventory を呼び出せます。`sd-api request` は method/path、JSON または file body、Content-Type、追加 header を指定できます。
- `backend-api request` で `tg`、`kc`、`lmstudio`、`llama-cpp`、`vllm`、`ollama` の HTTP route を直接呼び出せます。method/path と JSON/raw body を指定し、応答 JSON または binary bytes を出力できます。
- `sb` モードで Style-Bert-VITS2 に音声生成リクエストを送り、音声を保存できます。
- `cu` モードで ComfyUI の workflow を送信し、画像をアップロードして生成結果を保存できます。
- `{{...}}` 形式の式をプロンプトや対応するファイルパスで評価できます。ファイル読み込み、テキスト切り詰め、日時、乱数、環境変数、サブプロンプトなどの組み込み機能を利用できます。
- 複数回の反復、phase ごとの生成、名前付き Markdown コードブロックのファイル出力に対応します。
- 補助モードで PNG の生成パラメーターを抽出し、Windows 上でサーバープロセスを起動・終了できます。

公開マクロ言語は式を評価する機能です。代入文、`if` / `while` などの statement 構文は現在無効です。詳細な構文・組み込み関数・制約は [技術仕様書](docs/SPECIFICATION.md#マクロ言語) を参照してください。

## 必要な環境

- Windows x64
- Visual Studio の MSVC C++20 ビルド環境
- vcpkg と Visual Studio の MSBuild 連携
- vcpkg manifest が復元する Boost。テストをビルドする場合は Google Test も必要です。

Visual Studio の構成にはリポジトリ直下の `.vsconfig` を利用できます。vcpkg が利用可能で MSBuild 連携が済んでいれば、プロジェクトの manifest 設定に基づきビルド時に依存ライブラリが復元されます。必要な場合は vcpkg コマンドから一度連携を有効にします。

```powershell
vcpkg integrate install
```

Release x64 の差分ビルド例です。Visual Studio 18 を `vswhere.exe` で検索し、同一のコンソール内で開発環境を初期化して MSBuild を実行します。

```powershell
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$install = & $vswhere -products * -property installationPath | Where-Object { $_ -match '\\18\\' } | Select-Object -First 1
$devcmd = Join-Path $install 'Common7\Tools\VsDevCmd.bat'
$buildCommand = 'call "{0}" -arch=x64 -host_arch=x64 && where msbuild && msbuild .\Hermit.sln /m:1 /t:Build /p:VisualStudioVersion=18.0 /p:Configuration=Release /p:Platform=x64 /v:normal' -f $devcmd
cmd.exe /d /c $buildCommand
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

`Hermit.sln` にはアプリケーションとテストのプロジェクトが含まれます。通常は `/t:Build` を使い、クリーン状態が必要な場合に限って `/t:Rebuild` を使います。ビルド・テスト手順の詳細は [技術仕様書](docs/SPECIFICATION.md) と [Hermit build/test 手順](.agents/skills/hermit-build-test/SKILL.md) を参照してください。

## コマンドライン

オプションの完全な一覧は `Hermit.exe --help` で表示できます。コマンドを先に指定して実行します。

```powershell
Hermit.exe generate --backend tg --llm.host localhost --llm.port 5000 --llm.prompt-file prompt
```

主な実行モードは次の通りです。

| モード | 用途 |
| --- | --- |
| `tg` | text-generation-webui でテキスト生成 |
| `kc` | KoboldCpp でテキスト生成 |
| `lmstudio` | LM Studio の OpenAI 互換 API でテキスト生成 |
| `llama-cpp` | llama.cpp server の OpenAI 互換 API でテキスト生成 |
| `vllm` | vLLM の OpenAI 互換 API でテキスト生成 |
| `ollama` | Ollama のネイティブ API でテキスト生成 |
| `sd` | AUTOMATIC1111 Stable Diffusion WebUI で画像生成 |
| `sb` | Style-Bert-VITS2 で音声生成 |
| `cu` | ComfyUI workflow の実行 |
| `extract-png-parameters` | PNG から生成パラメーターを抽出 |
| `create-process` / `terminate-process` | サーバープロセスの起動・終了 |

コマンドには `generate`、`backend-api request`、`models`、`responses`、`decisions`、`embeddings`、`tokenize`、`server`、`png` などがあります。設定オプションは `--namespace.property` 形式で、INI でも同じ dotted key を使います。短縮形は `-h` のみです。旧 `--mode` とハイフン連結のフラットなオプション名は受け付けません。詳しくは [CLI オプション一覧](docs/SPECIFICATION.md#実装済みコマンドラインオプション一覧) を参照してください。

LM Studio Responses API は専用コマンドで呼び出せます。`--llm.api-options` に `model` と `input` を含む request JSON を渡します。通常は応答 JSON object を受け取り、`stream:true` の場合は受信した SSE bytes を標準出力へ逐次出力します。

```powershell
Hermit.exe responses create --backend lmstudio --llm.api-options '{"model":"openai/gpt-oss-20b","input":"Hello"}'
```

streaming の場合は次のように `stream:true` を指定します。

```powershell
Hermit.exe responses create --backend lmstudio --llm.api-options '{"model":"openai/gpt-oss-20b","input":"Hello","stream":true}'
```

SSE bytes を加工せず標準出力へ逐次書き込みます。tool 実行および response lifecycle は [バックエンド API 対応台帳](docs/BACKEND_WEB_API_COVERAGE.md) に記載の通り未対応です。

LM Studio の native stateful chat API は `lmstudio-chat create` で呼び出せます。追加の generation、MCP integration、stateful follow-up 等の公式 field を `--llm.api-options` で指定できます。`stream:true` は native named SSE events をそのまま標準出力へ逐次出力します。

```powershell
Hermit.exe lmstudio-chat create --backend lmstudio --llm.api-options '{"model":"ibm/granite-4-micro","input":"Hello"}'
Hermit.exe lmstudio-chat create --backend lmstudio --llm.api-options '{"model":"ibm/granite-4-micro","input":"Hello","stream":true}'
Hermit.exe lmstudio-chat create --backend lmstudio --llm.api-options '{"model":"ibm/granite-4-micro","input":"Continue","previous_response_id":"resp_..."}'
```

Anthropic Messages 互換 API は `messages create` で利用できます。API key を設定すると `x-api-key` header で送信します。`stream:true` の場合は Anthropic SSE event をそのまま逐次出力します。

```powershell
Hermit.exe messages create --backend lmstudio --llm.api-options '{"model":"ibm/granite-4-micro","max_tokens":128,"messages":[{"role":"user","content":"Hello"}]}'
Hermit.exe messages create --backend lmstudio --llm.api-options '{"model":"ibm/granite-4-micro","max_tokens":128,"messages":[{"role":"user","content":"Hello"}],"stream":true}'
```

Jev (TypeSafe AI) System One endpoint は decision model ID と `state`、ID付き `questions` を JSON で指定します。

```powershell
Hermit.exe systemone create --backend lmstudio --llm.api-options '{"model":"decision-model","state":"I was charged twice.","questions":{"angry":{"type":"noul","instructions":"Is the customer angry?"}}}'
```

vLLM の batch chat endpoint は複数 conversation を一度に送信します。streaming、tools は対象外です。

```powershell
Hermit.exe batch-chat create --backend vllm --llm.api-options '{"model":"Qwen/Qwen2.5-1.5B-Instruct","messages":[[{"role":"user","content":"Capital of France?"}],[{"role":"user","content":"Capital of Japan?"}]]}'
```

Text Generation Web UI の moderations endpoint は `moderations create` で使えます。`input` に文字列または文字列配列を指定してください。

```powershell
Hermit.exe moderations create --backend tg --llm.api-options '{"input":"Example text to classify"}'
```

Text Generation Web UI の API 稼働状態は `health check` で確認できます。API key を指定した場合は Bearer token で送信します。

```powershell
Hermit.exe health check --backend tg
```

現在ロードされている Text Generation Web UI モデルと LoRA 名は `models info` で取得できます。

```powershell
Hermit.exe models info --backend tg
```

Text Generation Web UI のモデルを admin API からロード・アンロードできます。load は `model_name` が必要です。`--llm.admin-key` を優先し、未指定なら `--llm.api-key` を送ります。

```powershell
Hermit.exe models load --backend tg --llm.model-action-options '{"model_name":"ExampleModel","args":{"n_gpu_layers":8}}' --llm.admin-key "admin-token"
Hermit.exe models unload --backend tg --llm.admin-key "admin-token"
```

未対応の HTTP API route は `backend-api request` で呼び出せます。例えば LM Studio Responses API は次のように実行します。

```powershell
Hermit.exe backend-api request --backend lmstudio --llm.api-method POST --llm.api-path /v1/responses --llm.api-options '{"model":"openai/gpt-oss-20b","input":"Hello"}'
```

`--llm.api-body-file` で raw/multipart body、`--llm.api-content-type` で Content-Type、`--llm.api-headers` で追加 header を指定できます。JSON/object API 応答はそのまま出力し、binary 応答は bytes を標準出力へ書きます。この共通経路は個別 API の schema 検証や streaming/WebSocket 処理を代替しません。operation inventory は [バックエンド API 対応台帳](docs/BACKEND_WEB_API_COVERAGE.md) を参照してください。

HTTP API から受け取る応答 body は 128 MiB を上限とし、上限を超える応答は受信エラーになります。

`exec(...)` マクロによる子プロセス起動は既定で無効です。信頼できるプロンプトだけを使う場合に限り、`--security.allow-process-exec` または INI の `security.allow-process-exec = true` で明示的に有効化してください。未信頼のプロンプトやテンプレートを使う場合は有効化しないでください。

### 設定ファイル

既定では `config.ini` を読み込みます。設定ファイルは INI 形式で、コマンドラインと設定ファイルの両方に同じオプションがある場合はコマンドラインの値を優先します。相対パスは `--path.base` を基準に解決されます。

```ini
backend = tg
llm.host = localhost
llm.port = 5000
llm.prompt-file = prompt
llm.output-file = output
logging.console = false
```

設定ファイルを使わず、コマンドラインだけで指定することもできます。

```powershell
Hermit.exe generate --backend kc --llm.host localhost --llm.port 5000 --llm.prompt-file prompt
```

ファイル名を受け取るオプションでは、相対パスを `--path.base` から解決します。パス区切りには `/` を使ってください。テキストファイルの拡張子は省略でき、省略時は `.txt` が補われます。パスの詳細と例外は [技術仕様書](docs/SPECIFICATION.md#起動コマンドと設定) を参照してください。

## バックエンドの準備と実行例

各バックエンドは別途インストールして起動してください。Hermit 側の通信先は、バックエンドの起動ポートに合わせます。

### text-generation-webui (`tg`)

text-generation-webui の起動設定で API を有効にし、サーバーを起動します。

```powershell
Hermit.exe generate --backend tg --llm.host localhost --llm.port 5000 --llm.prompt-file prompt --llm.output-file output
```

### KoboldCpp (`kc`)

KoboldCpp を API サーバーとして起動します。以下は Windows バッチの例です。

```bat
koboldcpp.exe --model models/model.gguf --usecuda --port 5000
```

Hermit から利用します。

```powershell
Hermit.exe generate --backend kc --llm.host localhost --llm.port 5000 --llm.prompt-file prompt --llm.output-file output
```

画像入力を使う場合は KoboldCpp の vision 対応設定とモデルを用意し、`--llm.mode chat-completions` と `--llm.image-file` を指定します。

### OpenAI 互換のローカル LLM server

LM Studio、llama.cpp server、vLLM は OpenAI 互換の `/v1/completions` と `/v1/chat/completions` を使います。既定ポートはそれぞれ 1234、8080、8000 です。`--llm.model` でサーバーが認識するモデル ID を指定し、API key が必要な場合は `--llm.api-key` を設定します。token count は completions API の `usage.prompt_tokens` を使います。

```powershell
Hermit.exe generate --backend lmstudio --llm.model "model-id" --llm.mode chat-completions --llm.prompt-file prompt
Hermit.exe generate --backend llama-cpp --llm.model "model-id" --llm.mode chat-completions --llm.prompt-file prompt
Hermit.exe generate --backend vllm --llm.model "model-id" --llm.mode chat-completions --llm.prompt-file prompt --llm.api-key "token"
```

OpenAI 互換 API の埋め込みは `POST /v1/embeddings` で作成できます。対象モデルは embedding 対応である必要があります。

```powershell
Hermit.exe embeddings create --backend lmstudio --llm.model "embedding-model" --llm.embedding-input "first text" "second text"
Hermit.exe embeddings create --backend llama-cpp --llm.model "embedding-model" --llm.prompt "text to embed"
Hermit.exe embeddings create --backend vllm --llm.model "embedding-model" --llm.prompt "text to embed" --llm.api-key "token"
```

### Ollama (`ollama`)

Ollama は `/api/generate` と `/api/chat` を使います。既定ポートは 11434 です。`--llm.model` にローカルのモデル名と tag を指定します。

```powershell
Hermit.exe generate --backend ollama --llm.model "llama3.2:latest" --llm.mode chat-completions --llm.prompt-file prompt
```

Ollama の埋め込み API は `POST /api/embed` です。複数テキストは `--llm.embedding-input` に並べ、API 固有の追加 field は JSON object の `--llm.embedding-options` で指定します。`input` と `model` は予約 field です。

```powershell
Hermit.exe embeddings create --backend ollama --llm.model "embeddinggemma" --llm.embedding-input "first text" "second text" --llm.embedding-options '{"truncate":false,"dimensions":256}'
```

ローカル LLM server のモデル一覧は `models list` で取得できます。LM Studio はネイティブ REST [`/api/v1/models`](https://lmstudio.ai/docs/developer/rest/list)、llama.cpp と vLLM は OpenAI 互換 [`/v1/models`](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md)、Ollama は [`/api/tags`](https://docs.ollama.com/api/tags)、Text Generation Web UI は admin route `/v1/internal/model/list` を使用します。vLLM の model endpoint は [公式 vLLM server utilities](https://docs.vllm.ai/en/stable/api/vllm/benchmarks/serve/) でも使われています。応答 JSON を標準出力へそのまま出力し、host/port、認証 key、`--llm.models-target` で接続先を上書きできます。TG では `--llm.admin-key` が admin API key を指定します。省略すると `--llm.api-key` を使います。

```powershell
Hermit.exe models list --backend lmstudio
Hermit.exe models list --backend llama-cpp
Hermit.exe models list --backend vllm --llm.api-key "token"
Hermit.exe models list --backend ollama
Hermit.exe models list --backend tg --llm.admin-key "admin-token"
```

Ollama のモデル詳細、稼働状況、サーバーバージョンは `models show`、`models running`、`models version` で確認できます。

```powershell
Hermit.exe models show --backend ollama --llm.model "gemma4" --llm.model-info-options '{"verbose":true}'
Hermit.exe models running --backend ollama
Hermit.exe models version --backend ollama
```

Ollama model lifecycle 操作は request body を JSON object で指定します。応答はサーバーの JSON/NDJSON をそのまま出力します。create、pull、push の進捗配信は `stream` field で制御できます。

LM Studio native REST のモデル load/unload/download と download job status は `models` command から呼び出せます。`--llm.model-action-options` に公式 API の JSON fields を渡します。例:

```powershell
Hermit.exe models load --backend lmstudio --llm.model-action-options '{"model":"openai/gpt-oss-20b","context_length":16384,"flash_attention":true}'
Hermit.exe models unload --backend lmstudio --llm.model-action-options '{"instance_id":"instance-id"}'
Hermit.exe models download --backend lmstudio --llm.model-action-options '{"model":"openai/gpt-oss-20b","quantization":"Q4_K_M"}'
Hermit.exe models download-status --backend lmstudio --llm.model-action-options '{"job_id":"job-id"}'
```

各操作の schema とバージョン差は [バックエンド API 対応台帳](docs/BACKEND_WEB_API_COVERAGE.md) を参照してください。

```powershell
Hermit.exe models pull --backend ollama --llm.model-action-options '{"model":"llama3.2","stream":false}'
Hermit.exe models copy --backend ollama --llm.model-action-options '{"source":"llama3.2","destination":"llama3.2-backup"}'
Hermit.exe models create --backend ollama --llm.model-action-options '{"model":"custom-assistant","from":"llama3.2","system":"You are helpful.","stream":false}'
Hermit.exe models push --backend ollama --llm.model-action-options '{"model":"account/custom-assistant","stream":false}'
Hermit.exe models delete --backend ollama --llm.model-action-options '{"model":"custom-assistant"}'
```

llama.cpp server と Text Generation Web UI の tokenization API は `tokenize encode` と `tokenize decode` で呼び出せます。llama.cpp は `/tokenize`・`/detokenize`、TG は `/v1/internal/encode`・`/v1/internal/decode` を使用します。`--llm.tokenize-options` は llama.cpp 用です。TG encode は `text` を送り、token ID と length を受け取り、decode は token ID 配列を送り、text を受け取ります。

```powershell
Hermit.exe tokenize encode --backend llama-cpp --llm.prompt "Hello world" --llm.tokenize-options '{"with_pieces":true}'
Hermit.exe tokenize decode --backend llama-cpp --llm.token-ids 123 456
Hermit.exe tokenize encode --backend tg --llm.prompt "Hello world"
Hermit.exe tokenize decode --backend tg --llm.token-ids 123 456
```

### AUTOMATIC1111 Stable Diffusion WebUI (`sd`)

`--sd.generation-options` は txt2img/img2img request に任意の JSON object fields を追加します。使用中の WebUI に固有の field、script/extension fields を指定でき、同じキーが既存設定にもある場合は JSON object 側が優先されます。

```powershell
Hermit.exe generate --backend sd --sd.generation-options '{"hr_prompt":"high detail","custom_extension_field":{"enabled":true}}'
```

生成のほかに、progress・interrogate・interrupt・skip・Extras upscaling・PNG 情報・options API・model/sampler/extension 一覧・refresh API・embedding/hypernetwork の作成と学習 API・memory/script 情報・checkpoint の unload/reload API を呼び出せます。画像データは Base64 で `--sd.api-options` の JSON object に渡します。`interrogate` の model は `clip` または `deepdanbooru` です。`options` は設定を取得し、`options-set` は JSON object で設定を更新します。`create-embedding`、`create-hypernetwork`、`train-embedding`、`train-hypernetwork` は非空 JSON object をそのまま各 API へ転送します。request field は WebUI の版や設定で変わるため、使用中サーバーに対応する値を指定してください。`cmd-flags`、`samplers`、`schedulers`、`upscalers`、`latent-upscale-modes`、`sd-models`、`sd-vae`、`hypernetworks`、`face-restorers`、`realesrgan-models`、`prompt-styles`、`embeddings` はサーバーが提供する一覧 JSON を出力します。`refresh-embeddings`、`refresh-checkpoints`、`refresh-vae` は各モデル一覧を再読込します。`memory` は RAM/VRAM 使用量を、`scripts` と `script-info` は script 名と API metadata を、`extensions` は extension metadata を返します。`unload-checkpoint` は現在の model weights を解放し、`reload-checkpoint` は現在の model を device へ戻します。`pending-tasks`、`internal-progress`、`quicksettings-hint`、`ping`、`profile-startup`、`sysinfo`、`sysinfo-download` は A1111 UI 内部 API を呼び出します。`routes` は `/openapi.json` を取得し、稼働中 WebUI と拡張機能が登録した OpenAPI routes を確認できます。汎用 `request` は method/path と任意 body、Content-Type、header を指定します。JSON body は `--sd.api-options`、binary/raw body（multipart を含む）は `--sd.api-body-file` で渡し、後者では `--sd.api-content-type` を指定してください。追加 header は `--sd.api-headers` に JSON string map で渡せます。Content-Length、Transfer-Encoding、Host、Connection、Content-Type header は個別 option と矛盾するため header map では指定できません。Content-Type が text または JSON/XML の応答はテキストとして表示し、その他の型は bytes を標準出力へ書きます。streaming と WebSocket は未対応です。`server-kill`、`server-restart`、`server-stop` は A1111 の起動時に `--api-server-stop` が指定されている場合に使えるサーバー制御 API です。

```powershell
Hermit.exe sd-api progress --sd.api-options '{"skip_current_image":true}'
Hermit.exe sd-api routes
Hermit.exe sd-api request --sd.api-method GET --sd.api-path /internal/ping
Hermit.exe sd-api request --sd.api-method POST --sd.api-path /extension/custom --sd.api-options '{"value":1}'
Hermit.exe sd-api request --sd.api-method POST --sd.api-path /upload/image --sd.api-body-file upload.multipart --sd.api-content-type "multipart/form-data; boundary=hermit-boundary" --sd.api-headers '{"Authorization":"Bearer <token>"}'
Hermit.exe sd-api pending-tasks
Hermit.exe sd-api internal-progress --sd.api-options '{"id_task":"task-id","live_preview":false}'
Hermit.exe sd-api quicksettings-hint
Hermit.exe sd-api sysinfo-download > sysinfo.json
Hermit.exe sd-api interrogate --sd.api-options '{"image":"<base64>","model":"clip"}'
Hermit.exe sd-api interrupt
Hermit.exe sd-api skip
Hermit.exe sd-api extra-single-image --sd.api-options '{"image":"<base64>","upscaler_1":"R-ESRGAN 4x+","upscaling_resize":2}'
Hermit.exe sd-api extra-batch-images --sd.api-options '{"imageList":[{"name":"input.png","data":"<base64>"}],"upscaling_resize":2}'
Hermit.exe sd-api png-info --sd.api-options '{"image":"<base64-png>"}'
Hermit.exe sd-api options
Hermit.exe sd-api options-set --sd.api-options '{"samples_save":false}'
Hermit.exe sd-api create-embedding --sd.api-options '{"<server-specific fields>":"<value>"}'
Hermit.exe sd-api create-hypernetwork --sd.api-options '{"<server-specific fields>":"<value>"}'
Hermit.exe sd-api train-embedding --sd.api-options '{"<server-specific fields>":"<value>"}'
Hermit.exe sd-api train-hypernetwork --sd.api-options '{"<server-specific fields>":"<value>"}'
Hermit.exe sd-api samplers
Hermit.exe sd-api sd-models
Hermit.exe sd-api embeddings
Hermit.exe sd-api refresh-embeddings
Hermit.exe sd-api refresh-checkpoints
Hermit.exe sd-api refresh-vae
Hermit.exe sd-api memory
Hermit.exe sd-api scripts
Hermit.exe sd-api script-info
Hermit.exe sd-api extensions
Hermit.exe sd-api unload-checkpoint
Hermit.exe sd-api reload-checkpoint
Hermit.exe sd-api server-kill
Hermit.exe sd-api server-restart
Hermit.exe sd-api server-stop
```

API 操作は [バックエンド API 対応表](docs/BACKEND_WEB_API_COVERAGE.md) に記録しています。

WebUI 起動引数で API を有効にします。ポートは WebUI の設定に合わせてください。

```powershell
Hermit.exe generate --backend sd --sd.host localhost --sd.port 7860 --sd.steps 30 --sd.prompt "sky" --sd.output-file "{{datetime}}.png"
```

`--sd.seed-resize-from-h/w` は両モード共通です。img2img だけに別の値を指定するには `--sd.img2img-seed-resize-from-h/w` を使います。専用値が `-1` の場合は共通値を引き継ぎます。

### Style-Bert-VITS2 (`sb`)

Style-Bert-VITS2 の設定で API サーバーを起動します。

```powershell
Hermit.exe generate --backend sb --sb.host localhost --sb.port 5001 --sb.model-name "amitaro" --sb.speaker-id 0 --sb.language JP --sb.text "こんにちは" --sb.output-file "{{datetime}}.wav"
```

### ComfyUI (`cu`)

ComfyUI を起動し、API 用 workflow JSON をファイルに保存します。`--cu.prompt-file` にそのファイルを指定します。

```powershell
Hermit.exe generate --backend cu --cu.host localhost --cu.port 8188 --cu.prompt-file workflow.json --cu.output-directory output
```

workflow の入力画像は `--cu.upload-images name=path` 形式で登録できます。アップロード後のサーバー側ファイル名はマクロ変数 `name` として workflow の展開に使えます。複数の画像を指定する場合は値を複数渡します。

```powershell
Hermit.exe generate --backend cu --cu.prompt-file workflow.json --cu.upload-images source=images/source.png mask=images/mask.png
```

ComfyUI が返す生成ファイルは `--cu.output-directory` に保存されます。`--cu.preserve-subdirectories` を有効にするとサーバー側のサブディレクトリ構成を保ちます。

ComfyUI の queue・history・job・node schema・model type・model list・embedding・extension・feature の取得、画像・mask アップロード、画像・モデル metadata の取得、queue 操作・job 取消・実行割り込み・モデル解放 API は `comfy` コマンドで呼び出せます。query は `--cu.api-options` に JSON object で渡します。

```powershell
Hermit.exe comfy queue
Hermit.exe comfy queue-update --cu.api-options '{"clear":true}'
Hermit.exe comfy queue-update --cu.api-options '{"delete":["prompt-id"]}'
Hermit.exe comfy history --cu.api-options '{"max_items":100,"offset":0}'
Hermit.exe comfy history-item --cu.prompt-id "prompt-id"
Hermit.exe comfy history-update --cu.api-options '{"clear":true}'
Hermit.exe comfy history-update --cu.api-options '{"delete":["prompt-id"]}'
Hermit.exe comfy interrupt --cu.api-options '{"prompt_id":"prompt-id"}'
Hermit.exe comfy free --cu.api-options '{"unload_models":true,"free_memory":true}'
Hermit.exe comfy events --cu.client-id "hermit-client" --cu.event-count 20
Hermit.exe comfy object-info
Hermit.exe comfy object-info --cu.node-class "KSampler"
Hermit.exe comfy models
Hermit.exe comfy models-folder --cu.model-folder checkpoints
Hermit.exe comfy embeddings
Hermit.exe comfy extensions
Hermit.exe comfy features
Hermit.exe comfy upload-image --cu.upload-file images/input.png --cu.api-options '{"type":"input","subfolder":"references","overwrite":true}'
Hermit.exe comfy upload-mask --cu.upload-file images/mask.png --cu.api-options '{"original_ref":{"filename":"input.png","type":"input","subfolder":"references"},"type":"input","subfolder":"masks"}'
Hermit.exe comfy view --cu.api-options '{"filename":"image.png","type":"output","subfolder":""}' > image.png
Hermit.exe comfy view-metadata --cu.metadata-folder checkpoints --cu.api-options '{"filename":"model.safetensors"}'
Hermit.exe comfy system-stats
Hermit.exe comfy default-workflow
Hermit.exe comfy prompt-status
Hermit.exe comfy workflow-templates
Hermit.exe comfy template-file --cu.template-path "ExampleNode/example_workflows/sample.json" > sample.json
Hermit.exe comfy i18n
Hermit.exe comfy web-ui
Hermit.exe comfy users
Hermit.exe comfy user-create --cu.api-options '{"username":"Example User"}'
Hermit.exe comfy userdata-list --cu.api-options '{"dir":"settings","recurse":"true","full_info":"true"}'
Hermit.exe comfy userdata-list-v2 --cu.api-options '{"path":"settings"}'
Hermit.exe comfy userdata-get --cu.userdata-path settings/config.json --cu.user-id example_user_id > config.json
Hermit.exe comfy userdata-put --cu.userdata-path settings/config.json --cu.upload-file config.json --cu.api-options '{"overwrite":true,"full_info":true}'
Hermit.exe comfy userdata-move --cu.userdata-path settings/config.json --cu.userdata-destination archive/config.json --cu.api-options '{"overwrite":false}'
Hermit.exe comfy userdata-delete --cu.userdata-path archive/config.json
Hermit.exe comfy jobs --cu.api-options '{"status":"pending,in_progress","sort_by":"created_at","sort_order":"desc","limit":50,"offset":0}'
Hermit.exe comfy job --cu.prompt-id "job-uuid"
Hermit.exe comfy job-cancel --cu.prompt-id "job-uuid"
Hermit.exe comfy jobs-cancel --cu.api-options '{"job_ids":["job-uuid-1","job-uuid-2"]}'
```

`comfy users` は `/users` の保存方式・ユーザー情報を取得し、`comfy user-create` は `--cu.api-options` の `username` でユーザーを作成します。GET 応答は ComfyUI の multi-user 設定により異なります。`comfy userdata-list` は legacy file listing、`comfy userdata-list-v2` は file/directory listing を取得します。`comfy userdata-get` は指定ファイルを binary で標準出力へ書き出すため、保存する場合は redirect を使います。`comfy userdata-put` は `--cu.upload-file` の raw bytes を送信し、move/delete はそれぞれ remote path を変更・削除します。`--cu.user-id` を指定すると `comfy-user` header を付けます。remote path は相対 slash 区切りで指定し、traversal は拒否されます。`comfy jobs` は `status`、`workflow_id`、`sort_by`、`sort_order`、`limit`、`offset` で job 一覧を絞り込みます。`comfy job` は `--cu.prompt-id` の job 詳細を取得し、`comfy job-cancel` は実行中または待機中の job を取り消します。`comfy jobs-cancel` は `job_ids` 配列を使って複数 job を取り消します。`comfy models`、`comfy models-folder`、`comfy embeddings`、`comfy extensions`、`comfy features` は各一覧・機能 API を呼び出します。`comfy system-stats` はサーバー・device 状態を、`comfy default-workflow` は既定 workflow JSON を取得します。`comfy upload-image` は画像をアップロードし、`comfy upload-mask` は `original_ref` が示す画像の alpha mask をアップロードします。両操作の multipart 追加項目は `--cu.api-options` の JSON object で指定します。`comfy view` は filename/type/subfolder/preview/channel query に応じた画像 bytes を標準出力へ書き出すため、ファイル保存時はリダイレクトを使います。`comfy view-metadata` は `--cu.metadata-folder` と filename を使って safetensors metadata JSON を取得します。`--cu.model-folder` は `comfy models-folder` の取得先を指定します。`comfy object-info` は全 node schema を取得し、`--cu.node-class` を指定すると `/object_info/{node_class}` の schema を取得します。`comfy events` は WebSocket の text frame を JSON のまま出力し、binary frame は `{"type":"binary","data":"<base64>"}` 形式で出力します。`--cu.event-count` の既定値 0 は接続を継続して受信します。

`comfy prompt-status` は ComfyUI の `GET /prompt` から現在の queue status を取得します。`comfy workflow-templates` と `comfy i18n` は `/workflow_templates` と `/i18n` の JSON を取得します。`comfy template-file` は `--cu.template-path` の `module/relative-file` を `/api/workflow_templates/{module}/...` から取得します。`comfy web-ui` は `/` の HTML を標準出力へ書き出します。

## プロンプト、反復、マクロ

`tg` / `kc` モードでは、既定で `--llm.prompt-file prompt` の内容を読み、展開後のテキストを送信します。`--llm.prompt` に値を直接渡すと、プロンプトファイルより直接指定した文字列が使われます。生成結果は `--llm.output-file output` に追記されます。

マクロは `{{...}}` で囲みます。たとえば過去の出力から末尾 1024 トークンを取り込むには、prompt ファイルに次のように書きます。

```text
これまでの内容:
{{tail(file("output"), 1024)}}
```

`head` / `tail` などのトークン数指定関数は、`tg` / `kc` の token-count API を使います。

日時や反復番号をファイル名に使うこともできます。

```powershell
Hermit.exe generate --backend tg --iteration.count 3 --llm.output-file "chapter-{{N}}-{{datetime}}"
```

式の構文、利用可能な変数と関数、ファイル処理上の注意は [マクロ言語仕様](docs/SPECIFICATION.md#マクロ言語) を参照してください。

`--iteration.phases` に複数の値を渡すと、各反復で phase ごとに処理します。`--llm.paragraphs-file` を指定すると、段落ファイルのリストから phase を読み込みます。現在・前・次の phase は `{{phase}}`、`{{prev_phase}}`、`{{next_phase}}` で参照できます。

`--llm.code-block-extract` を有効にすると、生成 Markdown 内の名前付きコードブロックを個別ファイルへ出力できます。コードブロック名 `stdout` の内容はファイルに保存せず標準出力へ出します。

## サーバープロセス制御

`create-process` モードはサーバーを非同期に起動し、指定したホストとポートに接続できるまで待機します。

```powershell
Hermit.exe server start --process.executable koboldcpp.exe --process.arguments "--model models/model.gguf --port 5000" --process.port 5000
```

`terminate-process` モードは実行ファイル名またはフルパスに一致するプロセスを終了します。プロセス列挙と終了は Windows で実装されています。

```powershell
Hermit.exe server stop --process.executable koboldcpp.exe
```

## 詳細資料

- [技術仕様書](docs/SPECIFICATION.md): 実装済み CLI オプション、既定値、マクロ構文、ファイル処理、依存関係
- [バックエンド Web API 対応表](docs/BACKEND_WEB_API_COVERAGE.md): 対象 API の棚卸しと実装・テスト状況
- [課題一覧](docs/ISSUES.md): 既知の問題、優先順位、次の作業
- [ビルドとテストの手順](.agents/skills/hermit-build-test/SKILL.md)

## ライセンス

本プロジェクトは [MIT License](LICENSE) の下で提供されています。
