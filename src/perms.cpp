/*

Copyright (c) 2013, 2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "perms.hpp"

#include <libtorrent/settings_pack.hpp>

namespace ltweb {

namespace {
bool is_local_setting(int name)
{
	switch (name) {
		// identity sent to peers and trackers
		case libtorrent::settings_pack::user_agent:
		case libtorrent::settings_pack::handshake_client_version:
		case libtorrent::settings_pack::peer_fingerprint:
		case libtorrent::settings_pack::announce_ip:
		case libtorrent::settings_pack::announce_port:

		// local network interfaces and ports
		case libtorrent::settings_pack::listen_interfaces:
		case libtorrent::settings_pack::outgoing_interfaces:
		case libtorrent::settings_pack::outgoing_port:
		case libtorrent::settings_pack::num_outgoing_ports:
		case libtorrent::settings_pack::listen_queue_size:
		case libtorrent::settings_pack::listen_system_port_fallback:
		case libtorrent::settings_pack::max_retry_port_bind:
		case libtorrent::settings_pack::peer_dscp:

		// proxy configuration
		case libtorrent::settings_pack::proxy_hostname:
		case libtorrent::settings_pack::proxy_username:
		case libtorrent::settings_pack::proxy_password:
		case libtorrent::settings_pack::proxy_type:
		case libtorrent::settings_pack::proxy_port:
		case libtorrent::settings_pack::proxy_hostnames:
		case libtorrent::settings_pack::proxy_peer_connections:
		case libtorrent::settings_pack::proxy_tracker_connections:
		case libtorrent::settings_pack::proxy_send_host_in_connect:
		case libtorrent::settings_pack::socks5_udp_send_local_ep:

		// I2P SAM bridge
		case libtorrent::settings_pack::i2p_hostname:
		case libtorrent::settings_pack::i2p_port:
		case libtorrent::settings_pack::i2p_inbound_quantity:
		case libtorrent::settings_pack::i2p_outbound_quantity:
		case libtorrent::settings_pack::i2p_inbound_length:
		case libtorrent::settings_pack::i2p_outbound_length:
		case libtorrent::settings_pack::i2p_inbound_length_variance:
		case libtorrent::settings_pack::i2p_outbound_length_variance:
		case libtorrent::settings_pack::allow_i2p_mixed:

		// NAT-PMP / PCP / UPnP port mapping
		case libtorrent::settings_pack::natpmp_gateway:
		case libtorrent::settings_pack::natpmp_lease_duration:
		case libtorrent::settings_pack::upnp_lease_duration:
		case libtorrent::settings_pack::upnp_ignore_nonrouters:
		case libtorrent::settings_pack::enable_upnp:
		case libtorrent::settings_pack::enable_natpmp:

		// local network discovery
		case libtorrent::settings_pack::enable_lsd:
		case libtorrent::settings_pack::local_service_announce_interval:

		// DHT bootstrap and WebTorrent infrastructure
		case libtorrent::settings_pack::dht_bootstrap_nodes:
		case libtorrent::settings_pack::webtorrent_stun_server:

		// OS-level resources and filesystem behavior
		case libtorrent::settings_pack::aio_threads:
		case libtorrent::settings_pack::hashing_threads:
		case libtorrent::settings_pack::file_pool_size:
		case libtorrent::settings_pack::enable_ip_notifier:
		case libtorrent::settings_pack::no_atime_storage:
		case libtorrent::settings_pack::disk_disable_copy_on_write:
		case libtorrent::settings_pack::enable_set_file_valid_data:
			return true;
	}
	return false;
}
} // namespace

bool no_permissions::allow_start() const { return false; }
bool no_permissions::allow_stop() const { return false; }
bool no_permissions::allow_recheck() const { return false; }
bool no_permissions::allow_set_file_prio() const { return false; }
bool no_permissions::allow_list() const { return false; }
bool no_permissions::allow_add() const { return false; }
bool no_permissions::allow_remove() const { return false; }
bool no_permissions::allow_remove_data() const { return false; }
bool no_permissions::allow_queue_change() const { return false; }
bool no_permissions::allow_get_settings(int) const { return false; }
bool no_permissions::allow_set_settings(int) const { return false; }
bool no_permissions::allow_get_data() const { return false; }
bool no_permissions::allow_session_status() const { return false; }

bool read_only_permissions::allow_start() const { return false; }
bool read_only_permissions::allow_stop() const { return false; }
bool read_only_permissions::allow_recheck() const { return false; }
bool read_only_permissions::allow_set_file_prio() const { return false; }
bool read_only_permissions::allow_list() const { return true; }
bool read_only_permissions::allow_add() const { return false; }
bool read_only_permissions::allow_remove() const { return false; }
bool read_only_permissions::allow_remove_data() const { return false; }
bool read_only_permissions::allow_queue_change() const { return false; }
bool read_only_permissions::allow_get_settings(int) const { return true; }
bool read_only_permissions::allow_set_settings(int) const { return false; }
bool read_only_permissions::allow_get_data() const { return true; }
bool read_only_permissions::allow_session_status() const { return true; }

bool full_permissions::allow_start() const { return true; }
bool full_permissions::allow_stop() const { return true; }
bool full_permissions::allow_recheck() const { return true; }
bool full_permissions::allow_set_file_prio() const { return true; }
bool full_permissions::allow_list() const { return true; }
bool full_permissions::allow_add() const { return true; }
bool full_permissions::allow_remove() const { return true; }
bool full_permissions::allow_remove_data() const { return true; }
bool full_permissions::allow_queue_change() const { return true; }
bool full_permissions::allow_get_settings(int) const { return true; }
bool full_permissions::allow_set_settings(int) const { return true; }
bool full_permissions::allow_get_data() const { return true; }
bool full_permissions::allow_session_status() const { return true; }

bool remote_user::allow_start() const { return true; }
bool remote_user::allow_stop() const { return true; }
bool remote_user::allow_recheck() const { return true; }
bool remote_user::allow_set_file_prio() const { return true; }
bool remote_user::allow_list() const { return true; }
bool remote_user::allow_add() const { return true; }
bool remote_user::allow_remove() const { return true; }
bool remote_user::allow_remove_data() const { return true; }
bool remote_user::allow_queue_change() const { return true; }
bool remote_user::allow_get_settings(int name) const { return !is_local_setting(name); }
bool remote_user::allow_set_settings(int name) const { return !is_local_setting(name); }
bool remote_user::allow_get_data() const { return true; }
bool remote_user::allow_session_status() const { return true; }

} // namespace ltweb
