#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sispopmq {
class SispopMQ;
struct Allow;
class Message;
} // namespace sispopmq

using sispopmq::SispopMQ;

namespace oxen {

struct oxend_key_pair_t;
class ServiceNode;
class RequestHandler;

class SispopmqServer {

    std::unique_ptr<SispopMQ> sispopmq_;

    // Has information about current SNs
    ServiceNode* service_node_;

    RequestHandler* request_handler_;

    // Get nodes' address
    std::string peer_lookup(std::string_view pubkey_bin) const;

    // Handle Session data coming from peer SN
    void handle_sn_data(sispopmq::Message& message);

    // Handle Session client requests arrived via proxy
    void handle_sn_proxy_exit(sispopmq::Message& message);

    // v2 indicates whether to use the new (v2) protocol
    void handle_onion_request(sispopmq::Message& message, bool v2);

    void handle_get_logs(sispopmq::Message& message);

    void handle_get_stats(sispopmq::Message& message);

    uint16_t port_ = 0;

    // Access keys for the 'service' category as binary
    std::vector<std::string> stats_access_keys;

  public:
    SispopmqServer(uint16_t port);
    ~SispopmqServer();

    // Initialize sispopmq
    void init(ServiceNode* sn, RequestHandler* rh,
              const oxend_key_pair_t& keypair,
              const std::vector<std::string>& stats_access_key);

    uint16_t port() { return port_; }

    /// True if SispopMQ instance has been set
    explicit operator bool() const { return (bool)sispopmq_; }
    /// Dereferencing via * or -> accesses the contained SispopMQ instance.
    SispopMQ& operator*() const { return *sispopmq_; }
    SispopMQ* operator->() const { return sispopmq_.get(); }
};

} // namespace oxen
