#ifndef HERMIT_LRU_IMPL_HPP
#define HERMIT_LRU_IMPL_HPP

#include <cstdint>
#include <filesystem>
#include <ios>
#include <iterator>
#include <vector>
#include <boost/nowide/fstream.hpp>
#include "nlohmann/json.hpp"
#include "Hermit_config.hpp"
#include "Hermit_filesystem.hpp"
#include "Hermit_log.hpp"

namespace Hermit
{
    template<std::size_t Capacity>
    lru_cache<Capacity>::lru_cache(const lru_cache::callback_type& callback)
        : callback{ callback }
    {
    }

    template<std::size_t Capacity>
    int lru_cache<Capacity>::get_tokens(std::string_view str)
    {
        int tokens{};
        if (const lru_cache::const_iterator iter{ get<by_key>().find(str) }; iter != get<by_key>().end())
        {
            tokens = iter->tokens;
            get<by_lru>().relocate(get<by_lru>().end(), get<by_lru>().iterator_to(*iter));
        }
        else
        {
            tokens = callback(str);
            insert({ std::string{ str }, tokens });
        }
        if (size() > capacity)
        {
            get<by_lru>().pop_front();
        }
        return tokens;
    }

    template<std::size_t Capacity>
    void lru_cache<Capacity>::to_file(const config& cfg) const
    {
        nlohmann::json cache{ nlohmann::json::array() };
        for (const token_count_string& element : get<by_lru>())
        {
            cache.push_back({ { "string", element.str }, { "tokens", element.tokens } });
        }
        const nlohmann::json json{ { "cache", std::move(cache) } };
        const std::vector<std::uint8_t> cbor{ nlohmann::json::to_cbor(json) };
        filesystem::write_file(cfg, reinterpret_cast<const char*>(cbor.data()), cbor.size(), ".token_cache.bin", std::ios::binary);
    }

    template<std::size_t Capacity>
    void lru_cache<Capacity>::from_file(const config& cfg)
    {
        const std::filesystem::path cache_path{ filesystem::string_to_path_by_config(".token_cache.bin", cfg) };
        if (!std::filesystem::exists(cache_path)) return;
        try
        {
            boost::nowide::ifstream ifs{ cache_path, std::ios::binary };
            if (!ifs.is_open()) return;
            const std::vector<std::uint8_t> cbor{ std::istreambuf_iterator<char>{ ifs }, std::istreambuf_iterator<char>{} };
            const nlohmann::json json{ nlohmann::json::from_cbor(cbor) };
            if (!json.is_object() || !json.contains("cache") || !json["cache"].is_array()) return;
            lru_cache temp_lru_cache{ callback };
            for (const nlohmann::json& cache : json["cache"])
            {
                if (cache.is_object() && cache.contains("string") && cache.contains("tokens"))
                    temp_lru_cache.insert({ cache["string"].get<std::string>(), cache["tokens"].get<int>() });
            }
            *this = std::move(temp_lru_cache);
        }
        catch (const nlohmann::json::exception& e)
        {
            HERMIT_LOG(warning) << boost::diagnostic_information(e);
        }
    }
}

#endif
