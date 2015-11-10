/*
connection.cpp
Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>
*/

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


/*
https://chromium.googlesource.com/external/webrtc/+/master/talk/media/sctp/sctpdataengine.cc
*/

#include "network/fm_connection_sctp.h"
#include "network/usrsctp/usrsctplib/usrsctp.h"
#include "serialization.h"
#include "log.h"
#include "porting.h"
#include "util/serialize.h"
#include "util/numeric.h"
#include "util/string.h"
#include "settings.h"
#include "profiler.h"

#include <cstdarg>
//#include <socket.h>

namespace con {

#define BUFFER_SIZE (1<<16)

//very ugly windows hack
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
	#define _WIN32_WINNT 0x0501
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

int inet_pton(int af, const char *src, void *dst) {
	struct sockaddr_storage ss;
	int size = sizeof(ss);
	char src_copy[INET6_ADDRSTRLEN + 1];

	ZeroMemory(&ss, sizeof(ss));
	/* stupid non-const API */
	strncpy (src_copy, src, INET6_ADDRSTRLEN + 1);
	src_copy[INET6_ADDRSTRLEN] = 0;

	if (WSAStringToAddress(src_copy, af, NULL, (struct sockaddr *)&ss, &size) == 0) {
		switch(af) {
		case AF_INET:
			*(struct in_addr *)dst = ((struct sockaddr_in *)&ss)->sin_addr;
			return 1;
		case AF_INET6:
			*(struct in6_addr *)dst = ((struct sockaddr_in6 *)&ss)->sin6_addr;
			return 1;
		}
	}
	return 0;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size) {
	struct sockaddr_storage ss;
	unsigned long s = size;

	ZeroMemory(&ss, sizeof(ss));
	ss.ss_family = af;

	switch(af) {
	case AF_INET:
		((struct sockaddr_in *)&ss)->sin_addr = *(struct in_addr *)src;
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)&ss)->sin6_addr = *(struct in6_addr *)src;
		break;
	default:
		return NULL;
	}
	/* cannot direclty use &size because of strict aliasing rules */
	return (WSAAddressToString((struct sockaddr *)&ss, sizeof(ss), NULL, dst, &s) == 0) ?
	       dst : NULL;
}
#endif

/*
	Connection
*/
void
debug_printf(const char *format, ...) {
	va_list ap;

	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
}


auto & cs = errorstream; // remove after debug

Connection::Connection(u32 protocol_id, u32 max_packet_size, float timeout,
                       bool ipv6, PeerHandler *peerhandler):
	m_protocol_id(protocol_id),
	m_max_packet_size(max_packet_size),
	m_timeout(timeout),
	sock(nullptr),
	m_peer_id(0),
	m_bc_peerhandler(peerhandler),
	m_last_recieved(0),
	m_last_recieved_warn(0) {

	sock_listen = sock_connect = false;
	sctp_inited_by_me = false;



	start();



}

bool con::Connection::sctp_inited = false;


Connection::~Connection() {

	join();
	deletePeer(0);

	for (auto & i : m_peers) {
		usrsctp_close(i.second);
	}


	if (sctp_inited_by_me) {

	for (int i = 0; i < 100; ++i) {
		if (!usrsctp_finish())
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}


		sctp_inited = false;
	}

}

/* Internal stuff */

void * Connection::run() {
	//ThreadStarted();
	reg("Connection");

	errorstream << "threadstart" << std::endl;


	while(!stopRequested()) {
		while(!m_command_queue.empty()) {
			ConnectionCommand c = m_command_queue.pop_frontNoEx();
			processCommand(c);
		}
		receive();
	}

	return nullptr;
}

void Connection::putEvent(ConnectionEvent &e) {
	assert(e.type != CONNEVENT_NONE);
	m_event_queue.push_back(e);
}

void Connection::processCommand(ConnectionCommand &c) {
	switch(c.type) {
	case CONNCMD_NONE:
		dout_con << getDesc() << " processing CONNCMD_NONE" << std::endl;
		return;
	case CONNCMD_SERVE:
		dout_con << getDesc() << " processing CONNCMD_SERVE port="
		         << c.address.getPort() << std::endl;
		serve(c.address);
		return;
	case CONNCMD_CONNECT:
		dout_con << getDesc() << " processing CONNCMD_CONNECT" << std::endl;
		connect(c.address);
		return;
	case CONNCMD_DISCONNECT:
		dout_con << getDesc() << " processing CONNCMD_DISCONNECT" << std::endl;
		disconnect();
		return;
	case CONNCMD_DISCONNECT_PEER:
		dout_con << getDesc() << " processing CONNCMD_DISCONNECT" << std::endl;
		deletePeer(c.peer_id, false); // its correct ?
		//DisconnectPeer(c.peer_id);
		return;
	case CONNCMD_SEND:
		dout_con << getDesc() << " processing CONNCMD_SEND" << std::endl;
		send(c.peer_id, c.channelnum, c.data, c.reliable);
		return;
	case CONNCMD_SEND_TO_ALL:
		dout_con << getDesc() << " processing CONNCMD_SEND_TO_ALL" << std::endl;
		sendToAll(c.channelnum, c.data, c.reliable);
		return;
	case CONNCMD_DELETE_PEER:
		dout_con << getDesc() << " processing CONNCMD_DELETE_PEER" << std::endl;
		deletePeer(c.peer_id, false);
		return;
	}
}

#if 0

static int sctp_recieve_callback(struct socket* sock, union sctp_sockstore addr,
                                 void* data, size_t length,
                                 struct sctp_rcvinfo rcv, int flags,
                                 void* ulp_info) {

	errorstream << "OnSctpInboundPacket" << sock << " l=" << length << " notif=" << (flags & MSG_NOTIFICATION) << std::endl;
	/*
	  SctpDataMediaChannel* channel = static_cast<SctpDataMediaChannel*>(ulp_info);
	  // Post data to the channel's receiver thread (copying it).
	  // TODO(ldixon): Unclear if copy is needed as this method is responsible for
	  // memory cleanup. But this does simplify code.
	  const SctpDataMediaChannel::PayloadProtocolIdentifier ppid =
	      static_cast<SctpDataMediaChannel::PayloadProtocolIdentifier>(
	          rtc::HostToNetwork32(rcv.rcv_ppid));
	  cricket::DataMessageType type = cricket::DMT_NONE;
	  if (!GetDataMediaType(ppid, &type) && !(flags & MSG_NOTIFICATION)) {
	    // It's neither a notification nor a recognized data packet.  Drop it.
	    LOG(LS_ERROR) << "Received an unknown PPID " << ppid
	                  << " on an SCTP packet.  Dropping.";
	  } else {
	    SctpInboundPacket* packet = new SctpInboundPacket;
	    packet->buffer.SetData(reinterpret_cast<uint8_t*>(data), length);
	    packet->params.ssrc = rcv.rcv_sid;
	    packet->params.seq_num = rcv.rcv_ssn;
	    packet->params.timestamp = rcv.rcv_tsn;
	    packet->params.type = type;
	    packet->flags = flags;
	    // The ownership of |packet| transfers to |msg|.
	    InboundPacketMessage* msg = new InboundPacketMessage(packet);
	    channel->worker_thread()->Post(channel, MSG_SCTPINBOUNDPACKET, msg);
	  }
	*/
	free(data);
	return 1;

}

