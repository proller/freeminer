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

#pragma once

#include "constants.h"
#include "exceptions.h"
#include "msgpack_fix.h"
#include "network/address.h"
#include "network/connection.h"
#include "network/networkprotocol.h"
#include "network/peerhandler.h"
#include "threading/concurrent_map.h"
#include "threading/concurrent_unordered_map.h"
#include "threading/thread_pool.h"
#include "util/container.h"
#include "util/pointer.h"

#include <websocketpp/server.hpp>

#if USE_SSL
#include <websocketpp/config/asio_no_tls.hpp>
#else
#include <websocketpp/config/asio.hpp>
#endif

// #define CHANNEL_COUNT 3

// class NetworkPacket;

namespace con
{
class PeerHandler;
}
namespace con_multi
{
class Connection;
}
namespace con_ws
{
using namespace con;

class Connection : public thread_pool
{
public:
	friend class con_multi::Connection;

	Connection(u32 protocol_id, u32 max_packet_size, float timeout, bool ipv6,
			con::PeerHandler *peerhandler = nullptr);
	~Connection();
	void *run();

	/* Interface */

	// ConnectionEvent getEvent();
	ConnectionEventPtr waitEvent(u32 timeout_ms);
	void putCommand(ConnectionCommandPtr c);

	void Serve(Address bind_addr);
	void Connect(Address address);
	bool Connected();
	void Disconnect();
	u32 Receive(NetworkPacket *pkt, int timeout = 1);
	bool TryReceive(NetworkPacket *pkt);

	void SendToAll(u8 channelnum, SharedBuffer<u8> data, bool reliable);
	void Send(session_t peer_id, u8 channelnum, SharedBuffer<u8> data, bool reliable);
	void Send(session_t peer_id, u8 channelnum, const msgpack::sbuffer &buffer,
			bool reliable);
	void Send(session_t peer_id, u8 channelnum, NetworkPacket *pkt, bool reliable);
	session_t GetPeerID() { return m_peer_id; }
	void DeletePeer(session_t peer_id);
	Address GetPeerAddress(session_t peer_id);
	float getPeerStat(session_t peer_id, rtt_stat_type type);
	float getLocalStat(rate_stat_type type);

	void DisconnectPeer(session_t peer_id);
	size_t events_size() { return m_event_queue.size(); }

private:
	void putEvent(ConnectionEventPtr e);
	void processCommand(ConnectionCommandPtr c);
	void send(float dtime);
	int receive();
	void runTimeouts(float dtime);
	void serve(Address address);
	void connect(Address address);
	void disconnect();
	void sendToAll(u8 channelnum, SharedBuffer<u8> data, bool reliable);
	void send(session_t peer_id, u8 channelnum, SharedBuffer<u8> data, bool reliable);

protected:
	websocketpp::connection_hdl getPeer(session_t peer_id);

private:
	bool deletePeer(session_t peer_id, bool timeout = 0);

	MutexedQueue<ConnectionEventPtr> m_event_queue;
	MutexedQueue<ConnectionCommandPtr> m_command_queue;

	u32 m_protocol_id;
	u32 m_max_packet_size;
	float m_timeout = 0;
	// struct sctp_udpencaps encaps;
	struct socket *sock = nullptr;
	session_t m_peer_id;

	concurrent_map<u16, websocketpp::connection_hdl> m_peers;
	concurrent_unordered_map<u16, Address> m_peers_address;

	// Backwards compatibility
	PeerHandler *m_bc_peerhandler;
	unsigned int m_last_recieved;
	int m_last_recieved_warn;

	void SetPeerID(const session_t id) { m_peer_id = id; }
	u32 GetProtocolID() { return m_protocol_id; }
	void PrintInfo(std::ostream &out);
	void PrintInfo();
	std::string getDesc();

	// bool sock_listen = false, sock_connect = false, sctp_inited_by_me = false;
	static bool sctp_inited;
	std::pair<int, bool> recv(session_t peer_id, struct socket *sock);
	// void sock_setup(/*session_t peer_id,*/ struct socket *sock);
	// void sctp_setup(u16 port = 9899);
	// std::unordered_map<session_t, std::array<std::string, 10>> recv_buf;

	//	void handle_association_change_event(
	//			session_t peer_id, const struct sctp_assoc_change *sac);
	/*
		int sctp_recieve_callback(struct socket *sock, union sctp_sockstore addr, void
	   *data, size_t datalen, struct sctp_rcvinfo, int flags, void *ulp_info);
	*/

private:
	using hdl_list = std::map<websocketpp::connection_hdl, session_t,
			std::owner_less<websocketpp::connection_hdl>>;

#if USE_SSL
	using server = websocketpp::server<websocketpp::config::asio_tls>;
#else
	using server = websocketpp::server<websocketpp::config::asio>;
#endif
	server Server;
	hdl_list hdls;
	std::shared_mutex peersMutex;

	typedef server::message_ptr message_ptr;

public:
	void on_http(websocketpp::connection_hdl hdl);
	void on_fail(websocketpp::connection_hdl hdl);
	void on_close(websocketpp::connection_hdl hdl);
	void on_open(websocketpp::connection_hdl hdl);
	void on_message(websocketpp::connection_hdl hdl, message_ptr msg);
};

} // namespace
