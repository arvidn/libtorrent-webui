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
		case lt::settings_pack::user_agent:
		case lt::settings_pack::handshake_client_version:
		case lt::settings_pack::peer_fingerprint:
		case lt::settings_pack::announce_ip:
		case lt::settings_pack::announce_port:

		// local network interfaces and ports
		case lt::settings_pack::listen_interfaces:
		case lt::settings_pack::outgoing_interfaces:
		case lt::settings_pack::outgoing_port:
		case lt::settings_pack::num_outgoing_ports:
		case lt::settings_pack::listen_queue_size:
		case lt::settings_pack::listen_system_port_fallback:
		case lt::settings_pack::max_retry_port_bind:
		case lt::settings_pack::peer_dscp:

		// proxy configuration
		case lt::settings_pack::proxy_hostname:
		case lt::settings_pack::proxy_username:
		case lt::settings_pack::proxy_password:
		case lt::settings_pack::proxy_type:
		case lt::settings_pack::proxy_port:
		case lt::settings_pack::proxy_hostnames:
		case lt::settings_pack::proxy_peer_connections:
		case lt::settings_pack::proxy_tracker_connections:
		case lt::settings_pack::proxy_send_host_in_connect:
		case lt::settings_pack::socks5_udp_send_local_ep:

		// I2P SAM bridge
		case lt::settings_pack::i2p_hostname:
		case lt::settings_pack::i2p_port:
		case lt::settings_pack::i2p_inbound_quantity:
		case lt::settings_pack::i2p_outbound_quantity:
		case lt::settings_pack::i2p_inbound_length:
		case lt::settings_pack::i2p_outbound_length:
		case lt::settings_pack::i2p_inbound_length_variance:
		case lt::settings_pack::i2p_outbound_length_variance:
		case lt::settings_pack::allow_i2p_mixed:

		// NAT-PMP / PCP / UPnP port mapping
		case lt::settings_pack::natpmp_gateway:
		case lt::settings_pack::natpmp_lease_duration:
		case lt::settings_pack::upnp_lease_duration:
		case lt::settings_pack::upnp_ignore_nonrouters:
		case lt::settings_pack::enable_upnp:
		case lt::settings_pack::enable_natpmp:

		// local network discovery
		case lt::settings_pack::enable_lsd:
		case lt::settings_pack::local_service_announce_interval:

		// DHT bootstrap and WebTorrent infrastructure
		case lt::settings_pack::dht_bootstrap_nodes:
		case lt::settings_pack::webtorrent_stun_server:

		// OS-level resources and filesystem behavior
		case lt::settings_pack::aio_threads:
		case lt::settings_pack::hashing_threads:
		case lt::settings_pack::file_pool_size:
		case lt::settings_pack::enable_ip_notifier:
		case lt::settings_pack::no_atime_storage:
		case lt::settings_pack::disk_disable_copy_on_write:
		case lt::settings_pack::enable_set_file_valid_data:
		case lt::settings_pack::mmap_file_size_cutoff:
		case lt::settings_pack::close_file_interval:

		// uTP settings
		case lt::settings_pack::utp_target_delay:
		case lt::settings_pack::utp_gain_factor:
		case lt::settings_pack::utp_min_timeout:
		case lt::settings_pack::utp_syn_resends:
		case lt::settings_pack::utp_fin_resends:
		case lt::settings_pack::utp_num_resends:
		case lt::settings_pack::utp_connect_timeout:
		case lt::settings_pack::utp_loss_multiplier:

		// shutdown related
		case lt::settings_pack::stop_tracker_timeout:

		case lt::settings_pack::alert_mask:
		case lt::settings_pack::alert_queue_size:
		case lt::settings_pack::tick_interval:
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
std::uint64_t no_permissions::allow_set_tag() const { return 0; }
aux::wire_flags_t no_permissions::allow_set_flags() const { return {}; }

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
std::uint64_t read_only_permissions::allow_set_tag() const { return 0; }
aux::wire_flags_t read_only_permissions::allow_set_flags() const { return {}; }

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
std::uint64_t full_permissions::allow_set_tag() const { return ~std::uint64_t(0); }
aux::wire_flags_t full_permissions::allow_set_flags() const { return aux::wire_flags_t::all(); }

bool remote_user::allow_start() const { return true; }
bool remote_user::allow_stop() const { return true; }
bool remote_user::allow_recheck() const { return true; }
bool remote_user::allow_set_file_prio() const { return true; }
bool remote_user::allow_list() const { return true; }
bool remote_user::allow_add() const { return true; }
bool remote_user::allow_remove() const { return true; }
bool remote_user::allow_remove_data() const { return true; }
bool remote_user::allow_queue_change() const { return true; }
// name == -1 denotes a webui-only setting that has no libtorrent
// counterpart. Every such setting in the codebase today is host-local
// (filesystem paths, listen port, autoload directory) so deny them all
// to remote users. If a portable webui setting is ever introduced this
// will need to gain an allowlist.
bool remote_user::allow_get_settings(int name) const
{
	return name >= 0 && !is_local_setting(name);
}
bool remote_user::allow_set_settings(int name) const
{
	return name >= 0 && !is_local_setting(name);
}
bool remote_user::allow_get_data() const { return true; }
bool remote_user::allow_session_status() const { return true; }
std::uint64_t remote_user::allow_set_tag() const { return ~std::uint64_t(0); }
aux::wire_flags_t remote_user::allow_set_flags() const { return aux::wire_flags_t::all(); }

} // namespace ltweb
