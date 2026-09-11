// wizard.js — the browser's disk wizard: a dialog over the module's wiz_*
// API (src/wizard_web.cpp), which holds the rules - the native wizard's,
// compiled from the same sources.  This file draws and fetches: the
// collection's disks.toml and file list come from its Pages, and a file is
// fetched into the module's file system (/software/...) only when the
// choice uses it, just before a plan or a build reads it.

const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

export class DiskComposer {
  // deps: { M, site (URL of the collection's Pages), index (its index.json),
  //         fetchBytes(url), onDisk(name, bytes, boot) -> Promise, say(text) }
  constructor(deps) {
    this.d = deps;
    this.api = null;
    this.fetched = new Set();
    this.current = null;                 // {kind, key} of the row last tapped
    this.dlg = null;
  }

  bind() {
    const c = (name, ret, args) => this.d.M.cwrap(name, ret, args);
    this.api = {
      open: c("wiz_open", "number", ["string", "string", "string"]),
      error: c("wiz_error", "string", []),
      state: c("wiz_state", "string", []),
      setMedia: c("wiz_set_media", "string", ["string"]),
      setSystem: c("wiz_set_system", "string", ["string"]),
      fold: c("wiz_fold", null, ["string"]),
      toggle: c("wiz_toggle", "string", ["string"]),
      setField: c("wiz_set_field", "string", ["string", "string"]),
      needed: c("wiz_needed", "string", []),
      plan: c("wiz_plan", "string", []),
      build: c("wiz_build", "number", ["string"]),
      save: c("wiz_save", "string", []),
      load: c("wiz_load", "number", ["string"]),
      details: c("wiz_details", "string", ["string"]),
    };
  }

  async open() {
    if (!this.api) {
      this.bind();
      const toml = new TextDecoder().decode(await this.d.fetchBytes(new URL("disks.toml", this.d.site)));
      const paths = this.d.index.paths ?? [];
      const sizes = paths.map((p) => this.d.index.sizes?.[p] ?? 0);
      if (!this.api.open(toml, paths.join("\n"), sizes.join("\n"))) throw new Error("disks.toml: " + this.api.error());
    }
    if (!this.dlg) this.build();
    this.render();
    this.dlg.showModal();
    this.replan();
  }

  // ── the dialog ─────────────────────────────────────────────────────────
  build() {
    const dlg = document.createElement("dialog");
    dlg.id = "wizdlg";
    dlg.innerHTML = `
      <div class="wiz">
        <div class="wiz-head">
          <b>Compose a disk</b>
          <span class="spacer"></span>
          <button class="small wiz-close" title="close">&times;</button>
        </div>
        <div class="wiz-body">
          <div class="wiz-list"></div>
          <div class="wiz-details"></div>
        </div>
        <div class="wiz-plan"></div>
        <div class="wiz-foot">
          <span class="wiz-msg"></span>
          <button class="wiz-savechoice">Save choice</button>
          <button class="wiz-openchoice">Open choice…</button>
          <input class="wiz-file" type="file" accept=".toml,text/plain" hidden>
          <button class="wiz-download">Download .dsk</button>
          <button class="wiz-boot">Boot it</button>
        </div>
      </div>`;
    document.body.appendChild(dlg);
    this.dlg = dlg;
    const q = (s) => dlg.querySelector(s);
    q(".wiz-close").onclick = () => dlg.close();
    q(".wiz-savechoice").onclick = () => this.saveChoice();
    q(".wiz-openchoice").onclick = () => q(".wiz-file").click();
    q(".wiz-file").onchange = (e) => this.openChoice(e.target.files[0]);
    q(".wiz-download").onclick = () => this.make(false).catch((e) => this.msg(e.message, true));
    q(".wiz-boot").onclick = () => this.make(true).catch((e) => this.msg(e.message, true));
  }

  msg(text, bad = false) {
    const m = this.dlg.querySelector(".wiz-msg");
    m.textContent = text ?? "";
    m.classList.toggle("bad", !!bad);
  }

  changed(message = "") {
    this.render();
    const notices = this.state.notices;
    this.msg(message || notices.join("; "), !!message);
    this.replan();
  }

  // ── drawing ────────────────────────────────────────────────────────────
  render() {
    const s = this.state = JSON.parse(this.api.state());
    const q = (sel) => this.dlg.querySelector(sel);
    q(".wiz-savechoice").disabled = !s.ready;
    this.renderList();
    this.renderDetails();
  }