#endif





void Connection::sctp_setup(u16 port) {
errorstream<<"sctp_setup i="<<sctp_inited<<" p="<<port<<std::endl;
	if (sctp_inited)
		return;
	sctp_inited = true;
	sctp_inited_by_me = true;

errorstream<<"sctp_setup "<<port<<std::endl;

	//usrsctp_init(9899, nullptr, nullptr);
	//usrsctp_init(9899, nullptr, debug_printf);
	//usrsctp_init(0, nullptr, debug_printf);
	usrsctp_init(port, nullptr, debug_printf);

#if SCTP_DEBUG
	//usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_NONE);
	usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_ALL);
#endif

	//usrsctp_sysctl_set_sctp_ecn_enable(0);

	//usrsctp_sysctl_set_sctp_nr_outgoing_streams_default(2);

	usrsctp_sysctl_set_sctp_multiple_asconfs(1);

	//usrsctp_sysctl_set_sctp_inits_include_nat_friendly(1);
#if __ANDROID__
	usrsctp_sysctl_set_sctp_mobility_fasthandoff(1);
	usrsctp_sysctl_set_sctp_mobility_base(1);
#endif

	usrsctp_sysctl_set_sctp_cmt_on_off(1); //SCTP_CMT_MAX
	usrsctp_sysctl_set_sctp_cmt_use_dac(1);
	usrsctp_sysctl_set_sctp_buffer_splitting(1);

	usrsctp_sysctl_set_sctp_max_retran_chunk(10);
	usrsctp_sysctl_set_sctp_shutdown_guard_time_default(40);

	//if ((sock = usrsctp_socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP, receive_cb, NULL, 0, NULL)) == NULL) {
	//struct sctp_udpencaps encaps;
	/*
		if ((sock = usrsctp_socket(AF_INET6, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL)) == NULL) {
			errorstream<<("usrsctp_socket")<<std::endl;
			ConnectionEvent ev(CONNEVENT_BIND_FAILED);
			putEvent(ev);
		}
	*/
	//sock->so_state |= SS_NBIO;

	//usrsctp_sysctl_set_sctp_blackhole(0);
}



