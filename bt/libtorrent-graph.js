/**
\param canvas is the name of the HTML canvas element to draw on. The
   canvas's CSS dimensions must be driven by CSS rules or inline style,
   not left to fall back on the bitmap-derived intrinsic size: each
   frame this function rewrites canvas.width/height to match
   clientWidth/clientHeight * devicePixelRatio, and if clientWidth ever
   reflects the bitmap (because no CSS sizing is in effect) the writes
   feed back into the next frame's reading and the canvas grows
   without bound. Setting "width" / "height" as CSS properties on the
   element (or on a containing layout box) prevents this
\param data is an array of objects. Each object is a data point with a
   'time' field indicating the position on the x-axis and then
   arbitrary additional fields for the y-axis for all the graphs.
   The data is assumed to be sorted by 'time' in ascending order.
\param graphs is an array of objects specifying metadata about each graph
   to plot. The most important field is 'name' which indicates the
   name of the field in the data array to use for the plot. Also specify
   'color' which is a string defining the color of the line (CSS style).
\param now is a number representing the right-most point on the x-axis,
   typically the timestamp of the most recent sample. It must be of the
   same unit as the numbers in the 'time' fields in 'data'.
\param pixels_per_time_unit is the horizontal scale in CSS pixels per
   unit of 'time'. The chart renders from right (now) to left, drawing
   as much history as fits in the canvas's CSS width. There is no
   auto-fit: making the canvas wider shows more history; making it
   narrower shows less. If 'time' is in seconds, this is literally
   pixels per second; if 'time' is in microseconds, scale accordingly.
\param unit this is an optional unit of the values on the y-axis, and will
   be printed in the right margin for each of the tics.
\param scale this is an optional scale factor for the data. The unit will
   automatically have an SI prefix added to it based on the scale. Specifying
   this locks the prefix to a specific one regardless of the magnitude of
   the data points. This is primarily useful to make a graph always specify
   transfer rates in kB/s, in which case it should be set to 1000.
\param multiplier this is an optional number that's multiplied to all y-values
   before used by any part of the graphing logic. This can be used to turn a
   scaled unit into the base SI unit, to make the applied prefixes apply correctly.
   For instance, time values specified in microseconds could have a multiplier
   of 0.000001 in order to turn them into seconds
\param use_legend this is an optional boolean defaulting to false. When true,
   a legend of the graphs is rendered in the top left corner. In this case,
   each object in the graphs array may have a 'label' field indicating the
   name for that graph in the legend. If no label is specified, the 'name'
   field is used.
\param seconds_per_x_tic is the spacing between x-axis tick marks, in
   units of 'time'. Defaults to 30. Ticks are anchored to the right
   edge ('now') and slide leftward as 'now' advances, so each tick
   represents a fixed age (30 s ago, 60 s ago, ...) rather than a
   fixed wall-clock time.
*/

"use strict";

