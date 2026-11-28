#ifndef HERMIT_APP_TYPES_HPP
#define HERMIT_APP_TYPES_HPP

#include <string>
#include <string_view>
#include <vector>

namespace hermit
{
    struct Item
    {
        std::string head;
        std::vector<std::string> descriptions;
    };

    enum class CommandMode
    {
        kTg, kKc, kLmStudio, kLlamaCpp, kVllm, kOllama, kSd, kSb, kCu, kListModels, kListRunningModels, kShowModel, kGetOllamaVersion,
        kManageOllamaModel, kManageLmStudioModel, kCreateLmStudioChat, kCreateLmStudioResponse, kCreateLmStudioDecision,
        kCreateLmStudioMessages, kCreateLmStudioSystemone, kCreateVllmBatchChat, kCreateTgModerations, kCheckTgHealth,
        kGetTgModelInfo, kManageTgModel, kStopTgGeneration, kGetTgLogits, kCreateTgChatPrompt,
        kListTgLoras, kManageTgLoras,
        kManageComfyuiApi, kManageSdApi, kRequestSdApi, kRequestLlmApi,
        kCreateEmbeddings, kTokenize, kDetokenize,
        kExtractPngParameters, kCreateProcess, kTerminateProcess
    };

    CommandMode StringToCommandMode(std::string_view name);
}

#endif