// Receive packets from the network and buffers and create ConnectionEvents
void Connection::receive() {
	/*
		if (!sock) {
			return;
		}
	*/

//errorstream<<"receive() ... "<<__LINE__<<std::endl;

	/*
		ENetEvent event;
		int ret = enet_host_service(m_enet_host, & event, 10);
		if (ret > 0)
		{
			m_last_recieved = porting::getTimeMs();
			switch (event.type)
			{
			case ENET_EVENT_TYPE_CONNECT:
				{
					//JMutexAutoLock peerlock(m_peers_mutex);
					u16 peer_id = PEER_ID_SERVER + 1;
					if (m_peers.size() > 0)
						// TODO: fix this shit
						peer_id = m_peers.rbegin()->first + 1;
					m_peers.set(peer_id, event.peer);
					m_peers_address.set(peer_id, Address(event.peer->address.host, event.peer->address.port));

					event.peer->data = new u16;
					*((u16*)event.peer->data) = peer_id;

					// Create peer addition event
					ConnectionEvent e;
					e.peerAdded(peer_id);
					putEvent(e);
				}
				break;
			case ENET_EVENT_TYPE_RECEIVE:
				{
					ConnectionEvent e;
					SharedBuffer<u8> resultdata(event.packet->data, event.packet->dataLength);
					e.dataReceived(*(u16*)event.peer->data, resultdata);
					putEvent(e);
				}

				/ * Clean up the packet now that we're done using it. * /
				enet_packet_destroy (event.packet);
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				deletePeer(*((u16*)event.peer->data), false);

				/ *  Reset the peer's client information. * /
				delete (u16*)event.peer->data;

				break;
			case ENET_EVENT_TYPE_NONE:
				break;
			}
		} else if (ret < 0) {
			infostream<<"enet_host_service failed = "<< ret << std::endl;
			if (m_peers.count(PEER_ID_SERVER))
				deletePeer(PEER_ID_SERVER,  false);
		} else { //0
			if (m_peers.count(PEER_ID_SERVER)) { //ugly fix. todo: fix enet and remove
				unsigned int time = porting::getTimeMs();
				if (time - m_last_recieved > 30000 && m_last_recieved_warn > 20000 && m_last_recieved_warn < 30000) {
					errorstream<<"connection lost [30s], disconnecting."<<std::endl;
	#if defined(__has_feature)
	#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
					if (0)
	#endif
	#endif
					{
						deletePeer(PEER_ID_SERVER,  false);
					}
					m_last_recieved_warn = 0;
					m_last_recieved = 0;
				} else if (time - m_last_recieved > 20000 && m_last_recieved_warn > 10000 && m_last_recieved_warn < 20000) {
					errorstream<<"connection lost [20s]!"<<std::endl;
					m_last_recieved_warn = time - m_last_recieved;
				} else if (time - m_last_recieved > 10000 && m_last_recieved_warn < 10000) {
					errorstream<<"connection lost [10s]? ping."<<std::endl;
					enet_peer_ping(m_peers.get(PEER_ID_SERVER));
					m_last_recieved_warn = time - m_last_recieved;
				}
			}
		}
	*/

	for (auto & i : m_peers) {
		recv(i.first, i.second);
	}

	//errorstream << "receive() pre accept s=" << sock<< " m_peers="<<m_peers.size()<<  std::endl;
	if (sock_connect && sock) {
		recv(PEER_ID_SERVER, sock);
	}
	if (sock_listen && sock) {

//if (m_peers.size() >=1 ) return;  // NONONONONONONONONONONONONONONONONON!!!!!!!!!!!!!!!!!!!!!!!

		usrsctp_set_non_blocking(sock, 1);

		//errorstream << "receive() try accept s=" << sock<<  std::endl;
		//if (!recv(0, sock)) {
		//}
		//struct socket **conn_sock;
		struct socket *conn_sock = nullptr;
		struct sockaddr_in6 remote_addr;
		socklen_t addr_len = sizeof(remote_addr);
		//conn_sock = (struct socket **)malloc(sizeof(struct socket *));
		if ((conn_sock = usrsctp_accept(sock, (struct sockaddr *) &remote_addr, &addr_len)) == NULL) {
			if (errno == EWOULDBLOCK) {
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				return;
			} else {
				cs<< "usrsctp_accept failed.  exiting...\n";
				return;
			}
			//continue;
		}

/*		u16 peer_id = PEER_ID_SERVER + 1;
		if (m_peers.size() > 0)
			// TODO: fix this shit
			peer_id = m_peers.rbegin()->first + 1;
*/

				u16 peer_id = 0;
				static u16 last_try = PEER_ID_SERVER + 1;
				if (m_peers.size() > 0) {
					for (int i = 0; i < 1000; ++i) {
						if (last_try > 30000)
							last_try = PEER_ID_SERVER;
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


		cs << "receive() accepted " << conn_sock << " addr_len=" << addr_len << " id=" << peer_id << std::endl;

		m_peers.set(peer_id, conn_sock);
		m_peers_address.set(peer_id, Address(remote_addr.sin6_addr, remote_addr.sin6_port));

		//event.peer->data = new u16;
		//*((u16*)event.peer->data) = peer_id;

		// Create peer addition event
		ConnectionEvent e;
		e.peerAdded(peer_id);
		putEvent(e);

	}



//errorstream<<"receive() ... "<<__LINE__<<std::endl;

}

//static
void Connection::handle_association_change_event(u16 peer_id, const struct sctp_assoc_change *sac) {
	unsigned int i, n;

	cs<<("Association change ");
	switch (sac->sac_state) {
	case SCTP_COMM_UP:
		cs<<("SCTP_COMM_UP");
		break;
	case SCTP_COMM_LOST:
		cs<<("SCTP_COMM_LOST");
		deletePeer(peer_id,  false);
		break;
	case SCTP_RESTART:
		cs<<("SCTP_RESTART");
		break;
	case SCTP_SHUTDOWN_COMP:
		cs<<("SCTP_SHUTDOWN_COMP");
		deletePeer(peer_id,  false);
		break;
	case SCTP_CANT_STR_ASSOC:
		cs<<("SCTP_CANT_STR_ASSOC");
		deletePeer(peer_id,  false);
		break;
	default:
		cs<<("UNKNOWN");
		break;
	}
	cs<<", streams (in/out) = ("<<sac->sac_inbound_streams<<"/"<<sac->sac_outbound_streams<<")";
	n = sac->sac_length - sizeof(struct sctp_assoc_change);
	if (((sac->sac_state == SCTP_COMM_UP) ||
	        (sac->sac_state == SCTP_RESTART)) && (n > 0)) {
		cs<<(", supports");
		for (i = 0; i < n; i++) {
			switch (sac->sac_info[i]) {
			case SCTP_ASSOC_SUPPORTS_PR:
				cs<<(" PR");
				break;
			case SCTP_ASSOC_SUPPORTS_AUTH:
				cs<<(" AUTH");
				break;
			case SCTP_ASSOC_SUPPORTS_ASCONF:
				cs<<(" ASCONF");
				break;
			case SCTP_ASSOC_SUPPORTS_MULTIBUF:
				cs<<(" MULTIBUF");
				break;
			case SCTP_ASSOC_SUPPORTS_RE_CONFIG:
				cs<<(" RE-CONFIG");
				break;
			default:
				cs<<" UNKNOWN("<< sac->sac_info[i]<<")";
				break;
			}
		}
	} else if (((sac->sac_state == SCTP_COMM_LOST) ||
	            (sac->sac_state == SCTP_CANT_STR_ASSOC)) && (n > 0)) {
		cs<<(", ABORT =");
		for (i = 0; i < n; i++) {
			cs<< " " << sac->sac_info[i];
		}
	}
	cs<<(".\n");
	if ((sac->sac_state == SCTP_CANT_STR_ASSOC) ||
	        (sac->sac_state == SCTP_SHUTDOWN_COMP) ||
	        (sac->sac_state == SCTP_COMM_LOST)) {
		// exit(0);
	}
	return;
}


static void
handle_peer_address_change_event(const struct sctp_paddr_change *spc) {
	char addr_buf[INET6_ADDRSTRLEN];
	const char *addr;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;

	switch (spc->spc_aaddr.ss_family) {
	case AF_INET:
		sin = (struct sockaddr_in *)&spc->spc_aaddr;
		addr = inet_ntop(AF_INET, &sin->sin_addr, addr_buf, INET_ADDRSTRLEN);
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)&spc->spc_aaddr;
		addr = inet_ntop(AF_INET6, &sin6->sin6_addr, addr_buf, INET6_ADDRSTRLEN);
		break;
	default:
#ifdef _WIN32
		_snprintf(addr_buf, INET6_ADDRSTRLEN, "Unknown family %d", spc->spc_aaddr.ss_family);
#else
		snprintf(addr_buf, INET6_ADDRSTRLEN, "Unknown family %d", spc->spc_aaddr.ss_family);
#endif
		addr = addr_buf;
		break;
	}
	cs<<"Peer address "<<addr<<" is now " ;
	switch (spc->spc_state) {
	case SCTP_ADDR_AVAILABLE:
		cs<<("SCTP_ADDR_AVAILABLE");
		break;
	case SCTP_ADDR_UNREACHABLE:
		cs<<("SCTP_ADDR_UNREACHABLE");
		break;
	case SCTP_ADDR_REMOVED:
		cs<<("SCTP_ADDR_REMOVED");
		break;
	case SCTP_ADDR_ADDED:
		cs<<("SCTP_ADDR_ADDED");
		break;
	case SCTP_ADDR_MADE_PRIM:
		cs<<("SCTP_ADDR_MADE_PRIM");
		break;
	case SCTP_ADDR_CONFIRMED:
		cs<<("SCTP_ADDR_CONFIRMED");
		break;
	default:
		cs<<("UNKNOWN");
		break;
	}
	char buf[100] ;
	sprintf(buf, " (error = 0x%08x).\n", spc->spc_error);
	cs<<buf;
	return;
}


int Connection::recv(u16 peer_id, struct socket *sock) {

//errorstream<<"receive() ... "<<__LINE__ << " peer_id="<<peer_id<<" sock="<<sock<<std::endl;
	if (!sock) {
		return 0;
	}

	usrsctp_set_non_blocking(sock, 1);


	struct sockaddr_in6 addr;
	socklen_t from_len = (socklen_t)sizeof(addr);
	int flags = 0;
	struct sctp_rcvinfo rcv_info;
	socklen_t infolen = (socklen_t)sizeof(rcv_info);
	unsigned int infotype;
	char buffer[BUFFER_SIZE]; //move to class
	ssize_t n = usrsctp_recvv(sock, (void*)buffer, BUFFER_SIZE, (struct sockaddr *) &addr, &from_len, (void *)&rcv_info,
	                          &infolen, &infotype, &flags);
	//errorstream << "receive() ... " << __LINE__ << " n=" << n << std::endl;
	if (n > 0) {
		//errorstream << "receive() ... " << __LINE__ << " n=" << n << std::endl;
		if (flags & MSG_NOTIFICATION) {
			cs<<"Notification of length "<<n<<" received.\n";

			const sctp_notification& notification =
			    reinterpret_cast<const sctp_notification&>(buffer);
			if (notification.sn_header.sn_length != n) {
				errorstream << " wrong notification" << std::endl;
			}

			// TODO(ldixon): handle notifications appropriately.
			switch (notification.sn_header.sn_type) {
			case SCTP_ASSOC_CHANGE:
				errorstream << "SCTP_ASSOC_CHANGE" << std::endl;
				//OnNotificationAssocChange(notification.sn_assoc_change);
				{
switch (notification.sn_assoc_change.sac_state) {
/*
	case SCTP_CANT_STR_ASSOC:
		cs<<("SCTP_CANT_STR_ASSOC");
		deletePeer(peer_id,  false);
		break;
*/
		case SCTP_COMM_UP:
							m_peers_address.set(peer_id, Address(addr.sin6_addr, addr.sin6_port));
/*							ConnectionEvent e;
							e.peerAdded(peer_id);
							putEvent(e);*/
		break;
}
					handle_association_change_event(peer_id, &(notification.sn_assoc_change));
#if 0
					const sctp_assoc_change& change = notification.sn_assoc_change;
					switch (change.sac_state) {
					case SCTP_COMM_UP:
						errorstream << "Association change SCTP_COMM_UP" << std::endl;

						{
							m_peers_address.set(peer_id, Address(addr.sin6_addr, addr.sin6_port));
							ConnectionEvent e;
							e.peerAdded(peer_id);
							putEvent(e);
						}
						break;
					case SCTP_COMM_LOST: {
						errorstream << "Association change SCTP_COMM_LOST" << std::endl;
						deletePeer(peer_id,  false);
					}
					break;
					case SCTP_RESTART:
						errorstream << "Association change SCTP_RESTART" << std::endl;
						break;
					case SCTP_SHUTDOWN_COMP:
						errorstream << "Association change SCTP_SHUTDOWN_COMP" << std::endl;
						break;
					case SCTP_CANT_STR_ASSOC:
						errorstream << "Association change SCTP_CANT_STR_ASSOC" << std::endl;
						break;
					default:
						errorstream << "Association change UNKNOWN " << std::endl;
						break;
					}
#endif
				}





				break;
			case SCTP_PEER_ADDR_CHANGE: {
				//auto * snp = (union sctp_notification *)buffer;
				const sctp_paddr_change* spc = &notification.sn_paddr_change;
				//printf("SCTP_PEER_ADDR_CHANGE: state=%d, error=%d\n",spc->spc_state, spc->spc_error);
				handle_peer_address_change_event(spc);
				errorstream << "SCTP_PEER_ADDR_CHANGE state=" << spc->spc_state << " error=" << spc->spc_error << std::endl;
			}
			case SCTP_REMOTE_ERROR:
				errorstream << "SCTP_REMOTE_ERROR" << std::endl;
				break;
			case SCTP_SHUTDOWN_EVENT:
				errorstream << "SCTP_SHUTDOWN_EVENT" << std::endl;
				deletePeer(peer_id,  false);
				break;
			case SCTP_ADAPTATION_INDICATION:
				errorstream << "SCTP_ADAPTATION_INDICATION" << std::endl;
				break;
			case SCTP_PARTIAL_DELIVERY_EVENT:
				errorstream << "SCTP_PARTIAL_DELIVERY_EVENT" << std::endl;
				break;
			case SCTP_AUTHENTICATION_EVENT:
				errorstream << "SCTP_AUTHENTICATION_EVENT" << std::endl;
				break;
			case SCTP_SENDER_DRY_EVENT:
				errorstream << "SCTP_SENDER_DRY_EVENT" << std::endl;
				//SignalReadyToSend(true);
				break;
			// TODO(ldixon): Unblock after congestion.
			case SCTP_NOTIFICATIONS_STOPPED_EVENT:
				errorstream << "SCTP_NOTIFICATIONS_STOPPED_EVENT" << std::endl;
				break;
			case SCTP_SEND_FAILED_EVENT:
				errorstream << "SCTP_SEND_FAILED_EVENT" << std::endl;
				break;
			case SCTP_STREAM_RESET_EVENT:
				errorstream << "SCTP_STREAM_RESET_EVENT" << std::endl;
				//OnStreamResetEvent(&notification.sn_strreset_event);
				break;
			case SCTP_ASSOC_RESET_EVENT:
				errorstream << "SCTP_ASSOC_RESET_EVENT" << std::endl;
				break;
			case SCTP_STREAM_CHANGE_EVENT:
				errorstream  << "SCTP_STREAM_CHANGE_EVENT" << std::endl;
				// An acknowledgment we get after our stream resets have gone through,
				// if they've failed.  We log the message, but don't react -- we don't
				// keep around the last-transmitted set of SSIDs we wanted to close for
				// error recovery.  It doesn't seem likely to occur, and if so, likely
				// harmless within the lifetime of a single SCTP association.
				break;
			default:
				errorstream << "Unknown SCTP event: "
				            << notification.sn_header.sn_type << std::endl;
				break;
			}

		} else {
			char name[INET6_ADDRSTRLEN];
			if (infotype == SCTP_RECVV_RCVINFO) {
				char buf[1000];
				sprintf(buf, "Msg of length %llu received from %s:%u on stream %d with SSN %u and TSN %u, PPID %d, context %u, complete %d.\n",
				       (unsigned long long)n,
				       inet_ntop(AF_INET6, &addr.sin6_addr, name, INET6_ADDRSTRLEN), ntohs(addr.sin6_port),
				       rcv_info.rcv_sid,
				       rcv_info.rcv_ssn,
				       rcv_info.rcv_tsn,
				       ntohl(rcv_info.rcv_ppid),
				       rcv_info.rcv_context,
				       (flags & MSG_EOR) ? 1 : 0);
				cs<<buf;
			} else {
				/*
								printf("Msg of length %llu received from %s:%u, complete %d.\n",
								       (unsigned long long)n,
								       inet_ntop(AF_INET6, &addr.sin6_addr, name, INET6_ADDRSTRLEN), ntohs(addr.sin6_port),
								       (flags & MSG_EOR) ? 1 : 0);
				*/
				recv_buf[peer_id] += std::string(buffer, n); // optimize here if firs packet complete`
				//verbosestream <<  "recieved data n="<< n << " complete="<<(flags & MSG_EOR)<< " buf="<<recv_buf[peer_id].size()<<" from sock="<<sock<<std::endl;
				if ((flags & MSG_EOR)) {

					ConnectionEvent e;
					//SharedBuffer<u8> resultdata((const unsigned char*)buffer, n);
					SharedBuffer<u8> resultdata((const unsigned char*)recv_buf[peer_id].c_str(), recv_buf[peer_id].size());
					e.dataReceived(peer_id, resultdata);
					putEvent(e);
					recv_buf.erase(peer_id);
				}


			}
		}
	} else {

	//if (n < 0)
	//	perror("sctp_recvv");

		// drop peer here
//errorstream<<"receive() ... drop "<<__LINE__<< " errno="<<errno << " EINPROGRESS="<<EINPROGRESS <<std::endl;
		//if (m_peers.count(peer_id)) { //ugly fix. todo: fix enet and remove
//					deletePeer(peer_id,  false);
		//}
		//break;
	}


	return n;

}


static uint16_t event_types[] = {SCTP_ASSOC_CHANGE,
                                 SCTP_PEER_ADDR_CHANGE,
                                 SCTP_REMOTE_ERROR,
                                 SCTP_SHUTDOWN_EVENT,
                                 SCTP_ADAPTATION_INDICATION,
                                 SCTP_SEND_FAILED_EVENT,
                                 SCTP_STREAM_RESET_EVENT,
                                 SCTP_STREAM_CHANGE_EVENT
                                };




void Connection::sock_setup(u16 peer_id, struct socket *sock) {



	/* Disable Nagle */
	/*
		uint32_t nodelay = 1;
		if(usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_NODELAY, &nodelay, sizeof(nodelay))) {
			errorstream << " setsockopt error: SCTP_NODELAY" << peer_id << std::endl;
			//return NULL;
		}
	*/

	struct sctp_event event = {};
	event.se_assoc_id = SCTP_ALL_ASSOC;
	event.se_on = 1;
	for (unsigned int i = 0; i < sizeof(event_types) / sizeof(uint16_t); i++) {
		event.se_type = event_types[i];
		if (usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_EVENT, &event, sizeof(event)) < 0) {
			perror("setsockopt SCTP_EVENT");
		}
	}
 /*
	if (usrsctp_set_non_blocking(sock, 1) < 0) {
		errorstream << "Failed to set SCTP to non blocking." << std::endl;
		//return false;
	}
 */

	const int on = 1;
	if (usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_I_WANT_MAPPED_V4_ADDR, (const void*)&on, (socklen_t)sizeof(int)) < 0) {
		perror("usrsctp_setsockopt SCTP_I_WANT_MAPPED_V4_ADDR");
	}

	const int one = 1;
	if (usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_EXPLICIT_EOR, &one, sizeof(int)) < 0) {
		perror("setsockopt SCTP_EXPLICIT_EOR");
	}

	/*
		if (usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_REUSE_PORT, &on, sizeof(int)) < 0) {
			perror("setsockopt SCTP_REUSE_PORT");
		}
	*/

}

