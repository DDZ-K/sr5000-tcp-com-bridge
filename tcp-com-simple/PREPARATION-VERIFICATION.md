# v1.3.0-field.1 准备入口验证记录

## 已交付源码/构建
- 主界面保留配对设置，新按钮「创建虚拟串口」打开独立分步窗口。两个可编辑下拉分别是桥接端/软件端；检查驱动、安装驱动、创建串口对是独立明确操作。
- 固定包内安装器，固定261544字节/SHA256，BCrypt与WinVerifyTrust校验；锁住安装器及祖先目录防替换，拒绝重解析点。只有真实Win7 workstation x64允许旧驱动；Win10/11拒绝。UAC确认，无cmd/BAT/可注入参数。
- 安装/创建句柄异步轮询，防重入，超时只警告不强杀。安装器退出后仅重新检查可信setupc，不按exit0宣称驱动成功，不自动创建/连接。
- 新建与旧配对入口共享namesFree、newPairCommand、BuildAdminCreateCommand、launchPairCommand及最终inspectPair回读。已存在/保留/库存冲突拒绝；成功回填主界面，失败不改原选择。已有对配置入口保持。

## 实际 RED → GREEN
按行为逐步添加测试并运行同一build.bat；以下都是工具实际输出，不是模拟的测试运行结果：
1. 首轮RED exit77：`FAILED scanner_bridge_test.cpp:66: GetDlgItem(h,311)`。最小独立窗口实现后全套GREEN exit0。
2. Win11阻止旧驱动RED exit77：`preparationText(7006).find(L"仅支持 Windows 7 x64")!=std::wstring::npos`；真实RtlGetVersion/架构门接入后GREEN。
3. 模拟Win7缺安装器/信任失败RED exit77：`preparationText(7006).find(L"校验失败")!=std::wstring::npos&&preparationSimulation.launches==0`；固定路径/哈希/签名校验后GREEN。
4. 明确取消/异步安装流程RED exit77：`preparationText(7006).find(L"取消")!=std::wstring::npos&&preparationSimulation.launches==0`；安装生命周期实现后GREEN。
5. 新建入口RED exit77：`preparationText(7006).find(L"先安装")!=std::wstring::npos`；安全配对创建流程后GREEN。
6. 可下拉建议RED exit77：`SendMessageW(GetDlgItem(preparation,7001),CB_GETCOUNT,0,0)>=2`；排除已有/保留的可编辑COM建议接入后GREEN。

附加覆盖：缺驱动、UAC取消/失败、无句柄锁定、超时不终止、重复拒绝、Win11拒绝安装、模拟库存成功/失败不回填、保留/现有COM/无保留证据拒绝、恶意COM文本拒绝、创建取消、默认无自动连接/无真实驱动操作。所有设备、安装与库存注入明确为模拟；不是实机创建结果。

## 最终实际执行
标准vcvars64.bat路径在本执行环境不存在。只读发现可用x64工具链：
`C:/Program Files/Microsoft Visual Studio/2022/Community/SDK/ScopeCppSDK/vc15`
`python tcp-com-simple/build_local.py` 设置该已安装工具链INCLUDE/LIB/PATH后调用未修改的build.bat。

最终执行命令（Git Bash）：
```
SR5000_TEST_DRIVER='E:\Download\AI\projects\sr5000-tcp-com-bridge\artifacts\ui-v1.2.2\SR5000-TCP-COM-Win7-x64-v1.2.2\drivers\win7-x64\Setup_com0com_v3.0.0.0_W7_x64_signed.exe' python E:/Download/AI/projects/sr5000-tcp-com-bridge/tcp-com-simple/build_local.py
```
实际exit0，生产和NDEBUG测试均/W4 /WX；输出含：
- `[TEST] real pinned driver SHA256 + Authenticode + denied modification/deletion (read-only; NEVER executed)`
- `[TEST] preparation GUI + Win11 refusal + explicit simulated Win7 installer verification/cancel/UAC/no-reentry/readback; no real installer executed`
- 全部原TCP/COM文件模拟/GUI40字节键盘/原字节转发/配置/启动回归PASS。
真实Windows Run值测试前后相同。真实安装器只复制到scratch临时副本做只读信任及文件锁验证，从未执行。

## 不变边界核验
Settings至TcpEngine完整文本SHA256仍为：
`9e514aa8c2491c873947821ce599a52d13c0706cdd32a48edef53c11e3119727`
与父代理baseline.json完全相同；tcp_codecs.h、serial_forward.h、native_inventory.h、com0com_inventory.h四文件SHA256均相同。

## 窗口自动化ID
主类 `SR5000TcpComOperator`；创建入口311，旧配对309。
新类 `SR5000VirtualComPreparation`；标题「创建虚拟串口」。
7001桥接端组合框，7002软件端组合框，7003安装驱动，7004创建串口对，7005检查驱动，7006状态，7007只读日志。
只为截图打开新窗口不会安装或创建。不要点击安装/创建进行真实操作。

## 现场/发布边界
未装任何系统驱动、未创建设备、未改真实自启、未git提交/推送、未关闭别人的程序。
Win7 x64真实UAC/安装器向导/重启/证书链/驱动实际加载及扫码枪到目标软件收发仍须现场验证；本机真实签名Valid不代表Win7证书链已具备。
父代理继续负责最终打包、实际EXE截图、发布与整包核验。可执行文件：tcp-com-simple/SR5000TcpComBridge.exe。
