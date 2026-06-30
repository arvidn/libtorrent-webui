libtorrent-webui
================

The libtorrent webui is a high-performing, scalable, interface to bittorrent clients
over a websocket. The intention is that applications, specifically web applications,
can control a bittorrent client over this interface, or be extended with bittorrent
functionality.

In this document, the "bittorrent client" and the "application" will be used to refer
to the websocket server and the websocket client respectively. i.e. the application
with bittorrent capabilities and the application talking to it and possibly controlling
it.

The libtorrent-webui protocol sits on top of the `websocket protocol`_. It consists
of independent messages, each encoded as a binary websocket frame.

An application talking to a bittorrent client communicate with it over an *RPC* protocol,
(Remote procedure call). This means each message it sends to the bittorrent client is
conceptually calling a function on the bittorrent client and the response from that
function is then returned back in another message. That is, each RPC call message, has
a corresponding RPC response message.

The protocol is designed specifically to have a very compact represenation on the wire
and to not send redundant information, especially not in the common case. This is why
the protocol is binary and why there's not intermediate structure representation (like
bencoding, rencoding or JSON).

.. _`websocket protocol`: http://tools.ietf.org/html/rfc6455

RPC format
----------

To make a function call, send a frame with this format:

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 0        | uint8_t            | ``function-id`` (the function to call)    |
|          |                    | with the most significant bit cleared.    |
+----------+--------------------+-------------------------------------------+
| 1        | uint16_t           | ``transaction-id`` (echoed back in        |
|          |                    | response)                                 |
+----------+--------------------+-------------------------------------------+
| 3        | ...                | *arguments* (RPC call specific)           |
+----------+--------------------+-------------------------------------------+

The response frame looks like this:

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 0        | uint8_t            | ``function-id`` (the function returning)  |
|          |                    | with the most significan bit set.         |
+----------+--------------------+-------------------------------------------+
| 1        | uint16_t           | ``transaction-id`` (copied from the call) |
+----------+--------------------+-------------------------------------------+
| 3        | uint8_t            | ``error-code`` (0 = success)              |
+----------+--------------------+-------------------------------------------+
| 4        | ...                | *return value* (RPC call specific)        |
+----------+--------------------+-------------------------------------------+

The most significant bit of the ``function-id`` field indicates whether the message
is a return value or a call in itself.

The ``transaction-id`` sent in a call, is repeated in the response message. This
allows the caller to pair up calls with their responses. Responses may
be returned out of order relative to the order the calls were made.

The values used for the ``error code`` field in the response are detailed in
`Appendix B`_.

settings
--------

Settings are primarily represented by 16-bit codes. Get- and Set calls for
settings only use these 16-bit codes to refer to settings. The mapping of
settings codes to the actual settings is not defined by the protcol. Instead,
an application need to query the bittorrent client for available settings
which returns a mapping of setting names to type and code. This function
is called `list-settings`_.

functions
---------

Reference for RPC format for all functions specified by this protocol.

All messages sent to the bittorrent client start with an 8 bit message identifier.
See `appendix A`_ for all message IDs.

get-torrent-updates
...................

function id 0.

This function requests updates of torrent states. Updates are typically relative
to the last update. The state for all torrents is assumed to be kept by the
application, and update fields that are changing by querying them with this function.

The frame number is a form of time stamp indicating the current time on the bittorrent
client. This frame number can be used later to request updates since a certain time.
The frame number is always the first argument, all subsequent arguments are updates for
one torrent each.

It's possible to always request the entire state for every torrent by passing in
a frame number of 0.

The call looks like this (not including the RPC-call header):

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 3        | uint32_t           | ``frame-number`` (timestamp)              |
+----------+--------------------+-------------------------------------------+
| 7        | uint64_t           | ``field-bitmask`` (only these fields are  |
|          |                    | returned)                                 |
+----------+--------------------+-------------------------------------------+
| 15       | uint8_t            | ``status-mask-old`` (optional, see        |
|          |                    | Filtering below)                          |
+----------+--------------------+-------------------------------------------+
| 16       | uint8_t            | ``status-value-old`` (optional)           |
+----------+--------------------+-------------------------------------------+
| 17       | uint64_t           | ``tag-mask-old`` (optional)               |
+----------+--------------------+-------------------------------------------+
| 25       | uint8_t            | ``status-mask-new`` (optional)            |
+----------+--------------------+-------------------------------------------+
| 26       | uint8_t            | ``status-value-new`` (optional)           |
+----------+--------------------+-------------------------------------------+
| 27       | uint64_t           | ``tag-mask-new`` (optional)               |
+----------+--------------------+-------------------------------------------+

The six filter fields are an optional 20-byte trailing block. The call is
either exactly 12 bytes after the RPC header (no filter) or exactly 32 bytes
(filter present). Any other length returns error code 5 (truncated request).
See Filtering_ below for semantics.

The torrent updates don't necessarily include all fields of the torrent. There is
a bitmask indicating which fields are included in this update. Any field not
included should be left at its previous value.

The return value for this function is (offset includes RPC-response header):

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 4        | uint32_t           | ``frame-number`` (timestamp)              |
+----------+--------------------+-------------------------------------------+
| 8        | uint32_t           | ``num-torrents`` (the number of torrent   |
|          |                    | updates to follow)                        |
+----------+--------------------+-------------------------------------------+
| 12       | uint32_t           | ``num-removed-torrents``                  |
|          |                    | at the end of the update, there is a      |
|          |                    | list of info-hashes with this many        |
|          |                    | entries.                                  |
+----------+--------------------+-------------------------------------------+
| 16       | uint8_t[20]        | ``info-hash`` indicate which torrent      |
|          |                    | the following update refers to.           |
+----------+--------------------+-------------------------------------------+
| 46       | uint64_t           | ``update-bitmask`` bitmask indicating     |
|          |                    | which torrent fields are being updated.   |
+----------+--------------------+-------------------------------------------+
| 32       | ...                | *values for all updated fields*           |
+----------+--------------------+-------------------------------------------+
| ...      | uint8_t[20]        | ``removed-info-hash``                     |
+----------+--------------------+-------------------------------------------+

The 3 fields ``info-hash``, ``update-bitmask`` and
*values for all updated fields*, are repeated ``num-torrents`` times.

The ``removed-info-hash`` field is repeated ``num-removed-torrents`` times.
These info-hashes have been removed and will no longer receive any updates
beoynd this frame number.

The fields on torrents, in bitmask bit-order (LSB is bit 0), are:

+----------+---------------------+------------------------------------------+
| field-id | type                | name                                     |
+==========+=====================+==========================================+
| 0        | uint64_t            | ``flags`` bitmask with the following     |
|          |                     | bits:                                    |
|          |                     |                                          |
|          |                     |  | 0x000001. stopped                     |
|          |                     |  | 0x000002. auto-managed                |
|          |                     |  | 0x000004. sequential-downloads        |
|          |                     |  | 0x000008. seeding                     |
|          |                     |  | 0x000010. finished                    |
|          |                     |  | 0x000020. -- unused --                |
|          |                     |  | 0x000040. has-metadata                |
|          |                     |  | 0x000080. has-incoming-connections    |
|          |                     |  | 0x000100. seed-mode                   |
|          |                     |  | 0x000200. upload-mode                 |
|          |                     |  | 0x000400. share-mode                  |
|          |                     |  | 0x000800. super-seeding               |
|          |                     |  | 0x001000. moving storage              |
|          |                     |  | 0x002000. announcing to trackers      |
|          |                     |  | 0x004000. announcing to lsd           |
|          |                     |  | 0x008000. announcing to dht           |
|          |                     |  | 0x010000. disable-pex                 |
|          |                     |  | 0x020000. disable-dht                 |
|          |                     |  | 0x040000. disable-lsd                 |
|          |                     |  | 0x080000. disable-v1-hashes           |
|          |                     |  | 0x100000. i2p-torrent                 |
|          |                     |                                          |
+----------+---------------------+------------------------------------------+
| 1        | uint16_t, uint8_t[] | ``name``. This is a variable length      |
|          |                     | string with a 16 bit length prefix.      |
|          |                     | it is encoded as UTF-8.                  |
+----------+---------------------+------------------------------------------+
| 2        | uint64_t            | ``total-uploaded`` (number of bytes)     |
+----------+---------------------+------------------------------------------+
| 3        | uint64_t            | ``total-downloaded`` (number of bytes)   |
+----------+---------------------+------------------------------------------+
| 4        | uint64_t            | ``added-time`` (posix time)              |
+----------+---------------------+------------------------------------------+
| 5        | uint64_t            | ``completed-time`` (posix time)          |
+----------+---------------------+------------------------------------------+
| 6        | uint32_t            | ``upload-rate`` (Bytes per second)       |
+----------+---------------------+------------------------------------------+
| 7        | uint32_t            | ``download-rate`` (Bytes per second)     |
+----------+---------------------+------------------------------------------+
| 8        | uint32_t            | ``progress`` (specified in the range     |
|          |                     | 0 - 1000000)                             |
+----------+---------------------+------------------------------------------+
| 9        | uint16_t, uint8_t[] | ``error`` Variable length string with 16 |
|          |                     | bit length prefix. Encoded as UTF-8.     |
+----------+---------------------+------------------------------------------+
| 10       | uint32_t            | ``connected-peers``                      |
+----------+---------------------+------------------------------------------+
| 11       | uint32_t            | ``connected-seeds``                      |
+----------+---------------------+------------------------------------------+
| 12       | uint32_t            | ``downloaded-pieces``                    |
+----------+---------------------+------------------------------------------+
| 13       | uint64_t            | ``total-done`` The total number of bytes |
|          |                     | completed (downloaded and checked)       |
+----------+---------------------+------------------------------------------+
| 14       | uint32_t, uint32_t  | ``distributed-copies``. The first int    |
|          |                     | is the integer portion of the fraction,  |
|          |                     | the second int is the fractional part.   |
+----------+---------------------+------------------------------------------+
| 15       | uint64_t            | ``all-time-upload`` (Bytes)              |
+----------+---------------------+------------------------------------------+
| 16       | uint64_t            | ``all-time-download`` (Bytes)            |
+----------+---------------------+------------------------------------------+
| 17       | uint32_t            | ``unchoked-peers``                       |
+----------+---------------------+------------------------------------------+
| 18       | uint32_t            | ``num-connections``                      |
+----------+---------------------+------------------------------------------+
| 19       | uint32_t            | ``queue-position``                       |
+----------+---------------------+------------------------------------------+
| 20       | uint8_t             | ``state``                                |
|          |                     |                                          |
|          |                     |    0. checking-files                     |
|          |                     |    1. downloading-metadata               |
|          |                     |    2. downloading                        |
|          |                     |    3. seeding                            |
|          |                     |                                          |
+----------+---------------------+------------------------------------------+
| 21       | uint64_t            | ``failed-bytes`` (Bytes)                 |
+----------+---------------------+------------------------------------------+
| 22       | uint64_t            | ``redundant-bytes`` (Bytes)              |
+----------+---------------------+------------------------------------------+
| 23       | uint64_t            | ``tag``. An application-defined 64-bit   |
|          |                     | bitfield set via the `set-tag`_ RPC. The |
|          |                     | semantics of individual bits are not     |
|          |                     | defined by this protocol; the client     |
|          |                     | application chooses what each bit means  |
|          |                     | (eg. labels, categories). A torrent with |
|          |                     | no tag set reports 0.                    |
+----------+---------------------+------------------------------------------+

For example, an update with the bitmask ``0x1`` means that the only thing that
changed since the last update for this torrent was one or more of the torrent's
flags. Only the flags field will follow for this torrent's update. If there are
more torrent updates, the next field to read will be the info-hash for the next
update.

*TODO: add a list of removed torrents*

.. _Filtering:

Filtering
.........

A get-torrent-updates request may carry a pair of filter specs in its
optional 20-byte trailing block. The request carries both the spec the
client used at the previous frame-number (the ``-old`` fields) and the
spec it wants now (the ``-new`` fields), so the server can decide per
torrent whether to emit a delta, a fresh full update for a newly-
matching torrent, or a removal for one that fell out of the view --
without keeping per-client state.

A filter spec is ``(status_mask, status_value, tag_mask)``. A torrent
matches iff::

   (status_mask == 0
     || (torrent.status_bits & status_mask) == (status_value & status_mask))
   AND
   (tag_mask == 0 || (torrent.tag & tag_mask) != 0)

The status axis is exact-match within the masked bits: bits selected by
``status_mask`` must equal the corresponding bits in ``status_value``,
bits outside the mask are don't-care. This lets a single filter require
some bits set and others cleared -- eg. matching "stopped" as paused
set AND auto-managed cleared.

The tag axis is any-of: a torrent matches when any bit selected by
``tag_mask`` is also set in the torrent's tag.

A mask of 0 disables that axis (``status_value`` is ignored when
``status_mask == 0``). Both axes all-zero on both specs degenerates to
an unfiltered query, identical to the request without the trailing 20
bytes.

``torrent.status_bits`` is an 8-bit projection of the torrent's state
and flags:

+-----+----------------------------------+
| bit | meaning                          |
+=====+==================================+
| 0   | stopped (``flags`` bit 0)        |
+-----+----------------------------------+
| 1   | auto-managed (``flags`` bit 1)   |
+-----+----------------------------------+
| 2   | -- reserved --                   |
+-----+----------------------------------+
| 3   | errored                          |
+-----+----------------------------------+
| 4   | state == checking-files          |
+-----+----------------------------------+
| 5   | state == downloading-metadata    |
+-----+----------------------------------+
| 6   | state == downloading             |
+-----+----------------------------------+
| 7   | state == seeding                 |
+-----+----------------------------------+

``torrent.tag`` is the 64-bit value reported as wire field 23 and set by
the `set-tag`_ RPC.

Per-torrent outcomes:

- *matches new filter, matched old filter*: normal delta of changed
  fields, exactly as if no filter were in effect.
- *matches new filter, did not match old*: the response carries every
  client-requested field for this torrent (treated as a fresh
  initialisation).
- *matched old, no longer matches new*: the torrent's info-hash appears
  in the ``removed-info-hash`` list alongside session-level tombstones;
  the client should drop it from its local view.
- *neither*: omitted from the response entirely.

torrent actions
...............

There is a group of commands that are simple. That just perform an action on one
or more torrents with no additional arguments. The torrents they operate on are
specified by their corresponding info-hash (encoded as a binary 20 byte string).

The functions that follow this simple syntax are (with function-id):

	1. start
	2. stop
	3. set-auto-managed
	4. clear-auto-managed
	5. queue up
	6. queue down
	7. queue top
	8. queue bottom
	9. remove
	10. remove + data
	11. force recheck
	12. set-sequential-download
	13. clear-sequential-download

The arguments for these functions are (offset includes RPC header):

