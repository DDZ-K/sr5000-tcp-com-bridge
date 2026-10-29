# v1.3.1-field.1 Win10 x64 preparation profile verification

## Scope
- Win7 x64 keeps its original installer path, exact size/SHA256 and required Authenticode check.
- Win10 uses real `RtlGetVersion`, workstation only, native AMD64, major/minor 10.0, builds 10240..21999. Win11, Server, x86 and native ARM64 reject bundled automatic installation. Existing trusted setupc discovery/pair creation is unchanged.
- Win10 package path: `drivers/win10-x64/setup-com0com-2.2.2-x64-signed.exe`; the same directory must contain the extracted `com0com.cat` and `com0com.sys`.
- Already-installed trusted tooling wins: no automatic downgrade, uninstall or repeated installation. Create commands continue using explicit numeric COM names, not COM#.

## Fixed artifacts and trust checks
- Installer: 216423 bytes; SHA256 `64cf92e5b56f94c1ca14bbbbdcf0cb38b866241c6400e67b5e41c58daec39c12`.
- CAT: 8221 bytes; SHA256 `0cede12ac635cbe4cd460d420caa3c9c41fb6bc7642fd81e053ff67db08e7694`.
- SYS inspected during verification: 76800 bytes; SHA256 `bca2c9f4cb587242470cf07ea2a7e0c33de8c8026c06f37a181ace1992d6108b` (membership, not a separate hardcoded full-file hash, is checked at runtime).
- Runtime verifies fixed installer hash, fixed CAT hash, CAT Authenticode, and actual SYS catalog membership with `CryptCATAdminCalcHashFromFileHandle` plus `WinVerifyTrust(WTD_CHOICE_CATALOG)`.
- Installer, CAT and SYS remain locked against modification/deletion, and their ancestor directories against rename/deletion, until the child installer exits; reparse points are refused. All verification is read-only and does not register catalogs.
- Outer installer is genuinely unsigned. Confirmation explicitly says: “安装向导本身未签名，包来源和固定哈希已核对，驱动目录签名已验证”. No fake Authenticode success is used for that installer.
- Generic Authenticode trust is not target-kernel-policy acceptance. Old cross-signing/current Win10 policy differences remain a field-trial limit. Code 52 means stop and report, not disable signature enforcement or Secure Boot.

## Actual RED -> GREEN runs
1. Added simulated Win10 confirmation test: build/test exited 77 at `scanner_bridge_test.cpp:87` because old Win7-only gate refused Win10. Added separate real-version profile selection; complete regression then exited 0.
2. Added independent Win10 installer-path/prompt test: exited 77 at line 89 because Win7 path was still selected. Independent path and unsigned-installer warning implemented; complete regression exited 0.
3. Real Win10 installer passed into original verification: exited 77 at line 315 (`valid`), correctly demonstrating the Win7 fixed artifact profile could not validate Win10. Added explicit `verifyWin10` with its fixed hash and signed-catalog SYS verification; real-artifact regression exited 0.
4. Added profile-aware Win10 guidance test: exited 77 at line 87 because old guidance still rejected Win10. Updated driver-check/status/preparation text; complete regression exited 0.

## Final actual build/test
Command (Git Bash; native Windows paths are intentional):

```bash
SR5000_TEST_DRIVER='E:\Download\AI\projects\sr5000-tcp-com-bridge\artifacts\win7-driver\Setup_com0com_v3.0.0.0_W7_x64_signed.exe' \
SR5000_TEST_WIN10_DRIVER='E:\Download\AI\hermes\cache\scratch\win10-profile-readonly\setup-com0com-2.2.2-x64-signed.exe' \
python E:/Download/AI/projects/sr5000-tcp-com-bridge/tcp-com-simple/build_local.py
```

- Final production `/W4 /WX` build and `/DNDEBUG` complete application regression: exit 0.
- Full output: `E:/Download/AI/hermes/cache/scratch/win10-profile-final-build.log`.
- Actual Win7 signature/hash/lock tests passed.
- Actual Win10 fixed installer/CAT/signature/SYS-member tests passed; unsigned outer installer explicitly rejected by standalone Authenticode check as expected.
- Temporary-copy negative tests passed: tampered SYS while genuine CAT remains trusted, missing CAT, tampered CAT, tampered installer, both cross-profile installer selections. File write/deletion lock checks passed for all three files; all temporary copies removed.
- Version-boundary/server/x86/ARM64 rejection, GUI explicit cancellation, verification failure, installed-driver preservation, unsigned prompt, Win7 asynchronous lifecycle, timeout/reentry and inventory-readback regressions passed. GUI installer/device actions are explicit simulations; no real installer was executed.
- Original TCP worker/keyboard/forwarding/startup regression passed. Test asserts real HKCU Run value is unchanged.
- Settings-through-TcpEngine section and `tcp_codecs.h`, `serial_forward.h`, `native_inventory.h`, `com0com_inventory.h` compare equal to preserved `tcp-com-ui` baseline. Engine section SHA256: `9e514aa8c2491c873947821ce599a52d13c0706cdd32a48edef53c11e3119727`.

## Not claimed
No driver installation, catalog registration, device creation, real startup change or signature/security-policy change was performed. Win7/Win10 driver loading and production scanner/host-application acceptance remain field work. Distribution/source/license provenance, final package screenshots and release publication are parent-agent scope.
