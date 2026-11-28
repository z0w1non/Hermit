# Hermit

Language: [日本語](../../../readme.md) | English

Hermit is a C++20 command-line application for Windows. It connects to text, image, and speech generation backends, expands prompt templates, runs iterative workflows, and saves generated output.

Supported backends include [text-generation-webui](https://github.com/oobabooga/text-generation-webui), [KoboldCpp](https://github.com/LostRuins/koboldcpp), [AUTOMATIC1111 Stable Diffusion WebUI](https://github.com/AUTOMATIC1111/stable-diffusion-webui), [Style-Bert-VITS2](https://github.com/litagin02/Style-Bert-VITS2), [ComfyUI](https://github.com/Comfy-Org/ComfyUI), [LM Studio](https://lmstudio.ai/), [llama.cpp server](https://github.com/ggml-org/llama.cpp/tree/master/tools/server), [vLLM](https://github.com/vllm-project/vllm), and [Ollama](https://ollama.com/). Hermit does not implement every API exposed by these backends. See the [backend API coverage ledger](../../BACKEND_WEB_API_COVERAGE.md) for supported operations and known gaps.

## Features

- Generate text with `tg`, `kc`, OpenAI-compatible `lmstudio`, `llama-cpp`, and `vllm`, or the native `ollama` API. Select Completions or Chat Completions where supported.
- Generate and save images through Stable Diffusion WebUI, and audio through Style-Bert-VITS2.
- Submit ComfyUI workflows, upload input images, and save generated files.
- Call selected backend APIs directly with `backend-api request` and Stable Diffusion APIs with `sd-api`.
- Evaluate `{{...}}` expressions in prompts and supported file paths. Built-ins include file reading, text truncation, dates, random values, environment variables, and sub-prompts.
- Run multiple iterations and phases, and extract named Markdown code blocks to files.
- Extract generation parameters from PNG files and start or stop server processes on Windows.

The public macro language evaluates expressions. Assignment and statement syntax such as `if` and `while` is disabled. See the [specification](../../SPECIFICATION.md) for syntax, built-ins, and limitations.

## Requirements

- Windows x64
- Visual Studio MSVC C++20 build tools
- vcpkg with Visual Studio MSBuild integration
- Boost restored from the vcpkg manifest; Google Test is also needed to build tests

Use the repository `.vsconfig` to install the Visual Studio components. If needed, enable vcpkg integration once:

```powershell
vcpkg integrate install
```

Example Release x64 incremental build. This locates Visual Studio 18 with `vswhere.exe`, initializes its environment, and invokes MSBuild in the same console:

```powershell
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$install = & $vswhere -products * -property installationPath | Where-Object { $_ -match '\\18\\' } | Select-Object -First 1
$devcmd = Join-Path $install 'Common7\Tools\VsDevCmd.bat'
$buildCommand = 'call "{0}" -arch=x64 -host_arch=x64 && where msbuild && msbuild .\Hermit.sln /m:1 /t:Build /p:VisualStudioVersion=18.0 /p:Configuration=Release /p:Platform=x64 /v:normal' -f $devcmd
cmd.exe /d /c $buildCommand
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

`Hermit.sln` contains the application and test projects. Prefer `/t:Build`; use `/t:Rebuild` only when a clean build is needed. See the [specification](../../SPECIFICATION.md) and [build/test instructions](../../../.agents/skills/hermit-build-test/SKILL.md) for details.

## Command line

Run `Hermit.exe --help` for the full option list. Commands come before their options:

```powershell
Hermit.exe generate --backend tg --llm.host localhost --llm.port 5000 --llm.prompt-file prompt
```

The main backends are `tg`, `kc`, `lmstudio`, `llama-cpp`, `vllm`, `ollama`, `sd`, `sb`, and `cu`. Common commands include `generate`, `backend-api request`, `models`, `responses`, `decisions`, `embeddings`, `tokenize`, `server`, and `png`. Options use the `--namespace.property` form; INI files use the same dotted keys. Only `-h` is a short option. The old `--mode` and flat hyphenated option names are not accepted. See the [CLI option reference](../../SPECIFICATION.md) for details.

LM Studio Responses API accepts a JSON request through `--llm.api-options`. A normal request returns a JSON object; `stream:true` writes incoming SSE bytes to standard output as they arrive. Tool execution and response lifecycle operations are not implemented.

```powershell
Hermit.exe responses create --backend lmstudio --llm.api-options '{"model":"openai/gpt-oss-20b","input":"Hello"}'
Hermit.exe responses create --backend lmstudio --llm.api-options '{"model":"openai/gpt-oss-20b","input":"Hello","stream":true}'
```

Other implemented LM Studio operations include native stateful chat, Anthropic Messages, and Jev System One. `stream:true` passes their native SSE events through to standard output.

```powershell
Hermit.exe lmstudio-chat create --backend lmstudio --llm.api-options '{"model":"ibm/granite-4-micro","input":"Hello","stream":true}'
Hermit.exe messages create --backend lmstudio --llm.api-options '{"model":"ibm/granite-4-micro","max_tokens":128,"messages":[{"role":"user","content":"Hello"}],"stream":true}'
Hermit.exe systemone create --backend lmstudio --llm.api-options '{"model":"decision-model","state":"I was charged twice.","questions":{"angry":{"type":"noul","instructions":"Is the customer angry?"}}}'
```

vLLM batch chat sends multiple conversations in one request. Streaming and tools are not supported for this operation.

```powershell
Hermit.exe batch-chat create --backend vllm --llm.api-options '{"model":"Qwen/Qwen2.5-1.5B-Instruct","messages":[[{"role":"user","content":"Capital of France?"}],[{"role":"user","content":"Capital of Japan?"}] ]}'
```

Text Generation Web UI also supports moderation, health checks, current model information, model listing/loading/unloading, and tokenization. Admin routes prefer `--llm.admin-key` and fall back to `--llm.api-key`.

```powershell
Hermit.exe moderations create --backend tg --llm.api-options '{"input":"Example text to classify"}'
Hermit.exe health check --backend tg
Hermit.exe models info --backend tg
Hermit.exe models list --backend tg --llm.admin-key "admin-token"
Hermit.exe models load --backend tg --llm.model-action-options '{"model_name":"ExampleModel","args":{"n_gpu_layers":8}}' --llm.admin-key "admin-token"
Hermit.exe models unload --backend tg --llm.admin-key "admin-token"
Hermit.exe tokenize encode --backend tg --llm.prompt "Hello world"
Hermit.exe tokenize decode --backend tg --llm.token-ids 123 456
```

For an HTTP route without a dedicated command, use `backend-api request`. JSON or object responses are printed as received; binary responses are written to standard output. The generic path does not provide per-operation schema validation or streaming/WebSocket handling.

```powershell
Hermit.exe backend-api request --backend lmstudio --llm.api-method POST --llm.api-path /v1/responses --llm.api-options '{"model":"openai/gpt-oss-20b","input":"Hello"}'
```

Use `--llm.api-body-file` for raw or multipart bodies, `--llm.api-content-type` for Content-Type, and `--llm.api-headers` for additional headers. HTTP response bodies are limited to 128 MiB. See the [API coverage ledger](../../BACKEND_WEB_API_COVERAGE.md) for operation status.

Launching child processes through the `exec(...)` macro is disabled by default. Enable `--security.allow-process-exec` or set `security.allow-process-exec = true` in the INI file only when using trusted prompts and templates.

### Configuration file

Hermit reads `config.ini` by default. The file uses INI syntax. Command-line values take precedence over values in the configuration file. Relative paths are resolved from `--path.base`.

```ini
backend = tg
llm.host = localhost
llm.port = 5000
llm.prompt-file = prompt
llm.output-file = output
logging.console = false
```

You can configure everything on the command line instead:

```powershell
Hermit.exe generate --backend kc --llm.host localhost --llm.port 5000 --llm.prompt-file prompt
```

File paths are relative to `--path.base`. Use `/` as the path separator. Text-file extensions may be omitted; `.txt` is used by default. See [startup and configuration](../../SPECIFICATION.md) for path rules and exceptions.

## Backend setup and examples

Install and start each backend separately. Configure Hermit's host and port to match the server.

### text-generation-webui (`tg`)

Enable the API in the text-generation-webui startup settings, then start the server:

```powershell
Hermit.exe generate --backend tg --llm.host localhost --llm.port 5000 --llm.prompt-file prompt --llm.output-file output
```

### KoboldCpp (`kc`)

Start KoboldCpp as an API server, for example:

```bat
koboldcpp.exe --model models/model.gguf --usecuda --port 5000
```

Then run Hermit:

```powershell
Hermit.exe generate --backend kc --llm.host localhost --llm.port 5000 --llm.prompt-file prompt --llm.output-file output
```

For image input, use a vision-capable KoboldCpp model and configuration, then set `--llm.mode chat-completions` and `--llm.image-file`.

### OpenAI-compatible local LLM servers

LM Studio, llama.cpp server, and vLLM use OpenAI-compatible `/v1/completions` and `/v1/chat/completions` endpoints. Their default ports are 1234, 8080, and 8000. Set the server's model ID with `--llm.model`; use `--llm.api-key` if authentication is required. Token counts use `usage.prompt_tokens` from the completions API.

```powershell
Hermit.exe generate --backend lmstudio --llm.model "model-id" --llm.mode chat-completions --llm.prompt-file prompt
Hermit.exe generate --backend llama-cpp --llm.model "model-id" --llm.mode chat-completions --llm.prompt-file prompt
Hermit.exe generate --backend vllm --llm.model "model-id" --llm.mode chat-completions --llm.prompt-file prompt --llm.api-key "token"
```

Embeddings use `POST /v1/embeddings`; the selected model must support embeddings.

```powershell
Hermit.exe embeddings create --backend lmstudio --llm.model "embedding-model" --llm.embedding-input "first text" "second text"
Hermit.exe embeddings create --backend llama-cpp --llm.model "embedding-model" --llm.prompt "text to embed"
Hermit.exe embeddings create --backend vllm --llm.model "embedding-model" --llm.prompt "text to embed" --llm.api-key "token"
```

### Ollama (`ollama`)

Ollama uses `/api/generate` and `/api/chat` on port 11434 by default. Set the local model name and tag with `--llm.model`.

```powershell
Hermit.exe generate --backend ollama --llm.model "llama3.2:latest" --llm.mode chat-completions --llm.prompt-file prompt
Hermit.exe embeddings create --backend ollama --llm.model "embeddinggemma" --llm.embedding-input "first text" "second text" --llm.embedding-options '{"truncate":false,"dimensions":256}'
```

List models with `models list`. LM Studio uses `/api/v1/models`; llama.cpp and vLLM use `/v1/models`; Ollama uses `/api/tags`; and Text Generation Web UI uses its admin route `/v1/internal/model/list`. Override the route with `--llm.models-target`.

```powershell
Hermit.exe models list --backend lmstudio
Hermit.exe models list --backend llama-cpp
Hermit.exe models list --backend vllm --llm.api-key "token"
Hermit.exe models list --backend ollama
Hermit.exe models list --backend tg --llm.admin-key "admin-token"
```

Ollama model details, running models, and server version are available through `models show`, `models running`, and `models version`. Model lifecycle commands accept JSON objects through `--llm.model-action-options`.

```powershell
Hermit.exe models show --backend ollama --llm.model "gemma4" --llm.model-info-options '{"verbose":true}'
Hermit.exe models running --backend ollama
Hermit.exe models version --backend ollama
Hermit.exe models pull --backend ollama --llm.model-action-options '{"model":"llama3.2","stream":false}'
Hermit.exe models copy --backend ollama --llm.model-action-options '{"source":"llama3.2","destination":"llama3.2-backup"}'
Hermit.exe models delete --backend ollama --llm.model-action-options '{"model":"custom-assistant"}'
```

LM Studio native REST model load, unload, download, and download-job status are also available from `models`:

```powershell
Hermit.exe models load --backend lmstudio --llm.model-action-options '{"model":"openai/gpt-oss-20b","context_length":16384,"flash_attention":true}'
Hermit.exe models unload --backend lmstudio --llm.model-action-options '{"instance_id":"instance-id"}'
Hermit.exe models download --backend lmstudio --llm.model-action-options '{"model":"openai/gpt-oss-20b","quantization":"Q4_K_M"}'
Hermit.exe models download-status --backend lmstudio --llm.model-action-options '{"job_id":"job-id"}'
```

The llama.cpp and Text Generation Web UI tokenization APIs are available through `tokenize encode` and `tokenize decode`.

```powershell
Hermit.exe tokenize encode --backend llama-cpp --llm.prompt "Hello world" --llm.tokenize-options '{"with_pieces":true}'
Hermit.exe tokenize decode --backend llama-cpp --llm.token-ids 123 456
Hermit.exe tokenize encode --backend tg --llm.prompt "Hello world"
Hermit.exe tokenize decode --backend tg --llm.token-ids 123 456
```

### AUTOMATIC1111 Stable Diffusion WebUI (`sd`)

`--sd.generation-options` adds arbitrary JSON fields to txt2img/img2img requests. Server-specific extension fields are supported; fields in this JSON object take precedence over existing settings with the same key.

```powershell
Hermit.exe generate --backend sd --sd.generation-options '{"hr_prompt":"high detail","custom_extension_field":{"enabled":true}}'
Hermit.exe generate --backend sd --sd.host localhost --sd.port 7860 --sd.steps 30 --sd.prompt "sky" --sd.output-file "{{datetime}}.png"
```

The `sd-api` command covers progress, interrogation, interrupt/skip, Extras, PNG info, options, model and extension lists, refresh operations, embedding/hypernetwork operations, memory and script information, checkpoint unload/reload, and arbitrary HTTP requests. Runtime OpenAPI routes can be listed with `sd-api routes`. Streaming and WebSocket are not supported. The WebUI must be started with API support enabled. The default port is 7860.

```powershell
Hermit.exe sd-api routes
Hermit.exe sd-api request --sd.api-method GET --sd.api-path /internal/ping
Hermit.exe sd-api options-set --sd.api-options '{"samples_save":false}'
Hermit.exe sd-api interrogate --sd.api-options '{"image":"<base64>","model":"clip"}'
Hermit.exe sd-api interrupt
```

`--sd.seed-resize-from-h/w` applies to both generation modes. Use `--sd.img2img-seed-resize-from-h/w` for img2img-specific values; a value of `-1` inherits the common setting.

### Style-Bert-VITS2 (`sb`)

Start the Style-Bert-VITS2 API server and configure its host and port:

```powershell
Hermit.exe generate --backend sb --sb.host localhost --sb.port 5001 --sb.model-name "amitaro" --sb.speaker-id 0 --sb.language JP --sb.text "Hello" --sb.output-file "{{datetime}}.wav"
```

### ComfyUI (`cu`)

Start ComfyUI and save an API workflow as JSON. Pass it with `--cu.prompt-file`:

```powershell
Hermit.exe generate --backend cu --cu.host localhost --cu.port 8188 --cu.prompt-file workflow.json --cu.output-directory output
Hermit.exe generate --backend cu --cu.prompt-file workflow.json --cu.upload-images source=images/source.png mask=images/mask.png
```

Generated files are saved to `--cu.output-directory`. `--cu.preserve-subdirectories` retains server-side subdirectories. The `comfy` command also provides queue/history and job operations, node schemas, model and extension lists, image/mask upload and retrieval, metadata, user data, system status, workflow templates, and WebSocket events. JSON query fields are passed with `--cu.api-options`.

```powershell
Hermit.exe comfy queue
Hermit.exe comfy history --cu.api-options '{"max_items":100,"offset":0}'
Hermit.exe comfy object-info --cu.node-class "KSampler"
Hermit.exe comfy models
Hermit.exe comfy upload-image --cu.upload-file images/input.png --cu.api-options '{"type":"input","subfolder":"references","overwrite":true}'
Hermit.exe comfy view --cu.api-options '{"filename":"image.png","type":"output","subfolder":""}' > image.png
Hermit.exe comfy events --cu.client-id "hermit-client" --cu.event-count 20
Hermit.exe comfy jobs --cu.api-options '{"status":"pending,in_progress","sort_by":"created_at","sort_order":"desc","limit":50,"offset":0}'
```

`comfy events` prints text frames as JSON and binary frames as `{"type":"binary","data":"<base64>"}`. A `--cu.event-count` value of 0 keeps receiving events until interrupted. User-data paths are relative slash-separated paths; traversal is rejected. Use shell redirection to save binary output from `comfy userdata-get` and `comfy view`.

## Prompts, iterations, and macros

In `tg` and `kc` modes, Hermit reads `--llm.prompt-file prompt` by default and sends the expanded text. A value passed through `--llm.prompt` takes precedence. Results are appended to `--llm.output-file output`.

Wrap expressions in `{{...}}`. For example, include the last 1024 tokens of prior output in a prompt file:

```text
Previous content:
{{tail(file("output"), 1024)}}
```

Token-count functions such as `head` and `tail` use the token-count API of `tg` or `kc`.

Use dates and iteration numbers in output paths:

```powershell
Hermit.exe generate --backend tg --iteration.count 3 --llm.output-file "chapter-{{N}}-{{datetime}}"
```

Pass multiple values to `--iteration.phases` to process phases in each iteration. Or set `--llm.paragraphs-file` to read phases from a paragraph list. The current, previous, and next phase are available as `{{phase}}`, `{{prev_phase}}`, and `{{next_phase}}`.

Enable `--llm.code-block-extract` to save named code blocks from generated Markdown to separate files. A code block named `stdout` is written to standard output instead.

See the [specification](../../SPECIFICATION.md) for expression syntax, variables, functions, and file-processing constraints.

## Server process control

`server start` launches a server process asynchronously and waits until the configured host and port accept connections:

```powershell
Hermit.exe server start --process.executable koboldcpp.exe --process.arguments "--model models/model.gguf --port 5000" --process.port 5000
```

`server stop` terminates processes matching an executable name or full path. Process enumeration and termination are implemented on Windows.

```powershell
Hermit.exe server stop --process.executable koboldcpp.exe
```

## Documentation

- [Specification](../../SPECIFICATION.md): implemented CLI options, defaults, macro syntax, file handling, and dependencies
- [Backend Web API coverage ledger](../../BACKEND_WEB_API_COVERAGE.md): API inventory and implementation/test status
- [Issue tracker](../../ISSUES.md): known issues, priorities, and next steps
- [Build and test instructions](../../../.agents/skills/hermit-build-test/SKILL.md)

## License

This project is distributed under the [MIT License](../../../LICENSE).
