/* ESPanelHA — config SPA (vanilla JS, no framework so it gzips small).
   The dashboard editor is a WYSIWYG: the canvas is a scaled, pixel-faithful
   replica of the device screen (geometry from /api/device), tiles render like
   the device (real MDI icon + accent + ellipsis name + state line), so building
   a page shows exactly what the panel will display. */
(function () {
  "use strict";

  var app = document.getElementById("app");
  var toastEl = document.getElementById("toast");

  // Fallback if /api/device is unreachable (mirrors the S3 1.8" profile).
  var DEFAULT_SPEC = {
    board: "device", screenW: 368, screenH: 448,
    gridCols: 2, rowHeightPx: 104, colGapPx: 10, rowGapPx: 10,
    pagePadPx: 10, pageTitleGapPx: 8, topBarHeightPx: 42, dotsBarHeightPx: 14,
    tileRadiusPx: 18, tilePadPx: 16, tileGapPx: 8,
    iconSizePx: 40, nameFontPx: 18, stateFontPx: 14, titleFontPx: 24, sliderHeightPx: 14,
    valueFontSmallPx: 24, valueFontMedPx: 32, valueFontLargePx: 40,
    screenBg: "#101418", tileBg: "#1B2128", pressedBg: "#232B34", iconOff: "#5B6571",
    amber: "#FFC107", primary: "#03A9F4", textMuted: "#8AA0B2", nameColor: "#FFFFFF",
    activeMix: 60,
    rotation: 0, autoRotate: false, hasImu: false,
    colsPortrait: 2, colsLandscape: 3, maxColsPortrait: 2, maxColsLandscape: 3,
  };

  var state = {
    cfg: { ha: {}, auth: {} },
    layout: { pages: [], maxPages: 8, maxTilesPerPage: 12 },
    spec: DEFAULT_SPEC,
    curPage: 0,
    selTile: -1,       // selected tile on the current page (-1 = none -> palette)
    entities: [],
    entIndex: {},      // id -> entity (for O(1) lookup)
    domainFilter: "all",
    search: "",
    dirty: false,
    resizeHooked: false,
  };

  // ---- helpers -------------------------------------------------------------
  function getJSON(url) {
    return fetch(url).then(function (r) {
      if (!r.ok) throw new Error("HTTP " + r.status);
      return r.json();
    });
  }
  function postJSON(url, body) {
    return fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    }).then(function (r) {
      if (!r.ok) throw new Error("HTTP " + r.status);
      return r.json();
    });
  }
  function toast(msg, isErr) {
    toastEl.textContent = msg;
    toastEl.className = "toast show" + (isErr ? " err" : "");
    setTimeout(function () { toastEl.className = "toast"; }, 2200);
  }
  function el(tag, attrs, children) {
    var e = document.createElement(tag);
    if (attrs) Object.keys(attrs).forEach(function (k) {
      if (k === "class") e.className = attrs[k];
      else if (k === "text") e.textContent = attrs[k];
      else e.setAttribute(k, attrs[k]);
    });
    (children || []).forEach(function (c) { if (c) e.appendChild(c); });
    return e;
  }
  function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }

  // ---- color (mirror LVGL lv_color_mix) ------------------------------------
  function hexToRgb(h) {
    h = (h || "#000000").replace("#", "");
    return [parseInt(h.substr(0, 2), 16), parseInt(h.substr(2, 2), 16), parseInt(h.substr(4, 2), 16)];
  }
  function rgbToHex(r, g, b) {
    return "#" + [r, g, b].map(function (x) {
      return ("0" + Math.round(clamp(x, 0, 255)).toString(16)).slice(-2);
    }).join("");
  }
  // lv_color_mix(a, b, ratio) = a*ratio/255 + b*(255-ratio)/255.
  function colorMix(aHex, bHex, ratio) {
    var a = hexToRgb(aHex), b = hexToRgb(bHex), k = ratio / 255;
    return rgbToHex(a[0] * k + b[0] * (1 - k), a[1] * k + b[1] * (1 - k), a[2] * k + b[2] * (1 - k));
  }

  // ---- MDI sprite (injected once; #mdi-<name> referenced via <use>) ---------
  function loadSprite() {
    return fetch("/mdi-sprite.svg").then(function (r) { return r.text(); })
      .then(function (svg) {
        var holder = el("div");
        holder.style.display = "none";
        holder.innerHTML = svg;
        document.body.insertBefore(holder, document.body.firstChild);
      })
      .catch(function () { /* icons degrade to empty; editor still works */ });
  }
  function hasIcon(name) { return !!document.getElementById("mdi-" + name); }
  function iconEl(name, sizePx, color) {
    var svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("width", sizePx);
    svg.setAttribute("height", sizePx);
    svg.setAttribute("viewBox", "0 0 24 24");
    var use = document.createElementNS("http://www.w3.org/2000/svg", "use");
    use.setAttribute("href", "#mdi-" + name);
    svg.appendChild(use);
    svg.style.color = color;  // path uses fill="currentColor"
    return svg;
  }

  // ---- bootstrap -----------------------------------------------------------
  function init() {
    if (!state.resizeHooked) {
      state.resizeHooked = true;
      window.addEventListener("resize", layoutCanvas);
      window.addEventListener("beforeunload", function (e) {
        if (state.dirty) { e.preventDefault(); e.returnValue = ""; }
      });
    }
    Promise.all([
      getJSON("/api/config").catch(function () { return { ha: {}, auth: {} }; }),
      getJSON("/api/layout").catch(function () { return { pages: [] }; }),
      getJSON("/api/device").catch(function () { return DEFAULT_SPEC; }),
      loadSprite(),
    ]).then(function (res) {
      state.cfg = res[0] || { ha: {}, auth: {} };
      state.layout = res[1] || { pages: [] };
      if (!state.layout.pages) state.layout.pages = [];
      if (state.layout.maxPages == null) state.layout.maxPages = 8;
      if (state.layout.maxTilesPerPage == null) state.layout.maxTilesPerPage = 12;
      state.spec = res[2] || DEFAULT_SPEC;
      showView("dashboard");
    }).catch(function (e) {
      app.innerHTML = '<div class="empty">Failed to load: ' + e.message + "</div>";
    });
  }

  document.querySelectorAll(".tab").forEach(function (t) {
    t.addEventListener("click", function () {
      var target = t.getAttribute("data-view");
      // Guard against losing unsaved layout edits when leaving the dashboard.
      if (target === "settings" && state.dirty &&
          !confirm("You have unsaved layout changes. Leave anyway?")) return;
      document.querySelectorAll(".tab").forEach(function (x) { x.classList.remove("active"); });
      t.classList.add("active");
      showView(target);
    });
  });

  function showView(name) {
    app.innerHTML = "";
    if (name === "settings") renderSettings();
    else renderDashboard();
  }

  // ---- settings view -------------------------------------------------------
  function renderSettings() {
    var ha = state.cfg.ha || {};
    var auth = state.cfg.auth || {};

    var host = el("input", { type: "text", value: ha.host || "" });
    var port = el("input", { type: "number", value: ha.port || 8123 });
    var token = el("input", {
      type: "password", autocomplete: "off",
      placeholder: ha.hasToken ? "Saved — leave blank to keep" : "Paste long-lived token",
    });
    var tls = el("input", { type: "checkbox" });
    tls.checked = !!ha.useTls;

    var haCard = el("div", { class: "card" }, [
      el("h2", { text: "Home Assistant" }),
      field("Host / IP", host),
      field("Port", port),
      field("Long-lived token", token),
      el("label", { class: "switch" }, [tls, el("span", { text: "Use HTTPS / WSS (TLS)" })]),
    ]);
    var haSave = el("button", { class: "btn", text: "Save connection" });
    haSave.addEventListener("click", function () {
      var body = { host: host.value.trim(), port: parseInt(port.value, 10) || 8123, useTls: tls.checked };
      if (token.value.trim()) body.token = token.value.trim();
      postJSON("/api/config/ha", body)
        .then(function () { token.value = ""; toast("Connection saved"); return refreshConfig(); })
        .catch(function (e) { toast("Save failed: " + e.message, true); });
    });
    haCard.appendChild(el("div", { class: "actions" }, [haSave]));

    // Auth.
    var aEn = el("input", { type: "checkbox" });
    aEn.checked = !!auth.enabled;
    var aUser = el("input", { type: "text", value: auth.user || "" });
    var aPass = el("input", {
      type: "password", autocomplete: "new-password",
      placeholder: auth.enabled ? "Leave blank to keep current" : "Set a password",
    });
    var authCard = el("div", { class: "card" }, [
      el("h2", { text: "Config access (login)" }),
      el("p", { class: "hint", text: "Protect this configuration interface with a username and password. Disabled until set." }),
      el("label", { class: "switch" }, [aEn, el("span", { text: "Require login" })]),
      field("Username", aUser),
      field("Password", aPass),
    ]);
    var aSave = el("button", { class: "btn", text: "Save login" });
    aSave.addEventListener("click", function () {
      postJSON("/api/auth", { enabled: aEn.checked, user: aUser.value.trim(), password: aPass.value })
        .then(function () { aPass.value = ""; toast("Login settings saved"); return refreshConfig(); })
        .catch(function (e) { toast("Save failed: " + e.message, true); });
    });
    authCard.appendChild(el("div", { class: "actions" }, [aSave]));

    // Display (orientation + grid density). Geometry comes from /api/device.
    var dsp = state.spec || DEFAULT_SPEC;
    var orient = el("select", {});
    [["Portrait", 0], ["Landscape", 1], ["Portrait (flipped)", 2], ["Landscape (flipped)", 3]]
      .forEach(function (o) {
        var opt = el("option", { value: String(o[1]), text: o[0] });
        if ((dsp.rotation || 0) === o[1]) opt.selected = true;
        orient.appendChild(opt);
      });
    var maxP = dsp.maxColsPortrait || 2;
    var maxL = dsp.maxColsLandscape || 3;
    var colsP = el("input", { type: "number", min: "1", max: String(maxP), value: dsp.colsPortrait || 2 });
    var colsL = el("input", { type: "number", min: "1", max: String(maxL), value: dsp.colsLandscape || 3 });

    var dspChildren = [
      el("h2", { text: "Display" }),
      field("Orientation", orient),
      field("Columns — portrait (max " + maxP + ")", colsP),
      field("Columns — landscape (max " + maxL + ")", colsL),
    ];
    var autoR = null;
    if (dsp.hasImu) {
      autoR = el("input", { type: "checkbox" });
      autoR.checked = !!dsp.autoRotate;
      dspChildren.push(el("label", { class: "switch" },
        [autoR, el("span", { text: "Auto-rotate (orientation sensor)" })]));
    }
    var dspCard = el("div", { class: "card" }, dspChildren);
    var dSave = el("button", { class: "btn", text: "Save display" });
    dSave.addEventListener("click", function () {
      var body = {
        rotation: parseInt(orient.value, 10) || 0,
        colsPortrait: clamp(parseInt(colsP.value, 10) || 2, 1, maxP),
        colsLandscape: clamp(parseInt(colsL.value, 10) || 3, 1, maxL),
      };
      if (autoR) body.autoRotate = autoR.checked;
      postJSON("/api/config/display", body)
        .then(function () { toast("Display saved"); return refreshDevice(); })
        .catch(function (e) { toast("Save failed: " + e.message, true); });
    });
    dspCard.appendChild(el("div", { class: "actions" }, [dSave]));

    // System.
    var ota = el("a", { href: "/update", text: "Update firmware (OTA) →" });
    var reset = el("button", { class: "btn danger", text: "Factory reset" });
    reset.addEventListener("click", function () {
      if (!confirm("Erase all settings and reboot?")) return;
      postJSON("/api/reset", {}).then(function () { toast("Resetting…"); })
        .catch(function (e) { toast("Failed: " + e.message, true); });
    });
    var sysCard = el("div", { class: "card" }, [
      el("h2", { text: "System" }),
      el("p", {}, [ota]),
      el("div", { class: "actions" }, [reset]),
    ]);

    app.appendChild(el("div", { class: "row" }, [
      el("div", { class: "col" }, [haCard, sysCard]),
      el("div", { class: "col" }, [dspCard, authCard]),
    ]));
  }

  function field(labelText, inputEl) {
    return el("label", { class: "field" }, [el("span", { text: labelText }), inputEl]);
  }
  function refreshConfig() {
    return getJSON("/api/config").then(function (c) { state.cfg = c; });
  }

  // Re-fetch the device spec after a display change so the dashboard preview
  // reflects the new orientation / column count.
  function refreshDevice() {
    return getJSON("/api/device").then(function (d) { if (d) state.spec = d; });
  }

  // ---- dashboard editor ----------------------------------------------------
  function pages() { return state.layout.pages; }
  function curPage() { return pages()[state.curPage]; }

  function renderDashboard() {
    app.innerHTML = "";
    if (state.curPage >= pages().length) state.curPage = Math.max(0, pages().length - 1);

    var editor = el("div", { class: "editor" });
    if (!pages().length) {  // clean empty state — no page bar / save until a page exists
      editor.appendChild(buildEmptyState());
      app.appendChild(editor);
      return;
    }

    editor.appendChild(buildPageBar());
    var workspace = el("div", { class: "workspace" });
    var canvasWrap = el("div", { class: "canvaswrap" });
    canvasWrap.appendChild(buildFrameBox(curPage()));
    workspace.appendChild(canvasWrap);
    workspace.appendChild(el("div", { class: "sidepanel" },
      [state.selTile >= 0 ? buildInspector() : buildPalette()]));
    editor.appendChild(workspace);
    app.appendChild(editor);

    layoutCanvas();
    setDirtyUi();
    if (state.selTile < 0) loadCatalog();
  }

  // --- page bar (chips + add + save) + active-page tools --------------------
  function buildPageBar() {
    var chips = el("div", { class: "pagechips" });
    pages().forEach(function (p, i) {
      var c = el("button", {
        class: "chip" + (i === state.curPage ? " active" : ""),
        text: p.title && p.title.length ? p.title : "Page " + (i + 1),
      });
      c.addEventListener("click", function () { switchPage(i); });
      chips.appendChild(c);
    });
    if (pages().length < state.layout.maxPages) {
      var add = el("button", { class: "chip add", text: "+ Add page" });
      add.addEventListener("click", addPage);
      chips.appendChild(add);
    }

    var saveBtn = el("button", { class: "btn", id: "savebtn", text: "Save layout" });
    saveBtn.addEventListener("click", saveLayout);
    var save = el("div", { class: "savewrap" }, [
      el("span", { class: "dirtydot", id: "dirtydot" }), saveBtn,
    ]);

    var bar = el("div", { class: "pagebar" }, [chips, save]);
    bar.appendChild(buildPageTools());
    return bar;
  }

  function buildPageTools() {
    var p = curPage();
    var left = el("button", { class: "iconbtn", text: "‹", title: "Move page left" });
    left.disabled = state.curPage === 0;
    left.addEventListener("click", function () { movePage(state.curPage, state.curPage - 1); });

    var rename = el("input", { class: "rename", type: "text", value: p.title || "",
      placeholder: "Page title (blank = no on-screen title)" });
    rename.addEventListener("input", function () {
      p.title = rename.value; markDirty();
      refreshChip(); refreshCanvas();
    });

    var right = el("button", { class: "iconbtn", text: "›", title: "Move page right" });
    right.disabled = state.curPage >= pages().length - 1;
    right.addEventListener("click", function () { movePage(state.curPage, state.curPage + 1); });

    var del = el("button", { class: "iconbtn danger", text: "🗑", title: "Delete page" });
    del.addEventListener("click", deletePage);

    return el("div", { class: "pagetools" }, [left, rename, right, del]);
  }

  function refreshChip() {
    var chip = app.querySelectorAll(".pagechips .chip")[state.curPage];
    if (chip) chip.textContent = curPage().title && curPage().title.length
      ? curPage().title : "Page " + (state.curPage + 1);
  }

  function buildEmptyState() {
    var addBtn = el("button", { class: "btn", text: "+ Add your first page" });
    addBtn.addEventListener("click", addPage);
    return el("div", { class: "emptystate" }, [
      el("div", { class: "emptyframe" }),
      el("h2", { text: "Your dashboard is empty" }),
      el("p", { class: "hint", text: "Create a page, then click entities to place them on the screen preview." }),
      addBtn,
    ]);
  }

  // --- WYSIWYG canvas -------------------------------------------------------
  function buildFrameBox(page) {
    var box = el("div", { class: "framebox" });
    var scaler = el("div", { class: "framescaler" });
    scaler.appendChild(buildFrame(page));
    box.appendChild(scaler);
    return box;
  }

  // The device screen, drawn at 1:1 device px; the parent scaler transform-scales
  // the whole thing, so every child (fonts, paddings, radii) scales together.
  function buildFrame(page) {
    var s = state.spec;
    var packed = packTiles(page.tiles, s.gridCols);
    // The preview grows past the real device height so every tile stays reachable
    // for editing; a dashed line marks where the actual screen ends.
    var gridRows = packed.rows || 1;
    var gridH = gridRows * s.rowHeightPx + Math.max(0, gridRows - 1) * s.rowGapPx;
    var deviceTvH = s.screenH - s.topBarHeightPx - s.dotsBarHeightPx;
    var tvH = Math.max(deviceTvH, gridH + 2 * s.pagePadPx);
    var frameH = s.topBarHeightPx + tvH + s.dotsBarHeightPx;

    var frame = el("div", { class: "pvframe" });
    frame.style.width = s.screenW + "px";
    frame.style.height = frameH + "px";
    frame.style.background = s.screenBg;

    // Top bar: centered title on the same line as the HA status icon (right).
    var topbar = el("div", { class: "pvtopbar" });
    topbar.style.height = s.topBarHeightPx + "px";
    var ti = el("div", { class: "pvtitle", text: page.title || "" });
    ti.style.color = s.nameColor;
    ti.style.fontSize = s.titleFontPx + "px";
    topbar.appendChild(ti);
    var status = iconEl("home-assistant", s.iconSizePx, s.primary);
    status.classList.add("pvstatus");
    topbar.appendChild(status);
    frame.appendChild(topbar);

    var tv = el("div", { class: "pvtileview" });
    tv.style.top = s.topBarHeightPx + "px";
    tv.style.height = tvH + "px";
    tv.style.padding = s.pagePadPx + "px";
    frame.appendChild(tv);

    // Center the content when it doesn't fill the grid: build only the used columns
    // at the normal cell width and center that block (mirrors the device buildPage,
    // e.g. a 2-wide tile on a 3-column landscape grid).
    var usedCols = 1;
    packed.placements.forEach(function (p) { if (p.col + p.w > usedCols) usedCols = p.col + p.w; });
    if (usedCols > s.gridCols) usedCols = s.gridCols;
    var centerCols = usedCols < s.gridCols;
    var singleTile = page.tiles.length === 1;
    var cellW = (s.screenW - 2 * s.pagePadPx - (s.gridCols - 1) * s.colGapPx) / s.gridCols;

    var content = el("div", { class: "pvcontent" });
    content.style.display = "grid";
    content.style.gridTemplateColumns = centerCols
      ? ("repeat(" + usedCols + "," + cellW + "px)")
      : ("repeat(" + s.gridCols + ",1fr)");
    content.style.gridAutoRows = s.rowHeightPx + "px";
    content.style.columnGap = s.colGapPx + "px";
    content.style.rowGap = s.rowGapPx + "px";
    if (centerCols) content.style.justifyContent = "center";
    if (singleTile) content.style.alignContent = "center";
    tv.appendChild(content);

    page.tiles.forEach(function (t, i) {
      content.appendChild(buildTile(t, i, packed.placements[i], page));
    });

    if (!page.tiles.length) {
      var hint = el("div", { class: "pvhint", text: "Click an entity to place it here" });
      hint.style.color = s.textMuted;
      tv.appendChild(hint);
    }

    // Bottom bar: one navigation dot per page (only with more than one page).
    var all = pages();
    if (all.length > 1) {
      var dots = el("div", { class: "pvdots" });
      all.forEach(function (_, i) {
        var d = el("div", { class: "pvdot" + (i === state.curPage ? " active" : "") });
        d.style.background = i === state.curPage ? s.primary : s.iconOff;
        dots.appendChild(d);
      });
      frame.appendChild(dots);
    }

    // Dashed marker where the real screen ends (tiles below scroll on the device).
    if (frameH > s.screenH) {
      var fold = el("div", { class: "pvfold" });
      fold.style.top = s.screenH + "px";
      frame.appendChild(fold);
    }
    return frame;
  }

  // First-fit packing onto the grid — a verbatim port of the device packTiles(),
  // so the preview shows tiles in the exact cells the panel will compute.
  function packTiles(tiles, cols) {
    cols = cols || 2;
    var occ = [];
    function isFree(r, c, w, h) {
      for (var dr = 0; dr < h; dr++) for (var dc = 0; dc < w; dc++) {
        var rr = r + dr, cc = c + dc;
        if (cc >= cols) return false;
        if (rr < occ.length && occ[rr][cc]) return false;
      }
      return true;
    }
    var out = [];
    (tiles || []).forEach(function (t) {
      var w = clamp(t.w || 1, 1, cols), h = clamp(t.h || 1, 1, 2);
      var pr = 0, pc = 0, placed = false;
      for (var r = 0; !placed && r < 256; r++) {
        for (var c = 0; c + w <= cols; c++) {
          if (isFree(r, c, w, h)) { pr = r; pc = c; placed = true; break; }
        }
      }
      while (occ.length < pr + h) { var row = []; for (var i = 0; i < cols; i++) row.push(false); occ.push(row); }
      for (var dr = 0; dr < h; dr++) for (var dc = 0; dc < w; dc++) occ[pr + dr][pc + dc] = true;
      out.push({ col: pc, row: pr, w: w, h: h });
    });
    return { placements: out, rows: occ.length };
  }

  // One tile, mirroring the device's buildTile/applyVisual (active "on" look).
  function buildTile(tile, idx, place, page) {
    var s = state.spec;
    var domain = (tile.id || "").split(".")[0];
    var ent = state.entIndex[tile.id];
    var accent = accentColor(domain);                          // icon
    var bg = colorMix(tileTintColor(domain), s.tileBg, s.activeMix);  // calmer bg tint

    var node = el("div", { class: "pvtile" + (idx === state.selTile ? " sel" : ""), "data-idx": String(idx) });
    node.style.gridColumn = (place.col + 1) + " / span " + place.w;
    node.style.gridRow = (place.row + 1) + " / span " + place.h;
    node.style.background = bg;
    node.style.borderRadius = s.tileRadiusPx + "px";
    node.style.padding = s.tilePadPx + "px";
    node.style.gap = s.tileGapPx + "px";

    // Entity icon pinned to the top-left corner (inset by the tile padding).
    var icon = iconEl(resolveIcon(domain, ent && ent.icon), s.iconSizePx, accent);
    icon.classList.add("pvicon");
    icon.style.top = s.tilePadPx + "px";
    icon.style.left = s.tilePadPx + "px";
    node.appendChild(icon);

    // Value/state: on short tiles it sits on the icon's row pushed right; on tall
    // tiles it's centered on the whole tile.
    var valuePx = valueFontFor(place.w, place.h);
    var cW = tileContentWidth(place.w);
    var val = el("div", { class: "pvvalue", text: placeholderState(domain) });
    val.style.fontSize = valuePx + "px";
    val.style.color = s.textMuted;
    if (place.h === 1) {
      var top = el("div", { class: "pvtoprow" });
      top.style.height = s.iconSizePx + "px";
      val.style.maxWidth = (cW - s.iconSizePx - s.tileGapPx) + "px";
      val.style.textAlign = "right";
      top.appendChild(val);
      node.appendChild(top);
    } else {
      var valwrap = el("div", { class: "pvvalwrap" });
      valwrap.appendChild(val);
      node.appendChild(valwrap);
    }

    // Entity name at the bottom: font shrinks to fit (below the icon on h=1, two
    // lines on h≥2); flag (and warn) if it overflows even at 14px.
    var nameText = tile.label || (ent ? ent.name : tile.id);
    var name = el("div", { class: "pvname" });
    name.textContent = nameText;
    name.style.fontSize = nameFitFont(nameText, cW, s.nameFontPx, nameMaxHeight(place.h)) + "px";
    name.style.color = s.nameColor;
    node.appendChild(name);
    if (nameTooLong(nameText, place.w, place.h)) node.classList.add("toolong");

    if (domain === "light") {
      var sl = el("div", { class: "pvslider" });
      sl.style.height = s.sliderHeightPx + "px";
      sl.style.borderRadius = (s.sliderHeightPx / 2) + "px";
      var fill = el("div", { class: "pvsliderfill" });
      fill.style.background = accent;
      sl.appendChild(fill);
      node.appendChild(sl);
    }

    attachTileDrag(node, idx, page);
    return node;
  }

  function accentColor(domain) {
    var s = state.spec;
    // Icon/slider ON look: lights & switches go amber, sensors stay primary blue.
    if (domain === "light" || domain === "switch") return s.amber;
    if (domain === "sensor") return s.primary;
    return s.iconOff;
  }
  // Tile-background tint: calmer pre-amber palette (switches primary, lights amber).
  function tileTintColor(domain) {
    var s = state.spec;
    if (domain === "light") return s.amber;
    return s.primary;  // switches & co
  }
  // Mirror the device resolveGlyph fallback chain (device_class isn't in the
  // catalog, so non-iconed sensors fall back to the generic gauge).
  function resolveIcon(domain, iconAttr) {
    if (iconAttr) {
      var n = String(iconAttr).replace(/^mdi:/, "");
      if (hasIcon(n)) return n;
    }
    if (domain === "light") return "lightbulb";
    if (domain === "switch") return "power-socket-eu";
    if (domain === "sensor") return "gauge";
    return "help-circle-outline";
  }
  function placeholderState(domain) {
    if (domain === "light") return "On · 60%";
    if (domain === "switch") return "On";
    if (domain === "sensor") return "21.5 °C";
    return "—";
  }
  // Mirror ui::valueFontFor — value text grows with the tile (wide gets largest).
  function valueFontFor(w, h) {
    var s = state.spec;
    if (w >= 2 && h >= 2) return s.valueFontLargePx;
    if (w >= 2 || h >= 2) return s.valueFontMedPx;
    return s.valueFontSmallPx;
  }

  // Content width/height (device px) of a tile spanning w×h cells — mirrors the
  // device geometry so the name-fit decision matches the panel.
  function tileContentWidth(w) {
    var s = state.spec;
    var cellW = (s.screenW - 2 * s.pagePadPx - (s.gridCols - 1) * s.colGapPx) / s.gridCols;
    return w * cellW + (w - 1) * s.colGapPx - 2 * s.tilePadPx;
  }
  function tileContentHeight(h) {
    var s = state.spec;
    return h * s.rowHeightPx + (h - 1) * s.rowGapPx - 2 * s.tilePadPx;
  }
  // Wrapped pixel height of `text` at `fontPx` within `widthPx` (hidden probe).
  var _measureEl = null;
  function measureTextHeight(text, widthPx, fontPx) {
    if (!_measureEl) {
      _measureEl = el("div");
      _measureEl.style.cssText =
        "position:absolute;visibility:hidden;left:-9999px;top:0;font-weight:600;word-break:break-word;";
      document.body.appendChild(_measureEl);
    }
    _measureEl.style.width = widthPx + "px";
    _measureEl.style.fontSize = fontPx + "px";
    _measureEl.style.lineHeight = "1.15";
    _measureEl.textContent = text || "";
    return _measureEl.scrollHeight;
  }
  // Max height available for the name: below the icon row on h=1, two lines on h≥2.
  function nameMaxHeight(h) {
    return h === 1 ? (tileContentHeight(1) - state.spec.iconSizePx)
                   : Math.round(state.spec.nameFontPx * 1.15 * 2);
  }
  // Mirror ui::nameFontFor — largest font (<= base) whose wrapped name fits maxH.
  function nameFitFont(text, widthPx, basePx, maxHPx) {
    if (measureTextHeight(text, widthPx, basePx) <= maxHPx) return basePx;
    return 14;
  }
  // Name that overflows its area even at the smallest (14px) font.
  function nameTooLong(text, w, h) {
    return measureTextHeight(text, tileContentWidth(w), 14) > nameMaxHeight(h);
  }

  // Scale the device frame to the canvas column width (recomputed on resize).
  function layoutCanvas() {
    var wrap = app.querySelector(".canvaswrap");
    if (!wrap || !state.spec) return;
    var box = wrap.querySelector(".framebox");
    var scaler = wrap.querySelector(".framescaler");
    if (!box || !scaler) return;
    var sc = wrap.clientWidth / state.spec.screenW;
    state.canvasScale = sc;  // used to size the drag ghost
    scaler.style.transformOrigin = "top left";
    scaler.style.transform = "scale(" + sc + ")";
    var pvframe = box.querySelector(".pvframe");
    var frameH = pvframe && pvframe.style.height ? parseFloat(pvframe.style.height) : state.spec.screenH;
    box.style.width = Math.round(state.spec.screenW * sc) + "px";
    box.style.height = Math.round(frameH * sc) + "px";
  }

  function refreshCanvas() {
    var wrap = app.querySelector(".canvaswrap");
    if (!wrap) return;
    wrap.innerHTML = "";
    wrap.appendChild(buildFrameBox(curPage()));
    layoutCanvas();
  }

  // Drag a tile to reorder: a ghost follows the cursor and the tiles re-pack live
  // so you can see where it will land. A plain click (no movement) selects it.
  function attachTileDrag(node, idx, page) {
    node.addEventListener("pointerdown", function (e) {
      if (e.button != null && e.button !== 0) return;
      var startX = e.clientX, startY = e.clientY, moved = false, curIdx = idx;
      var rect = node.getBoundingClientRect();
      var offX = e.clientX - rect.left, offY = e.clientY - rect.top;
      var ghost = null;

      function makeGhost() {
        ghost = node.cloneNode(true);
        ghost.classList.add("pvghost");
        ghost.classList.remove("sel", "dragging");
        ghost.style.width = node.offsetWidth + "px";    // device px, scaled below
        ghost.style.height = node.offsetHeight + "px";
        ghost.style.transform = "scale(" + (state.canvasScale || 1) + ")";
        document.body.appendChild(ghost);
      }
      function markDragged() {
        var dn = app.querySelector('.pvtile[data-idx="' + curIdx + '"]');
        if (dn) dn.classList.add("dragging");
      }
      function tileIdxAt(x, y) {
        var t = document.elementFromPoint(x, y);  // ghost is pointer-events:none
        t = t && t.closest ? t.closest(".pvtile") : null;
        if (!t) return -1;
        var di = parseInt(t.getAttribute("data-idx"), 10);
        return isNaN(di) ? -1 : di;
      }
      function mv(ev) {
        if (!moved) {
          if (Math.abs(ev.clientX - startX) + Math.abs(ev.clientY - startY) <= 6) return;
          moved = true;
          makeGhost();
          markDragged();
        }
        ghost.style.left = (ev.clientX - offX) + "px";
        ghost.style.top = (ev.clientY - offY) + "px";
        var over = tileIdxAt(ev.clientX, ev.clientY);
        if (over >= 0 && over !== curIdx) {   // live re-pack so neighbours shift
          moveTile(page, curIdx, over);
          curIdx = over;
          refreshCanvas();
          markDragged();
        }
      }
      function up() {
        document.removeEventListener("pointermove", mv);
        document.removeEventListener("pointerup", up);
        if (ghost) ghost.remove();
        if (!moved) { selectTile(idx); return; }
        markDirty();
        state.selTile = -1;
        renderDashboard();
      }
      document.addEventListener("pointermove", mv);
      document.addEventListener("pointerup", up);
    });
  }

  function selectTile(idx) {
    state.selTile = idx;
    renderDashboard();
  }
  function deselect() {
    state.selTile = -1;
    renderDashboard();
  }

  // --- inspector (selected tile) -------------------------------------------
  function buildInspector() {
    var page = curPage();
    var tile = page.tiles[state.selTile];
    if (!tile) { state.selTile = -1; return buildPalette(); }
    var ent = state.entIndex[tile.id];
    var s = state.spec;

    var head = el("div", { class: "card" }, [
      el("h2", { text: "Tile" }),
      el("div", { class: "ename", text: (ent ? ent.name : tile.id) }),
      el("div", { class: "eid", text: tile.id }),
    ]);

    // Size: explicit shape buttons (w × h), highlight the current one.
    var sizes = [{ w: 1, h: 1 }, { w: 2, h: 1 }, { w: 1, h: 2 }, { w: 2, h: 2 }];
    var sizeBtns = el("div", { class: "sizebtns" });
    sizes.forEach(function (sz) {
      var active = (tile.w || 1) === sz.w && (tile.h || 1) === sz.h;
      var b = el("button", { class: "sizebtn" + (active ? " active" : "") });
      var swatch = el("div", { class: "sizeswatch" });
      swatch.style.width = (sz.w === 2 ? 34 : 16) + "px";
      swatch.style.height = (sz.h === 2 ? 26 : 13) + "px";
      b.appendChild(swatch);
      b.appendChild(el("span", { text: (sz.w === 2 ? "Full" : "Half") + " · " + (sz.h === 2 ? "Tall" : "Short") }));
      b.addEventListener("click", function () { setTileSize(tile, sz.w, sz.h); });
      sizeBtns.appendChild(b);
    });

    // Warning shown when the name can't fit two lines even at the smallest font.
    var warn = el("div", { class: "warnbox" });
    function refreshWarn(curName) {
      var bad = nameTooLong(curName, tile.w || 1, tile.h || 1);
      warn.textContent = bad
        ? "⚠ Name too long for this tile — set a shorter label below."
        : "";
      warn.style.display = bad ? "block" : "none";
    }

    // Label override.
    var label = el("input", { type: "text", value: tile.label || "",
      placeholder: ent ? ent.name : tile.id });
    label.addEventListener("input", function () {
      tile.label = label.value; markDirty();
      var curName = tile.label || (ent ? ent.name : tile.id);
      var pv = app.querySelectorAll(".pvtile")[state.selTile];
      if (pv) {
        var nm = pv.querySelector(".pvname");
        if (nm) {
          nm.textContent = curName;
          var cW = tileContentWidth(tile.w || 1);
          nm.style.fontSize = nameFitFont(curName, cW, state.spec.nameFontPx, nameMaxHeight(tile.h || 1)) + "px";
        }
        pv.classList.toggle("toolong", nameTooLong(curName, tile.w || 1, tile.h || 1));
      }
      refreshWarn(curName);
    });
    refreshWarn(tile.label || (ent ? ent.name : tile.id));
    var resetLbl = el("button", { class: "btn ghost", text: "Reset to HA name" });
    resetLbl.addEventListener("click", function () {
      tile.label = ""; markDirty(); renderDashboard();
    });

    var remove = el("button", { class: "btn danger", text: "Remove tile" });
    remove.addEventListener("click", function () { removeTile(state.selTile); });
    var done = el("button", { class: "btn ghost", text: "Done" });
    done.addEventListener("click", deselect);

    var body = el("div", { class: "card" }, [
      el("h2", { text: "Size" }),
      sizeBtns,
      warn,
      field("Label", label),
      el("div", { class: "actions" }, [resetLbl]),
    ]);
    var foot = el("div", { class: "card" }, [
      el("div", { class: "actions" }, [done, remove]),
    ]);
    return el("div", {}, [head, body, foot]);
  }

  // --- palette (default panel) ---------------------------------------------
  function buildPalette() {
    var chipsDefs = [
      { k: "all", label: "All" },
      { k: "light", label: "Lights" },
      { k: "switch", label: "Switches" },
      { k: "sensor", label: "Sensors" },
    ];
    var chips = el("div", { class: "domainchips" });
    chipsDefs.forEach(function (d) {
      var c = el("button", { class: "echip" + (state.domainFilter === d.k ? " active" : ""), text: d.label });
      c.addEventListener("click", function () {
        state.domainFilter = d.k;
        chips.querySelectorAll(".echip").forEach(function (x) { x.classList.remove("active"); });
        c.classList.add("active");
        renderEntityList();
      });
      chips.appendChild(c);
    });

    var search = el("input", { class: "search", type: "text", placeholder: "Search entities…", value: state.search });
    search.addEventListener("input", function () { state.search = search.value.trim(); renderEntityList(); });

    var list = el("div", { class: "elist", id: "elist" });
    return el("div", { class: "card palette" }, [
      el("h2", { text: "Entities" }),
      chips, search, list,
    ]);
  }

  function loadCatalog() {
    var listEl = document.getElementById("elist");
    if (!listEl) return;
    if (state.entities && state.entities.length) { renderEntityList(); return; }
    getJSON("/api/entities").then(function (list) {
      state.entities = list || [];
      state.entIndex = {};
      state.entities.forEach(function (en) { state.entIndex[en.id] = en; });
      renderEntityList();
      refreshCanvas();  // tiles now resolve friendly names + real HA icons
    }).catch(function () {
      listEl.innerHTML = "";
      listEl.appendChild(el("div", { class: "empty", text: "No entities. Check the HA connection." }));
    });
  }

  function renderEntityList() {
    var listEl = document.getElementById("elist");
    if (!listEl) return;
    var f = state.search.toLowerCase();
    var dom = state.domainFilter;
    var placed = {};
    (curPage().tiles || []).forEach(function (t) { placed[t.id] = true; });

    var items = state.entities.filter(function (en) {
      if (dom !== "all" && en.domain !== dom) return false;
      return !f || (en.name + " " + en.id).toLowerCase().indexOf(f) >= 0;
    });
    listEl.innerHTML = "";
    if (!items.length) {
      listEl.appendChild(el("div", { class: "empty",
        text: state.entities.length ? "No match." : "No entities. Check the HA connection." }));
      return;
    }
    items.slice(0, 200).forEach(function (en) {  // cap DOM nodes for responsiveness
      var isPlaced = !!placed[en.id];
      var item = el("div", { class: "eitem" + (isPlaced ? " placed" : "") });
      item.appendChild(iconEl(resolveIcon(en.domain, en.icon), 22, accentColor(en.domain)));
      item.appendChild(el("div", { class: "en" }, [
        el("b", { text: en.name }), el("small", { text: en.id }),
      ]));
      // Placed -> a remove button; otherwise an add button.
      var btn = el("button", { class: "add" + (isPlaced ? " remove" : ""),
        text: isPlaced ? "−" : "+", title: isPlaced ? "Remove from page" : "Add to page" });
      btn.addEventListener("click", function (e) {
        e.stopPropagation();
        if (isPlaced) removeTileById(en.id); else addTile(en);
      });
      item.appendChild(btn);
      if (!isPlaced) item.addEventListener("click", function () { addTile(en); });
      listEl.appendChild(item);
    });
  }

  // --- mutations ------------------------------------------------------------
  function addTile(entity) {
    var page = curPage();
    if (page.tiles.length >= state.layout.maxTilesPerPage) {
      toast("Page is full (" + state.layout.maxTilesPerPage + " tiles max)", true);
      return;
    }
    if (page.tiles.some(function (t) { return t.id === entity.id; })) {
      toast("Already on this page", true);
      return;
    }
    page.tiles.push({ id: entity.id, label: "", w: 1, h: 1 });
    markDirty();
    refreshCanvas();
    renderEntityList();  // update the "placed" marker
    if (nameTooLong(entity.name || entity.id, 1, 1)) {
      toast("\"" + (entity.name || entity.id) + "\" is long — set a shorter label", true);
    }
  }
  function removeTile(idx) {
    curPage().tiles.splice(idx, 1);
    state.selTile = -1;
    markDirty();
    renderDashboard();
  }
  function removeTileById(id) {
    var i = (curPage().tiles || []).findIndex(function (t) { return t.id === id; });
    if (i >= 0) removeTile(i);
  }
  function moveTile(page, from, to) {
    if (from === to) return;
    var t = page.tiles.splice(from, 1)[0];
    page.tiles.splice(to, 0, t);
  }
  function setTileSize(tile, w, h) {
    tile.w = w; tile.h = h;
    markDirty();
    renderDashboard();
  }

  function switchPage(i) {
    state.curPage = i;
    state.selTile = -1;
    renderDashboard();
  }
  function addPage() {
    pages().push({ title: "", tiles: [] });
    state.curPage = pages().length - 1;
    state.selTile = -1;
    markDirty();
    renderDashboard();
  }
  function deletePage() {
    if (!confirm("Delete this page?")) return;
    pages().splice(state.curPage, 1);
    state.selTile = -1;
    markDirty();
    renderDashboard();
  }
  function movePage(from, to) {
    if (to < 0 || to >= pages().length) return;
    var p = pages().splice(from, 1)[0];
    pages().splice(to, 0, p);
    state.curPage = to;
    markDirty();
    renderDashboard();
  }

  // --- dirty + save ---------------------------------------------------------
  function markDirty() { state.dirty = true; setDirtyUi(); }
  function setDirtyUi() {
    var dot = document.getElementById("dirtydot");
    if (dot) dot.style.display = state.dirty ? "inline-block" : "none";
    var btn = document.getElementById("savebtn");
    if (btn) btn.classList.toggle("dirty", state.dirty);
  }

  function saveLayout() {
    // Trim the payload: omit empty labels and w/h == 1 (the device defaults to 1).
    var body = { pages: pages().map(function (p) {
      return { title: p.title || "", tiles: p.tiles.map(function (t) {
        var o = { id: t.id };
        if (t.label) o.label = t.label;
        if ((t.w || 1) !== 1) o.w = t.w;
        if ((t.h || 1) !== 1) o.h = t.h;
        return o;
      }) };
    }) };
    postJSON("/api/layout", body)
      .then(function () { state.dirty = false; setDirtyUi(); toast("Layout saved"); })
      .catch(function (e) { toast("Save failed: " + e.message, true); });
  }

  init();
})();
