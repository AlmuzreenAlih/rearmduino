# Handoff: machine activation for REARMduino (planned, not started)

Written 2026-11-14 at the end of a long session, so a new conversation can pick this up.
Nothing in this plan is implemented yet. Read `CLAUDE.md` first (project rules), then this.

## Where the app stands

- REARMduino: Qt 5.15 Widgets + QScintilla + QtSerialPort, CMake/Ninja, arduino-cli via QProcess.
  Windows (main) and Raspberry Pi OS Bullseye aarch64 (`ssh rpi`, repo at `~/myide`, `build/`).
- Latest commits (all pushed, all built on the Pi):
  - program file renamed to `REARMduino.exe` / `REARMduino` (CMake `OUTPUT_NAME`; target stays `myide`)
  - window title `<sketch> | REARMduino`
  - REARMduino logo (`resources/app/`), CLAUDE.md name/logo and portable USB driver notes
  - spinners hidden (`kEnabled = false` in `src/Spinner.cpp`)
  - port scan 3 s after startup (approved exception to "no network at startup")
  - auto-select a newly connected port when the selected one is disconnected
  - Tools > Restart USB Device; warning when a port does not come back after a restart
- Windows build: `scripts\build-win.cmd [Release|Debug]` (vcvars, Qt bin first on PATH, Ninja,
  `deploy` target = windeployqt). The release exe is locked while the user has it open (LNK1104):
  ask them to close it, or verify in a scratch build folder.
- Uncommitted, not ours: `image.png`, `sketches/EggshellProject/`, `sketches/EggshellsAutomationProject/`.

## Goal

