#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#ifdef HYPERATE_HAVE_LIBWEBSOCKETS
struct lws;
struct lws_context;
#endif

namespace hyperate {

enum class ConnectionStatus {
	Disconnected,
	Connecting,
	Connected,
	Error,
};

struct HyperateClientConfig {
	std::string host = "app.hyperate.io";
	std::string path = "/ws";
	int port = 443;
	bool use_tls = true;
	std::string channel_id;
	std::string api_key;
	double smoothing_alpha = 0.25;
};

class HyperateClient {
public:
	HyperateClient();
	~HyperateClient();

	HyperateClient(const HyperateClient &) = delete;
	HyperateClient &operator=(const HyperateClient &) = delete;

	void start(HyperateClientConfig config);
	void stop();

	ConnectionStatus status() const;
	std::string status_message() const;

#ifdef HYPERATE_HAVE_LIBWEBSOCKETS
	void on_lws_established(::lws *wsi);
	void on_lws_receive(const char *message, size_t len);
	void on_lws_writable();
	void on_lws_connection_error();
	void on_lws_closed();
#endif

private:
	void run();
	void set_status(ConnectionStatus status, std::string message);
	void handle_text_message(const std::string &message);
	void send_join_frame();
	void send_leave_frame();
	void send_heartbeat_frame();
	void send_text_frame(const std::string &payload);

	HyperateClientConfig config_;
	std::thread thread_;
	std::atomic<bool> stop_requested_{false};
	mutable std::mutex status_mutex_;
	ConnectionStatus status_ = ConnectionStatus::Disconnected;
	std::string status_message_ = "Disconnected";

#ifdef HYPERATE_HAVE_LIBWEBSOCKETS
	::lws_context *lws_context_ = nullptr;
	::lws *wsi_ = nullptr;
	std::deque<std::string> pending_messages_;
#endif
};

const char *connection_status_name(ConnectionStatus status);

} // namespace hyperate
