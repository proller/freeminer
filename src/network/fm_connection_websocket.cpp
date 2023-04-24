/*
This file is part of Freeminer.

Freeminer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Freeminer  is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Freeminer.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <string>
#include "debug/iostream_debug_helpers.h"

#include "config.h"

#if USE_WEBSOCKET

#include "fm_connection_websocket.h"

#include "log.h"
#include "network/networkpacket.h"
#include "network/networkprotocol.h"
#include "porting.h"
#include "profiler.h"
#include "serialization.h"
#include "settings.h"
#include "util/numeric.h"
#include "util/serialize.h"
#include "util/string.h"
#include <cstdarg>

#include <websocketpp/server.hpp>

#include <websocketpp/config/debug_asio_no_tls.hpp>
//#include <websocketpp/config/asio_no_tls.hpp>
//#include "websocketpp/common/connection_hdl.hpp"

// Custom logger
#include <websocketpp/logger/syslog.hpp>

#include <websocketpp/server.hpp>

// #include <iostream>

namespace con_ws
{

#define WS_DEBUG 1
// auto & cs = verbosestream; //errorstream; // remove after debug
#if WS_DEBUG
auto &cs = errorstream; // remove after debug
#else
auto &cs = verbosestream; // remove after debug
#endif

////////////////////////////////////////////////////////////////////////////////
///////////////// Custom Config for debugging custom policies //////////////////
////////////////////////////////////////////////////////////////////////////////

struct debug_custom : public websocketpp::config::debug_asio
{
	typedef debug_custom type;
	typedef debug_asio base;

	typedef base::concurrency_type concurrency_type;

	typedef base::request_type request_type;
	typedef base::response_type response_type;

	typedef base::message_type message_type;
	typedef base::con_msg_manager_type con_msg_manager_type;
	typedef base::endpoint_msg_manager_type endpoint_msg_manager_type;

	/// Custom Logging policies
	/*typedef websocketpp::log::syslog<concurrency_type,
		websocketpp::log::elevel> elog_type;
	typedef websocketpp::log::syslog<concurrency_type,
		websocketpp::log::alevel> alog_type;
	*/
	typedef base::alog_type alog_type;
	typedef base::elog_type elog_type;

	typedef base::rng_type rng_type;

	struct transport_config : public base::transport_config
	{
		typedef type::concurrency_type concurrency_type;
		typedef type::alog_type alog_type;
		typedef type::elog_type elog_type;
		typedef type::request_type request_type;
		typedef type::response_type response_type;
		typedef websocketpp::transport::asio::basic_socket::endpoint socket_type;
	};

	typedef websocketpp::transport::asio::endpoint<transport_config> transport_type;

	static const long timeout_open_handshake = 0;
};

////////////////////////////////////////////////////////////////////////////////

typedef websocketpp::server<debug_custom> server;

// using websocketpp::lib::bind;
// using websocketpp::lib::placeholders::_1;
// using websocketpp::lib::placeholders::_2;

// pull out the type of messages sent by our config
/*
bool validatewebsocketpp::connection_hdl)
{
	// sleep(6);
	return true;
}
*/

void Connection::on_http(websocketpp::connection_hdl hdl)
{
	server::connection_ptr con = Server.get_con_from_hdl(hdl);

	std::string res = con->get_request_body();

	std::stringstream ss;
	ss << "got HTTP request with " << res.size() << " bytes of body data.";
	cs << ss.str() << std::endl;

	con->set_body(ss.str());
	con->set_status(websocketpp::http::status_code::ok);
}

void Connection::on_fail(websocketpp::connection_hdl hdl)
{
	server::connection_ptr con = Server.get_con_from_hdl(hdl);

	cs << "Fail handler: " << con->get_ec() << " " << con->get_ec().message()
	   << std::endl;
}

void Connection::on_close(websocketpp::connection_hdl)
{
	cs << "Close handler" << std::endl;
}

