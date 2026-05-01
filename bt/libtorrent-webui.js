"use strict";

(function () {
  var utf8_decoder = new TextDecoder("utf-8");

  // read a string with a 16 bit length prefix
  function read_string16(view, offset) {
    var len = view.getUint16(offset);
    return [
      utf8_decoder.decode(
        new Uint8Array(view.buffer, view.byteOffset + offset + 2, len),
      ),
      len,
    ];
  }

  function read_string8(view, offset) {
    var len = view.getUint8(offset);
    return [
      utf8_decoder.decode(
        new Uint8Array(view.buffer, view.byteOffset + offset + 1, len),
      ),
      len,
    ];
  }

  function read_infohash(view, offset) {
    var ret = "";
    for (var j = 0; j < 20; ++j) {
      var b = view.getUint8(offset + j);
      if (b < 16) ret += "0";
      ret += b.toString(16);
    }
    return ret;
  }

  // read a 64 bit value
  function read_uint64(view, offset) {
    var high = view.getUint32(offset);
    offset += 4;
    var low = view.getUint32(offset);
    offset += 4;
    return high * 4294967296 + low;
  }

  function _check_error(e, callback) {
    if (e == 0) return false;

    var error = "unknown error";
    switch (e) {
      case 1:
        error = "no such function";
        break;
      case 2:
        error = "invalid number of arguments";
        break;
      case 3:
        error = "invalid argument type";
        break;
      case 4:
        error = "invalid argument";
        break;
      case 5:
        error = "truncated message";
        break;
      case 6:
        error = "resource not found";
        break;
    }

    console.log("ERROR: " + error);
    if (typeof callback !== "undefined") callback(error);
    return true;
  }

  var libtorrent_connection = function (url, callback) {
    var self = this;

    this._socket = new WebSocket(url);
    this._socket.onopen = function () {
      callback("OK");
    };
    this._socket.onerror = function (ev) {
      callback(ev.data);
    };
    this._socket.onmessage = function (ev) {
      var view = new DataView(ev.data);
      var fun = view.getUint8(0);
      var tid = view.getUint16(1);

      if (fun >= 128) {
        var e = view.getUint8(3);
        fun &= 0x7f;
        //			console.log('RESPONSE: fun: ' + fun + ' tid: ' + tid + ' error: ' + e);

        if (!self._transactions.hasOwnProperty(tid)) return;

        var handler = self._transactions[tid];
        delete self._transactions[tid];

        // this handler will deal with parsing out the remaining
        // return value and pass it on to the user supplied
        // callback function
        handler(view, fun, e);
      } else {
        // This is a function call
      }
    };
    this._socket.binaryType = "arraybuffer";
    this._frame = 0;
    this._stats_frame = 0;
    this._transactions = {};
    this._tid = 0;
  };

  libtorrent_connection.prototype["list_settings"] = function (callback) {
    if (this._socket.readyState != WebSocket.OPEN) {
      window.setTimeout(function () {
        callback("socket closed");
      }, 0);
      return;
    }

    var tid = this._tid++;
    if (this._tid > 65535) this._tid = 0;

    // this is the handler of the response for this call. It first
    // parses out the return value, the passes it on to the user
    // supplied callback.
    var self = this;
    this._transactions[tid] = function (view, fun, e) {
      if (_check_error(e, callback)) return;
      var ret = [];
      var num_strings = view.getUint32(4);
      var num_ints = view.getUint32(8);
      var num_bools = view.getUint32(12);

      // this is a local copy of the settings-id -> type map
      // the types are encoded as 0 = string, 1 = int, 2 = bool.
      self._settings = {};

      var offset = 16;
      for (var i = 0; i < num_strings + num_ints + num_bools; ++i) {
        var [name, len] = read_string8(view, offset);
        offset += 1 + len;
        var code = view.getUint16(offset);
        offset += 2;
        var type;
        if (i >= num_strings + num_ints) {
          type = "bool";
          self._settings[code] = 2;
        } else if (i >= num_strings) {
          type = "int";
          self._settings[code] = 1;
        } else {
          type = "string";
          self._settings[code] = 0;
        }

        ret.push({ name: name, id: code, type: type });
      }

      if (typeof callback !== "undefined") callback(ret);
    };

    var call = new ArrayBuffer(3);
    var view = new DataView(call);
    // function 14
    view.setUint8(0, 14);
    // transaction-id
    view.setUint16(1, tid);

    //	console.log('CALL list_settings() tid = ' + tid);
    this._socket.send(call);
  };

  libtorrent_connection.prototype["get_settings"] = function (
    settings,
    callback,
  ) {
    // TODO: factor out this RPC boiler plate
    if (this._socket.readyState != WebSocket.OPEN) {
      window.setTimeout(function () {
        callback("socket closed");
      }, 0);
      return;
    }

    if (typeof this._settings === "undefined") {
      window.setTimeout(function () {
        callback("must call list_settings first");
      }, 0);
      return;
    }

    var tid = this._tid++;
    if (this._tid > 65535) this._tid = 0;

    // this is the handler of the response for this call. It first
    // parses out the return value, the passes it on to the user
    // supplied callback.
    var self = this;
    this._transactions[tid] = function (view, fun, e) {
      if (_check_error(e, callback)) return;
      var num_settings = view.getUint16(4);
      var offset = 6;

      var ret = [];

      if (settings.length != num_settings) {
        callback("get_settings returned invalid number of items");
        return;
      }
      for (var i = 0; i < num_settings; ++i) {
        var type = self._settings[settings[i]];
        if (typeof type !== "number" || type < 0 || type > 2) {
          if (typeof callback !== "undefined")
            callback("invalid setting ID (" + settings[i] + ")");
          return;
        }
        switch (type) {
          case 0: // string
            var [n, len] = read_string16(view, offset);
            ret.push(n);
            offset += 2 + len;
            break;
          case 1: // int
            ret.push(view.getUint32(offset));
            offset += 4;
            break;
          case 2: // bool
            ret.push(view.getUint8(offset) ? true : false);
            offset += 1;
            break;
        }
      }
      if (typeof callback !== "undefined") callback(ret);
    };

    var call = new ArrayBuffer(5 + settings.length * 2);
    var view = new DataView(call);
    // function 15
    view.setUint8(0, 15);
    // transaction-id
    view.setUint16(1, tid);
    // num settings
    view.setUint16(3, settings.length);

    var offset = 5;
    for (var i = 0; i < settings.length; ++i) {
      view.setUint16(offset, settings[i]);
      offset += 2;
    }

    //	console.log('CALL get_settings( num: ' + settings.length + ' ) tid = ' + tid);
    this._socket.send(call);
  };

  // settings is an object mapping settings-id -> value
  libtorrent_connection.prototype["set_settings"] = function (
    settings,
    callback,
  ) {
    // TODO: factor out this RPC boiler plate
    if (this._socket.readyState != WebSocket.OPEN) {
      window.setTimeout(function () {
        callback("socket closed");
      }, 0);
      return;
    }

    var tid = this._tid++;
    if (this._tid > 65535) this._tid = 0;

    // this is the handler of the response for this call. It first
    // parses out the return value, the passes it on to the user
    // supplied callback.
    this._transactions[tid] = function (view, fun, e) {
      if (_check_error(e, callback)) return;
      if (typeof callback !== "undefined") callback(null);
    };

    // since ArrayBuffers can't be resized, we need to
    // figure out how big the buffer will need to be for this
    // RPC first, before allocating the buffer for it.
    var size = 6; // RPC header + counter
    for (var s in settings) {
      size += 2;
      var type = this._settings[s];
      switch (type) {
        // string
        case 0:
          size += 2 + settings[s].length;
          break;
        // int
        case 1:
          size += 4;
          break;
        // bool
        case 2:
          ++size;
          break;
      }
    }

    var call = new ArrayBuffer(size);
    var view = new DataView(call);

    // function 16
    view.setUint8(0, 16);
    // transaction-id
    view.setUint16(1, tid);
    // num settings
    view.setUint16(3, Object.keys(settings).length);

    var offset = 5;
    for (var s in settings) {
      view.setUint16(offset, s);
      offset += 2;
      var type = this._settings[s];
      switch (type) {
        case 0:
          var str = settings[s];
          view.setUint16(offset, str.length);
          offset += 2;
          for (var i = 0; i < str.length; ++i) {
            view.setUint8(offset, str.charCodeAt(i));
            ++offset;
          }
          break;
        case 1:
          view.setUint32(offset, settings[s]);
          offset += 4;
          break;
        case 2:
          view.setUint8(offset, settings[s]);
          ++offset;
          break;
        default:
          if (typeof callback !== "undefined") callback("invalid setting-id");
          return;
      }
    }

    //	console.log('CALL set_settings( settings: ' + Object.keys(settings).length + ') tid = ' + tid);
    this._socket.send(call);
  };

  libtorrent_connection.prototype["get_updates"] = function (mask, callback) {
    if (this._socket.readyState != WebSocket.OPEN) {
      window.setTimeout(function () {
        callback("socket closed");
      }, 0);
      return;
    }

    var tid = this._tid++;
    if (this._tid > 65535) this._tid = 0;

    // this is the handler of the response for this call. It first
    // parses out the return value, the passes it on to the user
    // supplied callback.
    var self = this;
    this._transactions[tid] = function (view, fun, e) {
      if (_check_error(e, callback)) return;

      self._frame = view.getUint32(4);
      var num_torrents = view.getUint32(8);
      var num_removed_torrents = view.getUint32(12);
      //		console.log('frame: ' + self._frame + ' num-torrents: ' + num_torrents + ' num-removed-torrents: ' + num_removed_torrents);
      var ret = {};
      var offset = 16;
      for (var i = 0; i < num_torrents; ++i) {
        var infohash = read_infohash(view, offset);
        offset += 20;
        var torrent = {};

        //			var mask_high = view.getUint32(offset);
        offset += 4;
        var mask_low = view.getUint32(offset);
        offset += 4;

        for (var field = 0; field < 32; ++field) {
          var mask = 1 << field;
          if ((mask_low & mask) == 0) continue;
          switch (field) {
            case 0: // flags
              // skip high bytes, since we can't
              // represent 64 bits in one field anyway
              offset += 4;
              torrent["flags"] = view.getUint32(offset);
              offset += 4;
              break;
            case 1: // name
              var [name, len] = read_string16(view, offset);
              offset += 2 + len;
              torrent["name"] = name;
              break;
            case 2: // total-uploaded
              torrent["total-uploaded"] = read_uint64(view, offset);
              offset += 8;
              break;
            case 3: // total-downloaded
              torrent["total-downloaded"] = read_uint64(view, offset);
              offset += 8;
              break;
            case 4: // added-time
              torrent["added-time"] = read_uint64(view, offset);
              offset += 8;
              break;
            case 5: // completed-time
              torrent["completed-time"] = read_uint64(view, offset);
              offset += 8;
              break;
            case 6: // upload-rate
              torrent["upload-rate"] = view.getUint32(offset);
              offset += 4;
              break;
            case 7: // download-rate
              torrent["download-rate"] = view.getUint32(offset);
              offset += 4;
              break;
            case 8: // progress
              torrent["progress"] = view.getUint32(offset);
              offset += 4;
              break;
            case 9: // error
              var [e, len] = read_string16(view, offset);
              offset += 2 + len;
              torrent["error"] = e;
              break;
            case 10: // connected-peers
              torrent["connected-peers"] = view.getUint32(offset);
              offset += 4;
              break;
            case 11: // connected-seeds
              torrent["connected-seeds"] = view.getUint32(offset);
              offset += 4;
              break;
            case 12: // downloaded-pieces
              torrent["downloaded-pieces"] = view.getUint32(offset);
              offset += 4;
              break;
            case 13: // total-done
              torrent["total-done"] = read_uint64(view, offset);
              offset += 8;
              break;
            case 14: // distributed-copies
              var integer = view.getUint32(offset);
              offset += 4;
              var fraction = view.getUint32(offset);
              offset += 4;
              torrent["distributed-copies"] = integer + fraction / 1000.0;
              break;
            case 15: // all-time-upload
              torrent["all-time-upload"] = read_uint64(view, offset);
              offset += 8;
              break;
            case 16: // all-time-download
              torrent["all-time-download"] = read_uint64(view, offset);
              offset += 8;
              break;
            case 17: // unchoked-peers
              torrent["unchoked-peers"] = view.getUint32(offset);
              offset += 4;
              break;
            case 18: // num-connections
              torrent["num-connections"] = view.getUint32(offset);
              offset += 4;
              break;
            case 19: // queue-position
              torrent["queue-position"] = view.getUint32(offset);
              offset += 4;
              break;
            case 20: // state
              torrent["state"] = view.getUint8(offset);
              offset += 1;
              break;
            case 21: // failed-bytes
              torrent["failed-bytes"] = read_uint64(view, offset);
              offset += 8;
              break;
            case 22: // redundant-bytes
              torrent["redundant-bytes"] = read_uint64(view, offset);
              offset += 8;
              break;
          }
        }
        ret[infohash] = torrent;
      }

      ret["snapshot"] = num_removed_torrents == 0xffffffff;

      var removed = [];
      if (num_removed_torrents != 0xffffffff) {
        for (var i = 0; i < num_removed_torrents; ++i) {
          removed.push(read_infohash(view, offset));
          offset += 20;
        }
      }
      ret["removed"] = removed;

      if (typeof callback !== "undefined") callback(ret);
    };

    var call = new ArrayBuffer(15);
    var view = new DataView(call);
    // function 0
    view.setUint8(0, 0);
    // transaction-id
    view.setUint16(1, tid);
    // frame-number
    view.setUint32(3, this._frame);
    view.setUint32(7, 0);
    view.setUint32(11, mask);

    //	console.log('CALL get_updates( frame: ' + this._frame + ' mask: ' + mask.toString(16) + ' ) tid = ' + tid);
    this._socket.send(call);
  };

  libtorrent_connection.prototype["list_stats"] = function (callback) {
    // TODO: factor out this RPC boiler plate
    if (this._socket.readyState != WebSocket.OPEN) {
      window.setTimeout(function () {
        callback("socket closed");
      }, 0);
      return;
    }

    var tid = this._tid++;
    if (this._tid > 65535) this._tid = 0;

    var self = this;
    this._transactions[tid] = function (view, fun, e) {
      if (_check_error(e, callback)) return;

      var num_metrics = view.getUint16(4);

      // this is a local copy of the stats metric names
      self._stats = {};

      var offset = 6;
      var ret = [];
      for (var i = 0; i < num_metrics; ++i) {
        var id = view.getUint16(offset);
        var type = view.getUint8(offset + 2);
        var [name, len] = read_string8(view, offset + 3);
        ret[name] = { type: type, id: id };
        self._stats[id] = name;
        offset += 4 + len;
      }
      if (typeof callback !== "undefined") callback(ret);
    };

    var call = new ArrayBuffer(3);
    var view = new DataView(call);
    // function 17
    view.setUint8(0, 17);
    // transaction-id
    view.setUint16(1, tid);

    //	console.log('CALL list_stats () tid = ' + tid);
    this._socket.send(call);
  };

  libtorrent_connection.prototype["get_stats"] = function (stats, callback) {
    // TODO: factor out this RPC boiler plate
    if (this._socket.readyState != WebSocket.OPEN) {
      window.setTimeout(function () {
        callback("socket closed");
      }, 0);
      return;
    }

    if (this._stats == null) {
      window.setTimeout(function () {
        callback("need to call list_stats first");
      }, 0);
      return;
    }

    var tid = this._tid++;
    if (this._tid > 65535) this._tid = 0;

    var self = this;
    this._transactions[tid] = function (view, fun, e) {
      if (_check_error(e, callback)) return;

      self._stats_frame = view.getUint32(4);

      var num_updates = view.getUint16(8);
      var offset = 10;

      // read values
      var ret = {};
      for (var i = 0; i < num_updates; ++i) {
        var id = view.getUint16(offset);
        var val = read_uint64(view, offset + 2);
        offset += 10;
        ret[self._stats[id]] = val;
      }
      if (typeof callback !== "undefined") callback(ret);
    };

    var call = new ArrayBuffer(3 + 4 + 2 + stats.length * 2);
    var view = new DataView(call);
    // function 18
    view.setUint8(0, 18);
    // transaction-id
    view.setUint16(1, tid);
    // frame number
    view.setUint32(3, this._stats_frame);
    // num-stats
    view.setUint16(7, stats.length);

    var offset = 9;
    for (var i in stats) {
      view.setUint16(offset, stats[i]);
      offset += 2;
    }

    //	console.log('CALL get_stats () tid = ' + tid);
    this._socket.send(call);
  };

  libtorrent_connection.prototype["get_file_updates"] = function (
    ih,
    last_frame,
    field_mask,
    callback,
  ) {
    if (this._socket.readyState != WebSocket.OPEN) {
      window.setTimeout(function () {
        callback("socket closed");
      }, 0);
      return;
    }

    var tid = this._tid++;
    if (this._tid > 65535) this._tid = 0;

    // this is the handler of the response for this call. It first
    // parses out the return value, the passes it on to the user
    // supplied callback.
    this._transactions[tid] = function (view, fun, e) {
      if (_check_error(e, callback)) return;

      var frame = view.getUint32(4);
      var num_files = view.getUint32(8);
      var files = [];
      var offset = 12;
      var mask = 0;
      for (var i = 0; i < num_files; ++i) {
        var file = {};

        if (i % 8 == 0) {
          mask = view.getUint8(offset);
          offset += 1;
        }
        if ((mask & (0x80 >> (i & 7))) == 0) {
          files.push(file);
          continue;
        }

        var file_mask = view.getUint16(offset);
        offset += 2;

        for (var field = 0; field < 16; ++field) {
          var bit = 1 << field;
          if ((file_mask & bit) == 0) continue;
          switch (field) {
            case 0: // flags
              file["flags"] = view.getUint8(offset);
              offset += 1;
              break;
            case 1: // name
              var [name, len] = read_string16(view, offset);
              offset += 2 + len;
              file["name"] = name;
              break;
            case 2: // total-size
              file["size"] = read_uint64(view, offset);
              offset += 8;
              break;
            case 3: // total-downloaded
              file["downloaded"] = read_uint64(view, offset);
              offset += 8;
              break;
            case 4: // priority
              file["prio"] = view.getUint8(offset);
              offset += 1;
              break;
            case 5: // open-mode
              file["open-mode"] = view.getUint8(offset);
              offset += 1;
              break;
          }
        }
        files.push(file);
      }

      if (typeof callback !== "undefined")
        callback({ frame: frame, files: files });
    };

    // 3 header + 20 info-hash + 4 frame + 2 field-mask = 29 bytes
    var call = new ArrayBuffer(29);
    var view = new DataView(call);
    // function 19
    view.setUint8(0, 19);
    // transaction-id
    view.setUint16(1, tid);

    var offset = 3;
    for (var i = 0; i < 40; i += 2) {
      var b = parseInt(ih.substring(i, i + 2), 16);
      view.setUint8(offset, b);
      offset += 1;
    }

    // frame-number
    view.setUint32(offset, last_frame);
    offset += 4;

    // field-mask
    view.setUint16(offset, field_mask);

    this._socket.send(call);
  };
  libtorrent_connection.prototype["start"] = function (info_hashes, callback) {
    this._send_simple_call(1, info_hashes, callback);
  };

  libtorrent_connection.prototype["stop"] = function (info_hashes, callback) {
    this._send_simple_call(2, info_hashes, callback);
  };

  libtorrent_connection.prototype["set_auto_managed"] = function (
    info_hashes,
    callback,
  ) {
    this._send_simple_call(3, info_hashes, callback);
  };

  libtorrent_connection.prototype["clear_auto_managed"] = function (
    info_hashes,
    callback,
  ) {
    this._send_simple_call(4, info_hashes, callback);
  };

  libtorrent_connection.prototype["queue_up"] = function (
    info_hashes,
    callback,
  ) {
    this._send_simple_call(5, info_hashes, callback);
  };

  libtorrent_connection.prototype["queue_down"] = function (
    info_hashes,
    callback,
  ) {
    this._send_simple_call(6, info_hashes, callback);
  };

  libtorrent_connection.prototype["queue_top"] = function (
    info_hashes,
    callback,
  ) {
    this._send_simple_call(7, info_hashes, callback);
  };

  libtorrent_connection.prototype["queue_bottom"] = function (
    info_hashes,
    callback,
  ) {
    this._send_simple_call(8, info_hashes, callback);
  };

  libtorrent_connection.prototype["remove"] = function (info_hashes, callback) {
    this._send_simple_call(9, info_hashes, callback);
  };

  libtorrent_connection.prototype["remove_with_data"] = function (
    info_hashes,
    callback,
  ) {
    this._send_simple_call(10, info_hashes, callback);
  };

  libtorrent_connection.prototype["force_recheck"] = function (
    info_hashes,
    callback,
  ) {
    this._send_simple_call(11, info_hashes, callback);
  };

  libtorrent_connection.prototype["set_sequential_download"] = function (
    info_hashes,
    callback,
  ) {
    this._send_simple_call(12, info_hashes, callback);
  };

  libtorrent_connection.prototype["clear_sequential_download"] = function (
    info_hashes,
    callback,
  ) {
    this._send_simple_call(13, info_hashes, callback);
  };

  libtorrent_connection.prototype._send_simple_call = function (
    fun_id,
    info_hashes,
    callback,
  ) {
    var call = new ArrayBuffer(3 + 2 + info_hashes.length * 20);
    var view = new DataView(call);

    if (fun_id < 1 || fun_id > 13) {
      window.setTimeout(function () {
        callback("socket closed");
      }, 0);
      return;
    }

    var tid = this._tid++;
    if (this._tid > 65535) this._tid = 0;

    // function-id
    view.setUint8(0, fun_id);
    // transaction-id
    view.setUint16(1, tid);
    // num_torrents
    view.setUint16(3, info_hashes.length);

    var offset = 5;
    for (var ih in info_hashes) {
      for (var i = 0; i < 40; i += 2) {
        var b = parseInt(info_hashes[ih].substring(i, i + 2), 16);
        view.setUint8(offset, b);
        offset += 1;
      }
    }

    //	console.log('CALL ' + fun_id + '() tid = ' + tid);

    // this is the handler of the response for this call. It first
    // parses out the return value, the passes it on to the user
    // supplied callback.
    this._transactions[tid] = function (view, fun, e) {
      if (_check_error(e, callback)) return;
      var num_torrents = view.getUint16(4);
      if (typeof callback !== "undefined") callback(num_torrents);
    };

    this._socket.send(call);
  };

  libtorrent_connection.prototype["get_peers_updates"] = function (
    ih,
    mask,
    callback,
  ) {
    if (this._socket.readyState != WebSocket.OPEN) {
      window.setTimeout(function () {
        callback("socket closed");
      }, 0);
      return;
    }

    var tid = this._tid++;
    if (this._tid > 65535) this._tid = 0;

    this._transactions[tid] = function (view, fun, e) {
      if (_check_error(e, callback)) return;

      var num_updates = view.getUint32(8);
      // num-removed at offset 12; 0xffffffff means all non-included peers left
      var ret = [];
      var offset = 16;

      for (var i = 0; i < num_updates; ++i) {
        var peer = {};
        peer["id"] = view.getUint32(offset);
        offset += 4;

        // field bitmask: skip high 32 bits, all 20 fields fit in low 32 bits
        offset += 4;
        var field_mask = view.getUint32(offset);
        offset += 4;

        for (var field = 0; field < 20; ++field) {
          if ((field_mask & (1 << field)) == 0) continue;
          switch (field) {
            case 0: // flags
              peer["flags"] = view.getUint32(offset);
              offset += 4;
              break;
            case 1: // source
              peer["source"] = view.getUint8(offset);
              offset += 1;
              break;
            case 2: // read-state
              peer["read-state"] = view.getUint8(offset);
              offset += 1;
              break;
            case 3: // write-state
              peer["write-state"] = view.getUint8(offset);
              offset += 1;
              break;
            case 4: // client (uint8 length prefix)
              var [client, len] = read_string8(view, offset);
              offset += 1 + len;
              peer["client"] = client;
              break;
            case 5: // num-pieces
              peer["num-pieces"] = view.getUint32(offset);
              offset += 4;
              break;
            case 6: // pending-disk-bytes
              peer["pending-disk-bytes"] = view.getUint32(offset);
              offset += 4;
              break;
            case 7: // pending-disk-read-bytes
              peer["pending-disk-read-bytes"] = view.getUint32(offset);
              offset += 4;
              break;
            case 8: // hashfails
              peer["hashfails"] = view.getUint32(offset);
              offset += 4;
              break;
            case 9: // down-rate (payload bytes/s)
              peer["down-rate"] = view.getUint32(offset);
              offset += 4;
              break;
            case 10: // up-rate (payload bytes/s)
              peer["up-rate"] = view.getUint32(offset);
              offset += 4;
              break;
            case 11: {
              // peer-id (20 bytes, hex-encoded)
              var pid = "";
              for (var j = 0; j < 20; ++j) {
                var pb = view.getUint8(offset + j);
                if (pb < 16) pid += "0";
                pid += pb.toString(16);
              }
              peer["peer-id"] = pid;
              offset += 20;
              break;
            }
            case 12: // download-queue length (blocks)
              peer["download-queue"] = view.getUint32(offset);
              offset += 4;
              break;
            case 13: // upload-queue length (blocks)
              peer["upload-queue"] = view.getUint32(offset);
              offset += 4;
              break;
            case 14: // timed-out-reqs
              peer["timed-out-reqs"] = view.getUint32(offset);
              offset += 4;
              break;
            case 15: // progress [0, 1000000]
              peer["progress"] = view.getUint32(offset);
              offset += 4;
              break;
            case 16: {
              // endpoints
              var ep_type = view.getUint8(offset);
              offset += 1;
              if (ep_type === 2) {
                // I2P: 32-byte destination hash
                var i2p = "";
                for (var k = 0; k < 32; ++k) {
                  var db = view.getUint8(offset + k);
                  if (db < 16) i2p += "0";
                  i2p += db.toString(16);
                }
                peer["endpoint"] = i2p;
                offset += 32;
              } else if (ep_type === 1) {
                // IPv6: 18 bytes each (16 IP + 2 port)
                var lp6 = [];
                for (var k = 0; k < 8; ++k)
                  lp6.push(view.getUint16(offset + k * 2).toString(16));
                var lport6 = view.getUint16(offset + 16);
                offset += 18;
                var rp6 = [];
                for (var k = 0; k < 8; ++k)
                  rp6.push(view.getUint16(offset + k * 2).toString(16));
                var rport6 = view.getUint16(offset + 16);
                offset += 18;
                peer["local-endpoint"] = "[" + lp6.join(":") + "]:" + lport6;
                peer["endpoint"] = "[" + rp6.join(":") + "]:" + rport6;
              } // IPv4: 6 bytes each (4 IP + 2 port)
              else {
                var lip4 =
                  view.getUint8(offset) +
                  "." +
                  view.getUint8(offset + 1) +
                  "." +
                  view.getUint8(offset + 2) +
                  "." +
                  view.getUint8(offset + 3);
                var lport4 = view.getUint16(offset + 4);
                offset += 6;
                var rip4 =
                  view.getUint8(offset) +
                  "." +
                  view.getUint8(offset + 1) +
                  "." +
                  view.getUint8(offset + 2) +
                  "." +
                  view.getUint8(offset + 3);
                var rport4 = view.getUint16(offset + 4);
                offset += 6;
                peer["local-endpoint"] = lip4 + ":" + lport4;
                peer["endpoint"] = rip4 + ":" + rport4;
              }
              break;
            }
            case 17: {
              // pieces bitfield (uint32 byte-count + bytes, MSB first)
              var num_bytes = view.getUint32(offset);
              offset += 4;
              var pieces = [];
              for (var k = 0; k < num_bytes; ++k)
                pieces.push(view.getUint8(offset + k));
              peer["pieces"] = pieces;
              offset += num_bytes;
              break;
            }
            case 18: // total-download
              peer["total-download"] = read_uint64(view, offset);
              offset += 8;
              break;
            case 19: // total-upload
              peer["total-upload"] = read_uint64(view, offset);
              offset += 8;
              break;
          }
        }
        ret.push(peer);
      }

      if (typeof callback !== "undefined") callback(ret);
    };

    // request: 3 header + 20 info-hash + 4 frame + 8 bitmask = 35 bytes
    var call = new ArrayBuffer(35);
    var view = new DataView(call);
    view.setUint8(0, 21);
    view.setUint16(1, tid);

    var offset = 3;
    for (var i = 0; i < 40; i += 2) {
      view.setUint8(offset, parseInt(ih.substring(i, i + 2), 16));
      offset += 1;
    }
    // frame-number (always 0 - no delta tracking yet)
    view.setUint32(offset, 0);
    offset += 4;
    // field-bitmask: high 32 bits = 0, low 32 bits = mask
    view.setUint32(offset, 0);
    offset += 4;
    view.setUint32(offset, mask);

    this._socket.send(call);
  };

  libtorrent_connection.prototype["get_piece_updates"] = function (
    ih,
    last_frame,
    callback,
  ) {
    if (this._socket.readyState != WebSocket.OPEN) {
      window.setTimeout(function () {
        callback("socket closed");
      }, 0);
      return;
    }

    var tid = this._tid++;
    if (this._tid > 65535) this._tid = 0;

    this._transactions[tid] = function (view, fun, e) {
      if (_check_error(e, callback)) return;

      var frame = view.getUint32(4);
      var num_pieces = view.getUint16(8);
      var num_block_updates = view.getUint16(10);
      var num_removed = view.getUint16(12);

      var ret = {};
      ret["frame"] = frame;
      ret["snapshot"] = num_removed === 0xffff;
      var offset = 14;

      var pieces = [];
      for (var pi = 0; pi < num_pieces; ++pi) {
        var piece = {};
        piece["index"] = view.getUint32(offset);
        offset += 4;
        var nb = view.getUint16(offset);
        piece["num-blocks"] = nb;
        offset += 2;

        var blocks = [];
        for (var bi = 0; bi < nb; ++bi) {
          blocks.push(view.getUint8(offset));
          offset += 1;
        }
        piece["blocks"] = blocks;
        pieces.push(piece);
      }
      ret["pieces"] = pieces;

      var block_updates = [];
      for (var bi = 0; bi < num_block_updates; ++bi) {
        var bu = {};
        bu["piece"] = view.getUint32(offset);
        offset += 4;
        bu["block"] = view.getUint16(offset);
        offset += 2;
        bu["state"] = view.getUint8(offset);
        offset += 1;
        block_updates.push(bu);
      }
      ret["block-updates"] = block_updates;

      var removed = [];
      if (num_removed !== 0xffff) {
        for (var ri = 0; ri < num_removed; ++ri) {
          removed.push(view.getUint32(offset));
          offset += 4;
        }
      }
      ret["removed"] = removed;

      if (typeof callback !== "undefined") callback(ret);
    };

    // request: 3 header + 20 info-hash + 4 frame = 27 bytes
    let call = new ArrayBuffer(27);
    let view = new DataView(call);
    view.setUint8(0, 22);
    view.setUint16(1, tid);

    let offset = 3;
    for (let i = 0; i < 40; i += 2) {
      view.setUint8(offset, parseInt(ih.substring(i, i + 2), 16));
      offset += 1;
    }
    view.setUint32(offset, last_frame);
    offset += 4;

    this._socket.send(call);
  };

  libtorrent_connection.prototype["add_torrent"] = function (
    magnet_link,
    callback,
  ) {
    const encoder = new TextEncoder();
    const link = encoder.encode(magnet_link);

    var call = new ArrayBuffer(3 + 2 + link.length);
    var view = new DataView(call);

    var tid = this._tid++;
    if (this._tid > 65535) this._tid = 0;

    // function-id
    view.setUint8(0, 20);
    // transaction-id
    view.setUint16(1, tid);
    // string length
    view.setUint16(3, link.length);

    var offset = 5;
    for (var i = 0; i < link.length; i++) {
      view.setUint8(offset, link[i]);
      offset++;
    }

    //	console.log('CALL 20 ("' + magnet_link + '") tid = ' + tid);

    // this is the handler of the response for this call. It first
    // parses out the return value, the passes it on to the user
    // supplied callback.
    this._transactions[tid] = function (view, fun, e) {
      if (typeof callback !== "undefined") callback(e);
    };

    this._socket.send(call);
  };

  libtorrent_connection.prototype["set_file_priority"] = function (
    ih,
    updates,
    callback,
  ) {
    if (this._socket.readyState != WebSocket.OPEN) {
      window.setTimeout(function () {
        callback("socket closed");
      }, 0);
      return;
    }

    var tid = this._tid++;
    if (this._tid > 65535) this._tid = 0;

    this._transactions[tid] = function (view, fun, e) {
      if (typeof callback !== "undefined") callback(e);
    };

    // 3 header + 20 info-hash + 4 num-updates + updates.length * 5
    var call = new ArrayBuffer(3 + 20 + 4 + updates.length * 5);
    var view = new DataView(call);
    view.setUint8(0, 23);
    view.setUint16(1, tid);

    var offset = 3;
    for (var i = 0; i < 40; i += 2) {
      view.setUint8(offset, parseInt(ih.substring(i, i + 2), 16));
      offset += 1;
    }
    view.setUint32(offset, updates.length);
    offset += 4;

    for (var i = 0; i < updates.length; ++i) {
      view.setUint32(offset, updates[i]["index"]);
      offset += 4;
      view.setUint8(offset, updates[i]["priority"]);
      offset += 1;
    }

    this._socket.send(call);
  };

  libtorrent_connection.prototype["get_tracker_updates"] = function (
    ih,
    last_frame,
    callback,
  ) {
    if (this._socket.readyState != WebSocket.OPEN) {
      window.setTimeout(function () {
        callback("socket closed");
      }, 0);
      return;
    }

    var tid = this._tid++;
    if (this._tid > 65535) this._tid = 0;

    this._transactions[tid] = function (view, fun, e) {
      if (_check_error(e, callback)) return;

      var frame = view.getUint32(4);
      var timestamp = view.getUint32(8);
      var num_updates = view.getUint16(12);
      var num_removed = view.getUint16(14);
      var snapshot = num_removed === 0xffff;

      var offset = 16;
      var updates = {};

      for (var i = 0; i < num_updates; ++i) {
        var tracker_id = view.getUint16(offset);
        offset += 2;
        var field_mask = view.getUint16(offset);
        offset += 2;

        var tracker = {};

        // field 0: url (uint16_t length + bytes)
        if (field_mask & 0x001) {
          var [url, url_len] = read_string16(view, offset);
          offset += 2 + url_len;
          tracker["url"] = url;
        }
        // field 1: tier (uint8_t)
        if (field_mask & 0x002) {
          tracker["tier"] = view.getUint8(offset);
          offset += 1;
        }
        // field 2: source (uint8_t bitmask)
        if (field_mask & 0x004) {
          tracker["source"] = view.getUint8(offset);
          offset += 1;
        }
        // field 3: complete (int32_t; -1 = unknown)
        if (field_mask & 0x008) {
          tracker["complete"] = view.getInt32(offset);
          offset += 4;
        }
        // field 4: incomplete (int32_t; -1 = unknown)
        if (field_mask & 0x010) {
          tracker["incomplete"] = view.getInt32(offset);
          offset += 4;
        }
        // field 5: downloaded (int32_t; -1 = unknown)
        if (field_mask & 0x020) {
          tracker["downloaded"] = view.getInt32(offset);
          offset += 4;
        }
        // field 6: next-announce (int32_t, lt::clock_type seconds; 0 = not scheduled)
        if (field_mask & 0x040) {
          tracker["next-announce"] = view.getInt32(offset);
          offset += 4;
        }
        // field 7: min-announce (int32_t, lt::clock_type seconds; 0 = not set)
        if (field_mask & 0x080) {
          tracker["min-announce"] = view.getInt32(offset);
          offset += 4;
        }
        // field 8: last-error (uint8_t length + bytes, max 255)
        if (field_mask & 0x100) {
          var [err_str, err_len] = read_string8(view, offset);
          offset += 1 + err_len;
          tracker["last-error"] = err_str;
        }
        // field 9: message (uint8_t length + bytes, max 255)
        if (field_mask & 0x200) {
          var [msg_str, msg_len] = read_string8(view, offset);
          offset += 1 + msg_len;
          tracker["message"] = msg_str;
        }
        // field 10: flags (uint8_t)
        // 0x01=updating, 0x02=complete-sent, 0x04=verified, 0x08=enabled, 0x10=v2
        if (field_mask & 0x400) {
          tracker["flags"] = view.getUint8(offset);
          offset += 1;
        }
        // field 11: local-endpoint (uint8_t type + addr bytes + uint16_t port)
        // type: 0 = IPv4 (4 addr bytes), 1 = IPv6 (16 addr bytes)
        if (field_mask & 0x800) {
          var ep_type = view.getUint8(offset++);
          if (ep_type === 1) {
            // IPv6: 16 addr + 2 port
            var parts = [];
            for (var k = 0; k < 8; ++k)
              parts.push(view.getUint16(offset + k * 2).toString(16));
            var ep_port = view.getUint16(offset + 16);
            offset += 18;
            tracker["local-endpoint"] = "[" + parts.join(":") + "]:" + ep_port;
          } // IPv4: 4 addr + 2 port
          else {
            var ep_ip =
              view.getUint8(offset) +
              "." +
              view.getUint8(offset + 1) +
              "." +
              view.getUint8(offset + 2) +
              "." +
              view.getUint8(offset + 3);
            var ep_port = view.getUint16(offset + 4);
            offset += 6;
            tracker["local-endpoint"] = ep_ip + ":" + ep_port;
          }
        }

        updates[tracker_id] = tracker;
      }

      var removed = [];
      if (!snapshot) {
        for (var i = 0; i < num_removed; ++i) {
          var rid = view.getUint16(offset);
          offset += 2;
          removed.push(rid);
        }
      }

      if (typeof callback !== "undefined")
        callback({
          frame: frame,
          timestamp: timestamp,
          snapshot: snapshot,
          updates: updates,
          removed: removed,
        });
    };

    // 3 header + 20 info-hash + 4 frame = 27 bytes
    var call = new ArrayBuffer(27);
    var view = new DataView(call);
    view.setUint8(0, 24);
    view.setUint16(1, tid);

    var offset = 3;
    for (var i = 0; i < 40; i += 2) {
      view.setUint8(offset, parseInt(ih.substring(i, i + 2), 16));
      offset += 1;
    }
    view.setUint32(offset, last_frame);

    this._socket.send(call);
  };

  libtorrent_connection.prototype["close"] = function () {
    this._socket.close();
  };

  var fields = {
    flags: 1 << 0,
    name: 1 << 1,
    total_uploaded: 1 << 2,
    total_downloaded: 1 << 3,
    added_time: 1 << 4,
    completed_time: 1 << 5,
    upload_rate: 1 << 6,
    download_rate: 1 << 7,
    progress: 1 << 8,
    error: 1 << 9,
    connected_peers: 1 << 10,
    connected_seeds: 1 << 11,
    downloaded_pieces: 1 << 12,
    total_done: 1 << 13,
    distributed_copies: 1 << 14,
    all_time_upload: 1 << 15,
    all_time_download: 1 << 16,
    unchoked_peers: 1 << 17,
    num_connections: 1 << 18,
    queue_position: 1 << 19,
    state: 1 << 20,
    failed_bytes: 1 << 21,
    redundant_bytes: 1 << 22,
  };

  var file_fields = {
    flags: 1 << 0,
    name: 1 << 1,
    size: 1 << 2,
    downloaded: 1 << 3,
    priority: 1 << 4, // extra cost: fetches all file priorities
    open_mode: 1 << 5, // extra cost: fetches open file status
  };

  var tracker_fields = {
    url: 1 << 0,
    tier: 1 << 1,
    source: 1 << 2,
    complete: 1 << 3,
    incomplete: 1 << 4,
    downloaded: 1 << 5,
    next_announce: 1 << 6,
    min_announce: 1 << 7,
    last_error: 1 << 8,
    message: 1 << 9,
    flags: 1 << 10,
    local_endpoint: 1 << 11,
  };

  var peer_fields = {
    flags: 1 << 0,
    source: 1 << 1,
    read_state: 1 << 2,
    write_state: 1 << 3,
    client: 1 << 4,
    num_pieces: 1 << 5,
    pending_disk_bytes: 1 << 6,
    pending_disk_read_bytes: 1 << 7,
    hashfails: 1 << 8,
    down_rate: 1 << 9,
    up_rate: 1 << 10,
    peer_id: 1 << 11,
    download_queue: 1 << 12,
    upload_queue: 1 << 13,
    timed_out_reqs: 1 << 14,
    progress: 1 << 15,
    endpoints: 1 << 16,
    pieces: 1 << 17,
    total_download: 1 << 18,
    total_upload: 1 << 19,
  };

  // prevent the compiler from optimizing these away
  window["libtorrent_connection"] = libtorrent_connection;
  window["fields"] = fields;
  window["file_fields"] = file_fields;
  window["peer_fields"] = peer_fields;
  window["tracker_fields"] = tracker_fields;
})();
