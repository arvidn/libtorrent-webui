/*

Copyright (c) 2013, 2020, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LTWEB_PERM_INTERFACE_HPP
#define LTWEB_PERM_INTERFACE_HPP

#include <string>

namespace ltweb {

/**
		This is the interface an object need to implement in order
		to specify custom access permissions.
	*/
struct permissions_interface {
	/// If returning true, the user may start torrents
	virtual bool allow_start() const = 0;

	/// If returning true, the user may stop torrents
	virtual bool allow_stop() const = 0;

	/// If returning true, the user may re-check torrents
	virtual bool allow_recheck() const = 0;

	/// If returning true, the user may modifiy the priority of files
	virtual bool allow_set_file_prio() const = 0;

	/// If returning true, the user may list torrents
	// TODO: separate this out to listing torrents and listing files
	virtual bool allow_list() const = 0;

	/// If returning true, the user may add torrents
	virtual bool allow_add() const = 0;

	/// If returning true, the user may remove torrents
	virtual bool allow_remove() const = 0;

	/// If returning true, the user may remove torrents
	/// and delete their data from disk
	virtual bool allow_remove_data() const = 0;

	/// If returning true, the user may queue-up or -down torrents torrents
	virtual bool allow_queue_change() const = 0;

	/// If returning true, the user may GET the specified setting
	/// \param name is the constant used in lt::settings_pack
	/// or -1 for settings that don't fit a libtorrent setting
	virtual bool allow_get_settings(int name) const = 0;

	/// If returning true, the user may SET the specified setting
	/// \param name is the constant used in lt::settings_pack
	/// or -1 for settings that don't fit a libtorrent setting
	virtual bool allow_set_settings(int name) const = 0;

	/// If returning true, the user may download the content of torrents
	virtual bool allow_get_data() const = 0;

	// TODO: separate permissions to alter torrent state. separate different categories of settings?
	/// If returning true, the user is allowed to query session status,
	/// like global upload and download rates
	virtual bool allow_session_status() const = 0;
};

/**
		The interface to an authentication module. This plugs into web interfaces
		to authenticate users and determine their access permissions. The two main
		implementations are auth and pam_auth.
	*/
struct auth_interface {
	/// finds an appropriate permissions objects for the given account.
	/// \return the persmissions object for the specified
	/// account, or NULL in case authentication fails.
	virtual permissions_interface const*
	find_user(std::string username, std::string password) const = 0;
};

/// an implementation of permissions_interface that reject all access
struct no_permissions : permissions_interface {
	no_permissions() {}
	bool allow_start() const override { return false; }
	bool allow_stop() const override { return false; }
	bool allow_recheck() const override { return false; }
	bool allow_set_file_prio() const override { return false; }
	bool allow_list() const override { return false; }
	bool allow_add() const override { return false; }
	bool allow_remove() const override { return false; }
	bool allow_remove_data() const override { return false; }
	bool allow_queue_change() const override { return false; }
	bool allow_get_settings(int) const override { return false; }
	bool allow_set_settings(int) const override { return false; }
	bool allow_get_data() const override { return false; }
	bool allow_session_status() const override { return false; }
};

/// an implementation of permissions_interface that only allow inspecting
/// the stat of the torrent client, not altering it in any way. No modification
/// of settings, no adding/removing/rechecking of torrents.
struct read_only_permissions : permissions_interface {
	read_only_permissions() {}
	bool allow_start() const override { return false; }
	bool allow_stop() const override { return false; }
	bool allow_recheck() const override { return false; }
	bool allow_set_file_prio() const override { return false; }
	bool allow_list() const override { return true; }
	bool allow_add() const override { return false; }
	bool allow_remove() const override { return false; }
	bool allow_remove_data() const override { return false; }
	bool allow_queue_change() const override { return false; }
	bool allow_get_settings(int) const override { return true; }
	bool allow_set_settings(int) const override { return false; }
	bool allow_get_data() const override { return true; }
	bool allow_session_status() const override { return true; }
};

/// an implementation of permissions_interface that permit all access.
struct full_permissions : permissions_interface {
	full_permissions() {}
	bool allow_start() const override { return true; }
	bool allow_stop() const override { return true; }
	bool allow_recheck() const override { return true; }
	bool allow_set_file_prio() const override { return true; }
	bool allow_list() const override { return true; }
	bool allow_add() const override { return true; }
	bool allow_remove() const override { return true; }
	bool allow_remove_data() const override { return true; }
	bool allow_queue_change() const override { return true; }
	bool allow_get_settings(int) const override { return true; }
	bool allow_set_settings(int) const override { return true; }
	bool allow_get_data() const override { return true; }
	bool allow_session_status() const override { return true; }
};

} // namespace ltweb

#endif // LTWEB_PERM_INTERFACE_HPP