+----------+--------------------+-----------------------------------------+
| offset   | type               | name                                    |
+==========+====================+=========================================+
| 3        | uint16_t           | ``num-info-hashes``                     |
+----------+--------------------+-----------------------------------------+
| 5        | uint8_t[20]        | ``info-hash``                           |
+----------+--------------------+-----------------------------------------+
| 25       | uint8_t[20]        | additional info-hash (optional)         |
+----------+--------------------+-----------------------------------------+
| ...      | ...                | ...                                     |
+----------+--------------------+-----------------------------------------+

That is, each command can apply to any number of torrents. The 20 byte info-hash
field is repeated ``num-info-hashes`` times. The command is applied to each
torrent whose info hash is specified.

The return value for these commands are the number of torrents that were found
and had the command invoked on them.

+----------+--------------------+-----------------------------------------+
| offset   | type               | name                                    |
+==========+====================+=========================================+
| 4        | uint16_t           | ``num-success-torrents``                |
+----------+--------------------+-----------------------------------------+


list-settings
.............

function id 14.

This message returns all available settings as strings, as well as their
corresponding setting id and type.

This function does not take any arguments. The return value is:

+----------+--------------------+-----------------------------------------+
| offset   | type               | name                                    |
+==========+====================+=========================================+
| 4        | uint32_t           | ``num-string-settings``                 |
+----------+--------------------+-----------------------------------------+
| 8        | uint32_t           | ``num-int-settings``                    |
+----------+--------------------+-----------------------------------------+
| 12       | uint32_t           | ``num-bool-settings``                   |
+----------+--------------------+-----------------------------------------+
| 16       | uint8_t, uint8_t[] | ``setting-name``                        |
+----------+--------------------+-----------------------------------------+
| 17+ n    | uint16_t           | ``setting-id``                          |
+----------+--------------------+-----------------------------------------+

The last 2 fields are repeated ``num-stringsettings`` * ``num-int-settings``
* ``num-bool-settings``  times.

This list of name -> id pairs tells you all of the available settings
for the bittorrent client. Note that the length prefix for the settings name
string is 8 bits.

The ``num-string-settings`` entries are of *string* type, the following
``num-int-settings`` are of *int* type and the following ``num-bool-settings``
are of type *boolean*.

get-settings
............

function id 15.

The get-settings function can be used to query the settings values for one
or more settings.

+----------+--------------------+-----------------------------------------+
| offset   | type               | name                                    |
+==========+====================+=========================================+
| 3        | uint16_t           | ``num-settings-values``                 |
+----------+--------------------+-----------------------------------------+
| 7        | uint16_t           | ``settings-id``                         |
+----------+--------------------+-----------------------------------------+

The last field is repeated ``num-settings-values`` times.

+----------+---------------------+-----------------------------------------+
| offset   | type                | name                                    |
+==========+=====================+=========================================+
| 4        | uint16_t            | ``num-values``                          |
+----------+---------------------+-----------------------------------------+
| 6        | uint32_t *or*       | *value*. ``int`` values are encoded as  |
|          | uint16_t, uint8_t[] | uint32_t, ``string`` values are encoded |
|          | *or* uint8_t        | as a 16-bit length prefix followed by   |
|          |                     | the string, ``bool`` values are encoded |
|          |                     | as uint8_t as either 0 or 1.            |
+----------+---------------------+-----------------------------------------+

The last field is repeated ``num-values`` times. The settings are returned
in the same order as they are requested.

set-settings
............

function id 16.

This RPC changes one or more settings. Settings are identifid by their settings
ID and the type of the values must match the types specified by a call to
list-settings_.

+----------+---------------------+-----------------------------------------+
| offset   | type                | name                                    |
+==========+=====================+=========================================+
| 3        | uint16_t            | ``num-settings``                        |
+----------+---------------------+-----------------------------------------+
| 7        | uint16_t            | ``settings-id``                         |
+----------+---------------------+-----------------------------------------+
| 9        | uint32_t *or*       | *value*. ``int`` values are encoded as  |
|          | uint16_t, uint8_t[] | uint32_t, ``string`` values are encoded |
|          | *or* uint8_t        | as a 16-bit length prefix followed by   |
|          |                     | the string, ``bool`` values are encoded |
|          |                     | as uint8_t as either 0 or 1.            |
|          |                     | The type must match the settings ID.    |
+----------+---------------------+-----------------------------------------+

The last two fields are repeated ``num-settings`` times. Each value must have
the type corresponding to the type of the preceeding ``settings-id``, as returned
by list-settings_.

There is no return value for this function.

list-stats
..........

function id 17.

This function requests a list of the names of all stats counters, in the order
they are controlled by the bitmask in ``get-stats``.

The function does not have any arguments. The return value is a list of strings.

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 4        | uint16_t           | ``num-counters``                          |
+----------+--------------------+-------------------------------------------+
| 6        | uint16_t           | ``stats-id``                              |
+----------+--------------------+-------------------------------------------+
| 7        | uint8_t            | ``counter-type`` 0=counter, 1=gauge       |
+----------+--------------------+-------------------------------------------+
| 9        | uint8_t, uint8_t[] | ``counter-name``                          |
+----------+--------------------+-------------------------------------------+

The three last 3 fields are repeated ``num-counters`` times.

get-stats
.........

function id 18.

This function requests values for the stats metrics represented by the ``field-bitmask``.
The ``frame-number`` for stats is a different frame number than for torrent updates, so
keep those separate.

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 3        | uint32_t           | ``frame-number`` (timestamp)              |
+----------+--------------------+-------------------------------------------+
| 7        | uint16_t           | ``num-stats`` The number of stats-ids     |
|          |                    | we're interested in, to follow.           |
+----------+--------------------+-------------------------------------------+
| 9        | uint16_t           | ``stats-id``                              |
+----------+--------------------+-------------------------------------------+

The last field is repeated ``num-stats`` times.

The response is:

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 4        | uint32_t           | ``frame-number`` (timestamp)              |
+----------+--------------------+-------------------------------------------+
| 8        | uint16_t           | ``num-stats`` The number of updates to    |
|          |                    | to follow.                                |
+----------+--------------------+-------------------------------------------+
| 10       | uint16_t           | ``stats-id``                              |
+----------+--------------------+-------------------------------------------+
| 12       | uint64_t           | ``stats-value``                           |
+----------+--------------------+-------------------------------------------+

The last two fields are repeated the ``num-stats``  times.

get-file-updates
................

function id 19.

This function returns the status of the files of a torrent.

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 3        | uint8_t[20]        | ``info-hash`` of the torrent.             |
+----------+--------------------+-------------------------------------------+
| 23       | uint32_t           | ``frame-number`` (timestamp)              |
|          |                    | of last update for this torrent.          |
+----------+--------------------+-------------------------------------------+
| 27       | uint16_t           | ``field-mask`` bitmask selecting which    |
|          |                    | fields to include in each file update.    |
|          |                    | Bit positions match the field-id column   |
|          |                    | in the fields table below (bit 0 =        |
|          |                    | flags, …, bit 5 = open-mode). Bits for    |
|          |                    | ``priority`` (bit 4) and ``open-mode``    |
|          |                    | (bit 5) require extra server-side work;   |
|          |                    | omit them when not needed.                |
+----------+--------------------+-------------------------------------------+