// host
void Connection::serve(Address bind_addr) {
	errorstream << "serve()" << bind_addr.serializeString() << " :" << bind_addr.getPort() << std::endl;
	/*
	#if defined(ENET_IPV6)
		address.host = in6addr_any;
	#else
		address.host = ENET_HOST_ANY;
	#endif
		address.port = bind_addr.getPort(); // fmtodo

		m_enet_host = enet_host_create(&address, g_settings->getU16("max_users"), CHANNEL_COUNT, 0, 0);
	*/
	sctp_setup(bind_addr.getPort());

	//usrsctp_sysctl_set_sctp_udp_tunneling_port(bind_addr.getPort());
	//usrsctp_sysctl_set_sctp_udp_tunneling_port(9899);

	//if ((sock = usrsctp_socket(AF_INET6, SOCK_STREAM, IPPROTO_SCTP, this->*sctp_recieve_callback, NULL, 0, NULL)) == NULL) {
	//if ((sock = usrsctp_socket(AF_INET6, SOCK_STREAM, IPPROTO_SCTP, &con::Connection::sctp_recieve_callback, NULL, 0, NULL)) == NULL) {
	if ((sock = usrsctp_socket(AF_INET6, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL)) == NULL) {
		//if ((sock = usrsctp_socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL)) == NULL) {
		//if ((sock = usrsctp_socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP, OnSctpInboundPacket, NULL, 0, NULL)) == NULL) {
		errorstream << ("usrsctp_socket") << std::endl;
		ConnectionEvent ev(CONNEVENT_BIND_FAILED);
		putEvent(ev);
	}

	sock_setup(0, sock);

//for connect too
	//if (argc > 2) {
	/*
		//	memset(&encaps, 0, sizeof(encaps));
		encaps = {};
		encaps.sue_address.ss_family = AF_INET6;
		encaps.sue_port = htons(bind_addr.getPort());
		//encaps.sue_port = htons(9899);
		if (usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_REMOTE_UDP_ENCAPS_PORT, (const void*)&encaps, (socklen_t)sizeof(encaps)) < 0) {
			errorstream << ("setsockopt") << std::endl;
			ConnectionEvent ev(CONNEVENT_BIND_FAILED);
			putEvent(ev);
		}
	 */
	//}


	struct sockaddr_in6 addr;

	if (!bind_addr.isIPv6()) {
		errorstream << "connect() transform to v6 " << __LINE__ << std::endl;

		inet_pton (AF_INET6, ("::ffff:" + bind_addr.serializeString()).c_str(), &addr.sin6_addr);
	} else
		addr = bind_addr.getAddress6();



//addr = bind_addr.getAddress6();
	//struct sockaddr_in6 addr;
	//struct sockaddr_in addr;
	//addr. = in6addr_any;

	/* Acting as the 'server' */
	//memset((void *)&addr, 0, sizeof(addr));
	/*
	#ifdef HAVE_SIN_LEN
	    addr.sin_len = sizeof(struct sockaddr_in);
	#endif
	*/
#ifdef HAVE_SIN6_LEN
	addr.sin6_len = sizeof(struct sockaddr_in6);
#endif
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(bind_addr.getPort()); //htons(13);
	//addr.sin6_addr = in6addr_any; //htonl(INADDR_ANY);
	//addr.sin6_addr = in6addr_loopback;

	//addr.sin_family = AF_INET;
	//addr.sin_port = htons(bind_addr.getPort()); //htons(13);
	//printf("Waiting for connections on port %d\n",ntohs(addr.sin6_port));
	cs<<"Waiting for connections on sctp port "<<ntohs(addr.sin6_port)<<"\n";
	if (usrsctp_bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("usrsctp_bind");
	}
	if (usrsctp_listen(sock, 10) < 0) {
		perror("usrsctp_listen");
	}

	errorstream << "serve() ok " << sock<< std::endl;

	sock_listen = true;

}

