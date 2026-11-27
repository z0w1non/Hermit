#ifndef HERMIT_LRU_IMPL_HPP
#define HERMIT_LRU_IMPL_HPP

#include <cstdint>
#include <filesystem>
#include <ios>
#include <vector>
#include "nlohmann/json.hpp"
#include "Hermit_config.hpp"
#include "Hermit_errors.hpp"
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
    void lru_cache<Capacity>::to_file(const config& cfg, const filesystem::file_writer& writer) const
    {
        nlohmann::json cache{ nlohmann::json::array() };
        for (const token_count_string& element : get<by_lru>())
        {
            cache.push_back({ { "string", element.str }, { "tokens", element.tokens } });
        }
        const nlohmann::json json{ { "cache", std::move(cache) } };
        const std::vector<std::uint8_t> cbor{ nlohmann::json::to_cbor(json) };
        filesystem::write_file(cfg, reinterpret_cast<const char*>(cbor.data()), cbor.size(), ".token_cache.bin", std::ios::binary, writer);
    }

    template<std::size_t Capacity>
    void lru_cache<Capacity>::from_file(const config& cfg)
    {
        from_file(cfg, [](const std::filesystem::path& file, std::ios_base::openmode mode)
        {
            return filesystem::read_file_to_string(file, mode);
        });
    }

    template<std::size_t Capacity>
    void lru_cache<Capacity>::from_file(const config& cfg, const filesystem::file_reader& reader)
    {
        const std::filesystem::path cache_path{ filesystem::string_to_path_by_config(".token_cache.bin", cfg) };
        try
        {
            const std::string bytes{ reader(cache_path, std::ios::binary) };
            const std::vector<std::uint8_t> cbor{ bytes.begin(), bytes.end() };
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
        catch (const file_open_exception&)
        {
            return;
        }
        catch (const nlohmann::json::exception& e)
        {
            HERMIT_LOG(warning) << boost::diagnostic_information(e);
        }
    }
}

#endif
