# Reading and maintaining the legacy runtime

The legacy relay, Lua adapters, Mission Director scripts and launchers are
maintained for human review. Prefer explicit control flow and domain names over
compact expressions. The existing `AGENTS.md` rules remain the shared policy.

## Layout and names

- Use four spaces for indentation and one statement per line. Expand control
  flow into blocks. Python and Lua target 100 columns; do not break a protocol
  literal or an MD expression merely to meet that target.
- Name values for their meaning: `fields`, `ship_id`, `lease`, `generation`,
  `baseline`, `bubbleState`, `worldProtocol`, `engineApi`. Coordinates `x/y/z`
  and short loop indices are useful where their meaning is immediate.
- Follow each language's conventions: Python `snake_case` functions/locals and
  `UPPER_CASE` constants; the Lua adapters retain their existing `camelCase` API.
  Do not mix a naming migration with an interface change.
- Put imports on separate lines. Keep imports and Lua loads in their existing
  order unless their initialization effects have been checked.
- Split independent declarations when evaluation order is unchanged. Keep
  multiple return unpacking together. Do not turn `a, b = b, a` into sequential
  assignments or duplicate a call to make a declaration look simpler.

## Comments and structure

Use module docstrings and short comments to explain responsibility, input units,
coordinate frames, authority boundaries and engine constraints. Explain *why*
an unusual check or sequence exists; avoid narrating an obvious assignment.
Remove or correct stale comments when changing the code they describe.

Keep related validation and mutation together. Use guard clauses and named
helpers when they simplify a function without changing validation, exception or
side-effect order. Large packet handlers can be reviewed by message type; do not
extract every branch into a wrapper solely to reduce line count. A readability
change should not introduce a new abstraction or protocol.

The main paths through the legacy implementation are:

| Responsibility | Where to start |
| --- | --- |
| Authentication, leases, UDP delivery | `src/x4lan/host.py`, `ui/client.lua`, `ui/transport.lua` |
| Player authority and cooperative ships | `src/x4lan/coop.py`, `ui/coop_game.lua` |
| Sector baselines and NPC replicas | `src/x4lan/universe.py`, `ui/universe_protocol.lua`, `ui/universe_game.lua` |
| Interpolation and prediction | `ui/motion.lua` |
| Mission journal and native mission actions | `src/x4lan/missions.py`, `ui/mission_*`, `md/x4lan_missions.xml` |
| Session/profile ownership and shared starting save | `tools/distribution/session_launcher.py`, `world_launcher.py`, `profile_settings.py` |

Here `ui/` and `md/` are under `src/extension/x4_lan_lab/`. The Rust dedicated
runtime is a separate implementation; its existing formatter and conventions
remain in `src/rust/`.

## Compatibility during cleanup

Local names can change after checking their binding and captures. Public Python
parameters may be used as keywords; Lua global names, table keys, callbacks, FFI
declarations and MD variables may be used by other modules or saved state. Keep
them unchanged during a local cleanup. Short compatibility fields such as `seq`
or `rev` are not spelling mistakes to replace blindly.

Preserve packet literals, error messages, config defaults, numeric limits,
validation order and units. Keep pure formatting separate from renaming or
structural changes so reviewers can compare each step. Generated binaries and
sealed test packages are not rebuilt by a source readability change.

## Reproducible formatting

`tools/dev/readability-files.txt` explicitly lists the adopted Python, Lua and MD
XML files. This avoids silently rewriting newer engine code, historical probes,
third-party code or generated packages. C and PowerShell changes are checked by
their compilers/parsers and reviewed separately. `.editorconfig` supplies editor
indentation; it does not run a formatter.

Install the optional Python formatter in your development environment:

```sh
python -m pip install -r requirements-style.txt
```

Use StyLua **2.6.0** from its official release and `xmllint` from libxml2. Put
them on PATH or provide `--stylua PATH` and `--xmllint PATH`:

```sh
python tools/dev/check_style.py
python tools/dev/check_style.py --fix
python tools/dev/check_style.py src/x4lan/coop.py
```

The default checks without rewriting. Style tools are development dependencies;
they are not needed by testers or the running game. After regenerating adopted
MD scripts, format those files again and review the XML tree for changes.
The checker verifies availability, not installed versions. Use the documented
formatter versions for consistent Python/Lua output; an update to libxml2 may
also change XML whitespace and should be reviewed separately.

Run affected tests after renaming, then the complete `tools/dev/check.py` suite
before integration. This includes LuaJIT adapters and loopback networking; a
missing native transport or blocked socket is incomplete validation. Automated
checks do not establish native Windows or two-player gameplay acceptance.

The conventions draw on [PEP 8](https://peps.python.org/pep-0008/) and
[PEP 257](https://peps.python.org/pep-0257/), with the existing project rules taking
precedence. Formatter behavior is documented by
[Ruff](https://docs.astral.sh/ruff/formatter/) and
[StyLua](https://github.com/JohnnyMorganz/StyLua). Lua refactoring must preserve
the scope and multiple-result rules in the
[Lua 5.1 manual](https://www.lua.org/manual/5.1/manual.html).
