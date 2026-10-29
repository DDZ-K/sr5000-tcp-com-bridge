# com0com Win10 x64 Secure Boot 只读研究结论

## 可继续验证的候选

官方 `com0com-2.2.2.0-x64-fre-signed.zip` 是 **pre-2015 交叉签名例外候选**，不是新 Microsoft/attestation 签名版；不能仅凭静态检查保证任意 Win10 x64 Secure Boot 系统可加载。[18][8]

本机只读签名检查：CAT `Valid`；签名者 `Steven William Hatchett`，证书有效期 `2010-09-22` 至 `2011-09-23`，发行者 `GlobalSign ObjectSign CA`，时间戳签名者 `Certum Time-Stamping Authority`。使用 cryptography 解析 CAT 内证书，发现 `GlobalSign Root CA` 的发行者为 `Microsoft Code Verification Root`：这是旧交叉证书链，不是 Microsoft Hardware Compatibility Publisher 签名。

Microsoft 官方政策明确：Win10 1607 起原则上要求 Dev Portal 签名；例外包括由 2015-07-29 之前颁发的终端证书签署，且链到受支持交叉签名 CA 的驱动。这使该 2.2.2 包具有政策依据，但不等于已验证当前目标机的证书撤销/代码完整性/内存完整性策略，更不等于完成干净 Win10 Secure Boot 安装验收。[8]

## 原接口兼容性

已下载官方 **2.2.2.0 源码 ZIP**，SHA256 `0dd767a0b9949c380ce513e91d82b71211b0c9e10a82a30181ae786d472ee766`。[17]

- `include/com0com.h`：`C0C_PIN_ROPEN = 0x00000080`，`C0C_DEFAULT_PIN_DSR = C0C_PIN_RDTR`；DSR 不是默认映射 ropen，需要显式设置。
- `setup/params.cpp`：包含 `dsr` 参数，`ropen` 的解析/序列化，以及 `EmuOverrun` flag。
- 父代理提供的实际 2.2.2 payload 中，`setup.dll` 静态字符串同样包含 `ropen`、`EmuOverrun`、`remote/local port is open`，`com0com.sys` 中包含 `EmuOverrun` / `dsr` 参数名。没有执行任何 setupc 或驱动安装器。
- `ReadMe.txt` 有 `change CNCB0 EmuOverrun=yes` 例子。因此静态证据支持 `dsr=ropen` 和 `EmuOverrun`，但开关端口时 DSR 变化及缓存行为仍需目标机实测。[17]
- 注意 3.0 Changes 写明“Added ability to put the port to the Ports class”；2.2.2 不是 3.0 Ports-class/COM#/RealPortName 部署路径的无条件替代，优先显式 COMn，不能直接套用新版本 provisioning 假设。

## 许可证

2.2.2 官方源码包含完整 `license.txt`（GPL v2），各源码头写明 GPL-2.0-or-later。允许商业使用/再分发，但应保留版权和许可证，并按 GPL 第3节提供匹配的完整源码或合规书面要约；最简稳妥做法是原签名二进制与同版源码、许可证一起分发。不是商业试用包。[17]

## 已排除或未找到

官方 3.0 signed ZIP 实际下载 SHA256 `6e5d4359865277430d4ae88c73fb7e648a0ed8e81aea5002478179cfcb0bb0e1`。只解包，SYS/CAT 的 Authenticode 都是 Valid，签名者 CyberCircuits，终端证书起始 `2016-01-04`；不能使用 pre-2015 终端证书例外。signed-drivers 项目自己明确 Win10 1607+ 限制，并给出关闭 Secure Boot 的 workaround，故不满足本任务条件。[10][9]

检索 SourceForge、GitHub 项目和公开发行页，未找到可以核验为 Microsoft 新签名、免费可分发并兼容 setupc/ropen 的官方维护版。brakmic 新维护 fork 文档仍是 test-signing；LiJoeAllen Rust 重写 README 也要求测试签名，均无可用发行包。[11][12]

HHD 的所谓 Free Virtual Serial Ports 页面明确 `No use for commercial, government or military purposes`；再分发文档要求另取带 redistribution rights 的许可证。不能当成工厂永久免费可随包分发替代品。[13][14]

## 操作边界

未修改项目、未运行驱动/安装器/配置工具、未改变 Secure Boot/签名/注册表设置。仅保存网页、源码、下载档案和解包文件到 `E:/Download/AI/hermes/cache/scratch/com0com-research/`，另有同级研究 Python 脚本及引用账本。

## Sources

[8] https://learn.microsoft.com/en-us/windows-hardware/drivers/install/kernel-mode-code-signing-policy--windows-vista-and-later-
[9] https://sourceforge.net/projects/signed-drivers/files/com0com/v3.0
[10] https://sourceforge.net/projects/com0com/files/com0com/3.0.0.0/com0com-3.0.0.0-i386-and-x64-signed.zip/download
[11] https://github.com/brakmic/com0com/blob/main/docs/BUILDING.md
[12] https://github.com/LiJoeAllen/com0com-rust
[13] https://freevirtualserialports.com
[14] https://hhdsoftwaredocs.online/vspt/redistribution/overview.html
[17] https://sourceforge.net/projects/com0com/files/com0com/2.2.2.0/com0com-2.2.2.0.zip/download
[18] https://sourceforge.net/projects/com0com/files/com0com/2.2.2.0/com0com-2.2.2.0-x64-fre-signed.zip/download
