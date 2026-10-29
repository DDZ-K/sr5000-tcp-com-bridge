import pathlib, re, sys
root = pathlib.Path(__file__).resolve().parents[2]
text = (root / "create_port_pair.bat").read_text(encoding="utf-8-sig")
assert not re.search(r"%[~*0-9]", text.replace("%~dp0", "")), "unsafe: BAT expands caller arguments"
assert not re.search(r"\b(setupc|powershell|runas|cmd|install)\b", "\n".join(l for l in text.splitlines() if not l.lower().startswith("rem ")), re.I), "BAT must only launch native GUI"
assert '"%~dp0SR5000Bridge-v2.1.exe"' in text, "missing exact sibling GUI launcher"
assert 'DisableDelayedExpansion' in text, "delayed expansion must be disabled"
print("PASS static launcher: no argument interpolation or elevated install path")