Stop casual copying of REARMduino (e.g. a customer copying the portable folder while we're away).
Only the developer and his development partner should be able to run it. Each machine must be
approved once by the developer; he can revoke a machine later. Threat model: casual copying only,
not cracking.

## Decisions (made by the user)

| Question | Decision |
|---|---|
| What is approved | A **machine** (copying an approved folder to another PC makes a new request) |
| Which builds check | **Every Windows Release build**, portable or not, including the developer's own. Debug builds and the Pi skip it. (Reason: if only portable copies checked, deleting the portable marker file would bypass it.) |
| Offline use after approval | **Forever**, no expiry. Revocation only takes effect when the app is online. |
| On revoke | **Warn, let the user save (Save/Discard), then close**, and delete the local license. |
| Denied/revoked machine sees | **The same "waiting for approval" window** as a pending one (a customer can't tell). |
| Approval UI | **Adminer for now** (edit the `status` column). Admin API routes still get built for a later dashboard page. |
| Notifications | **None.** The partner tells the developer the request code. |
| Info sent with a request | PC name, Windows user name, copy name typed in the portable builder, app version, IP (taken by the server). |
| Also wanted | **When each copy was last used** (see "Last used" below). |

## Design

### App flow
1. Start. If the build is Windows Release, load the saved license and verify it **locally**
   (signature + machine ID). This takes milliseconds and needs no network, so startup speed is kept.
2. Valid: open the main window as normal. A few seconds later, on a background thread, call the
   server (status check + "last used" report). Server unreachable: do nothing.
   Server says revoked/denied: warn, run the normal close flow with Save/Discard (no Cancel), delete
   the license, exit.
3. No valid license: don't open the main window. Show a small window: "This copy is waiting for
   approval. Request code: K7P2-9XQM", with Retry and Close. It sends the request in the background.
   If the server returns a license, save it and open the main window.
   No internet on a first run: the window says it can't reach the server (Retry/Close).

### Last used (new requirement)
- Server columns `last_check_at` + `last_ip` are updated by every check that reaches the server.
- Offline starts never reach the server, so the app also keeps a local `lastUsedAt` (time of the
  most recent start) and sends it with the next check. The server stores `last_used_at` = the
  later of the stored value and the reported one. Optionally a local `runCount` too.
- Adminer then shows, per machine: last used (even if offline at the time), last seen online, IP.

### License
- Signed by the server with an **ECDSA P-256** private key (Node's built-in `crypto`, no new package).
  Private key only in the server's `.env` (`IDE_LICENSE_PRIVATE_KEY`); it is never committed.
- The public key is compiled into the app. Verify with **Windows CNG** (`BCryptVerifySignature`),
  so no new library (Qt 5.15 has no signature API; its HTTPS needs OpenSSL DLLs, which we avoid).
- Payload, e.g. `{"v":1,"machine":"<hash>","copyId":"<uuid>","issuedAt":"..."}` + base64 signature.
  Stored with the settings (registry for normal builds, `settings/` in a portable copy).
- Machine ID: SHA-256 of `HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid` + the C: volume serial.
  Reinstalling Windows or replacing the C: drive changes it, so that machine is approved again.
- **Back up the private key.** If it's lost, approved machines keep working, but new ones can't be
  approved until a build with a new public key ships. Changing the key invalidates all licenses.

### HTTP from the app
- **WinHTTP** (built into Windows) on a worker thread, results posted back to the UI thread.
- Server: `https://rearm.webserviceapp.online/api/ide/...` (URL compiled in).
- Never on the startup path except when there is no valid license.

### Portable builder
- Gives each copy a random copy ID (UUID) and stores it with the copy name typed in the builder,
  e.g. in the marker file or a small `copy.json`. A normal (non-portable) build reports copy name
  "dev" or the PC name.
- The builder must never copy a license into a new copy (it doesn't copy `settings/` today; keep it so).

## Backend (existing repo)

`C:\SirAlih\Projects\htdocs\webserviceapp2`, project **`projects/rearm`** (Node/Express + PostgreSQL,
served at `rearm.webserviceapp.online` through the shared `gateway` process). Read that repo's
`CLAUDE.md` and `projects/rearm/README.md` before touching it. Relevant conventions found there:
- Migrations are numbered SQL files in `projects/rearm/migrations/` (latest `015_...`), applied with
  `psql` on the server. Next one: **`016_create_ide_machines.sql`**.
- Routes live in `src/routes/*.js`, wired in `src/routes/index.js`; **every route change must update
  `src/routes/README.md` in the same commit**.
- Auth: superadmin login with JWT (`src/middleware/auth.js`). `express-rate-limit` is already a dependency.
- Config: each project parses its own `.env` (`src/config.js`); never throw on missing config
  (log it and return 503), since a crash takes down the shared gateway.
- Pushing `projects/rearm/**` triggers the live deploy (`deploy-rearm.yml`). Ask before pushing,
  and before any server step (migration, `.env`, `pm2 reload gateway`).

### Table `ide_machines` (sketch)
`id`, `machine_hash` (unique), `copy_id`, `copy_name`, `pc_name`, `win_user`, `app_version`,
`request_code`, `status` (`pending` | `approved` | `denied` | `revoked`, default `pending`),
`created_at`, `decided_at`, `last_check_at`, `last_used_at`, `last_ip`, optional `run_count`.

### Endpoints (sketch)
- `POST /api/ide/check`, public, rate-limited. Body: `machine`, `copyId`, `copyName`, `pcName`,
  `winUser`, `appVersion`, `lastUsedAt`. Upserts the row, updates last-* columns, returns
  `{status, requestCode, license?}`. Returns a license only when `approved`. Return the same shape
  for pending/denied/revoked if you want them indistinguishable on the wire too (the app shows the
  same window either way).
- Admin (superadmin JWT): `GET /api/ide/machines`, `PATCH /api/ide/machines/:id` (`status`),
  `DELETE /api/ide/machines/:id`.

## Order of work (small separate commits)

1. Backend: migration 016, `/api/ide/check`, admin routes, `IDE_LICENSE_PRIVATE_KEY` in
   `.env.example` + config, route docs. Test locally (`npm run setup-local` exists; check what it does).
2. Generate the key pair locally with Node `crypto`. Public key into the app source; hand the
   private key to the user for the server `.env` (never commit it).
3. App: `Activation` module (machine ID, CNG verify, WinHTTP worker, local lastUsedAt), the waiting
   window, the gate in `main.cpp` (Windows Release only), copy ID in the portable builder,
   CLAUDE.md update (network exception for activation). Test against the local backend.
4. Deploy (ask first): push backend, run migration 016 on the server, add the key to `.env`,
   `pm2 reload gateway`, check `/api/readyz`.
5. Approve the developer's own machine in Adminer, then build a portable copy for the partner.

## Open details to settle while building
- Exact place for the copy ID (marker file vs `copy.json`).
- Request code format (8 chars from the machine hash, e.g. `K7P2-9XQM`).
- Whether a revoke while the app is running should also be checked periodically (plan says: only
  at the post-startup check).