The response is:

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 4        | uint32_t           | ``frame-number`` (timestamp)              |
|          |                    | for this update to this torrent.          |
+----------+--------------------+-------------------------------------------+
| 8        | uint32_t           | ``num-files`` the total number of files   |
|          |                    | in the torrent.                           |
+----------+--------------------+-------------------------------------------+
| 12       | uint8_t            | ``file-update-bitmask`` bitmask           |
|          |                    | indicating which ones of the next 8 files |
|          |                    | contain an update.                        |
+----------+--------------------+-------------------------------------------+
| 13       | ...                | file-update (see below). There is one     |
|          |                    | update for each set bit in the update     |
|          |                    | bitmask above.                            |
|          |                    |                                           |
|          |                    | the first and mandatory field in the      |
|          |                    | file-update is a 16 bit field-update-     |
|          |                    | bitmask. Each bit representing a field    |
|          |                    | for the update. See below.                |
+----------+--------------------+-------------------------------------------+

The ``file-update-bitmask`` along with the associated file-updates, are
repeated num-files / 8 times. Each representing 8 more files.

Each file-update has a similar format to the torrent updates. There is a
16 bit bitmask indicating which fields of the file has updates. Followed by
those fields.

The fields on files, in bitmask bit-order (LSB is bit 0), are:

+----------+---------------------+------------------------------------------+
| field-id | type                | name                                     |
+==========+=====================+==========================================+
| 0        | uint8_t             | ``flags`` bitmask with the following     |
|          |                     | bits:                                    |
|          |                     |                                          |
|          |                     |  | 0x001. pad-file                       |
|          |                     |  | 0x002. hidden-attribute               |
|          |                     |  | 0x004. executable-attribute           |
|          |                     |                                          |
+----------+---------------------+------------------------------------------+
| 1        | uint16_t, uint8_t[] | ``name``. This is a variable length      |
|          |                     | string with a 16 bit length prefix.      |
|          |                     | it is encoded as UTF-8.                  |
+----------+---------------------+------------------------------------------+
| 2        | uint64_t            | ``size`` (number of bytes)               |
+----------+---------------------+------------------------------------------+
| 3        | uint64_t            | ``downloaded`` (number of bytes)         |
+----------+---------------------+------------------------------------------+
| 4        | uint8_t             | ``priority`` (0-7)                       |
+----------+---------------------+------------------------------------------+
| 5        | uint8_t             | ``open-mode`` bitmask with the following |
|          |                     | bits:                                    |
|          |                     |                                          |
|          |                     |  | 0x01. write-only                      |
|          |                     |  | 0x02. read-write                      |
|          |                     |  | 0x04. sparse                          |
|          |                     |  | 0x08. no-atime                        |
|          |                     |  | 0x10. random-access                   |
|          |                     |  | 0x20. -- unused --                    |
|          |                     |  | 0x40. memory mapped                   |
|          |                     |                                          |
+----------+---------------------+------------------------------------------+

add-torrent
...........

function id 20.

This function adds a torrent from a magnet link, along with the new
torrent's initial ``add_torrent_params``. At present those parameters
are a flag bitmask and a tag bitfield; further parameters may be added
in future revisions of the protocol.

+----------+---------------------+------------------------------------------+
| offset   | type                | name                                     |
+==========+=====================+==========================================+
| 3        | uint16_t, uint8_t[] | Magnet link to add. 16 bit string length |
|          |                     | followed by the string itself.           |
+----------+---------------------+------------------------------------------+
| ...      | uint32_t            | ``flags`` configuration bitmask applied  |
|          |                     | to the torrent at creation. Bits 0-15    |
|          |                     | share positions with the ``flags`` field |
|          |                     | of `get-torrent-updates`_ (field 0);     |
|          |                     | bits 17 and above are add-torrent-only:  |
|          |                     |                                          |
|          |                     |  | 0x000001. stopped                     |
|          |                     |  | 0x000002. auto-managed                |
|          |                     |  | 0x000004. sequential-download         |
|          |                     |  | 0x000100. seed-mode                   |
|          |                     |  | 0x000200. upload-mode                 |
|          |                     |  | 0x000400. share-mode                  |
|          |                     |  | 0x000800. super-seeding               |
|          |                     |  | 0x010000. disable-pex                 |
|          |                     |  | 0x020000. disable-dht                 |
|          |                     |  | 0x040000. disable-lsd                 |
|          |                     |  | 0x080000. disable-v1-hashes           |
|          |                     |  | 0x100000. i2p-torrent                 |
|          |                     |  | 0x200000. default-dont-download       |
|          |                     |  | 0x400000. metadata-only               |
|          |                     |                                          |
|          |                     | Bits not listed are reserved; servers    |
|          |                     | may reject calls with unknown bits set.  |
+----------+---------------------+------------------------------------------+
| ...      | uint64_t            | ``tag``. Initial value of the torrent's  |
|          |                     | tag bitfield -- the same field reported  |
|          |                     | as field 23 of                           |
|          |                     | `get-torrent-updates`_ and modified by   |
|          |                     | the `set-tag`_ RPC.                      |
+----------+---------------------+------------------------------------------+

The ``flags`` bit values above use the same bit positions as the
``flags`` field returned by `get-torrent-updates`_ (field 0), so a
client can pass a saved ``flags`` value directly to ``add-torrent``
without re-mapping bits. Read-only status bits from ``get-torrent-updates``
(``seeding``, ``finished``, ``has-metadata``, etc.) are absent because
they describe observed state rather than initial configuration; they are
reserved and must be zero.

``stopped`` starts the torrent in the paused state. ``auto-managed``
lets the session scheduler start and stop the torrent automatically;
setting both ``stopped`` and ``auto-managed`` is valid and means "start
paused but become auto-managed". ``seed-mode`` skips hash-checking and
trusts that all pieces are already present. ``upload-mode`` suppresses
downloading (only uploads). ``share-mode`` enables the share-ratio
seeding mode. ``super-seeding`` enables super-seeding.