void Connection::on_open(websocketpp::connection_hdl)
{
	cs << "open handler" << std::endl;
}

// Define a callback to handle incoming messages
void Connection::on_message(websocketpp::connection_hdl hdl, message_ptr msg)
{

	cs << "on_message called with hdl: " << hdl.lock().get()
	   << " and message: " << msg->get_payload().size() << " " << msg->get_payload()
	   << std::endl;
	if (hdls.count(hdl)) {
		//for (int header_size = 0; header_size < msg->get_payload().size();++header_size) 
		{
			 constexpr size_t header_size = 8;
			DUMP("try hdsz=", header_size);
			SharedBuffer<u8> resultdata(
					(const unsigned char *)msg->get_payload().data() + header_size,
					msg->get_payload().size() - header_size);
			putEvent(ConnectionEvent::dataReceived(hdls[hdl], resultdata));
		}
		return;
	}
	{

		u16 peer_id = 0;
		static u16 last_try = PEER_ID_SERVER + 1;
		if (m_peers.size() > 0) {
			for (int i = 0; i < 1000; ++i) {
				if (last_try > 30000)
					last_try = PEER_ID_SERVER + 20000;
				++last_try;
				if (!m_peers.count(last_try)) {
					peer_id = last_try;
					break;
				}
			}
		} else {
			peer_id = last_try;
		}
		if (!peer_id)
			last_try = peer_id = m_peers.rbegin()->first + 1;

		hdls.emplace(hdl, peer_id);
		putEvent(ConnectionEvent::peerAdded(peer_id, {}));
	}

	try {
		Server.send(hdl, "PROXY OK", msg->get_opcode());

	} catch (const websocketpp::exception &e) {
		cs << "Echo failed because: "
		   << "(" << e.what() << ")" << std::endl;
	}
}

/*
int zzmain() {
	// Create a server endpoint
	server echo_server;

	try {
		// Set logging settings
		echo_server.set_access_channels(websocketpp::log::alevel::all);
		echo_server.clear_access_channels(websocketpp::log::alevel::frame_payload);

		// Initialize ASIO
		echo_server.init_asio();
		echo_server.set_reuse_addr(true);

		// Register our message handler
		echo_server.set_message_handler(bind(&Connection::on_message,&echo_server,::_1,::_2));

		echo_server.set_http_handler(bind(&Connection::on_http,&echo_server,::_1));
		echo_server.set_fail_handler(bind(&Connection::on_fail,&echo_server,::_1));
		echo_server.set_close_handler(&Connection::on_close);

		echo_server.set_validate_handler(bind(&Connection::validate,&echo_server,::_1));

		// Listen on port 9012
		echo_server.listen(9012);

		// Start the server accept loop
		echo_server.start_accept();

		// Start the ASIO io_service run loop
		echo_server.run();
	} catch (websocketpp::exception const & e) {
		std::cout << e.what() << std::endl;
	} catch (const std::exception & e) {
		std::cout << e.what() << std::endl;
	} catch (...) {
		std::cout << "other exception" << std::endl;
	}
}
*/

/*
	Connection
*/
void debug_printf(const char *format, ...)
{
	printf("WS_DEBUG: ");
	va_list ap;
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
}

