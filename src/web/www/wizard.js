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
    this.folded = new Set(["System"]);   // the system's own parts: there, but rarely the point
    this.current = "";
    this.dlg = null;
  }

  bind() {
    const c = (name, ret, args) => this.d.M.cwrap(name, ret, args);
    this.api = {
      open: c("wiz_open", "number", ["string", "string", "string"]),
      error: c("wiz_error", "string", []),
      state: c("wiz_state", "string", []),
      setSystem: c("wiz_set_system", null, ["string"]),
      setMedia: c("wiz_set_media", null, ["string"]),
      toggle: c("wiz_toggle", "string", ["string"]),
      setStartup: c("wiz_set_startup", null, ["string"]),
      setVolumeId: c("wiz_set_volume_id", null, ["string"]),
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
          <label>System <select class="wiz-system"></select></label>
          <label>Media <select class="wiz-media"></select></label>
          <span class="spacer"></span>
          <button class="small wiz-close" title="close">&times;</button>
        </div>
        <div class="wiz-body">
          <div class="wiz-list"></div>
          <div class="wiz-details"></div>
        </div>
        <div class="wiz-plan"></div>
        <details class="wiz-more">
          <summary>More: startup lines, volume id, saved choices</summary>
          <label>START.COM lines after the system's and the bundles' (one a line)
            <textarea class="wiz-startup" rows="2" spellcheck="false"></textarea></label>
          <label>Volume id <input class="wiz-volid" maxlength="12" spellcheck="false"></label>
          <div class="wiz-row"><button class="wiz-savechoice">Save choice</button>
            <button class="wiz-openchoice">Open choice…</button>
            <input class="wiz-file" type="file" accept=".toml,text/plain" hidden></div>
        </details>
        <div class="wiz-foot">
          <span class="wiz-msg"></span>
          <button class="wiz-download">Download .dsk</button>
          <button class="wiz-boot">Boot it</button>
        </div>
      </div>`;
    document.body.appendChild(dlg);
    this.dlg = dlg;
    const q = (s) => dlg.querySelector(s);
    q(".wiz-close").onclick = () => dlg.close();
    q(".wiz-system").onchange = (e) => { this.api.setSystem(e.target.value); this.changed(); };
    q(".wiz-media").onchange = (e) => { this.api.setMedia(e.target.value); this.changed(); };
    q(".wiz-startup").onchange = (e) => { this.api.setStartup(e.target.value); this.changed(); };
    q(".wiz-volid").onchange = (e) => { this.api.setVolumeId(e.target.value.toUpperCase()); this.changed(); };
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
    q(".wiz-system").innerHTML = s.systems.map((x) => `<option value="${esc(x.key)}"${x.key === s.system ? " selected" : ""}>${esc(x.title)}</option>`).join("");
    const sys = s.systems.find((x) => x.key === s.system);
    const label = { ss: "ss - one side, 400 KB", dz: "dz - two sides, 800 KB", dv: "dv - one DV volume, 800 KB" };
    q(".wiz-media").innerHTML = (sys?.media ?? []).map((m) => `<option value="${m}"${m === s.media ? " selected" : ""}>${label[m]}</option>`).join("");
    q(".wiz-startup").value = s.startup.join("\n");
    q(".wiz-volid").value = s.volumeId;
    this.renderList();
    this.renderDetails();
  }

  renderList() {
    const list = this.dlg.querySelector(".wiz-list");
    const out = [];
    let hidden = false, radioName = "";
    for (const r of this.state.rows) {
      if (r.kind === "group") {
        hidden = this.folded.has(r.key);
        out.push(`<div class="wiz-group" data-group="${esc(r.key)}">${hidden ? "&#9656;" : "&#9662;"} ${esc(r.title)}</div>`);
        continue;
      }
      if (hidden) continue;
      if (r.kind === "radio") { radioName = r.key; out.push(`<div class="wiz-radio">one of: ${esc(r.title)}</div>`); continue; }
      const on = r.mark !== "off";
      const input = r.radio
        ? `<input type="radio" name="wiz-${esc(radioName)}"${on ? " checked" : ""}${r.mark === "system" || !r.available ? " disabled" : ""}>`
        : `<input type="checkbox"${on ? " checked" : ""}${r.mark === "system" || !r.available ? " disabled" : ""}>`;
      const note = r.mark === "system" ? "system" : r.mark === "added" ? `for ${esc(r.requiredBy.split(/ - | \(/)[0])}` : r.available ? "" : esc(r.why);
      const cls = ["wiz-item", `d${r.depth}`, r.mark, r.available ? "" : "na", r.key === this.current ? "cur" : ""].join(" ");
      out.push(`<div class="${cls}" data-key="${esc(r.key)}" title="${esc(r.available ? r.title : r.why)}">${input}` +
               `<span class="t">${esc(r.title)}</span><span class="b">${r.blocks}</span><span class="n">${note}</span></div>`);
    }
    list.innerHTML = out.join("");
    list.querySelectorAll(".wiz-group").forEach((g) => g.onclick = () => {
      const k = g.dataset.group;
      if (!this.folded.delete(k)) this.folded.add(k);
      this.renderList();
    });
    list.querySelectorAll(".wiz-item").forEach((it) => it.onclick = (e) => {
      e.preventDefault();
      this.current = it.dataset.key;
      const row = this.state.rows.find((r) => r.kind === "bundle" && r.key === this.current);
      if (row && !row.available) { this.renderList(); this.renderDetails(); this.msg(row.why, true); return; }
      const why = this.api.toggle(this.current);
      this.changed(why);
    });
  }

  renderDetails() {
    const box = this.dlg.querySelector(".wiz-details");
    if (!this.current) { box.innerHTML = `<p class="dim">Tap a line: it is ticked or unticked, and its details show here.</p>`; return; }
    const d = JSON.parse(this.api.details(this.current));
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
    this.fetchNeeded().then(() => {
      if (token !== this.planToken) return;
      const p = JSON.parse(this.api.plan());
      const bars = p.volumes.map((v) => {
        const pct = Math.min(100, Math.round(100 * v.used / v.capacity));
        return `<div class="wiz-vol"><span>${v.name}</span><div class="wiz-bar${pct >= 100 ? " full" : ""}"><i style="width:${pct}%"></i></div>` +
               `<span>${v.used} / ${v.capacity}, ${v.free} free</span></div>`;
      }).join("");
      this.dlg.querySelector(".wiz-plan").innerHTML = bars +
        `<div class="wiz-startline"><span class="dim">START.COM</span> ${esc(p.startup.join(" · "))}</div>` +
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
