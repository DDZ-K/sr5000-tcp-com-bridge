# Provisioning safety integration contract

Scope: only `com0com_inventory.h`, the root `create_port_pair.bat`, and this isolated test directory. No main application integration or shipped archive rebuild is performed here. The BAT ignores every caller argument and starts only its sibling `SR5000Bridge-v2.1.exe`; it never invokes setupc or requests elevation. Copies of the old BAT inside historical packages/artifacts are not modified.

## Public API (`namespace com0com`, header only, Win7 x64/C++14)

- `bool IsCanonicalCom(const std::string&)`: exact `COM` + 1..99999, no leading zeros, lowercase, device-path prefixes, whitespace, quotes or extra parameters. This is a conservative application policy, not a claim about the maximum ports supported by every driver.
- `bool ParseInventory(const std::string&, Inventory&, std::string& error)`: parse one endpoint per line; `Endpoint.id` = CNCA<n>/CNCB<n>, `pairId` = the canonical numeric suffix, `side` = A/B, `portName` resolves COM# through RealPortName; case-insensitive parameter keys are retained in `parameters`. Duplicate endpoint IDs/visible names/keys, malformed lines, diagnostics, unknown encoding, NULs, oversized output, and unresolved COM# fail the entire parse atomically. Empty output is a valid empty inventory, never a verified pair. Unknown well-formed parameters remain uninterpreted rather than silently changing meaning.
- `CaptureResult CaptureInventory(const std::wstring& approvedAbsoluteSetupc, DWORD timeoutMs=5000, size_t maxBytes=1048576)`: exact `CreateProcessW` application path, fixed `--silent --detail-prms list`, no cmd, shell, PATH lookup, install, rename, removal, registry change or automatic UAC. Capture stdout/stderr through a pipe; only pipe write and NUL stdin handles are inherited via STARTUPINFOEX. Start suspended, assign private kill-on-close Job, then resume; drain pipe while waiting. Timeout/output cap/exit/parse failures return empty inventory. Bounds: 1..30000 ms and 1..1048576 bytes; read loop deadline plus at most 1000 ms failure cleanup wait (process creation/OS calls themselves are not hard-real-time). Job assignment failure is fail-closed, including Win7 hosts already inside a non-breakaway Job. Caller must inspect `status == CaptureStatus::Ok` before using `.inventory`; inspect `.win32Error`, `.exitCode`, `.error` for failures. Unknown errors, including required elevation, must block automatic runtime start rather than fall back to a DOS COM-name check.
- `PairCheck ValidatePair(const Inventory&, const std::string& packaging, const std::string& bridge)`: `identityVerified` requires the selected names on opposite A/B endpoints sharing the same suffix; `pinsVerified` requires packaging `EmuOverrun=yes`, bridge `EmuBR=yes`, bridge `dsr=ropen` (not `!ropen`). `.ok()` requires both. Public Inventory is intended to come from the parser, not caller-fabricated structs. These are configuration facts only, not evidence of a healthy driver, live peer, working electrical pin, no stale bytes, free ports, or successful byte delivery. Check at each startup/reconnect and invalidate old evidence after provisioning/configuration change; a snapshot cannot eliminate subsequent external changes.
- `bool BuildAdminCreateCommand(const std::wstring& approvedAbsoluteSetupc, const std::string& packaging, const std::string& bridge, AdminCreateCommand&, std::string& error)`: PURE command-data builder; never executes, elevates or changes devices. Rejects non-absolute path, wrong executable basename, invalid/identical COM names; clears output on failure. Fields are separate: `.verb = L"runas"`, `.file` exact setupc path, `.parameters` fixed validated command. No arbitrary flags or user-supplied command fragment are accepted.

Creation must remain an explicit native GUI operation. Before any actual `ShellExecuteExW`, independently verify administrator-approved executable origin/publisher/ACLs and dependency search path, confirm driver/OS compatibility, bridge inactive, and both names unoccupied/unreserved (inventory alone cannot detect physical/offline COM reservations). Show explicit operator confirmation. Pass builder fields separately, never through cmd.exe; use `SEE_MASK_NOCLOSEPROCESS`, retain process handle until signaled, and do not declare success or retry while the installer still runs. Never kill an installer on a UI wait timeout. Re-capture and validate the pair only after completion, then perform real target-machine/device acceptance. This module intentionally has NO install executor or UAC API; the risky existing main implementation is outside this patch's scope.