Connection::Connection(u32 protocol_id, u32 max_packet_size, float timeout, bool ipv6,
		PeerHandler *peerhandler) :
		// /thread_pool("Connection"),
		m_protocol_id(protocol_id),
		m_max_packet_size(max_packet_size), m_timeout(timeout), sock(nullptr),
		m_peer_id(0), m_bc_peerhandler(peerhandler), m_last_recieved(0),
		m_last_recieved_warn(0)
{

	{

		if (0 /*con_debug*/) {

			Server.set_error_channels(websocketpp::log::elevel::all);
			Server.set_access_channels(websocketpp::log::alevel::all ^
									   websocketpp::log::alevel::frame_payload ^
									   websocketpp::log::alevel::frame_header);

		} else {
			Server.set_error_channels(websocketpp::log::elevel::none);
			Server.set_access_channels(websocketpp::log::alevel::none);
		}
		const auto timeouts = 30; // Config.GetWsTimeoutsMs();
		Server.set_open_handshake_timeout(timeouts);
		Server.set_close_handshake_timeout(timeouts);
		Server.set_pong_timeout(timeouts);
		Server.set_listen_backlog(100);
		Server.init_asio();

		Server.set_reuse_addr(true);
		Server.set_open_handler(websocketpp::lib::bind(
				&Connection::on_open, this, websocketpp::lib::placeholders::_1));
		Server.set_close_handler(websocketpp::lib::bind(
				&Connection::on_close, this, websocketpp::lib::placeholders::_1));
		Server.set_message_handler(websocketpp::lib::bind(&Connection::on_message, this,
				websocketpp::lib::placeholders::_1, websocketpp::lib::placeholders::_2));
		// Server.set_message_handler(websocketpp::lib::bind(&Connection::on_message,
		// this, websocketpp::lib::placeholders::_1, websocketpp::lib::placeholders::_2));
		Server.set_http_handler(websocketpp::lib::bind(
				&Connection::on_http, this, websocketpp::lib::placeholders::_1));
#if USE_SSL
		Server.set_tls_init_handler(bind(&broadcast_server::on_tls_init, this,
				websocketpp::lib::placeholders::_1));
#endif
		// Server.set_timer(long duration, timer_handler callback);
		//}

#if USE_SSL
		context_ptr on_tls_init(websocketpp::connection_hdl /* hdl */)
		{
			namespace asio = websocketpp::lib::asio;
			context_ptr ctx = websocketpp::lib::make_shared<asio::ssl::context>(
					asio::ssl::context::tlsv12);
			try {
				ctx->set_options(asio::ssl::context::default_workarounds |
								 asio::ssl::context::no_sslv2 |
								 asio::ssl::context::no_sslv3 |
								 asio::ssl::context::single_dh_use);
				ctx->set_password_callback(std::bind([&]() { return GetSSLPassword(); }));
				ctx->use_certificate_chain_file(GetSSLCertificateChain());
				ctx->use_private_key_file(GetSSLPrivateKey(), asio::ssl::context::pem);
				std::string ciphers = GetSSLCiphers();
				if (ciphers.empty())
					ciphers = "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:"
							  "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:"
							  "DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:kEDH+"
							  "AESGCM:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA256:"
							  "ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-"
							  "AES256-SHA384:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-"
							  "SHA:ECDHE-ECDSA-AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-"
							  "AES128-SHA:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA256:"
							  "DHE-DSS-AES256-SHA:DHE-RSA-AES256-SHA:!aNULL:!eNULL:!"
							  "EXPORT:!DES:!RC4:!3DES:!MD5:!PSK";

				if (SSL_CTX_set_cipher_list(ctx->native_handle(), ciphers.c_str()) != 1) {
					warningstream << "Error setting cipher list";
				}
			} catch (const std::exception &e) {
				errorstream << "Exception: " << e.what();
			}
			return ctx;
		}
#endif
	}

	start();
}

// bool Connection::sctp_inited = false;

Connection::~Connection()
{

	join();
	//?deletePeer(0);

	disconnect();
}

/* Internal stuff */