// peer
void Connection::connect(Address addr) {
	errorstream << "connect() " << addr.serializeString() << " :" << addr.getPort() << std::endl;

	//sctp_setup(addr.getPort()+100);
	sctp_setup(addr.getPort() + myrand_range(1000, 10000));
	//sctp_setup(0);

	m_last_recieved = porting::getTimeMs();
	//JMutexAutoLock peerlock(m_peers_mutex);
	//m_peers.lock_unique_rec();
	auto node = m_peers.find(PEER_ID_SERVER);
	if(node != m_peers.end()) {
		//throw ConnectionException("Already connected to a server");
		ConnectionEvent ev(CONNEVENT_CONNECT_FAILED);
		putEvent(ev);
	}

	struct socket *sock;

	if ((sock = usrsctp_socket(AF_INET6, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL)) == NULL) {
		errorstream << ("usrsctp_socket") << sock<< std::endl;
		ConnectionEvent ev(CONNEVENT_BIND_FAILED);
		putEvent(ev);
		return;
	}


	sock_setup(PEER_ID_SERVER, sock);

	m_peers.set(PEER_ID_SERVER, sock);

	/*
		m_enet_host = enet_host_create(NULL, 1, 0, 0, 0);
		ENetAddress address;
	#if defined(ENET_IPV6)
		if (!addr.isIPv6())
			inet_pton (AF_INET6, ("::ffff:"+addr.serializeString()).c_str(), &address.host);
		else
			address.host = addr.getAddress6().sin6_addr;
	#else
		if (addr.isIPv6()) {
			//throw ConnectionException("Cant connect to ipv6 address");
			ConnectionEvent ev(CONNEVENT_CONNECT_FAILED);
			putEvent(ev);
		} else {
			address.host = addr.getAddress().sin_addr.s_addr;
		}
	#endif

		address.port = addr.getPort();
		ENetPeer *peer = enet_host_connect(m_enet_host, &address, CHANNEL_COUNT, 0);
		peer->data = new u16;
		*((u16*)peer->data) = PEER_ID_SERVER;

		ENetEvent event;
		int ret = enet_host_service (m_enet_host, & event, 5000);
		if (ret > 0 && event.type == ENET_EVENT_TYPE_CONNECT) {
			m_peers.set(PEER_ID_SERVER, peer);
			m_peers_address.set(PEER_ID_SERVER, addr);
		} else {
			if (ret == 0)
				errorstream<<"enet_host_service ret="<<ret<<std::endl;
			enet_peer_reset(peer);
		}
	*/

	/*
		// needed???
		struct sockaddr_in6 addr_local;
	#ifdef HAVE_SIN6_LEN
		addr_local.sin6_len = sizeof(struct sockaddr_in6);
	#endif
		addr_local.sin6_family = AF_INET6;
		//addr.sin6_port = htons(bind_addr.getPort()); //htons(13);
		addr_local.sin6_port = htons(addr.getPort()+100); //htons(13);
		//addr_local.sin6_port = htons(0); //htons(13);
		addr_local.sin6_addr = in6addr_any; //htonl(INADDR_ANY);
		//addr.sin6_addr = in6addr_loopback;

		//addr.sin_family = AF_INET;
		//addr.sin_port = htons(bind_addr.getPort()); //htons(13);
		//printf("Waiting for connections on port %d\n",ntohs(addr.sin6_port));
		//printf("Waiting for connections on port %d\n",ntohs(addr.sin6_port));
		if (usrsctp_bind(sock, (struct sockaddr *)&addr_local, sizeof(addr)) < 0) {
			perror("usrsctp_bind");
		}
	*/

	//memset(&encaps, 0, sizeof(encaps));

	errorstream << "connect() using encaps " << addr.getPort() << std::endl;

	sctp_udpencaps encaps = {};
	encaps.sue_address.ss_family = AF_INET6;
	encaps.sue_port = htons(addr.getPort());
	//encaps.sue_port = htons(30042);
	//encaps.sue_port = htons(9899);
	if (usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_REMOTE_UDP_ENCAPS_PORT, (const void*)&encaps, (socklen_t)sizeof(encaps)) < 0) {
		errorstream << ("connect setsockopt fail") << std::endl;
		ConnectionEvent ev(CONNEVENT_CONNECT_FAILED);
		putEvent(ev);
	}

	struct sockaddr_in6 addr6 = {};

	//memset((void *)&addr6, 0, sizeof(addr6));
