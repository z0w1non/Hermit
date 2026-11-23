#ifndef HERMIT_PUBLIC_HTTP_ADAPTER_HPP
#define HERMIT_PUBLIC_HTTP_ADAPTER_HPP

#include "Hermit_public_types.hpp"
#include "Hermit_tcp.hpp"

namespace Hermit::public_api
{
    http_request_data make_http_request_data(std::string_view host, std::string_view port,
        std::string_view target, std::string body, http_transport_options options = {});
    http_request make_http_request(tcp::request_executor executor);
    http_executor make_http_executor(tcp::request_executor executor);
    tcp::request_executor make_tcp_executor(http_executor executor);
}

#endif