(function () {
  // Format an age in seconds as e.g. "30s", "1m", "1:30m", "2m". Under
  // a minute we show plain seconds; on exact-minute boundaries the
  // seconds component is dropped; otherwise the seconds are
  // zero-padded. Used for x-axis tick labels, which are anchored to
  // 'now' and step backwards by seconds_per_x_tic.
  function format_age(seconds) {
    seconds = Math.round(seconds);
    if (seconds < 60) return seconds + "s";
    var mins = Math.floor(seconds / 60);
    var secs = seconds % 60;
    if (secs === 0) return mins + "m";
    return mins + ":" + (secs < 10 ? "0" : "") + secs + "m";
  }

  function render_graph(
    canvas,
    data,
    graphs,
    now,
    pixels_per_time_unit,
    unit,
    scale,
    multiplier,
    use_legend,
    seconds_per_x_tic,
  ) {
    if (typeof unit == "undefined") unit = "";
    if (typeof scale == "undefined") scale = "auto";
    if (typeof multiplier == "undefined") multiplier = 1;
    if (typeof use_legend == "undefined") use_legend = false;
    if (typeof seconds_per_x_tic == "undefined") seconds_per_x_tic = 30;

    var canvas = document.getElementById(canvas);
    var ctx = canvas.getContext("2d");

    var root_styles = window.getComputedStyle(document.documentElement);
    var text_color =
      root_styles.getPropertyValue("--text-primary").trim() || "#000";
    var grid_color =
      root_styles.getPropertyValue("--border-medium").trim() || "#ccc";
    var legend_bg =
      root_styles.getPropertyValue("--bg-legend").trim() ||
      "rgba(255,255,255,0.7)";
    var legend_border =
      root_styles.getPropertyValue("--border-strong").trim() || "#000";

    // Match the bitmap to the CSS layout box each frame so the advertised
    // pixels_per_time_unit is honest: one CSS pixel of horizontal travel
    // corresponds to exactly 1 / pixels_per_time_unit units of 'time'.
    // Without this, CSS scaling would silently shrink (or stretch) the
    // time axis while the function still claims a fixed scale.
    var dpr = window.devicePixelRatio || 1;
    var canvas_width = canvas.clientWidth;
    var canvas_height = canvas.clientHeight;
    // The canvas hasn't been laid out yet (modal closed, display:none,
    // or detached). Nothing to draw, and continuing would divide by zero
    // when computing scaley further down.
    if (canvas_width === 0 || canvas_height === 0) return;
    var target_w = Math.round(canvas_width * dpr);
    var target_h = Math.round(canvas_height * dpr);
    if (canvas.width !== target_w) canvas.width = target_w;
    if (canvas.height !== target_h) canvas.height = target_h;
    // Assigning canvas.width / canvas.height resets the transform, so
    // re-apply the DPR scaling every frame -- otherwise hi-DPI displays
    // would render at half size after the first resize.
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    ctx.clearRect(0, 0, canvas_width, canvas_height);
    ctx.save();

    var view_width = canvas_width - 40;
    // Reserve ~15 px at the bottom for x-axis tick labels (e.g. "1:30m")
    // that sit below the plot area.
    var view_height = canvas_height - 20;

    // the 0.5 is to align lines with pixels
    ctx.translate(0.5, 4.5);

    // used for text
    ctx.fillStyle = text_color;
    ctx.lineWidth = 1;

    // Anything older than this scrolled off the left edge and is invisible.
    var visible_start = now - view_width / pixels_per_time_unit;

    // Find the peak across visible samples only. Old peaks that have
    // scrolled off the left must not hold the y-axis range high, or the
    // chart would mysteriously compress whenever an old spike was still
    // "remembered" outside the visible window.
    var peak = 0;
    for (var dp = 0; dp < data.length; ++dp) {
      var t = data[dp].time;
      if (t < visible_start) continue;
      if (t > now) break;
      for (var g in graphs) {
        var n = graphs[g].name;
        if (!data[dp].hasOwnProperty(n)) continue;
        if (!Number.isFinite(data[dp][n])) continue;
        peak = Math.max(data[dp][n] * multiplier, peak);
      }
    }
    if (peak == 0) peak = 1 * (scale == "auto" ? 1 : scale);

    // calculate the number of tics and the peak
    var num_tics = 10;
    var new_peak = 1;
    while (peak > new_peak) new_peak *= 10;

    num_tics = Math.ceil((peak / new_peak) * 10);
    peak = (new_peak * num_tics) / 10;
    if (num_tics < 5) num_tics *= 2;

    if (scale == "auto") {
      if (peak >= 1000000000) {
        scale = 1000000000;
      } else if (peak >= 1000000) {
        scale = 1000000;
      } else if (peak >= 1000) {
        scale = 1000;
      } else if (peak >= 1) {
        scale = 1;
      } else if (peak >= 0.001) {
        scale = 1 / 1000;
      } else if (peak >= 0.000001) {
        scale = 1 / 1000000;
      } else if (peak >= 0.000000001) {
        scale = 1 / 1000000000;
      }
    }

    if (scale >= 1000000000) {
      unit = "G" + unit;
    } else if (scale >= 1000000) {
      unit = "M" + unit;
    } else if (scale >= 1000) {
      unit = "k" + unit;
    } else if (scale >= 1) {
      // do nothing
    } else if (scale >= 0.001) {
      unit = "m" + unit;
    } else if (scale >= 0.00001) {
      unit = "u" + unit;
    } else if (scale >= 0.000000001) {
      unit = "n" + unit;
    }

    // Constant horizontal velocity: each unit of 'time' is exactly
    // pixels_per_time_unit CSS pixels wide, regardless of how much data
    // the buffer holds or how wide the canvas is.
    var scalex = pixels_per_time_unit;
    var scaley = view_height / peak;

    // draw y-axis tics
    for (var i = 0; i < num_tics; ++i) {
      ctx.strokeStyle = text_color;
      if ("setLineDash" in ctx) ctx.setLineDash([]);
      var y = Math.floor((view_height * i) / num_tics);
      ctx.beginPath();
      ctx.moveTo(view_width - 6, y);
      ctx.lineTo(view_width, y);
      ctx.lineTo(view_width, y + view_height / num_tics);
      ctx.stroke();
      var rate = peak - (peak * i) / num_tics;
      ctx.fillText(
        (rate / scale).toFixed(peak < 5 * scale ? 1 : 0) + " " + unit,
        view_width + 2,
        y + 4,
      );

      if ("setLineDash" in ctx) ctx.setLineDash([5]);
      ctx.strokeStyle = grid_color;
      ctx.beginPath();
      ctx.moveTo(view_width - 6, y);
      ctx.lineTo(0, y);
      ctx.stroke();
    }

    // draw x-axis tics, anchored to the right edge ('now') and walking
    // leftward. Each tick represents a fixed age (e.g. 30 s ago) and
    // slides leftward with the data, so the grid scrolls in lockstep
    // with the plot rather than creeping pixel-by-pixel against it.
    // Iterate by age so labels read as clean multiples of the tick
    // interval rather than the round-trip x->t->format showing 29.999m.
    ctx.textAlign = "center";
    for (var t = seconds_per_x_tic; ; t += seconds_per_x_tic) {
      var x = view_width - t * pixels_per_time_unit;
      if (x < 0) break;
      var tx = Math.floor(x);

      ctx.strokeStyle = text_color;
      if ("setLineDash" in ctx) ctx.setLineDash([]);
      ctx.beginPath();
      ctx.moveTo(tx, view_height - 6);
      ctx.lineTo(tx, view_height);
      ctx.stroke();

      ctx.fillText(format_age(t), tx, view_height + 14);

      if ("setLineDash" in ctx) ctx.setLineDash([5]);
      ctx.strokeStyle = grid_color;
      ctx.beginPath();
      ctx.moveTo(tx, view_height - 6);
      ctx.lineTo(tx, 0);
      ctx.stroke();
    }
    ctx.textAlign = "start";

    if ("setLineDash" in ctx) ctx.setLineDash([]);

    // plot all the graphs
    for (var k in graphs) {
      var g = graphs[k];

      ctx.strokeStyle = g.color;
      var first = true;
      // Last finite sample seen *before* the visible window. Carrying it
      // forward lets us draw the leftmost segment from off-screen into the
      // window, so the line doesn't start mid-canvas as a stub. Canvas
      // clips the off-screen portion for us.
      var prev_off_x;
      var prev_off_y;
      var has_prev_off = false;
      ctx.beginPath();
      for (var i = 0; i < data.length; ++i) {
        var time = data[i].time;
        if (time > now) break;
        var y = data[i][g.name] * multiplier;
        if (!Number.isFinite(y)) continue;
        if (time < visible_start) {
          prev_off_x = view_width - (now - time) * scalex;
          prev_off_y = view_height - y * scaley;
          has_prev_off = true;
          continue;
        }
        var x = view_width - (now - time) * scalex;
        var py = view_height - y * scaley;
        if (first) {
          if (has_prev_off) {
            ctx.moveTo(prev_off_x, prev_off_y);
            ctx.lineTo(x, py);
          } else {
            ctx.moveTo(x, py);
          }
          first = false;
        } else {
          ctx.lineTo(x, py);
        }
      }
      ctx.stroke();
    }

    if (use_legend) {
      ctx.font = "normal 12pt Calibri";
      var legend_width = 0;
      for (var k in graphs) {
        g = graphs[k];
        var label;
        if (g.label) label = g.label;
        else label = g.name;
        legend_width = Math.floor(
          Math.max(legend_width, ctx.measureText(label).width),
        );
      }

      var offset = 15;
      ctx.fillStyle = legend_bg;
      ctx.strokeStyle = legend_border;
      ctx.fillRect(4, offset - 9, 24 + legend_width, graphs.length * 16 + 2);
      ctx.strokeRect(4, offset - 9, 24 + legend_width, graphs.length * 16 + 2);
      ctx.fillStyle = text_color;

      // in order to have our lines properly pixel aligned
      // we need to add 0.5 to the offset when we double the
      // line width
      ctx.lineWidth = 2;
      offset += 0.5;

      for (var k in graphs) {
        g = graphs[k];
        var label;
        if (g.label) label = g.label;
        else label = g.name;

        ctx.strokeStyle = g.color;
        ctx.beginPath();
        ctx.moveTo(7.5, offset);
        ctx.lineTo(20.5, offset);
        ctx.stroke();

        ctx.fillText(label, 23, offset + 4);
        offset += 16;
      }
    }
    ctx.restore();
  }

  window["render_graph"] = render_graph;
})();