void *Connection::run()
{
	while (!stopRequested()) {
		while (!m_command_queue.empty()) {
			auto c = m_command_queue.pop_frontNoEx();
			processCommand(c);
		}
		if (receive() <= 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	disconnect();

	return nullptr;
}

/* Internal stuff */

void Connection::putEvent(ConnectionEventPtr e)
{
	assert(e->type != CONNEVENT_NONE); // Pre-condition
	m_event_queue.push_back(e);
}

void Connection::processCommand(ConnectionCommandPtr c)
{
	switch (c->type) {
	case CONNCMD_NONE:
		dout_con << getDesc() << " processing CONNCMD_NONE" << std::endl;
		return;
	case CONNCMD_SERVE:
		dout_con << getDesc() << " processing CONNCMD_SERVE port=" << c->address.getPort()
				 << std::endl;
		serve(c->address);
		return;
	case CONNCMD_CONNECT:
		dout_con << getDesc() << " processing CONNCMD_CONNECT" << std::endl;
		connect(c->address);
		return;
	case CONNCMD_DISCONNECT:
		dout_con << getDesc() << " processing CONNCMD_DISCONNECT" << std::endl;
		disconnect();
		return;
	case CONNCMD_DISCONNECT_PEER:
		dout_con << getDesc() << " processing CONNCMD_DISCONNECT" << std::endl;
		deletePeer(c->peer_id, false); // its correct ?
		// DisconnectPeer(c.peer_id);
		return;
	case CONNCMD_SEND:
		dout_con << getDesc() << " processing CONNCMD_SEND" << std::endl;
		send(c->peer_id, c->channelnum, c->data, c->reliable);
		return;
	case CONNCMD_SEND_TO_ALL:
		dout_con << getDesc() << " processing CONNCMD_SEND_TO_ALL" << std::endl;
		sendToAll(c->channelnum, c->data, c->reliable);
		return;
		/*	case CONNCMD_DELETE_PEER:
				dout_con << getDesc() << " processing CONNCMD_DELETE_PEER" << std::endl;
				deletePeer(c.peer_id, false);
				return;
		*/
	case CONCMD_ACK:
	case CONCMD_CREATE_PEER:
		break;
	}
}

// Receive packets from the network and buffers and create ConnectionEvents
int Connection::receive()
{
	/*
	int n = 0;
	{
		auto lock = m_peers.lock_unique_rec();
		for (const auto &i : m_peers) {
			const auto [nn, brk] = recv(i.first, i.second);
			n += nn;
			if (brk)
				break;
		}
	}

	if (sock_connect && sock) {
		const auto [nn, brk] = recv(PEER_ID_SERVER, sock);
		n += nn;
	}

	if (sock_listen && sock) {

		usrsctp_set_non_blocking(sock, 1);

		struct socket *conn_sock = nullptr;
		struct sockaddr_in6 remote_addr;
		socklen_t addr_len = sizeof(remote_addr);
		if ((conn_sock = usrsctp_accept(
					 sock, (struct sockaddr *)&remote_addr, &addr_len)) == NULL) {
			if (errno == EWOULDBLOCK) {
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				return n;
			} else {
				cs << "usrsctp_accept failed.  exiting...\n";
				return n;
			}
		}

		u16 peer_id = 0;
		static u16 last_try = PEER_ID_SERVER + 1;
		if (m_peers.size() > 0) {
			for (int i = 0; i < 1000; ++i) {
				if (last_try > 30000)
					last_try = PEER_ID_SERVER + 0x3fff;
				++last_try;
				if (!m_peers.count(last_try)) {
					peer_id = last_try;
					break;
				}
			}
		} else {
			peer_id = last_try;
		}
		if (!peer_id)
			last_try = peer_id = m_peers.rbegin()->first + 1;

		cs << "receive() accepted " << conn_sock << " addr_len=" << addr_len
		   << " id=" << peer_id << std::endl;

		m_peers.insert_or_assign(peer_id, conn_sock);
		Address sender(remote_addr.sin6_addr, remote_addr.sin6_port);
		m_peers_address.insert_or_assign(peer_id, sender);

		putEvent(ConnectionEvent::peerAdded(peer_id, sender));
		++n;
	}
	return n;
	*/
	return 0;
}

std::pair<int, bool> Connection::recv(session_t peer_id, struct socket *sock)
{

	if (!sock) {
		return {0, false};
	}

	return {0, false};
}

// host
void Connection::serve(Address bind_address)
{
	infostream << getDesc() << "WS serving at " << bind_address.serializeString() << ":"
			   << std::to_string(bind_address.getPort()) << std::endl;
	// Server.listen(bind_address.getAddress6());
	//  Listen on port 9012
	Server.listen(bind_address.getPort());

	// Start the server accept loop
	Server.start_accept();

	// Start the ASIO io_service run loop
	Server.run();
}

// peer
void Connection::connect(Address address)
{
}

void Connection::disconnect()
{
	sock = nullptr;
	{
		auto lock = m_peers.lock_unique_rec();

		for (auto i = m_peers.begin(); i != m_peers.end(); ++i) {
			// usrsctp_close(i->second);
		}
		m_peers.clear();
	}
	m_peers_address.clear();
}

void Connection::sendToAll(u8 channelnum, SharedBuffer<u8> data, bool reliable)
{
	auto lock = m_peers.lock_unique_rec();
	for (const auto &i : m_peers)
		send(i.first, channelnum, data, reliable);
}

void Connection::send(
		session_t peer_id, u8 channelnum, SharedBuffer<u8> data, bool reliable)
{
	std::string tmp (0, 8);
	tmp += std::string_view((const char *)*data, data.getSize());
	Server.send(m_peers.get(peer_id), tmp.data(), tmp.size(),			websocketpp::frame::opcode::text);
	//Server.send(m_peers.get(peer_id), *data, data.getSize(),			websocketpp::frame::opcode::text);
}

websocketpp::connection_hdl Connection::getPeer(session_t peer_id)
{
	auto node = m_peers.find(peer_id);

	if (node == m_peers.end())
		return {};

	return node->second;
}

bool Connection::deletePeer(session_t peer_id, bool timeout)
{
	cs << "Connection::deletePeer " << peer_id << ", " << timeout << std::endl;
	Address peer_address;
	// any peer has a primary address this never fails!
	// peer->getAddress(MTP_PRIMARY, peer_address);

	if (!peer_id) {
		if (sock) {
			// usrsctp_close(sock);
			sock = nullptr;

			putEvent(ConnectionEvent::peerRemoved(peer_id, timeout, {}));

			return true;
		} else {
			return false;
		}
	}
	if (m_peers.find(peer_id) == m_peers.end())
		return false;

	// Create event
	putEvent(ConnectionEvent::peerRemoved(peer_id, timeout, {}));

	{
		auto lock = m_peers.lock_unique_rec();
		auto sock = m_peers.get(peer_id);
		// if (sock)
		//	usrsctp_close(sock);

		// delete m_peers[peer_id]; -- enet should handle this
		m_peers.erase(peer_id);
	}
	m_peers_address.erase(peer_id);
	return true;
}

/* Interface */
/*
ConnectionEvent Connection::getEvent() {
	if(m_event_queue.empty()) {
		ConnectionEvent e;
		e.type = CONNEVENT_NONE;
		return e;
	}
	return m_event_queue.pop_frontNoEx();
}
*/

ConnectionEventPtr Connection::waitEvent(u32 timeout_ms)
{
	try {
		return m_event_queue.pop_front(timeout_ms);
	} catch (const ItemNotFoundException &ex) {
		return ConnectionEvent::create(CONNEVENT_NONE);
	}
}

void Connection::putCommand(ConnectionCommandPtr c)
{
	// TODO? if (!m_shutting_down)
	{
		m_command_queue.push_back(c);
		// m_sendThread->Trigger();
	}
}

void Connection::Serve(Address bind_addr)
{
	putCommand(ConnectionCommand::serve(bind_addr));
}

void Connection::Connect(Address address)
{
	putCommand(ConnectionCommand::connect(address));
}

bool Connection::Connected()
{
	auto node = m_peers.find(PEER_ID_SERVER);
	if (node == m_peers.end())
		return false;

	// TODO: why do we even need to know our peer id?
	if (!m_peer_id)
		m_peer_id = 2;

	if (m_peer_id == PEER_ID_INEXISTENT)
		return false;

	return true;
}

void Connection::Disconnect()
{
	putCommand(ConnectionCommand::disconnect());
}

u32 Connection::Receive(NetworkPacket *pkt, int timeout)
{
	for (;;) {
		auto e = waitEvent(timeout);
		if (e->type != CONNEVENT_NONE)
			dout_con << getDesc() << ": Receive: got event: " << e->describe()
					 << std::endl;
		switch (e->type) {
		case CONNEVENT_NONE:
			// throw NoIncomingDataException("No incoming data");
			return 0;
		case CONNEVENT_DATA_RECEIVED:
			if (e->data.getSize() < 2) {
				continue;
			}
			DUMP("cdr", e->data.getSize(), e->peer_id);
			pkt->putRawPacket(*e->data, e->data.getSize(), e->peer_id);
			return e->data.getSize();
		case CONNEVENT_PEER_ADDED: {
			if (m_bc_peerhandler)
				m_bc_peerhandler->peerAdded(e->peer_id);
			continue;
		}
		case CONNEVENT_PEER_REMOVED: {
			if (m_bc_peerhandler)
				m_bc_peerhandler->deletingPeer(e->peer_id, e->timeout);
			continue;
		}
		case CONNEVENT_BIND_FAILED:
			throw ConnectionBindFailed("Failed to bind socket "
									   "(port already in use?)");
		case CONNEVENT_CONNECT_FAILED:
			throw ConnectionException("Failed to connect");
		}
	}
	return 0;
}

bool Connection::TryReceive(NetworkPacket *pkt)
{
	return Receive(pkt, 0);
}

/*
void Connection::SendToAll(u8 channelnum, SharedBuffer<u8> data, bool reliable) {
	assert(channelnum < CHANNEL_COUNT);

	ConnectionCommand c;
	c.sendToAll(channelnum, data, reliable);
	putCommand(c);
}
*/

void Connection::Send(session_t peer_id, u8 channelnum, NetworkPacket *pkt, bool reliable)
{
	assert(channelnum < CHANNEL_COUNT); // Pre-condition

	putCommand(ConnectionCommand::send(peer_id, channelnum, pkt, reliable));
}

void Connection::Send(
		session_t peer_id, u8 channelnum, SharedBuffer<u8> data, bool reliable)
{
	assert(channelnum < CHANNEL_COUNT); // Pre-condition

	putCommand(ConnectionCommand::send(peer_id, channelnum, data, reliable));
}

void Connection::Send(
		session_t peer_id, u8 channelnum, const msgpack::sbuffer &buffer, bool reliable)
{
	SharedBuffer<u8> data((unsigned char *)buffer.data(), buffer.size());
	Send(peer_id, channelnum, data, reliable);
}

Address Connection::GetPeerAddress(session_t peer_id)
{
	if (!m_peers_address.count(peer_id))
		return Address();
	return m_peers_address.get(peer_id);
}

/*
void Connection::DeletePeer(u16 peer_id) {
	ConnectionCommand c;
	c.deletePeer(peer_id);
	putCommand(c);
}
*/

void Connection::PrintInfo(std::ostream &out)
{
	out << getDesc() << ": ";
}

void Connection::PrintInfo()
{
	PrintInfo(dout_con);
}

std::string Connection::getDesc()
{
	return "";
	// return std::string("con(")+itos(m_socket.GetHandle())+"/"+itos(m_peer_id)+")";
}

float Connection::getPeerStat(session_t peer_id, rtt_stat_type type)
{
	return 0;
}

float Connection::getLocalStat(con::rate_stat_type type)
{
	return 0;
}

void Connection::DisconnectPeer(session_t peer_id)
{
	putCommand(ConnectionCommand::disconnect_peer(peer_id));
}

} // namespace

#endif