  // One tree: the diskette, the system on it, then the groups of bundles.
  // What is folded is the module's: the rows here are the ones to show.
  renderList() {
    const list = this.dlg.querySelector(".wiz-list");
    const out = [];
    let radioName = "";
    const pad = (r) => `style="padding-left:${0.7 + 1.2 * r.depth}em"`;
    const systemTitle = this.state.rows.find((r) => r.kind === "system" && r.mark !== "off")?.title
                     ?? this.state.rows.find((r) => r.key === "#system")?.summary;
    for (const r of this.state.rows) {
      if (r.kind === "group") {
        const note = r.available ? r.summary : r.why;
        out.push(`<div class="wiz-group${r.available ? "" : " na"}" data-group="${esc(r.key)}" ${pad(r)}>` +
                 `<span class="t">${r.open ? "&#9662;" : "&#9656;"} ${esc(r.title)}</span><span class="s">${esc(note)}</span></div>`);
        continue;
      }
      if (r.kind === "radio") { radioName = r.key; out.push(`<div class="wiz-radio" ${pad(r)}>one of: ${esc(r.title)}</div>`); continue; }
      if (r.kind === "line") {
        const from = r.requiredBy === systemTitle ? "the system's" : `from ${r.requiredBy.split(/ - | \(/)[0]}`;
        out.push(`<div class="wiz-line" ${pad(r)}><span class="t">${esc(r.title)}</span><span class="n">${esc(from)}</span></div>`);
        continue;
      }
      if (r.kind === "field") {
        const volume = r.parent === "#label";
        const own = r.key.startsWith("#startup:") && r.value !== "";
        out.push(`<label class="wiz-field" ${pad(r)}>${r.title ? `<span>${esc(r.title)}</span>` : ""}` +
                 `<input id="wiz-f${esc(r.key.replace(/[^a-z0-9]/gi, "-"))}" data-key="${esc(r.key)}" data-parent="${esc(r.parent)}"` +
                 ` value="${esc(r.value)}" placeholder="${esc(r.summary)}" spellcheck="false" autocomplete="off"` +
                 `${volume ? ` maxlength="12" class="vol"` : ""}>` +
                 `${own ? `<button class="small wiz-drop" data-key="${esc(r.key)}" title="take the line out">&times;</button>` : ""}</label>`);
        continue;
      }
      const on = r.mark !== "off";
      const name = r.kind === "bundle" ? `wiz-${esc(radioName)}` : `wiz-${r.kind}`;
      // A radio button stays live when it is the system's: picking another replaces it.
      const locked = (r.mark === "system" && !r.radio) || !r.available ? " disabled" : "";
      const input = r.radio ? `<input type="radio" name="${name}"${on ? " checked" : ""}${locked}>`
                            : `<input type="checkbox"${on ? " checked" : ""}${locked}>`;
      const note = r.native ? "native" : r.mark === "system" ? "system" : r.mark === "added" ? `for ${esc(r.requiredBy.split(/ - | \(/)[0])}` : r.available ? "" : esc(r.why);
      const cur = this.current?.kind === r.kind && this.current?.key === r.key ? "cur" : "";
      const cls = ["wiz-item", r.mark, r.available ? "" : "na", cur].join(" ");
      out.push(`<div class="${cls}" data-kind="${r.kind}" data-key="${esc(r.key)}" ${pad(r)} title="${esc(r.available ? r.title : r.why)}">${input}` +
               `<span class="t">${esc(r.title)}</span><span class="b">${r.kind === "bundle" ? r.blocks : ""}</span><span class="n">${note}</span></div>`);
    }
    list.innerHTML = out.join("");
    // A field: Enter keeps it and goes on to the next field of its block (a
    // START.COM line emptied is gone, and the next one comes up to its place),
    // Esc puts back what was there, x takes a START.COM line out.
    const fieldsOf = (parent) => [...this.dlg.querySelectorAll(`.wiz-field input[data-parent="${parent}"]`)];
    const keep = (input, goOn) => {
      const { key, parent } = input.dataset;
      const at = fieldsOf(parent).indexOf(input);
      const emptied = key.startsWith("#startup:") && input.value.trim() === "";
      const why = this.api.setField(key, input.value);
      this.changed(why);
      if (goOn && !why) fieldsOf(parent)[emptied ? at : at + 1]?.focus();
    };
    list.querySelectorAll(".wiz-field input").forEach((input) => {
      input.onkeydown = (e) => {
        if (e.key === "Enter") { e.preventDefault(); input.dataset.kept = "1"; keep(input, true); }
        if (e.key === "Escape") { e.preventDefault(); input.value = this.state.rows.find((r) => r.key === input.dataset.key)?.value ?? ""; input.blur(); }
      };
      input.onchange = () => { if (!input.dataset.kept) keep(input, false); };
    });
    list.querySelectorAll(".wiz-drop").forEach((b) => b.onclick = (e) => {
      e.preventDefault();
      this.changed(this.api.setField(b.dataset.key, ""));
    });
    list.querySelectorAll(".wiz-group").forEach((g) => g.onclick = () => {
      const row = this.state.rows.find((r) => r.kind === "group" && r.key === g.dataset.group);
      if (row && !row.available) { this.msg(row.why, true); return; }
      this.api.fold(g.dataset.group);
      this.render();
    });
    list.querySelectorAll(".wiz-item").forEach((it) => it.onclick = (e) => {
      e.preventDefault();
      const { kind, key } = it.dataset;
      this.current = { kind, key };
      const row = this.state.rows.find((r) => r.kind === kind && r.key === key);
      if (row && !row.available) { this.renderList(); this.renderDetails(); this.msg(row.why, true); return; }
      if (row?.radio && row.mark !== "off") { this.renderList(); this.renderDetails(); return; }   // picked already
      const wasReady = this.state.ready;
      const why = kind === "media" ? this.api.setMedia(key) : kind === "system" ? this.api.setSystem(key) : this.api.toggle(key);
      this.changed(why);
      // A step taken: what was chosen stays open, the list moves on to the next.
      if (kind === "media") this.scrollTo('.wiz-group[data-group="#label"]');
      if (kind === "system" && !wasReady && this.state.ready) this.scrollTo('.wiz-group:not([data-group^="#"])');
    });
  }

