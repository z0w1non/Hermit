#ifndef HERMIT_LRU_HPP
#define HERMIT_LRU_HPP

#include <functional>
#include <string_view>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include "context.hpp"
#include "filesystem.hpp"

namespace hermit
{
    struct config;
    namespace detail::lru_cache
    {
        struct by_key {};
        struct by_lru {};
        using key_index = boost::multi_index::hashed_unique<boost::multi_index::tag<by_key>, boost::multi_index::member<token_count_string, std::string, &token_count_string::str>, string_hash, std::equal_to<std::string_view>>;
        using lru_sequenced_index = boost::multi_index::sequenced<boost::multi_index::tag<by_lru>>;
        using indices = boost::multi_index::indexed_by<key_index, lru_sequenced_index>;
        using base = boost::multi_index::multi_index_container<token_count_string, indices>;
    }
    template<std::size_t Capacity>
    struct lru_cache : private detail::lru_cache::base
    {
        using by_key = detail::lru_cache::by_key;
        using by_lru = detail::lru_cache::by_lru;
        using callback_type = std::function<int(std::string_view)>;
        static constexpr std::size_t capacity{ Capacity };
        lru_cache(const callback_type& callback);
        lru_cache(const lru_cache&) = default;
        lru_cache(lru_cache&&) = default;
        lru_cache& operator=(const lru_cache&) = default;
        lru_cache& operator=(lru_cache&&) = default;
        int get_tokens(std::string_view str);
        void to_file(const config& cfg) const;
        void to_file(const config& cfg, const filesystem::file_writer& writer) const;
        void from_file(const config& cfg);
        void from_file(const config& cfg, const filesystem::file_reader& reader);
    private:
        callback_type callback;
    };

}

#endif
