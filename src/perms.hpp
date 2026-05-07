/*

Copyright (c) 2013, 2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_PERMS_HPP
#define LTWEB_PERMS_HPP

namespace ltweb {

// This is the interface an object needs to implement in order
// to specify custom access permissions.
struct permissions_interface {
	// If returning true, the user may start torrents
	virtual bool allow_start() const = 0;

	// If returning true, the user may stop torrents
	virtual bool allow_stop() const = 0;

	// If returning true, the user may re-check torrents
	virtual bool allow_recheck() const = 0;

	// If returning true, the user may modify the priority of files
	virtual bool allow_set_file_prio() const = 0;

	// If returning true, the user may list torrents
	// TODO: separate this out to listing torrents and listing files
	virtual bool allow_list() const = 0;

	// If returning true, the user may add torrents
	virtual bool allow_add() const = 0;

	// If returning true, the user may remove torrents
	virtual bool allow_remove() const = 0;

	// If returning true, the user may remove torrents
	// and delete their data from disk
	virtual bool allow_remove_data() const = 0;

	// If returning true, the user may queue-up or -down torrents
	virtual bool allow_queue_change() const = 0;

	// If returning true, the user may GET the specified setting
	// name is the constant used in lt::settings_pack or -1 for
	// settings that don't fit a libtorrent setting
	virtual bool allow_get_settings(int name) const = 0;

	// If returning true, the user may SET the specified setting
	// name is the constant used in lt::settings_pack or -1 for
	// settings that don't fit a libtorrent setting
	virtual bool allow_set_settings(int name) const = 0;

	// If returning true, the user may download the content of torrents
	virtual bool allow_get_data() const = 0;

	// TODO: separate permissions to alter torrent state. separate different categories of settings?
	// If returning true, the user is allowed to query session status,
	// like global upload and download rates
	virtual bool allow_session_status() const = 0;
};

// an implementation of permissions_interface that rejects all access
struct no_permissions : permissions_interface {
	bool allow_start() const override;
	bool allow_stop() const override;
	bool allow_recheck() const override;
	bool allow_set_file_prio() const override;
	bool allow_list() const override;
	bool allow_add() const override;
	bool allow_remove() const override;
	bool allow_remove_data() const override;
	bool allow_queue_change() const override;
	bool allow_get_settings(int) const override;
	bool allow_set_settings(int) const override;
	bool allow_get_data() const override;
	bool allow_session_status() const override;
};

// an implementation of permissions_interface that only allow inspecting
// the state of the torrent client, not altering it in any way. No modification
// of settings, no adding/removing/rechecking of torrents.
struct read_only_permissions : permissions_interface {
	bool allow_start() const override;
	bool allow_stop() const override;
	bool allow_recheck() const override;
	bool allow_set_file_prio() const override;
	bool allow_list() const override;
	bool allow_add() const override;
	bool allow_remove() const override;
	bool allow_remove_data() const override;
	bool allow_queue_change() const override;
	bool allow_get_settings(int) const override;
	bool allow_set_settings(int) const override;
	bool allow_get_data() const override;
	bool allow_session_status() const override;
};

// an implementation of permissions_interface that permit all access.
struct full_permissions : permissions_interface {
	bool allow_start() const override;
	bool allow_stop() const override;
	bool allow_recheck() const override;
	bool allow_set_file_prio() const override;
	bool allow_list() const override;
	bool allow_add() const override;
	bool allow_remove() const override;
	bool allow_remove_data() const override;
	bool allow_queue_change() const override;
	bool allow_get_settings(int) const override;
	bool allow_set_settings(int) const override;
	bool allow_get_data() const override;
	bool allow_session_status() const override;
};

// an implementation of permissions_interface for users connecting from a
// remote machine. They have full access to the torrent client itself but
// are denied access to settings that are inherently local to the host
// running the daemon: network interfaces, proxy configuration, server
// identity sent on the wire, NAT and port-mapping, local peer discovery,
// and OS/filesystem-tuning knobs. A remote user generally has no way to
// know what is sensible for these on the host, and some of them (proxy
// credentials in particular) would leak host-side secrets if readable.
struct remote_user : permissions_interface {
	bool allow_start() const override;
	bool allow_stop() const override;
	bool allow_recheck() const override;
	bool allow_set_file_prio() const override;
	bool allow_list() const override;
	bool allow_add() const override;
	bool allow_remove() const override;
	bool allow_remove_data() const override;
	bool allow_queue_change() const override;
	bool allow_get_settings(int name) const override;
	bool allow_set_settings(int name) const override;
	bool allow_get_data() const override;
	bool allow_session_status() const override;
};

} // namespace ltweb

#endif // LTWEB_PERMS_HPP