#ifdef HAVE_SIN6_LEN
	addr6.sin6_len = sizeof(struct sockaddr_in6);
#endif
	addr6.sin6_family = AF_INET6;
	addr6.sin6_addr = in6addr_any;

	if (usrsctp_bind(sock, (struct sockaddr *)&addr6, sizeof(addr)) < 0) {
		perror("usrsctp_bind");
	}


	addr6 = {};

	if (!addr.isIPv6()) {
		errorstream << "connect() transform to v6 " << addr.serializeString() << std::endl;
		if (addr.serializeString() == "127.0.0.1")
			addr6.sin6_addr = in6addr_loopback;
		else
			inet_pton (AF_INET6, ("::ffff:" + addr.serializeString()).c_str(), &addr6.sin6_addr);
	} else
		addr6 = addr.getAddress6();

	/* Acting as the connector */
	//printf("Connecting to %s %s\n",argv[3],argv[4]);
	//memset((void *)&addr4, 0, sizeof(struct sockaddr_in));
//#ifdef HAVE_SIN_LEN
//    addr4.sin_len = sizeof(struct sockaddr_in);
//#endif
#ifdef HAVE_SIN6_LEN
	addr6.sin6_len = sizeof(struct sockaddr_in6);
#endif
	//addr4.sin_family = AF_INET;
	addr6.sin6_family = AF_INET6;
	//addr4.sin_port = htons(atoi(argv[4]));
	addr6.sin6_port = htons(addr.getPort()); //atoi(argv[4]));
	//addr6.sin6_port = addr.getPort(); //atoi(argv[4]));
	//if (inet_pton(AF_INET6, argv[3], &addr6.sin6_addr) == 1) {

	//errorstream << "connect() ... " << __LINE__ << std::endl;
	if (auto connect_result = usrsctp_connect(sock, (struct sockaddr *)&addr6, sizeof(addr6)) < 0) {
		if (connect_result < 0 && errno != EINPROGRESS) {
			perror("usrsctp_connect fail");
			sock = nullptr;
		}
	}
	/*
	    } else if (inet_pton(AF_INET, argv[3], &addr4.sin_addr) == 1) {
	      if (usrsctp_connect(sock, (struct sockaddr *)&addr4, sizeof(struct sockaddr_in)) < 0) {
	        perror("usrsctp_connect");
	      }
	    } else {
	      printf("Illegal destination address.\n");
	    }
	*/

	errorstream << "connect() ok sock=" << sock << std::endl;

	sock_connect = true;

}