Executable trust is a caller precondition, NOT guaranteed by a syntactically absolute path or a setupc.exe filename. For deployed setupc requiring administrator rights, non-elevated capture can return `ERROR_ELEVATION_REQUIRED (740)` or access-denied; this module must not silently elevate or weaken machine security. Main must choose an approved read-only elevated workflow or block with a clear explanation.

## Primary-source evidence (read, never run)

Official source archive retrieved from:
`https://downloads.sourceforge.net/project/com0com/com0com/3.0.0.0/com0com-3.0.0.0.zip`

SHA-256: `6751e911f73980b23cc878a456eb99d1dc6d0603c11c1d3ced109abe1c556380`

All following paths are inside that source ZIP:
- `com0com-3.0.0.0/ReadMe.txt`, lines 157-164: literal `list` output used by the first test fixture (not an invented live capture).
- `com0com-3.0.0.0/setup/setup.cpp`, lines 753-795: one shared bus index formats CNCA<n>/CNCB<n> and emits endpoint ID, a space, then parameter text; list returns before the parameter-changing path. Lines 1842-1843 document `--detail-prms` / `--silent`; lines 1853-1857 document `install <prmsA> <prmsB>`; lines 2060-2062 dispatch `list` to `Change(NULL,NULL)`.
- `com0com-3.0.0.0/setup/params.cpp`, lines 801-870: `FillParametersStr` emits PortName first, optional RealPortName, comma-separated name=value flags, yes/no and pin mappings; `--detail-prms` includes default values otherwise omitted. Lines 958-960 document EmuBR/EmuOverrun; lines 979-987 explain dsr / ropen and negation.
- `com0com-3.0.0.0/setup/msg.cpp`, lines 91-108: output is written to the standard-output handle, using WriteFile when redirected, so a pipe capture is supported by the implementation.
- `com0com-3.0.0.0/requireAdministrator.manifest`: administrator execution policy exists upstream; no assumption that list works unelevated for every binary/build.

Microsoft API references:
`https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw`
`https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects`
`https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute`

### Literal documentation fixture

```text
       CNCA0 PortName=COM#,RealPortName=COM3
       CNCB0 PortName=COM#,RealPortName=COM4
```

Result: identity verified, mapping NOT verified (values absent). Strict runtime check fails closed.

### Configured grammar fixture from the fake executable

```text
 CNCA0 PortName=COM7,EmuOverrun=yes
 CNCB0 PortName=COM8,dsr=ropen,EmuBR=yes
```

Result: identity AND requested configuration mappings verified for packaging COM7 / bridge COM8, only within the fixture. Not a real device capture. Actual driver identity/pins/hardware remain UNVERIFIED on this machine.

## Isolated tests

Run `python tests/provisioning/run_tests.py` from the project root. Compiler: supplied VS ScopeCppSDK vc15 x64 cl.exe; `/W4 /WX /std:c++14 /MT /D_WIN32_WINNT=0x0601 /SUBSYSTEM:CONSOLE,6.01`. It builds and runs only fake setupc and the isolated test executable, never the GUI or any installed setupc. Outputs go to `E:/Download/AI/hermes/cache/scratch/sr5000 provisioning & tests/` (spaces and `&` deliberately exercise exact-path launch). The official ZIP was only read in memory; no driver or downloaded executable was run.

Coverage: original BAT interpolation regression (RED before replacement, GREEN after), upstream literal fixture, source-derived mapping fixture, same/different pair and reversed-role failures, duplicate/ambiguous/malformed/oversized/NUL inputs, canonical COM command-injection rejection, fixed administrator command data only, fixed fake argv, pipe flood limit, process timeout, nonzero exit, invalid stdout, non-inheritance of an unrelated inheritable event and repeated-capture handle count. No real driver installation, device/registry/autostart/security setting changes, or hardware operation is tested or performed.
