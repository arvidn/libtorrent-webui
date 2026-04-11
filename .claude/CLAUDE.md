# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

This project uses **Boost.Build (b2)**. The `libtorrent` library is a git submodule.

```bash
# Clone with submodules
git clone --recursive <repo>
# or after cloning:
git submodule update --init --recursive

# Install dependencies (Linux)
sudo apt-get install -y libboost-dev libboost-tools-dev libboost-json-dev libssl-dev libsqlite3-dev

# Build everything
b2 webtorrent=on

# Build with a specific toolset
b2 gcc webtorrent=on

# Build in debug mode
b2 gcc debug webtorrent=on
```

The `webui_test` binary is the reference/example program (see `run_debug.sh` which runs it under gdb).

## Running Tests

Tests live in `tests/` and use **Boost.Test** (header-only, `<boost/test/included/unit_test.hpp>`) via Boost.Build:

```bash
# Run all tests
cd tests
b2
```

Each test binary uses `BOOST_TEST_MODULE` and `BOOST_AUTO_TEST_CASE` / `BOOST_AUTO_TEST_SUITE`. There is no separate test library to link — Boost.Test is compiled in via the header. The Jamfile uses the `unit-test` rule (not `run`) for all test targets.

Tests depend on both `/torrent` (libtorrent submodule) and `/torrent-webui` (this project).

## Architecture

### Core HTTP Server (`src/webui.hpp`, `src/webui.cpp`)

`webui_base` is the HTTPS server built on **Boost.Beast + Boost.Asio**. It manages an `io_context`, an SSL context, a thread pool, and a `listener`. HTTP request routing works by registering `http_handler` instances — each handler declares a `path_prefix()` and receives matching requests via `handle_http()`.

### Web UI Implementations

Two complete web UI protocol implementations exist as `http_handler` subclasses:

- **`libtorrent_webui`** (`src/libtorrent_webui.hpp/cpp`) — native protocol using WebSocket RPC. Upgrades HTTP connections to WebSocket and dispatches binary RPC calls. Each RPC is a method on the class (e.g. `get_torrent_updates`, `add_torrent`).
- **`utorrent_webui`** (`src/utorrent_webui.hpp/cpp`) — uTorrent-compatible HTTP API at `/gui`. Lets uTorrent web clients talk to libtorrent.

### Alert System (`src/alert_handler.hpp`, `src/alert_observer.hpp`)

`alert_handler` is a dispatcher that fans out libtorrent alerts to registered `alert_observer` instances by alert type. Components subscribe by calling `subscribe()` with a va-list of alert type IDs. `libtorrent_webui` and `torrent_history` both implement `alert_observer`.

### Torrent History (`src/torrent_history.hpp/cpp`)

Implements delta-tracking for torrent status updates. Uses a `frame_t` counter (uint32) to version each field of `torrent_history_entry`. Clients request updates since a given frame number; only changed fields are returned. Uses a `boost::bimap<list_of<frame_t>, unordered_set_of<torrent_history_entry>>` to efficiently find torrents modified after a frame.

### Auth & Permissions (`src/auth_interface.hpp`)

Two orthogonal interfaces:
- `auth_interface` — authenticates credentials, returns a `permissions_interface*` or null
- `permissions_interface` — fine-grained capability checks (allow_start, allow_add, etc.)

Built-in implementations: `no_auth` (accepts all), `pam_auth` (Linux PAM), `auth` (file-based). Permission presets: `no_permissions`, `read_only_permissions`, `full_permissions`.

### WebSocket Connection (`src/websocket_conn.hpp/cpp`)

`websocket_conn` manages a single WebSocket client for `libtorrent_webui`. It holds the upgraded `ws::stream<ssl_stream<tcp_stream>>`, a send queue (`std::deque<std::vector<char>>`), and calls back into `libtorrent_webui::on_websocket_read()` when data arrives.

### Supporting Components

- **`alert_handler`** — dispatches alerts from the libtorrent session to observers
- **`save_resume`** / **`save_settings`** — persistence of session state and settings
- **`auto_load`** — watches a directory for new `.torrent` files
- **`serve_files`** / **`file_downloader`** / **`file_requests`** — serve torrent content over HTTP
- **`torrent_post`** — handles `.torrent` file uploads
- **`stats_logging`** — logs session statistics
- **`json_util`** + **`jsmn`** — JSON building/parsing (jsmn is a minimal C tokenizer)
- **`base64`** / **`hex`** / **`escape_json`** — encoding utilities
- **`percent_encode`** / **`url_decode`** — URL encoding/decoding utilities
- **`utils.hpp`** — shared inline utilities: `is_whitespace`, `trim`, `extension`, `split`, `parse_quoted_string` (RFC 7230 quoted-string), `ci_find` (case-insensitive substring search via `boost::algorithm`), `iequals`, `str` (variadic string builder)

## Key Design Patterns

- **All HTTP handlers are registered with `webui_base::add_handler()`** — routing is purely prefix-based, first match wins.
- **`send_http()` is a free template function** in the `libtorrent` namespace — use it to send any Beast response body type asynchronously.
- **The `done` callback** passed to `handle_http` must be called exactly once when the response is complete (signals readiness for next request).
- **`frame_t` counters** are the key mechanism for efficient incremental updates — always pass the client's last-known frame when querying torrent/stats updates.