void Connection::disconnect() {
	//JMutexAutoLock peerlock(m_peers_mutex);
	m_peers.lock_shared_rec();

	for (auto i = m_peers.begin();
	        i != m_peers.end(); ++i) {
		usrsctp_close(i->second);
		i->second = nullptr;
	}
	m_peers.clear();
}

void Connection::sendToAll(u8 channelnum, SharedBuffer<u8> data, bool reliable) {
	/*
		ENetPacket *packet = enet_packet_create(*data, data.getSize(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
		enet_host_broadcast(m_enet_host, 0, packet);
	*/
	m_peers.lock_shared_rec();
	for (auto i = m_peers.begin();
	        i != m_peers.end(); ++i)
		send(i->first, channelnum, data, reliable);
}

void Connection::send(u16 peer_id, u8 channelnum,
                      SharedBuffer<u8> data, bool reliable) {
//errorstream<<" === sending to peer_id="<<peer_id <<" channelnum="<<(int)channelnum<< " reliable="<<reliable<< " bytes="<<data.getSize()<<std::endl;
	{
		//JMutexAutoLock peerlock(m_peers_mutex);
		if (m_peers.find(peer_id) == m_peers.end()) {
			errorstream << " === send no peer " << peer_id << std::endl;
			return;
		}
	}
	dout_con << getDesc() << " sending to peer_id=" << peer_id << std::endl;

	if(channelnum >= CHANNEL_COUNT) {
		errorstream << " === send no chan " << channelnum << "/" << CHANNEL_COUNT << std::endl;
		return;
	}

	/*
		ENetPacket *packet = enet_packet_create(*data, data.getSize(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
	*/

	auto sock = getPeer(peer_id);
	if(!sock) {
		errorstream << " === send no peer sock" << std::endl;
		deletePeer(peer_id, false);
		return;
	}
	/*
		if (enet_peer_send(peer, channelnum, packet) < 0)
			errorstream<<"enet_peer_send failed"<<std::endl;
	*/

//errorstream<<" === send to peer " << peer_id<< "sock="<< peer<<std::endl;

	usrsctp_set_non_blocking(sock, 0);

	struct sctp_sndinfo sndinfo = {};
	//char buffer[BUFFER_SIZE];
	//todo
//sndinfo.snd_sid = channelnum;
	sndinfo.snd_sid = 1;
	//sndinfo.snd_flags = 0;
	//sndinfo.snd_flags = SCTP_EOR;
	//sndinfo.snd_ppid = htonl(DISCARD_PPID);
	//sndinfo.snd_context = 0;
	//sndinfo.snd_assoc_id = 0;
	/*
		if (usrsctp_sendv(peer, *data, data.getSize(), NULL, 0, (void *)&sndinfo,
		                  sizeof(sndinfo), SCTP_SENDV_SNDINFO, 0) < 0) {
			perror("usrsctp_sendv");
			deletePeer(peer_id, 0);
	errorstream<<" === sending FAILED to peer_id="<<peer_id << " bytes="<<data.getSize()<< " sock="<< peer<<std::endl;
		}
	*/
	size_t maxlen = 0xffff - 1000;
	//size_t maxlen = 1400;
	size_t buflen = data.getSize();
	size_t sendlen = std::min(buflen, maxlen);
	size_t remlen  = buflen;
	size_t curpos = 0;

	while (remlen > 0) {


		if (remlen <= maxlen) {

			sndinfo.snd_flags |= SCTP_EOR;

		}

//errorstream<<" psend" << " remlen=" << remlen << " curpos="<<curpos<< " sendlen="<<sendlen << " buflen="<<buflen<< " nowsent="<<(curpos+sendlen)<<" flags="<<sndinfo.snd_flags<<std::endl;

		int len = usrsctp_sendv(sock, *data + curpos, sendlen, NULL, 0, (void *)&sndinfo,
		                        sizeof(sndinfo), SCTP_SENDV_SNDINFO, 0);
		if (len < 0) {
			perror("usrsctp_sendv");
			deletePeer(peer_id, 0);

			errorstream << " === sending FAILED to peer_id=" << peer_id << " bytes=" << data.getSize() << " sock=" << sock << std::endl;
			break;
		}


		//ssize_t len = sendto(sock, curpos, sendlen, flags, dest_addr, addrlen);
		//if (len == -1)
		//    return -1;

		curpos += len;
		remlen -= len;
		sendlen = std::min(remlen, maxlen);
	}

	//return buflen;


}