The add-torrent-only flags (bits 17+) have no corresponding observable
status in ``get-torrent-updates``. ``disable-pex``, ``disable-dht``,
and ``disable-lsd`` suppress the respective peer-discovery mechanisms
for this torrent only. ``disable-v1-hashes`` prevents the torrent from
announcing with v1 hashes. ``i2p-torrent`` restricts the torrent to the
I2P network. ``default-dont-download`` sets the initial file priority to
0 (skip) for all files instead of the usual default priority.
``metadata-only`` (libtorrent's ``stop_when_ready``) pauses the torrent
automatically once metadata has been fetched.

Unlike `set-tag`_, this call does not take a mask for ``tag``. The
torrent is being created, so there are no prior bits to preserve; every
bit of the torrent's tag is set directly from the supplied value. The
per-bit permission policy described under `set-tag`_ still applies:
bits the authenticated user is not permitted to write are silently
cleared from the supplied value before the tag is stored.

The standard response indicates whether adding the torrent was successful
or not. If the torrent already exists in the session, the call will fail.

get-peers-updates
.................

function id 21.

This function returns the status of the peers of a torrent.

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 3        | uint8_t[20]        | ``info-hash`` of the torrent.             |
+----------+--------------------+-------------------------------------------+
| 23       | uint32_t           | ``frame-number`` (timestamp)              |
|          |                    | of last update for this torrent.          |
+----------+--------------------+-------------------------------------------+
| 27       | uint64_t           | ``field-bitmask`` (only these fields are  |
|          |                    | returned)                                 |
+----------+--------------------+-------------------------------------------+

The response is:

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 4        | uint32_t           | ``frame-number`` (timestamp)              |
|          |                    | of this update for this torrent.          |
+----------+--------------------+-------------------------------------------+
| 8        | uint32_t           | ``num-updates`` the number of updates to  |
|          |                    | follow. New peers are included in the     |
|          |                    | update.                                   |
+----------+--------------------+-------------------------------------------+
| 12       | uint32_t           | ``num-removed`` the number of removed     |
|          |                    | peers since the last update.              |
+----------+--------------------+-------------------------------------------+
| 16       | *see below*        | peer info updates, one entry for each     |
|          |                    | num-updates. See the format for peer      |
|          |                    | updates below.                            |
+----------+--------------------+-------------------------------------------+

Each peer-update has a unique ``uint32_t`` value identifying the peer. This
is unique within the specific torrent. This is not necessarily based on the
peer ID or IP.

Followed by a ``uint64_t`` bitmask indicating which peer fields are included in
the update, followed by those fields.

The peer fields, in bitmask bit-order (LSB is bit 0), are:

+----------+---------------------+------------------------------------------+
| field-id | type                | name                                     |
+==========+=====================+==========================================+
| 0        | uint32_t            | ``flags`` bitmask with the following     |
|          |                     | bits:                                    |
|          |                     |                                          |
|          |                     |  | 0x000001. interesting                 |
|          |                     |  | 0x000002. choked                      |
|          |                     |  | 0x000004. remote interested in us     |
|          |                     |  | 0x000008. remote choked us            |
|          |                     |  | 0x000010. supports extensions         |
|          |                     |  | 0x000020. outgoing                    |
|          |                     |  | 0x000040. handshake                   |
|          |                     |  | 0x000080. connecting                  |
|          |                     |  | 0x000100. -- unused --                |
|          |                     |  | 0x000200. on parole                   |
|          |                     |  | 0x000400. seed                        |
|          |                     |  | 0x000800. optimistic unchoke          |
|          |                     |  | 0x001000. snubbed                     |
|          |                     |  | 0x002000. upload-only                 |
|          |                     |  | 0x004000. end-game mode               |
|          |                     |  | 0x008000. hole-punched                |
|          |                     |  | 0x010000. i2p tunnel                  |
|          |                     |  | 0x020000. uTP transport               |
|          |                     |  | 0x040000. SSL                         |
|          |                     |  | 0x080000. RC4 obfuscated              |
|          |                     |  | 0x100000. plaintext obfuscated        |
|          |                     |                                          |
+----------+---------------------+------------------------------------------+
| 1        | uint8_t             | ``source`` bitmask of sources we found   |
|          |                     | this peer at:                            |
|          |                     |                                          |
|          |                     |  | 0x01. tracker                         |
|          |                     |  | 0x02. DHT                             |
|          |                     |  | 0x04. peer exchange                   |
|          |                     |  | 0x08. local service discovery         |
|          |                     |  | 0x10. resume data                     |
|          |                     |  | 0x20. incoming                        |
|          |                     |                                          |
+----------+---------------------+------------------------------------------+
| 2        | uint8_t             | ``read-state`` bitmask of what the peer  |
|          |                     | is blocked on, for downloading:          |
|          |                     |                                          |
|          |                     |  | 0x01. idle (not downloading)          |
|          |                     |  | 0x02. bandwidth limit                 |
|          |                     |  | 0x04. network                         |
|          |                     |  | 0x08. -- unused --                    |
|          |                     |  | 0x10. disk                            |
|          |                     |                                          |
+----------+---------------------+------------------------------------------+
| 3        | uint8_t             | ``write-state`` bitmask of what the peer |
|          |                     | is blocked on, for uploading:            |
|          |                     |                                          |
|          |                     |  | 0x01. idle (not uploading)            |
|          |                     |  | 0x02. bandwidth limit                 |
|          |                     |  | 0x04. network                         |
|          |                     |  | 0x08. -- unused --                    |
|          |                     |  | 0x10. disk                            |
|          |                     |                                          |
+----------+---------------------+------------------------------------------+
| 4        | uint8_t, uint8_t[]  | ``client`` advertised name of remote     |
|          |                     | client software.                         |
+----------+---------------------+------------------------------------------+
| 5        | uint32_t            | ``num-pieces`` number of pieces this     |
|          |                     | peer has.                                |
+----------+---------------------+------------------------------------------+
| 6        | uint32_t            | ``pending-disk-bytes``                   |
+----------+---------------------+------------------------------------------+
| 7        | uint32_t            | ``pending-disk-read-bytes``              |
+----------+---------------------+------------------------------------------+
| 8        | uint32_t            | ``hashfails``, the number of failed      |
|          |                     | piece hashes this peer has been part of  |
+----------+---------------------+------------------------------------------+
| 9        | uint32_t            | ``down-rate`` in Bytes/s                 |
+----------+---------------------+------------------------------------------+
| 10       | uint32_t            | ``up-rate`` in Bytes/s                   |
+----------+---------------------+------------------------------------------+
| 11       | uint8_t[20]         | ``peer-id``                              |
+----------+---------------------+------------------------------------------+
| 12       | uint32_t            | ``download-queue`` length (in blocks)    |
+----------+---------------------+------------------------------------------+
| 13       | uint32_t            | ``upload-queue`` length (in blocks)      |
+----------+---------------------+------------------------------------------+
| 14       | uint32_t            | ``timed-out-reqs``                       |
+----------+---------------------+------------------------------------------+
| 15       | uint32_t            | ``progress`` in the range [0, 1000000]   |
+----------+---------------------+------------------------------------------+
| 16       | uint8_t, uint8_t[]  | ``endpoints`` the first byte indicates   |
|          |                     | the type of endpoint(s):                 |
|          |                     |                                          |
|          |                     | | 0. IPv4 (6 bytes local endpoint, 6     |
|          |                     | |    bytes remote endpoint)              |
|          |                     | | 1. IPv6 (18 bytes local endpoint, 18   |
|          |                     | |    bytes remote endpoint)              |
|          |                     | | 2. I2P (32 bytes endpoint)             |
|          |                     |                                          |
+----------+---------------------+------------------------------------------+
| 17       | uint32_t, uint8_t[] | ``pieces`` a bitmask of the pieces this  |
|          |                     | peer has. The first word is the number   |
|          |                     | of bytes following it. the bytes is a    |
|          |                     | bitmask where each bit represents a      |
|          |                     | piece in the torrent.                    |
+----------+---------------------+------------------------------------------+
| 18       | uint64_t            | ``total-download`` total payload bytes   |
|          |                     | downloaded from this peer.               |
+----------+---------------------+------------------------------------------+
| 19       | uint64_t            | ``total-upload`` total payload bytes     |
|          |                     | uploaded to this peer.                   |
+----------+---------------------+------------------------------------------+

If ``num-removed`` is zero, it means that no existing peer connection was lost
since the last update.

If ``num-removed`` is 0xffffffff, it means that all peers that were not
included in the updates disconnected.

Otherwise, a list follows of ``num-removed`` peer identifiers that disconnected
since the last update. A peer identifier is ``uint32_t``.

get-piece-updates
.................

function id 22.

This function returns the state of the currently downloading pieces for a torrent.

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 3        | uint8_t[20]        | ``info-hash`` of the torrent.             |
+----------+--------------------+-------------------------------------------+
| 23       | uint32_t           | ``frame-number`` (timestamp)              |
|          |                    | of last update for this torrent.          |
+----------+--------------------+-------------------------------------------+

The response is:

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 4        | uint32_t           | ``frame-number`` (timestamp)              |
|          |                    | of this update for this torrent.          |
+----------+--------------------+-------------------------------------------+
| 8        | uint16_t           | ``num-updates`` the number of full piece  |
|          |                    | updates to follow. New pieces are         |
|          |                    | included in the update.                   |
+----------+--------------------+-------------------------------------------+
| 10       | uint16_t           | ``num-block-updates`` the number of       |
|          |                    | updates to single blocks, since the last  |
|          |                    | update.                                   |
+----------+--------------------+-------------------------------------------+
| 12       | uint16_t           | ``num-removed`` the number of removed     |
|          |                    | pieces since the last update. If this     |
|          |                    | is 0xffff, any piece not mentioned in one |
|          |                    | of the update lists were removed.         |
+----------+--------------------+-------------------------------------------+
| 14       | *see below*        | piece update lists: full piece updates,   |
|          |                    | block updates, removed pieces.            |
+----------+--------------------+-------------------------------------------+

What follows are 3 lists. A list of full piece updates, a list of individual
block updates and pieces removed from the download list.

First the list of full piece updates follow. It has ``num-updates`` entries.

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 0        | uint32_t           | ``piece-index`` the piece index           |
+----------+--------------------+-------------------------------------------+
| 4        | uint16_t           | ``num-blocks`` the number of blocks in    |
|          |                    | this piece.                               |
+----------+--------------------+-------------------------------------------+
| 6        | uint8_t[]          | ``block-state`` this is repeated for each |
|          |                    | block in the piece. Each byte has this    |
|          |                    | meaning:                                  |
|          |                    |                                           |
|          |                    | | 0. not requested                        |
|          |                    | | 1. requested                            |
|          |                    | | 2. writing (or in disk cache)           |
|          |                    | | 3. written to disk                      |
|          |                    |                                           |
+----------+--------------------+-------------------------------------------+

After the list of full piece updates come the *block* updates. If a piece has
few changes since the last update, only the blocks that have changed are
updated.

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 0        | uint32_t           | ``piece-index`` the piece index           |
+----------+--------------------+-------------------------------------------+
| 4        | uint16_t           | ``block-index`` the block index           |
+----------+--------------------+-------------------------------------------+
| 6        | uint8_t            | ``block-state`` the new state of the      |
|          |                    | specified block.                          |
|          |                    |                                           |
|          |                    | | 0. not requested                        |
|          |                    | | 1. requested                            |
|          |                    | | 2. writing (or in disk cache)           |
|          |                    | | 3. written to disk                      |
|          |                    |                                           |
+----------+--------------------+-------------------------------------------+

The list of removed pieces has ``num-removed`` entries. A length of ``0xffff``
is a special value to mean the update is a complete snapshot of the pieces. The
removed pieces list is empty in this case.

This list just has one ``uint32_t`` per entry, the index of the piece that's
being removed.

set-file-priority
.................

function id 23.

This function sets the download priority of one or more files in a torrent.

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 3        | uint8_t[20]        | ``info-hash`` of the torrent.             |
+----------+--------------------+-------------------------------------------+
| 23       | uint32_t           | ``num-updates`` the number of             |
|          |                    | file-priority updates that follow.        |
+----------+--------------------+-------------------------------------------+
| 27       | *see below*        | list of file-priority updates.            |
+----------+--------------------+-------------------------------------------+

The list is ``num-updates`` entries long. Each entry in the list has the
following format:

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 0        | uint32_t           | ``file-index`` the index of the file      |
|          |                    | within the torrent.                       |
+----------+--------------------+-------------------------------------------+
| 4        | uint8_t            | ``priority`` the new download priority    |
|          |                    | for the file. 0 means "do not download",  |
|          |                    | 7 is the highest priority and 4 is        |
|          |                    | default.                                  |
|          |                    |                                           |
+----------+--------------------+-------------------------------------------+

get-tracker-updates
...................

function id 24.

This function returns the status of the trackers of a torrent. Internally
libtorrent represents trackers as a three-level hierarchy
(``announce_entry`` → ``announce_endpoint`` → ``announce_infohash``), but
this protocol flattens that hierarchy into a single list of **tracker
records**. Each tracker record corresponds to one leaf of that tree: a unique
combination of tracker URL, local network interface, and hash protocol (v1 or
v2). The server assigns each such leaf a stable ``tracker-id`` (``uint16_t``)
that the client uses to match delta updates to known tracker state.

Only changed fields are sent per update. A ``uint16_t`` bitmask in each
update record indicates which fields are present. Passing a ``frame-number``
of ``0`` requests a full snapshot.

The call is (offset includes RPC call header):

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 3        | uint8_t[20]        | ``info-hash`` of the torrent.             |
+----------+--------------------+-------------------------------------------+
| 23       | uint32_t           | ``frame-number`` timestamp of the last    |
|          |                    | update received by the caller. Pass 0     |
|          |                    | to request a full snapshot.               |
+----------+--------------------+-------------------------------------------+

The response is (offset includes RPC response header):

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 4        | uint32_t           | ``frame-number`` timestamp of this        |
|          |                    | response.                                 |
+----------+--------------------+-------------------------------------------+
| 8        | uint32_t           | ``timestamp`` current wall-clock time on  |
|          |                    | the server, in seconds. Used as the       |
|          |                    | reference clock for ``next-announce``     |
|          |                    | values in this response. This clock has   |
|          |                    | an unspecified epoch.                     |
+----------+--------------------+-------------------------------------------+
| 12       | uint16_t           | ``num-updates`` number of tracker update  |
|          |                    | records to follow.                        |
+----------+--------------------+-------------------------------------------+
| 14       | uint16_t           | ``num-removed`` number of removed         |
|          |                    | ``tracker-id`` values that follow the     |
|          |                    | update records. ``0xffff`` means this     |
|          |                    | response is a full snapshot; any          |
|          |                    | ``tracker-id`` not present in the update  |
|          |                    | list no longer exists, and no removed-id  |
|          |                    | list follows.                             |
+----------+--------------------+-------------------------------------------+
| 16       | *see below*        | tracker updates                           |
+----------+--------------------+-------------------------------------------+

Each **tracker update record** has the following format:

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 0        | uint16_t           | ``tracker-id`` server-assigned identifier |
|          |                    | for this tracker record. Stable across    |
|          |                    | delta updates.                            |
+----------+--------------------+-------------------------------------------+
| 2        | uint16_t           | ``field-bitmask`` indicates which fields  |
|          |                    | follow. Bit positions match the field-id  |
|          |                    | table below (bit 0 = LSB).                |
+----------+--------------------+-------------------------------------------+
| 4        | *see below*        | the fields indicated by                   |
|          |                    | ``field-bitmask``, in field-id order.     |
+----------+--------------------+-------------------------------------------+

The tracker fields, in bitmask bit-order (LSB is bit 0), are:

+----------+---------------------+------------------------------------------+
| field-id | type                | name                                     |
+==========+=====================+==========================================+
| 0        | uint16_t, uint8_t[] | ``url`` the tracker announce URL.        |
|          |                     | 16-bit length prefix, UTF-8 encoded.     |
|          |                     | This field is only sent when the tracker |
|          |                     | first appears or its URL changes.        |
+----------+---------------------+------------------------------------------+
| 1        | uint8_t             | ``tier`` the tracker tier. Trackers in   |
|          |                     | lower tiers are contacted first.         |
+----------+---------------------+------------------------------------------+
| 2        | uint8_t             | ``source`` bitmask indicating how this   |
|          |                     | tracker was added:                       |
|          |                     |                                          |
|          |                     |  | 0x01. torrent file                    |
|          |                     |  | 0x02. client (added manually)         |
|          |                     |  | 0x04. magnet link                     |
|          |                     |  | 0x08. tracker exchange (tex)          |
|          |                     |                                          |
+----------+---------------------+------------------------------------------+
| 3        | int32_t             | ``complete`` the number of seeders as    |
|          |                     | reported by the tracker. ``-1`` means    |
|          |                     | the tracker did not report this value.   |
+----------+---------------------+------------------------------------------+
| 4        | int32_t             | ``incomplete`` the number of leechers    |
|          |                     | as reported by the tracker. ``-1``       |
|          |                     | means unknown.                           |
+----------+---------------------+------------------------------------------+
| 5        | int32_t             | ``downloaded`` the number of times the   |
|          |                     | torrent has been downloaded as reported  |
|          |                     | by the tracker (scrape). ``-1`` means    |
|          |                     | unknown.                                 |
+----------+---------------------+------------------------------------------+
| 6        | int32_t             | ``next-announce`` absolute timestamp (in |
|          |                     | seconds) of the next scheduled announce, |
|          |                     | in the same clock as ``timestamp`` at    |
|          |                     | the top of the response. May be in the   |
|          |                     | past if an announce is overdue. Only     |
|          |                     | sent when the scheduled time changes,    |
|          |                     | not on every poll.                       |
+----------+---------------------+------------------------------------------+
| 7        | int32_t             | ``min-announce`` absolute timestamp (in  |
|          |                     | seconds) of the earliest time at which   |
|          |                     | the next announce is permitted, in the   |
|          |                     | same clock as ``timestamp`` at the top   |
|          |                     | of the response. Only sent when the      |
|          |                     | value changes, not on every poll.        |
+----------+---------------------+------------------------------------------+
| 8        | uint8_t, uint8_t[]  | ``last-error`` error message from the    |
|          |                     | most recent failed announce. 8-bit       |
|          |                     | length prefix, UTF-8 encoded, truncated  |
|          |                     | to 255 bytes. Empty string if the last   |
|          |                     | announce succeeded.                      |
+----------+---------------------+------------------------------------------+
| 9        | uint8_t, uint8_t[]  | ``message`` message returned by the      |
|          |                     | tracker in the last announce response.   |
|          |                     | 8-bit length prefix, UTF-8 encoded,      |
|          |                     | truncated to 255 bytes.                  |
+----------+---------------------+------------------------------------------+
| 10       | uint8_t             | ``flags`` bitmask:                       |
|          |                     |                                          |
|          |                     |  | 0x01. updating (announce in progress) |
|          |                     |  | 0x02. complete-sent (stop event sent  |
|          |                     |  |       on seeding completion)          |
|          |                     |  | 0x04. verified (tracker has           |
|          |                     |  |       successfully responded)         |
|          |                     |  | 0x08. enabled (endpoint is active)    |
|          |                     |  | 0x10. v2 torrent (this flag being     |
|          |                     |  |       clear means a v1 torrent)       |
|          |                     |                                          |
+----------+---------------------+------------------------------------------+
| 11       | uint8_t,            | ``local-endpoint`` the local network     |
|          | uint8_t[], uint16_t | interface used for this tracker. The     |
|          |                     | first byte is the address type:          |
|          |                     |                                          |
|          |                     |  | 0. IPv4 (4 address bytes + 2 port     |
|          |                     |  |    bytes)                             |
|          |                     |  | 1. IPv6 (16 address bytes + 2 port    |
|          |                     |  |    bytes)                             |
|          |                     |                                          |
+----------+---------------------+------------------------------------------+

get-piece-states
................

function id 25.

This function returns which pieces of a torrent have been fully downloaded.
It is distinct from `get-piece-updates`_, which tracks the block-level state
of currently downloading pieces; ``get-piece-states`` tracks the completed
("have") set across all pieces in the torrent.

The response is either a *snapshot* (a complete bitfield of which pieces we
have) or a *delta* (a list of piece indices that have been newly completed
since the supplied frame number). The server picks the form: it sends a
delta whenever it can, and falls back to a snapshot when a delta would be
incorrect or unavailable.

The frame-number is **per-torrent**, just like for `get-piece-updates`_.
A client tracking multiple torrents must keep a separate frame counter
for each info-hash; counters from one torrent are not comparable with
counters from another, and a given torrent's counter may even reset to 0
(for example, if the torrent is removed and re-added, or after a server
restart).

The call is (offset includes RPC call header):

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 3        | uint8_t[20]        | ``info-hash`` of the torrent.             |
+----------+--------------------+-------------------------------------------+
| 23       | uint32_t           | ``frame-number`` timestamp of the last    |
|          |                    | piece-states update received by the       |
|          |                    | caller for this torrent. Pass 0 to        |
|          |                    | request a full snapshot.                  |
+----------+--------------------+-------------------------------------------+

The response is (offset includes RPC response header):

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 4        | uint32_t           | ``frame-number`` timestamp of this        |
|          |                    | response. The client should pass this     |
|          |                    | value as ``frame-number`` in the next     |
|          |                    | call for this torrent.                    |
+----------+--------------------+-------------------------------------------+
| 8        | uint8_t            | ``response-type``                         |
|          |                    |                                           |
|          |                    |  | 0. delta (list of newly completed      |
|          |                    |  |    pieces follows)                     |
|          |                    |  | 1. snapshot (full bitfield follows)    |
|          |                    |                                           |
+----------+--------------------+-------------------------------------------+
| 9        | *see below*        | payload, format depends on                |
|          |                    | ``response-type``.                        |
+----------+--------------------+-------------------------------------------+

When ``response-type`` is **1 (snapshot)**, the payload is:

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 9        | uint32_t           | ``num-pieces`` total number of pieces in  |
|          |                    | the torrent. May be 0 if the torrent's    |
|          |                    | metadata has not yet been retrieved (for  |
|          |                    | example, a magnet link that has not       |
|          |                    | resolved yet); in that case the bitfield  |
|          |                    | below is empty.                           |
+----------+--------------------+-------------------------------------------+
| 13       | uint8_t[]          | ``bitfield`` ``ceil(num-pieces / 8)``     |
|          |                    | bytes. Bit ordering: piece index ``i``    |
|          |                    | is at bit ``7 - (i mod 8)`` of byte       |
|          |                    | ``i / 8``. That is, piece 0 is the most-  |
|          |                    | significant bit of byte 0, piece 7 is     |
|          |                    | the least-significant bit of byte 0,      |
|          |                    | piece 8 is the most-significant bit of    |
|          |                    | byte 1, and so on. This matches the       |
|          |                    | bit ordering of the BitTorrent wire       |
|          |                    | bitfield message. A 1 bit means we have   |
|          |                    | the corresponding piece (downloaded and   |
|          |                    | hash-checked); a 0 bit means we don't.    |
|          |                    | If ``num-pieces`` is not a multiple of 8, |
|          |                    | the unused trailing bits in the last      |
|          |                    | byte must be 0.                           |
+----------+--------------------+-------------------------------------------+

A snapshot replaces any prior state the client has for this torrent. The
client should advance its per-torrent frame counter to the response's
``frame-number`` and apply subsequent deltas relative to it.

When ``response-type`` is **0 (delta)**, the payload is:

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 9        | uint32_t           | ``num-added`` the number of piece         |
|          |                    | indices to follow. May be 0 if no         |
|          |                    | pieces have been completed since the      |
|          |                    | requested frame.                          |
+----------+--------------------+-------------------------------------------+
| 13       | uint32_t           | ``piece-index`` index of a piece that     |
|          |                    | has been completed since the requested    |
|          |                    | frame. Repeated ``num-added`` times.      |
+----------+--------------------+-------------------------------------------+

A delta is purely additive: it lists pieces that have transitioned from
*not-have* to *have* since the requested frame. To apply it, the client
sets the corresponding bits in its locally cached bitfield.

The server will choose to respond with a snapshot (``response-type`` 1)
in any of the following cases:

  * The caller passed ``frame-number`` of 0.
  * The supplied ``frame-number`` is not known to the server. This
    happens when the value is older than the server's history window,
    or larger than the server's current frame number for this torrent
    (which can occur after a server restart, or after the torrent has
    been removed and re-added, both of which can reset the frame
    counter).
  * One or more pieces have *regressed* from *have* to *not-have* since
    the requested frame. This is uncommon but can happen, for example,
    when the user issues a force-recheck and some piece hashes fail.
    Since the delta format cannot represent a piece becoming
    not-downloaded, the server sends a fresh snapshot in this case.

If the torrent referred to by ``info-hash`` does not exist (for example
because it was just removed), the server responds with error code 6
(resource not found) and no payload.

set-tag
.......

function id 26.

This function sets the application-defined ``tag`` bitfield on one or more
torrents. The tag is the same 64-bit value that is reported as field 23 of
`get-torrent-updates`_; its individual bits are opaque to the protocol and
their meaning is chosen by the client application.

Each entry specifies the torrent (by info-hash), a ``value`` and a ``mask``.
The mask selects which bits of the tag the entry modifies. The resulting
tag is computed as::

   new_tag = (old_tag & ~mask) | (value & mask)

Bits outside the mask are preserved. This lets two clients (or two browser
tabs of the same client) each toggle their own bits without a read-modify-
write race over the others.

The call arguments (offset includes RPC call header):

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 3        | uint16_t           | ``num-tags`` the number of entries to     |
|          |                    | follow.                                   |
+----------+--------------------+-------------------------------------------+
| 5        | uint8_t[20]        | ``info-hash`` of the torrent.             |
+----------+--------------------+-------------------------------------------+
| 25       | uint64_t           | ``value`` the desired bit values.         |
+----------+--------------------+-------------------------------------------+
| 33       | uint64_t           | ``mask`` selects which bits to modify.    |
|          |                    | ``mask = 0`` is a deliberate no-op; ``mask|
|          |                    | = 0xffffffffffffffff`` overwrites the tag.|
+----------+--------------------+-------------------------------------------+

The ``info-hash``, ``value``, ``mask`` triple is repeated ``num-tags``
times (stride 36 bytes per entry).

The server enforces a per-bit permission mask: it ANDs each entry's
``mask`` with the bits the authenticated user is permitted to write.
Entries whose mask survives the AND apply the resulting effective mask;
entries whose mask is wiped out are silently skipped. The whole call
returns error code 8 (permission denied) only when every entry that
actually wanted to write (``mask != 0``) had all of its bits denied.
Pure-no-op calls (``mask == 0`` everywhere, or empty ``num-tags == 0``)
succeed regardless of permissions.

The return value is:

+----------+--------------------+-----------------------------------------+
| offset   | type               | name                                    |
+==========+====================+=========================================+
| 4        | uint16_t           | ``num-success`` the number of entries   |
|          |                    | whose tag value actually changed.       |
+----------+--------------------+-----------------------------------------+

Entries that did not change the tag (unknown info-hash, ``mask`` fully
denied, requested bits already at the requested values) are not counted
in ``num-success``. A successful tag change is reflected in the next
`get-torrent-updates`_ response as a delta on field 23.

Tag values are persisted across server restarts alongside each torrent's
add-torrent parameters.

.. raw:: pdf

   PageBreak oneColumn

Appendix A
==========

Function IDs

+-----+---------------------------+-----------------------------------------+
| ID  | Function name             | Arguments                               |
+=====+===========================+=========================================+
|   0 | get-torrent-updates       | last-frame-number (uint32_t)            |
|     |                           | bitmask indicating which fields to      |
|     |                           | return (uint64_t)                       |
+-----+---------------------------+-----------------------------------------+
|   1 | start                     | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|   2 | stop                      | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|   3 | set-auto-managed          | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|   4 | clear-auto-managed        | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|   5 | queue-up                  | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|   6 | queue-down                | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|   7 | queue-top                 | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|   8 | queue-bottom              | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|   9 | remove                    | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|  10 | remove_and_data           | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|  11 | force-recheck             | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|  12 | set-sequential-download   | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|  13 | clear-sequential-download | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|  14 | list-settings             |                                         |
+-----+---------------------------+-----------------------------------------+
|  15 | set-settings              | setting-id, type, value, ...            |
+-----+---------------------------+-----------------------------------------+
|  16 | get-settings              | setting-id, ...                         |
+-----+---------------------------+-----------------------------------------+
|  17 | list-stats                |                                         |
+-----+---------------------------+-----------------------------------------+
|  18 | get-stats                 | frame, num-stats, stats-id, ...         |
+-----+---------------------------+-----------------------------------------+
|  19 | get-file-updates          | info-hash, frame-number, field-mask     |
+-----+---------------------------+-----------------------------------------+
|  20 | add-torrent               | magnet-link, flags+tag                  |
+-----+---------------------------+-----------------------------------------+
|  21 | get-peers-updates         | info-hash, frame-number, bitmask        |
|     |                           | indicating which fields to return       |
|     |                           | (uint64_t)                              |
+-----+---------------------------+-----------------------------------------+
|  22 | get-piece-updates         | info-hash, frame-number                 |
+-----+---------------------------+-----------------------------------------+
|  23 | set-file-priority         | info-hash, num-updates,                 |
|     |                           | file-index (uint32_t),                  |
|     |                           | priority (uint8_t), ...                 |
+-----+---------------------------+-----------------------------------------+
|  24 | get-tracker-updates       | info-hash, frame-number (uint32_t)      |
+-----+---------------------------+-----------------------------------------+
|  25 | get-piece-states          | info-hash, frame-number (uint32_t)      |
+-----+---------------------------+-----------------------------------------+
|  26 | set-tag                   | num-tags, info-hash, value (uint64_t),  |
|     |                           | mask (uint64_t), ...                    |
+-----+---------------------------+-----------------------------------------+

.. raw:: pdf

   PageBreak oneColumn

Appendix B
==========

Error codes used in RPC response messages.

+------+------------------------------------------------+
| code | meaning                                        |
+======+================================================+
|    0 | no error                                       |
+------+------------------------------------------------+
|    1 | no such function                               |
+------+------------------------------------------------+
|    2 | invalid number of arguments for function       |
+------+------------------------------------------------+
|    3 | invalid argument type for function             |
+------+------------------------------------------------+
|    4 | invalid argument (correct type, but outside    |
|      | of valid domain)                               |
+------+------------------------------------------------+
|    5 | truncated request. The request message was     |
|      | truncated.                                     |
+------+------------------------------------------------+
|    6 | resource not found. e.g. torrent may have been |
|      | removed.                                       |
+------+------------------------------------------------+
|    7 | parse error. An argument failed to be parsed   |
+------+------------------------------------------------+
|    8 | permission denied. The user does not have      |
|      | credentials to perform the operation.          |
+------+------------------------------------------------+
|    9 | operation failed to complete successfully.     |
+------+------------------------------------------------+

