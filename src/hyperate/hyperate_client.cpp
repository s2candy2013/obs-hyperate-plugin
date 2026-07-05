#include "hyperate/hyperate_client.hpp"

#include "heart_rate/heart_rate_state.hpp"
#include "util/log.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#ifdef HYPERATE_HAVE_LIBWEBSOCKETS
#include <libwebsockets.h>
#endif

namespace hyperate {
namespace {

#ifdef HYPERATE_HAVE_LIBWEBSOCKETS
void hyperate_lws_log(int level, const char *line)
{
	const int obs_level = (level & LLL_ERR) ? LOG_ERROR : ((level & LLL_WARN) ? LOG_WARNING : LOG_INFO);
	HYPERATE_LOG(obs_level, "libwebsockets: %s", line ? line : "");
}
#endif

std::optional<double> extract_number_after_key(const std::string &message, const char *key)
{
	const std::string quoted_key = std::string("\"") + key + "\"";
	size_t key_pos = message.find(quoted_key);
	if (key_pos == std::string::npos)
		return std::nullopt;

	size_t colon = message.find(':', key_pos + quoted_key.size());
	if (colon == std::string::npos)
		return std::nullopt;

	size_t start = colon + 1;
	while (start < message.size() && std::isspace(static_cast<unsigned char>(message[start])))
		++start;

	size_t end = start;
	while (end < message.size() &&
	       (std::isdigit(static_cast<unsigned char>(message[end])) || message[end] == '.'))
		++end;

	if (end == start)
		return std::nullopt;

	try {
		return std::stod(message.substr(start, end - start));
	} catch (...) {
		return std::nullopt;
	}
}

std::optional<double> extract_bpm(const std::string &message)
{
	// HypeRate sends Phoenix JSON messages such as:
	// {"event":"hr_update","payload":{"hr":79},"ref":null,"topic":"hr:<id>"}
	// Keep this permissive so older examples that use bpm/heartRate also work.
	for (const char *key : {"bpm", "heartRate", "heart_rate", "hr"}) {
		if (auto value = extract_number_after_key(message, key))
			return value;
	}
	return std::nullopt;
}

bool message_contains_json_value(const std::string &message, const char *key, const char *value)
{
	const std::string expected = std::string("\"") + key + "\":\"" + value + "\"";
	return message.find(expected) != std::string::npos;
}

std::string append_path_segment(std::string path, const std::string &segment)
{
	if (segment.empty() || path.find(segment) != std::string::npos)
		return path;

	if (path.empty() || path.back() != '/')
		path += '/';

	return path + segment;
}

std::string build_connect_path(const std::string &path, const std::string &channel_id, const std::string &api_key)
{
	std::string output = append_path_segment(path.empty() ? "/ws" : path, channel_id);
	if (api_key.empty() || output.find("token=") != std::string::npos)
		return output;

	std::ostringstream with_token;
	with_token << output << (output.find('?') == std::string::npos ? '?' : '&') << "token=" << api_key;
	return with_token.str();
}

std::string redact_token(const std::string &path)
{
	const size_t token_pos = path.find("token=");
	if (token_pos == std::string::npos)
		return path;

	size_t value_start = token_pos + 6;
	size_t value_end = path.find('&', value_start);
	if (value_end == std::string::npos)
		value_end = path.size();

	std::string output = path;
	output.replace(value_start, value_end - value_start, "<redacted>");
	return output;
}

std::string hr_topic(const std::string &channel_id)
{
	return "hr:" + channel_id;
}

long long unix_time_millis()
{
	const auto now = std::chrono::system_clock::now().time_since_epoch();
	return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

#ifdef HYPERATE_HAVE_LIBWEBSOCKETS
int hyperate_lws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *, void *in, size_t len)
{
	auto *client = static_cast<HyperateClient *>(lws_get_opaque_user_data(wsi));
	if (!client)
		return 0;

	switch (reason) {
	case LWS_CALLBACK_CLIENT_ESTABLISHED:
		client->on_lws_established(wsi);
		break;
	case LWS_CALLBACK_CLIENT_RECEIVE:
		client->on_lws_receive(static_cast<const char *>(in), len);
		break;
	case LWS_CALLBACK_CLIENT_WRITEABLE:
		client->on_lws_writable();
		break;
	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		client->on_lws_connection_error();
		break;
	case LWS_CALLBACK_CLIENT_CLOSED:
		client->on_lws_closed();
		break;
	default:
		break;
	}

	return 0;
}
#endif

} // namespace

HyperateClient::HyperateClient() = default;

HyperateClient::~HyperateClient()
{
	stop();
}

void HyperateClient::start(HyperateClientConfig config)
{
	stop();
	config_ = std::move(config);
	heart_rate_state().set_smoothing_alpha(config_.smoothing_alpha);
	stop_requested_ = false;
	heart_rate_state().set_live(false);
	set_status(ConnectionStatus::Connecting, "Connecting");
	HYPERATE_LOG(LOG_INFO, "Connect requested for HypeRate ID '%s'", config_.channel_id.c_str());
	thread_ = std::thread(&HyperateClient::run, this);
}

void HyperateClient::stop()
{
	stop_requested_ = true;

#ifdef HYPERATE_HAVE_LIBWEBSOCKETS
	if (lws_context_)
		lws_cancel_service(lws_context_);
#endif

	if (thread_.joinable())
		thread_.join();

	heart_rate_state().set_live(false);
	set_status(ConnectionStatus::Disconnected, "Disconnected");
}

ConnectionStatus HyperateClient::status() const
{
	std::lock_guard<std::mutex> lock(status_mutex_);
	return status_;
}

std::string HyperateClient::status_message() const
{
	std::lock_guard<std::mutex> lock(status_mutex_);
	return status_message_;
}

void HyperateClient::set_status(ConnectionStatus status, std::string message)
{
	std::lock_guard<std::mutex> lock(status_mutex_);
	status_ = status;
	status_message_ = std::move(message);
}

void HyperateClient::handle_text_message(const std::string &message)
{
	if (message_contains_json_value(message, "event", "phx_reply") &&
	    message_contains_json_value(message, "status", "ok")) {
		set_status(ConnectionStatus::Connected, "Joined channel; waiting for BPM");
		return;
	}

	if (auto bpm = extract_bpm(message)) {
		heart_rate_state().submit_bpm(*bpm);
		set_status(ConnectionStatus::Connected, "Receiving heart rate");
		return;
	}

	HYPERATE_LOG(LOG_DEBUG, "Socket message did not contain a BPM field: %s", message.c_str());
}

void HyperateClient::run()
{
#ifndef HYPERATE_HAVE_LIBWEBSOCKETS
	(void)config_;
	set_status(ConnectionStatus::Error, "Built without libwebsockets");
	HYPERATE_LOG(LOG_WARNING, "libwebsockets is unavailable; HypeRate connection is disabled");
	while (!stop_requested_)
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
#else
	lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE, hyperate_lws_log);
	HYPERATE_LOG(LOG_INFO, "Using libwebsockets %s", lws_get_library_version());

