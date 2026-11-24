#include <array>
#include <algorithm>
#include <cstdint>
#include <vector>

#include "Hermit_png.hpp"
#include "Hermit_errors.hpp"

namespace Hermit
{
    namespace tEXt
    {
        using crc_table_type = std::array<uint32_t, 256>;

        constexpr crc_table_type generate_crc_table()
        {
            crc_table_type result{};
            for (std::uint32_t i{}; i <= 0xFF; ++i)
            {
                std::uint32_t value{ i };
                for (std::size_t k{}; k < 8; k++)
                {
                    value = (value & 1) ? (UINT32_C(0xEDB88320) ^ (value >> 1)) : (value >> 1);
                }
                result[i] = value;
            }
            return result;
        }

        constexpr const crc_table_type crc_table{ generate_crc_table() };
        constexpr std::array<unsigned char, 8> png_signature{ 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

        std::uint32_t calculate_crc32(const std::uint8_t* data, size_t length)
        {
            std::uint32_t c{ UINT32_C(0xFFFFFFFF) };
            for (std::size_t i{}; i < length; ++i)
            {
                c = crc_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
            }
            return c ^ UINT32_C(0xFFFFFFFF);
        }

        void append_uint32_be(std::uint32_t val, std::vector<unsigned char>& buffer)
        {
            buffer.push_back((val >> 24) & 0xFF);
            buffer.push_back((val >> 16) & 0xFF);
            buffer.push_back((val >> 8) & 0xFF);
            buffer.push_back(val & 0xFF);
        }

        std::vector<unsigned char> create_tEXt_chunk(std::string_view key, std::string_view text)
        {
            const std::size_t total_size{ 4 + 4 + key.size() + 1 + text.size() + 4 };
            std::vector<unsigned char> chunk;
            chunk.reserve(total_size);

            std::vector<unsigned char> data;
            data.insert(data.end(), key.begin(), key.end());
            data.push_back(0);
            data.insert(data.end(), text.begin(), text.end());

            append_uint32_be(static_cast<std::uint32_t>(data.size()), chunk);

            const std::size_t crc_start{ chunk.size() };
            chunk.push_back('t'); chunk.push_back('E'); chunk.push_back('X'); chunk.push_back('t');
            chunk.insert(chunk.end(), data.begin(), data.end());

            const std::uint32_t crc{ calculate_crc32(&chunk[crc_start], chunk.size() - crc_start) };
            append_uint32_be(crc, chunk);

            return chunk;
        }

        struct png_context
        {
            std::vector<unsigned char> result_bytes;
            std::vector<unsigned char> metadata_chunk;
            bool metadata_inserted{};
        };

        std::string insert_metadata(std::string_view image, std::string_view key, std::string_view metadata)
        {
            constexpr std::size_t ihdr_end_offset{ 8 + 25 };
            if (image.size() < ihdr_end_offset
                || !std::equal(png_signature.begin(), png_signature.end(), reinterpret_cast<const unsigned char*>(image.data()))
                || image[8] != 0 || image[9] != 0 || image[10] != 0 || image[11] != 13
                || image.substr(12, 4) != "IHDR")
            {
                Hermit::throw_exception(png_exception{});
            }

            const std::vector<unsigned char> text_chunk{ create_tEXt_chunk(key, metadata) };

            std::string result;
            result.reserve(image.size() + text_chunk.size());
            result.append(image.substr(0, ihdr_end_offset));
            result.append(reinterpret_cast<const char*>(text_chunk.data()), text_chunk.size());
            result.append(image.substr(ihdr_end_offset));
            return result;
        }

        std::uint32_t read_uint32_be(const unsigned char* p)
        {
            return (static_cast<std::uint32_t>(p[0]) << 24)
                | (static_cast<std::uint32_t>(p[1]) << 16)
                | (static_cast<std::uint32_t>(p[2]) << 8)
                | static_cast<std::uint32_t>(p[3]);
        }

        std::string extract_parameters(std::string_view image, std::string_view target_key)
        {
            const unsigned char* data{ reinterpret_cast<const unsigned char*>(image.data()) };
            const std::size_t size{ image.size() };

            constexpr std::size_t png_header_size{ 8 };
            if (size < png_header_size || !std::equal(png_signature.begin(), png_signature.end(), data))
            {
                Hermit::throw_exception(png_exception{});
            }

            std::size_t offset{ png_header_size };

            while (offset <= size && size - offset >= 12)
            {
                const std::uint32_t length{ read_uint32_be(data + offset) };
                const std::string_view type{ reinterpret_cast<const char*>(data + offset + 4), 4 };

                const std::size_t data_offset{ offset + 8 };
                const std::size_t remaining_size{ size - data_offset };
                if (remaining_size < 4 || static_cast<std::size_t>(length) > remaining_size - 4)
                {
                    break;
                }

                if (type == "tEXt")
                {
                    const unsigned char* chunk_data{ data + data_offset };

                    std::size_t key_length{};
                    while (key_length < length && chunk_data[key_length] != '\0')
                    {
                        ++key_length;
                    }

                    if (key_length < length)
                    {
                        const std::string_view key(reinterpret_cast<const char*>(chunk_data), key_length);

                        if (key == target_key)
                        {
                            const std::size_t text_offset{ key_length + 1 };
                            const std::size_t text_length{ length - text_offset };
                            return std::string(reinterpret_cast<const char*>(chunk_data + text_offset), text_length);
                        }
                    }
                }
                else if (type == "IEND")
                {
                    break;
                }

                offset += 12 + length;
            }

            Hermit::throw_exception(png_exception{});
        }
    }

} // namespace Hermit