struct socket * Connection::getPeer(u16 peer_id) {
	auto node = m_peers.find(peer_id);

	if(node == m_peers.end())
		return NULL;

	return node->second;
}

bool Connection::deletePeer(u16 peer_id, bool timeout) {
	errorstream << "Connection::deletePeer " << peer_id << ", " << timeout << std::endl;
	//JMutexAutoLock peerlock(m_peers_mutex);
	if (!peer_id) {
		if (sock) {
			usrsctp_close(sock);
			sock = nullptr;

			ConnectionEvent e;
			e.peerRemoved(peer_id, timeout);
			putEvent(e);

			return true;
		} else {
			return false;
		}
	}
	if(m_peers.find(peer_id) == m_peers.end())
		return false;

	// Create event
	ConnectionEvent e;
	e.peerRemoved(peer_id, timeout);
	putEvent(e);

	auto lock = m_peers.lock_unique_rec();

	usrsctp_close(m_peers.get(peer_id));

	// delete m_peers[peer_id]; -- enet should handle this
	m_peers.erase(peer_id);
	m_peers_address.erase(peer_id);
	return true;
}

/* Interface */

ConnectionEvent Connection::getEvent() {
	if(m_event_queue.empty()) {
		ConnectionEvent e;
		e.type = CONNEVENT_NONE;
		return e;
	}
	return m_event_queue.pop_frontNoEx();
}

ConnectionEvent Connection::waitEvent(u32 timeout_ms) {
	try {
		return m_event_queue.pop_front(timeout_ms);
	} catch(ItemNotFoundException &ex) {
		ConnectionEvent e;
		e.type = CONNEVENT_NONE;
		return e;
	}
}

void Connection::putCommand(ConnectionCommand &c) {
	m_command_queue.push_back(c);
	//processCommand(c);
}

void Connection::Serve(Address bind_address) {
	ConnectionCommand c;
	c.serve(bind_address);
	putCommand(c);
}

void Connection::Connect(Address address) {
	ConnectionCommand c;
	c.connect(address);
	putCommand(c);
}

bool Connection::Connected() {
	//JMutexAutoLock peerlock(m_peers_mutex);

	auto node = m_peers.find(PEER_ID_SERVER);
	if(node == m_peers.end())
		return false;

	// TODO: why do we even need to know our peer id?
	if (!m_peer_id)
		m_peer_id = 2;

	if(m_peer_id == PEER_ID_INEXISTENT)
		return false;

	return true;
}

void Connection::Disconnect() {
	ConnectionCommand c;
	c.disconnect();
	putCommand(c);
}

u32 Connection::Receive(NetworkPacket* pkt, int timeout) {
	for(;;) {
		ConnectionEvent e = waitEvent(timeout);
		if(e.type != CONNEVENT_NONE)
			dout_con << getDesc() << ": Receive: got event: "
			         << e.describe() << std::endl;
		switch(e.type) {
		case CONNEVENT_NONE:
			//throw NoIncomingDataException("No incoming data");
			return 0;
		case CONNEVENT_DATA_RECEIVED:
			if (e.data.getSize() < 2) {
				continue;
			}
			pkt->putRawPacket(*e.data, e.data.getSize(), e.peer_id);
			return e.data.getSize();
		case CONNEVENT_PEER_ADDED: {
			if(m_bc_peerhandler)
				m_bc_peerhandler->peerAdded(e.peer_id);
			continue;
		}
		case CONNEVENT_PEER_REMOVED: {
			if(m_bc_peerhandler)
				m_bc_peerhandler->deletingPeer(e.peer_id, e.timeout);
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
	//throw NoIncomingDataException("No incoming data");
}

void Connection::SendToAll(u8 channelnum, SharedBuffer<u8> data, bool reliable) {
	assert(channelnum < CHANNEL_COUNT);

	ConnectionCommand c;
	c.sendToAll(channelnum, data, reliable);
	putCommand(c);
}

void Connection::Send(u16 peer_id, u8 channelnum,
                      SharedBuffer<u8> data, bool reliable) {
	assert(channelnum < CHANNEL_COUNT);

	ConnectionCommand c;
	c.send(peer_id, channelnum, data, reliable);
	putCommand(c);
}

void Connection::Send(u16 peer_id, u8 channelnum, const msgpack::sbuffer &buffer, bool reliable) {
	SharedBuffer<u8> data((unsigned char*)buffer.data(), buffer.size());
	Send(peer_id, channelnum, data, reliable);
}

Address Connection::GetPeerAddress(u16 peer_id) {
	if (!m_peers_address.count(peer_id))
		return Address();
	return m_peers_address.get(peer_id);
	/*
		auto a = Address(0, 0, 0, 0, 0);
		if (!m_peers.get(peer_id))
			return a;
		a.setPort(m_peers.get(peer_id)->address.port);
		a.setAddress(m_peers.get(peer_id)->address.host);
		return a;
	*/
}

void Connection::DeletePeer(u16 peer_id) {
	ConnectionCommand c;
	c.deletePeer(peer_id);
	putCommand(c);
}

void Connection::PrintInfo(std::ostream &out) {
	out << getDesc() << ": ";
}

void Connection::PrintInfo() {
	PrintInfo(dout_con);
}

std::string Connection::getDesc() {
	return "";
	//return std::string("con(")+itos(m_socket.GetHandle())+"/"+itos(m_peer_id)+")";
}
float Connection::getPeerStat(u16 peer_id, rtt_stat_type type) {
	return 0;
}


void Connection::DisconnectPeer(u16 peer_id) {
	ConnectionCommand discon;
	discon.disconnect_peer(peer_id);
	putCommand(discon);
}

bool parse_msgpack_packet(char *data, u32 datasize, MsgpackPacket *packet, int *command, msgpack::unpacked *msg) {
	try {
		//msgpack::unpacked msg;
		msgpack::unpack(msg, data, datasize);
		msgpack::object obj = msg->get();
		*packet = obj.as<MsgpackPacket>();

		*command = (*packet)[MSGPACK_COMMAND].as<int>();
	} catch (msgpack::type_error e) { errorstream << "msgpack::type_error sz=" << datasize << " e=" << e.what() << std::endl; return false; }
	catch (msgpack::unpack_error e) { errorstream << "msgpack::type_error sz=" << datasize << " e=" << e.what() << std::endl; return false; }
	return true;
}

} // namespace