	lws_protocols protocols[2] = {};
	protocols[0].name = "obs-hyperate";
	protocols[0].callback = hyperate_lws_callback;
	protocols[0].per_session_data_size = 0;
	protocols[0].rx_buffer_size = 4096;

	lws_context_creation_info info{};
	info.port = CONTEXT_PORT_NO_LISTEN;
	info.protocols = protocols;
	info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
	info.uid = -1;
	info.gid = -1;

	lws_context_ = lws_create_context(&info);
	if (!lws_context_) {
		set_status(ConnectionStatus::Error, "Could not create WebSocket context");
		return;
	}

	const std::string connect_path = build_connect_path(config_.path, config_.channel_id, config_.api_key);
	HYPERATE_LOG(LOG_INFO, "Opening WebSocket to wss://%s%s", config_.host.c_str(),
		     redact_token(connect_path).c_str());

	lws_client_connect_info connect_info{};
	connect_info.context = lws_context_;
	connect_info.address = config_.host.c_str();
	connect_info.port = config_.port;
	connect_info.path = connect_path.c_str();
	connect_info.host = config_.host.c_str();
	connect_info.origin = "https://app.hyperate.io";
	connect_info.protocol = nullptr;
	connect_info.local_protocol_name = protocols[0].name;
	connect_info.alpn = "http/1.1";
	connect_info.ietf_version_or_minus_one = -1;
	connect_info.opaque_user_data = this;
	connect_info.pwsi = &wsi_;
	connect_info.ssl_connection = config_.use_tls ? LCCSCF_USE_SSL : 0;

	wsi_ = lws_client_connect_via_info(&connect_info);
	if (!wsi_) {
		set_status(ConnectionStatus::Error, "Could not start WebSocket connection");
		lws_context_destroy(lws_context_);
		lws_context_ = nullptr;
		return;
	}

