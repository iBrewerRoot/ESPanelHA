/* HA Control Panel — config SPA (vanilla JS, no framework so it gzips small).
   Talks to the device JSON REST API; edits a multi-page dashboard layout with
   drag-to-reorder, drag-from-palette-to-add, and drag handles to resize tiles. */
(function () {
  "use strict";

  var app = document.getElementById("app");
  var toastEl = document.getElementById("toast");

  var state = {
    cfg: { ha: {}, auth: {} },
    layout: { pages: [], maxPages: 8, maxTilesPerPage: 12 },
    curPage: 0,
    entities: [],
  };
  var drag = null; // { kind: 'reorder'|'add', ... }

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
      else if (k === "html") e.innerHTML = attrs[k];
      else e.setAttribute(k, attrs[k]);
    });
    (children || []).forEach(function (c) { if (c) e.appendChild(c); });
    return e;
  }

  // ---- bootstrap -----------------------------------------------------------
  function init() {
    Promise.all([getJSON("/api/config"), getJSON("/api/layout")])
      .then(function (res) {
        state.cfg = res[0] || { ha: {}, auth: {} };
        state.layout = res[1] || { pages: [] };
        if (!state.layout.pages) state.layout.pages = [];
        showView("dashboard");
      })
      .catch(function (e) {
        app.innerHTML = '<div class="empty">Failed to load: ' + e.message + "</div>";
      });
  }

  document.querySelectorAll(".tab").forEach(function (t) {
    t.addEventListener("click", function () {
      document.querySelectorAll(".tab").forEach(function (x) { x.classList.remove("active"); });
      t.classList.add("active");
      showView(t.getAttribute("data-view"));
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
      el("div", { class: "col" }, [authCard]),
    ]));
  }

  function field(labelText, inputEl) {
    return el("label", { class: "field" }, [el("span", { text: labelText }), inputEl]);
  }
  function refreshConfig() {
    return getJSON("/api/config").then(function (c) { state.cfg = c; });
  }

  // ---- dashboard editor ----------------------------------------------------
  function renderDashboard() {
    var pages = state.layout.pages;
    if (state.curPage >= pages.length) state.curPage = Math.max(0, pages.length - 1);

    // Page tabs.
    var tabs = el("div", { class: "pagetabs" });
    pages.forEach(function (p, i) {
      var t = el("button", { class: "pagetab" + (i === state.curPage ? " active" : ""),
        text: p.title && p.title.length ? p.title : "Page " + (i + 1) });
      t.addEventListener("click", function () { state.curPage = i; renderDashboard(); });
      tabs.appendChild(t);
    });
    if (pages.length < state.layout.maxPages) {
      var add = el("button", { class: "pagetab add", text: "+ Add page" });
      add.addEventListener("click", function () {
        pages.push({ title: "", tiles: [] });
        state.curPage = pages.length - 1;
        renderDashboard();
      });
      tabs.appendChild(add);
    }

    var content;
    if (!pages.length) {
      content = el("div", { class: "empty", text: "No pages yet. Add one to get started." });
    } else {
      content = renderPageEditor(pages[state.curPage]);
    }

    var saveBar = el("div", { class: "actions" }, [
      (function () {
        var b = el("button", { class: "btn", text: "Save layout" });
        b.addEventListener("click", saveLayout);
        return b;
      })(),
    ]);

    app.appendChild(el("div", {}, [tabs, content, saveBar]));
    if (pages.length) loadCatalog();
  }

  function renderPageEditor(page) {
    // Title row + delete page.
    var title = el("input", { type: "text", value: page.title || "", placeholder: "Page title (blank = no on-screen title)" });
    title.addEventListener("input", function () { page.title = title.value; });
    var delPage = el("button", { class: "btn ghost", text: "Delete page" });
    delPage.addEventListener("click", function () {
      if (!confirm("Delete this page?")) return;
      state.layout.pages.splice(state.curPage, 1);
      renderDashboard();
    });
    var titleCard = el("div", { class: "card" }, [
      el("h2", { text: "Page" }),
      field("Title", title),
      el("div", { class: "actions" }, [delPage]),
    ]);

    // Grid editor.
    var grid = el("div", { class: "grid" });
    grid.id = "grid";
    renderTiles(grid, page);
    // Drop target for palette adds.
    grid.addEventListener("dragover", function (e) {
      if (drag && drag.kind === "add") { e.preventDefault(); grid.classList.add("drop"); }
    });
    grid.addEventListener("dragleave", function () { grid.classList.remove("drop"); });
    grid.addEventListener("drop", function (e) {
      grid.classList.remove("drop");
      if (drag && drag.kind === "add") { e.preventDefault(); addTile(page, drag.entity); }
    });
    var gridCard = el("div", { class: "card" }, [
      el("h2", { text: "Tiles" }),
      el("p", { class: "hint", text: "Drag entities from the right to add. Drag tiles to reorder, drag the blue handles to resize (½ / full width, single / double height)." }),
      grid,
    ]);

    // Palette.
    var search = el("input", { class: "search", type: "text", placeholder: "Search entities…" });
    var listEl = el("div", { class: "elist" });
    listEl.id = "elist";
    search.addEventListener("input", function () { renderEntityList(search.value.trim()); });
    var paletteCard = el("div", { class: "card palette" }, [
      el("h2", { text: "Entities" }),
      search, listEl,
    ]);

    return el("div", { class: "row" }, [
      el("div", { class: "col" }, [titleCard, gridCard]),
      el("div", { class: "col" }, [paletteCard]),
    ]);
  }

  function renderTiles(grid, page) {
    grid.innerHTML = "";
    page.tiles.forEach(function (tile, idx) {
      var domain = (tile.id || "").split(".")[0];
      var node = el("div", { class: "tile", draggable: "true" });
      node.style.gridColumn = "span " + (tile.w || 1);
      node.style.gridRow = "span " + (tile.h || 1);
      node.appendChild(el("div", { class: "tname", text: tile.label && tile.label.length ? tile.label : tile.id }));
      node.appendChild(el("div", { class: "tdom", text: domain }));
      node.appendChild(el("div", { class: "tsize", text: (tile.w || 1) + "×" + (tile.h || 1) }));
      var del = el("button", { class: "del", text: "×" });
      del.addEventListener("click", function (e) {
        e.stopPropagation();
        page.tiles.splice(idx, 1);
        renderTiles(grid, page);
      });
      node.appendChild(del);

      var rsW = el("div", { class: "rs rs-w", title: "Width" });
      var rsH = el("div", { class: "rs rs-h", title: "Height" });
      node.appendChild(rsW);
      node.appendChild(rsH);
      attachResize(rsW, "w", tile, grid, page);
      attachResize(rsH, "h", tile, grid, page);

      // Reorder via native DnD.
      node.addEventListener("dragstart", function (e) {
        drag = { kind: "reorder", from: idx };
        node.classList.add("dragging");
        e.dataTransfer.effectAllowed = "move";
      });
      node.addEventListener("dragend", function () { node.classList.remove("dragging"); drag = null; });
      node.addEventListener("dragover", function (e) {
        if (drag && drag.kind === "reorder") e.preventDefault();
      });
      node.addEventListener("drop", function (e) {
        if (drag && drag.kind === "reorder") {
          e.preventDefault(); e.stopPropagation();
          moveTile(page, drag.from, idx);
          renderTiles(grid, page);
        }
      });
      grid.appendChild(node);
    });
  }

  // Drag a resize handle: past a small threshold, snap the span to 1 or 2.
  function attachResize(handle, axis, tile, grid, page) {
    handle.addEventListener("pointerdown", function (e) {
      e.preventDefault(); e.stopPropagation();
      var start = axis === "w" ? e.clientX : e.clientY;
      var base = axis === "w" ? (tile.w || 1) : (tile.h || 1);
      function move(ev) {
        var d = (axis === "w" ? ev.clientX : ev.clientY) - start;
        var v = base + (d > 36 ? 1 : d < -36 ? -1 : 0);
        v = Math.max(1, Math.min(2, v));
        if (axis === "w" && tile.w !== v) { tile.w = v; renderTiles(grid, page); }
        if (axis === "h" && tile.h !== v) { tile.h = v; renderTiles(grid, page); }
      }
      function up() {
        document.removeEventListener("pointermove", move);
        document.removeEventListener("pointerup", up);
      }
      document.addEventListener("pointermove", move);
      document.addEventListener("pointerup", up);
    });
  }

  function moveTile(page, from, to) {
    if (from === to) return;
    var t = page.tiles.splice(from, 1)[0];
    page.tiles.splice(to, 0, t);
  }

  function addTile(page, entity) {
    if (page.tiles.length >= state.layout.maxTilesPerPage) {
      toast("Page is full (" + state.layout.maxTilesPerPage + " tiles max)", true);
      return;
    }
    if (page.tiles.some(function (t) { return t.id === entity.id; })) {
      toast("Already on this page", true);
      return;
    }
    page.tiles.push({ id: entity.id, label: "", w: 1, h: 1 });
    var grid = document.getElementById("grid");
    if (grid) renderTiles(grid, page);
  }

  // ---- entity palette ------------------------------------------------------
  // Fetch the whole catalog once, then filter client-side (no per-keystroke
  // device work; the device keeps no per-entity RAM for the picker).
  function loadCatalog() {
    var listEl = document.getElementById("elist");
    if (!listEl) return;
    if (state.entities && state.entities.length) { renderEntityList(""); return; }
    getJSON("/api/entities")
      .then(function (list) {
        state.entities = list || [];
        renderEntityList("");
      })
      .catch(function () {
        listEl.innerHTML = "";
        listEl.appendChild(el("div", { class: "empty", text: "No entities. Check the HA connection." }));
      });
  }

  function renderEntityList(filter) {
    var listEl = document.getElementById("elist");
    if (!listEl) return;
    var f = (filter || "").toLowerCase();
    var items = state.entities.filter(function (en) {
      return !f || (en.name + " " + en.id).toLowerCase().indexOf(f) >= 0;
    });
    listEl.innerHTML = "";
    if (!items.length) {
      listEl.appendChild(el("div", { class: "empty",
        text: state.entities.length ? "No match." : "No entities. Check the HA connection." }));
      return;
    }
    items.slice(0, 200).forEach(function (en) {  // cap DOM nodes for responsiveness
      var item = el("div", { class: "eitem", draggable: "true" });
      item.appendChild(el("span", { class: "dot " + en.domain }));
      item.appendChild(el("div", { class: "en", html: "<b>" + escapeHtml(en.name) + "</b><small>" + escapeHtml(en.id) + "</small>" }));
      var addBtn = el("button", { class: "add", text: "+" });
      addBtn.addEventListener("click", function () { addTile(state.layout.pages[state.curPage], en); });
      item.appendChild(addBtn);
      item.addEventListener("dragstart", function (e) {
        drag = { kind: "add", entity: en };
        e.dataTransfer.effectAllowed = "copy";
      });
      item.addEventListener("dragend", function () { drag = null; });
      listEl.appendChild(item);
    });
  }

  function escapeHtml(s) {
    return (s || "").replace(/[&<>"]/g, function (c) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c];
    });
  }

  // ---- save ----------------------------------------------------------------
  function saveLayout() {
    var body = { pages: state.layout.pages.map(function (p) {
      return { title: p.title || "", tiles: p.tiles.map(function (t) {
        return { id: t.id, label: t.label || "", w: t.w || 1, h: t.h || 1 };
      }) };
    }) };
    postJSON("/api/layout", body)
      .then(function () { toast("Layout saved"); })
      .catch(function (e) { toast("Save failed: " + e.message, true); });
  }

  init();
})();
