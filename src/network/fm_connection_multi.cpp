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

#include "fm_connection_multi.h"

#include "connection.h"
#if USE_SCTP
#include "fm_connection_sctp.h"
#endif
#if USE_WEBSOCKET
#include "fm_connection_websocket.h"
#endif
#if USE_ENET
#include "fm_connection_enet.h"
#endif

namespace con_multi
{

Connection::Connection(u32 protocol_id, u32 max_packet_size, float timeout, bool ipv6,
		con::PeerHandler *peerhandler) :
#if USE_SCTP
		m_con_sctp(std::make_shared<con_sctp::Connection>(
				PROTOCOL_ID, max_packet_size, timeout, ipv6, peerhandler)),
#endif
#if USE_WEBSOCKET
		m_con_ws(std::make_shared<con_ws::Connection>(
				PROTOCOL_ID, max_packet_size, timeout, ipv6, peerhandler)),
#endif
#if USE_ENET
		m_con_enet(std::make_shared<con_enet::Connection>(
				PROTOCOL_ID, max_packet_size, timeout, ipv6, peerhandler)),
#endif
#if MINETEST_TRANSPORT
		m_con(std::make_shared<con::Connection>(
				PROTOCOL_ID, max_packet_size, timeout, ipv6, peerhandler)),
#endif
		dummy{}
{
}

Connection::~Connection()
{
}

void Connection::Serve(Address bind_address)
{
	infostream << "Multi serving at " << bind_address.serializeString() << ":"
			   << std::to_string(bind_address.getPort()) << std::endl;

#if USE_SCTP
	if (m_con_sctp) {
		auto addr = bind_address;
		addr.setPort(addr.getPort() + 100);
		m_con_sctp->Serve(addr);
	}
#endif
#if USE_WEBSOCKET
	if (m_con_ws) {
		auto addr = bind_address;
		addr.setPort(addr.getPort()); // same tcp
		m_con_ws->Serve(addr);
	}
#endif
#if USE_ENET
	if (m_con_enet) {
		auto addr = bind_address;
		addr.setPort(addr.getPort() + 200);
		m_con_enet->Serve(addr);
	}
#endif
#if MINETEST_TRANSPORT
	if (m_con)
		m_con->Serve(bind_address);
#endif
}

void Connection::Connect(Address address)
{
	infostream << "Multi connect to " << address.serializeString() << ":"
			   << std::to_string(address.getPort()) << std::endl;

#if USE_SCTP
	if (m_con_sctp)
		m_con_sctp->Connect(address);
#endif
#if USE_ENET
	if (m_con_enet)
		m_con_enet->Connect(address);
#endif
#if MINETEST_TRANSPORT
	if (m_con)
		m_con->Connect(address);
#endif
}

bool Connection::Connected()
{
#if USE_SCTP
	if (m_con_sctp)
		if (auto c = m_con_sctp->Connected(); c)
			return c;
#endif
#if USE_ENET
	if (m_con_enet)
		if (auto c = m_con_enet->Connected(); c)
			return c;
#endif
#if MINETEST_TRANSPORT
	if (m_con)
		if (auto c = m_con->Connected(); c)
			return c;
	return false;
#endif
}

void Connection::Disconnect()
{
#if USE_SCTP
	if (m_con_sctp)
		m_con_sctp->Disconnect();
#endif
#if USE_ENET
	if (m_con_enet)
		m_con_enet->Disconnect();
#endif
#if MINETEST_TRANSPORT
	if (m_con)
		m_con->Disconnect();
#endif
}

u32 Connection::Receive(NetworkPacket *pkt, int want_timeout)
{
	u32 ret = 0;
	for (const auto &timeout : {0, want_timeout}) {
#if USE_SCTP
		if (m_con_sctp)
			ret += m_con_sctp->Receive(pkt, timeout);
		if (ret)
			return ret;
#endif
#if USE_WEBSOCKET
		if (m_con_ws)
			ret += m_con_ws->Receive(pkt, timeout);
		if (ret)
			return ret;
#endif
#if USE_ENET
		if (m_con_enet)
			ret += m_con_enet->Receive(pkt, timeout);
		if (ret)
			return ret;
#endif
#if MINETEST_TRANSPORT
		if (m_con)
			ret += m_con->Receive(pkt, timeout);
#endif
		if (ret)
			return ret;
	}
	return ret;
}

bool Connection::TryReceive(NetworkPacket *pkt)
{
	return Receive(pkt, 0);
}

void Connection::Send(session_t peer_id, u8 channelnum, NetworkPacket *pkt, bool reliable)
{
	// TODO send to one
#if USE_SCTP
	if (m_con_sctp && m_con_sctp->getPeer(peer_id))
		m_con_sctp->Send(peer_id, channelnum, pkt, reliable);
#endif
#if USE_WEBSOCKET
	if (m_con_ws && m_con_ws->getPeer(peer_id).lock().get())
		m_con_ws->Send(peer_id, channelnum, pkt, reliable);
#endif
#if USE_ENET
	if (m_con_enet && m_con_enet->getPeer(peer_id))
		m_con_enet->Send(peer_id, channelnum, pkt, reliable);
#endif
#if MINETEST_TRANSPORT
	if (m_con && &m_con.get()->getPeerNoEx(peer_id))
		m_con->Send(peer_id, channelnum, pkt, reliable);
#endif
}

void Connection::Send(
		session_t peer_id, u8 channelnum, const msgpack::sbuffer &buffer, bool reliable)
{
	// TODO send to one
#if USE_SCTP
	if (m_con_sctp)
		m_con_sctp->Send(peer_id, channelnum, buffer, reliable);
#endif
#if USE_WEBSOCKET
	if (m_con_ws)
		m_con_ws->Send(peer_id, channelnum, buffer, reliable);
#endif
#if USE_ENET
	if (m_con_enet)
		m_con_enet->Send(peer_id, channelnum, buffer, reliable);
#endif
#if MINETEST_TRANSPORT
		//	if (m_con)
		//		m_con->Send(peer_id, channelnum, buffer, reliable);
#endif
}

Address Connection::GetPeerAddress(session_t peer_id)
{
#if USE_SCTP
	if (m_con_sctp && m_con_sctp->getPeer(peer_id))
		return m_con_sctp->GetPeerAddress(peer_id);
#endif
#if USE_WEBSOCKET
	if (m_con_ws && m_con_ws->getPeer(peer_id).lock().get())
		return m_con_ws->GetPeerAddress(peer_id);
#endif
#if USE_ENET
	if (m_con_enet && m_con_enet->getPeer(peer_id))
		return m_con_enet->GetPeerAddress(peer_id);
#endif
#if MINETEST_TRANSPORT
	if (m_con && &m_con.get()->getPeerNoEx(peer_id))
		return m_con->GetPeerAddress(peer_id);
#endif
	return {};
}

float Connection::getPeerStat(session_t peer_id, con::rtt_stat_type type)
{
#if USE_SCTP
	if (m_con_sctp && m_con_sctp->getPeer(peer_id))
		return m_con_sctp->getPeerStat(peer_id, type);
#endif
#if USE_ENET
	if (m_con_enet && m_con_enet->getPeer(peer_id))
		return m_con_enet->getPeerStat(peer_id, type);
#endif
#if MINETEST_TRANSPORT
	if (m_con && &m_con.get()->getPeerNoEx(peer_id))
		return m_con->getPeerStat(peer_id, type);
#endif
	return {};
}

float Connection::getLocalStat(con::rate_stat_type type)
{
#if MINETEST_TRANSPORT
	if (m_con)
		return m_con->getLocalStat(type);
#endif
	return {};
}

void Connection::DisconnectPeer(session_t peer_id)
{
#if USE_SCTP
	if (m_con_sctp && m_con_sctp->getPeer(peer_id))
		return m_con_sctp->DisconnectPeer(peer_id);
#endif
#if USE_WEBSOCKET
	if (m_con_ws && m_con_ws->getPeer(peer_id).lock().get())
		return m_con_ws->DisconnectPeer(peer_id);
#endif
#if USE_ENET
	if (m_con_enet && m_con_enet->getPeer(peer_id))
		return m_con_enet->DisconnectPeer(peer_id);
#endif
#if MINETEST_TRANSPORT
	if (m_con && &m_con.get()->getPeerNoEx(peer_id))
		return m_con->DisconnectPeer(peer_id);
#endif
}

size_t Connection::events_size()
{
	size_t ret = 0;
#if USE_SCTP
	if (m_con_sctp)
		ret += m_con_sctp->events_size();
#endif
#if USE_WEBSOCKET
	if (m_con_ws)
		ret += m_con_ws->events_size();
#endif
#if USE_ENET
	if (m_con_enet)
		ret += m_con_enet->events_size();
#endif
#if MINETEST_TRANSPORT
	if (m_con)
		ret += m_con->events_size();
#endif
	return ret;
}

} // namespace