  scrollTo(selector) {
    const el = this.dlg.querySelector(`.wiz-list ${selector}`);
    if (el) el.scrollIntoView({ block: "start", behavior: matchMedia("(prefers-reduced-motion: reduce)").matches ? "auto" : "smooth" });
  }

  renderDetails() {
    const box = this.dlg.querySelector(".wiz-details");
    if (this.current?.kind !== "bundle") {
      box.innerHTML = `<p class="dim">${this.state.ready ? "Tap a line: it is ticked or unticked, and its details show here."
                                                       : "Choose the diskette first, then the operating system on it."}</p>`;
      return;
    }
    const d = JSON.parse(this.api.details(this.current.key));
    const line = (k, v) => v && v.length ? `<div><span class="dim">${k}</span> ${esc(Array.isArray(v) ? v.join(" ") : v)}</div>` : "";
    box.innerHTML = `<div class="wiz-dtitle">${esc(d.title)}</div>${line("group", d.group)}${line("provides", d.provides)}` +
                    `${line("requires", d.requires)}${line("startup", d.startup)}${line("files", d.files)}<div>${d.blocks} blocks</div>`;
  }

  // ── the plan: fetch what the choice uses, then ask ────────────────────
  async fetchNeeded() {
    const M = this.d.M;
    const missing = JSON.parse(this.api.needed()).filter((p) => !this.fetched.has(p));
    if (missing.length) this.dlg.querySelector(".wiz-plan").innerHTML = `<div class="dim">fetching ${missing.length} file(s)…</div>`;
    await Promise.all(missing.map(async (p) => {
      const bytes = await this.d.fetchBytes(new URL(p, this.d.site));
      const path = "/software/" + p;
      M.FS.mkdirTree(path.slice(0, path.lastIndexOf("/")));
      M.FS.writeFile(path, bytes);
      this.fetched.add(p);
    }));
  }

  replan() {
    const token = this.planToken = (this.planToken ?? 0) + 1;
    if (!this.state.ready) {
      this.dlg.querySelector(".wiz-plan").innerHTML = `<div class="dim">Choose the diskette, then the operating system.</div>`;
      this.dlg.querySelector(".wiz-boot").disabled = true;
      this.dlg.querySelector(".wiz-download").disabled = true;
      return;
    }
    this.fetchNeeded().then(() => {
      if (token !== this.planToken) return;
      const p = JSON.parse(this.api.plan());
      const bars = p.volumes.map((v) => {
        const pct = Math.min(100, Math.round(100 * v.used / v.capacity));
        return `<div class="wiz-vol"><span>${v.name}</span><div class="wiz-bar${pct >= 100 ? " full" : ""}"><i style="width:${pct}%"></i></div>` +
               `<span>${v.used} / ${v.capacity}, ${v.free} free</span></div>`;
      }).join("");
      this.dlg.querySelector(".wiz-plan").innerHTML = bars +
        `<div class="wiz-startline"><span class="dim">blocks</span> ${esc(p.groups.map((g) => `${g.name} ${g.blocks}`).join("   "))}</div>` +
        (p.ok ? "" : `<div class="bad">${esc(p.problem)}</div>`);
      this.dlg.querySelector(".wiz-boot").disabled = !p.ok;
      this.dlg.querySelector(".wiz-download").disabled = !p.ok;
    }).catch((e) => this.msg("cannot fetch the collection's files: " + e.message, true));
  }

  // ── the disk and the choice ────────────────────────────────────────────
  async make(boot) {
    await this.fetchNeeded();
    const out = "/composed.dsk";
    if (!this.api.build(out)) throw new Error(this.api.error());
    const bytes = this.d.M.FS.readFile(out);
    const name = `${this.state.system}-${this.state.media}.dsk`;
    if (boot) {
      await this.d.onDisk(name, bytes, true);
      this.dlg.close();
    } else {
      this.download(name, new Blob([bytes]));
      this.msg(`${name} saved`);
    }
  }

  saveChoice() {
    const name = `${this.state.system}-${this.state.media}.toml`;
    this.download(name, new Blob([this.api.save()], { type: "text/plain" }));
    this.msg(`${name} saved`);
  }

  async openChoice(file) {
    if (!file) return;
    const text = await file.text();
    if (!this.api.load(text)) { this.msg(this.api.error(), true); return; }
    this.changed();
  }

  download(name, blob) {
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 10000);
  }
}
