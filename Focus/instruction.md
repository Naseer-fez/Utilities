# Focus Mode Engine V2 — Strict Commitment System

Create a native Windows application that transforms the PC into a controlled work environment.

This is NOT a productivity app.

This is a strict environment-locking engine designed to enforce intentional focus sessions with limited escape paths.

Core philosophy:
Once a focus session starts, casually exiting should be difficult.

However:
The system must remain safe, transparent, and fully user-controlled.

The application must NEVER behave like malware, ransomware, spyware, or hostile persistence software.

---

## PRIMARY GOAL

The app transforms the entire computer state into a predefined work-only environment.

When activated:

* distracting apps are closed
* work apps launch
* notifications disable
* wallpaper changes
* audio changes
* system enters strict mode

Strict mode remains active until:

1. session timer completes
2. user shuts down PC
3. authorized unlock procedure succeeds

---

## STRICT MODE RULES

Once strict mode begins:

* profile cannot be casually exited
* tray exit disabled
* hotkey disable blocked
* process close protection enabled
* accidental termination prevented

The user intentionally committed to the session.

---

## UNLOCK SYSTEM

Unlocking requires explicit authorization.

V1 Unlock Flow:

* user presses Unlock
* app requests unlock code
* temporary code generated internally
* user must retrieve code from email

Current simplified behavior:

* email flow placeholder
* unlock instantly succeeds for development

Future production behavior:

* generate one-time unlock token
* send token to TWO different email accounts
* user must enter matching code
* optional cooldown timer before unlock

Purpose:
Prevent impulsive exits.

---

## IMPORTANT SAFETY RULES

The application MUST:

* always allow shutdown/restart
* never encrypt files
* never block Task Manager permanently
* never interfere with Windows recovery
* never install drivers
* never hide itself
* never self-replicate
* never elevate privileges silently

No malicious persistence.

No anti-user behavior.

Everything must remain transparent and reversible.

---

## STRICT MODE TECHNICAL DESIGN

When strict mode activates:

1. Disable normal exit paths

* remove tray exit
* disable quick close
* intercept accidental closure

2. Watchdog process
   Optional:

* lightweight watchdog restarts app if crashed accidentally

3. Session state lock

* local encrypted session file
* marks active strict session
* restored on reboot

4. Boot recovery
   If PC restarts during session:

* app resumes strict mode automatically

5. Shutdown handling
   Shutdown is allowed.
   User intentionally chose to terminate session through full system stop.

---

## ARCHITECTURE

Use multi-process architecture.

Processes:

1. Core daemon

* controls system state
* manages profiles
* controls lock state

2. Tray UI

* lightweight frontend
* communicates via IPC

3. Optional watchdog

* restarts daemon if crash detected

IPC:

* named pipes
* local sockets
* shared memory
* avoid heavy frameworks

---

## SESSION FLOW

IDLE STATE
↓
User activates profile
↓
Pre-check validation
↓
Save session state
↓
Apply system modifications
↓
Enter strict mode
↓
Lock exit paths
↓
Session active
↓
Unlock request OR timer completion
↓
Restore previous environment
↓
Exit strict mode

---

## PROFILE FEATURES

Each strict profile may define:

* apps to close
* apps to block
* apps to launch
* websites to block
* audio device
* volume
* wallpaper
* brightness
* notification state
* timer duration
* unlock restrictions
* startup behavior

---

## OPTIONAL WEBSITE BLOCKING

Possible methods:

* Windows Firewall
* hosts file modification
* DNS override

Must:

* be reversible instantly
* restore original state safely

---

## ANTI-IMPULSE DESIGN

Purpose is reducing impulsive distraction.

Possible methods:

* unlock cooldown timer
* confirmation delay
* typed confirmation phrase
* email unlock
* secondary account verification
* delayed unlock countdown

Avoid fake “Are you sure?” spam.

Use meaningful friction instead.

---

## PERFORMANCE TARGETS

Must remain extremely lightweight.

Idle targets:

* near 0% CPU
* minimal RAM
* event-driven architecture

Avoid:

* polling loops
* Electron
* browser rendering engines
* telemetry services
* cloud dependency

---

## PREFERRED TECH STACK

Recommended:

* C++
* Win32 API
* COM APIs
* Core Audio APIs
* Named pipes IPC
* Dear ImGui or pure Win32
* JSON/TOML configs

Alternative:
Rust + windows-rs

---

## SECURITY MODEL

Everything local-first.

No telemetry.
No tracking.
No hidden networking.

Email unlock system:

* only active if configured
* encrypted credential storage
* secure token generation
* rate limited unlock attempts

---

## PROJECT PRIORITIES

Priority order:

1. Stability
2. Safe strict-mode behavior
3. Instant transitions
4. Low resource usage
5. Reliability
6. UX polish

---

## DELIVERABLES REQUIRED

Need:

* full architecture plan
* process communication design
* session lifecycle
* strict-mode enforcement logic
* unlock system design
* reboot recovery design
* config schema
* watchdog implementation
* system tray implementation
* optimization strategy
* safety fallback mechanisms
* crash recovery logic
* startup sequence
* modular folder structure

Keep V1 small and highly reliable.

Do NOT over-engineer.