	auto next_heartbeat = std::chrono::steady_clock::now() + std::chrono::seconds(15);
	auto handshake_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	bool handshake_timed_out = false;
	while (!stop_requested_)
	{
		lws_service(lws_context_, 100);

		const auto now = std::chrono::steady_clock::now();
		if (!handshake_timed_out && status() == ConnectionStatus::Connecting && now >= handshake_deadline) {
			handshake_timed_out = true;
			HYPERATE_LOG(LOG_ERROR, "WebSocket handshake timed out");
			set_status(ConnectionStatus::Error, "WebSocket handshake timed out");
		}

		if (wsi_ && status() == ConnectionStatus::Connected && now >= next_heartbeat) {
			send_heartbeat_frame();
			next_heartbeat = now + std::chrono::seconds(15);
		}
	}

	send_leave_frame();

	lws_context_destroy(lws_context_);
	lws_context_ = nullptr;
	wsi_ = nullptr;
	pending_messages_.clear();
#endif
}

void HyperateClient::send_join_frame()
{
#ifdef HYPERATE_HAVE_LIBWEBSOCKETS
	if (!wsi_ || config_.channel_id.empty())
		return;

	const std::string payload = "{\"topic\":\"" + hr_topic(config_.channel_id) +
				    "\",\"event\":\"phx_join\",\"payload\":{},\"ref\":\"1\"}";
	send_text_frame(payload);
#endif
}

void HyperateClient::send_leave_frame()
{
#ifdef HYPERATE_HAVE_LIBWEBSOCKETS
	if (!wsi_ || config_.channel_id.empty())
		return;

	const std::string payload = "{\"topic\":\"" + hr_topic(config_.channel_id) +
				    "\",\"event\":\"phx_leave\",\"payload\":{},\"ref\":0}";
	send_text_frame(payload);
#endif
}

void HyperateClient::send_heartbeat_frame()
{
#ifdef HYPERATE_HAVE_LIBWEBSOCKETS
	if (!wsi_)
		return;

	send_text_frame("{\"event\":\"ping\",\"payload\":{\"timestamp\":" + std::to_string(unix_time_millis()) +
			"}}");
#endif
}

void HyperateClient::send_text_frame(const std::string &payload)
{
#ifdef HYPERATE_HAVE_LIBWEBSOCKETS
	if (!wsi_)
		return;

	pending_messages_.push_back(payload);
	lws_callback_on_writable(wsi_);
#endif
}

#ifdef HYPERATE_HAVE_LIBWEBSOCKETS
void HyperateClient::on_lws_established(::lws *wsi)
{
	wsi_ = wsi;
	HYPERATE_LOG(LOG_INFO, "WebSocket established; sending channel join");
	heart_rate_state().set_live(true);
	set_status(ConnectionStatus::Connected, "Socket connected; joining channel");
	send_join_frame();
}

void HyperateClient::on_lws_receive(const char *message, size_t len)
{
	handle_text_message(std::string(message, len));
}

void HyperateClient::on_lws_writable()
{
	if (!wsi_ || pending_messages_.empty())
		return;

	std::string payload = std::move(pending_messages_.front());
	pending_messages_.pop_front();

	const size_t length = payload.size();
	std::vector<unsigned char> buffer(LWS_PRE + length);
	std::memcpy(&buffer[LWS_PRE], payload.data(), length);

	const int written = lws_write(wsi_, &buffer[LWS_PRE], length, LWS_WRITE_TEXT);
	if (written < static_cast<int>(length)) {
		HYPERATE_LOG(LOG_ERROR, "WebSocket write failed");
		set_status(ConnectionStatus::Error, "WebSocket write failed");
		return;
	}

	if (!pending_messages_.empty())
		lws_callback_on_writable(wsi_);
}

void HyperateClient::on_lws_connection_error()
{
	HYPERATE_LOG(LOG_ERROR, "WebSocket connection failed");
	heart_rate_state().set_live(false);
	set_status(ConnectionStatus::Error, "WebSocket connection failed");
}

void HyperateClient::on_lws_closed()
{
	HYPERATE_LOG(LOG_INFO, "WebSocket closed");
	heart_rate_state().set_live(false);
	set_status(ConnectionStatus::Disconnected, "Disconnected");
}
#endif

const char *connection_status_name(ConnectionStatus status)
{
	switch (status) {
	case ConnectionStatus::Connecting:
		return "Connecting";
	case ConnectionStatus::Connected:
		return "Connected";
	case ConnectionStatus::Error:
		return "Error";
	case ConnectionStatus::Disconnected:
	default:
		return "Disconnected";
	}
}

} // namespace hyperate
