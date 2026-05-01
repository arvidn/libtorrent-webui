/*

Copyright (c) 2013, 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "pam_auth.hpp"
#include <security/pam_appl.h>
#include "libtorrent/string_util.hpp"

#include <string>

namespace ltweb {

pam_auth::pam_auth(std::string service_name, int default_group)
	: default_group(default_group)
	, service_name(std::move(service_name))
{
}

pam_auth::~pam_auth() {}

namespace {

std::optional<int> fail(int ret, pam_handle_t* h)
{
	if (h) pam_end(h, ret);
	return std::nullopt;
}

struct auth_context {
	std::string username;
	std::string password;
};

int pam_conversation(
	int num_msgs, struct pam_message const** msg, struct pam_response** r, void* user
)
{
	auth_context* ctx = static_cast<auth_context*>(user);

	if (num_msgs == 0) return PAM_SUCCESS;

	// Allocate an array for responses. Memory is freed by PAM.
	*r = static_cast<pam_response*>(calloc(num_msgs, sizeof(pam_response)));
	if (*r == nullptr) return PAM_BUF_ERR;

	for (int i = 0; i < num_msgs; ++i) {
		switch (msg[i]->msg_style) {
			// echo on is code for "username"
			case PAM_PROMPT_ECHO_ON:
				r[i]->resp = allocate_string_copy(ctx->username.c_str());
				break;

			// echo off is code for "password"
			case PAM_PROMPT_ECHO_OFF:
				r[i]->resp = allocate_string_copy(ctx->password.c_str());
				break;

			case PAM_ERROR_MSG:
				fprintf(stderr, "authentication error: %s\n", msg[i]->msg);
				break;

			case PAM_TEXT_INFO:
				fprintf(stderr, "auth: %s\n", msg[i]->msg);
				break;
		}
	}
	return PAM_SUCCESS;
}

} // anonymous namespace

std::optional<int> pam_auth::verify(std::string_view username, std::string_view password) const
{
	if (username.empty()) return std::nullopt;

	auth_context ctx;
	ctx.username = std::string(username);
	ctx.password = std::string(password);

	pam_conv c;
	c.conv = &pam_conversation;
	c.appdata_ptr = &ctx;

	pam_handle_t* handle = nullptr;
	int ret = pam_start(service_name.c_str(), ctx.username.c_str(), &c, &handle);
	if (ret != PAM_SUCCESS) return fail(ret, handle);

	ret = pam_set_item(handle, PAM_RUSER, ctx.username.c_str());
	if (ret != PAM_SUCCESS) return fail(ret, handle);

	ret = pam_set_item(handle, PAM_RHOST, "localhost");
	if (ret != PAM_SUCCESS) return fail(ret, handle);

	ret = pam_authenticate(handle, 0);
	if (ret != PAM_SUCCESS) return fail(ret, handle);

	ret = pam_acct_mgmt(handle, 0);
	if (ret != PAM_SUCCESS) return fail(ret, handle);

	pam_end(handle, ret);

	auto i = users.find(ctx.username);
	if (i != users.end()) return i->second;
	return default_group;
}

} // namespace ltweb
