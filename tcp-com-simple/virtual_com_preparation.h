// Preparation UI only; never installs or creates anything on window open.
HWND preparationWindow=nullptr;
const UINT COM_PREPARE=311,PREP_BRIDGE=7001,PREP_APP=7002,PREP_INSTALL=7003,PREP_CREATE=7004,PREP_CHECK=7005,PREP_STATUS=7006,PREP_LOG=7007;
std::wstring preparationText(UINT id){wchar_t value[1025]={};GetWindowTextW(GetDlgItem(preparationWindow,(int)id),value,1025);return value;}
void preparationStatus(const std::wstring& message){
    if(preparationWindow){SetWindowTextW(GetDlgItem(preparationWindow,PREP_STATUS),message.c_str());HWND log=GetDlgItem(preparationWindow,PREP_LOG);if(GetWindowTextLengthW(log)>30000)SetWindowTextW(log,L"");SendMessageW(log,EM_SETSEL,(WPARAM)-1,(LPARAM)-1);std::wstring line=timestamp()+L" "+message+L"\r\n";SendMessageW(log,EM_REPLACESEL,FALSE,(LPARAM)line.c_str());}
    engine.info(L"虚拟串口准备",message);
}
std::wstring preparationSetup(){
#ifdef TCP_TESTER_TEST
    if(preparationSimulation.active)return preparationSimulation.setup;
#endif
    return serialbridge::findSetupc();
}
preparationsecurity::LockedInstaller installerPin;
void preparationEnable(){
    bool idle=!driverBusy()&&!pairProcess&&!pairLaunching&&!pairBlocked;
    for(UINT id:{PREP_INSTALL,PREP_CREATE,PREP_BRIDGE,PREP_APP,PREP_CHECK})EnableWindow(GetDlgItem(preparationWindow,(int)id),idle);
    EnableWindow(GetDlgItem(window,CONNECT),idle);EnableWindow(GetDlgItem(window,COM_PAIR),idle);
}
enum class PreparationProfile {Unsupported,Win7x64,Win10x64};
PreparationProfile preparationProfileFor(DWORD major,DWORD minor,DWORD build,BYTE product,WORD architecture){
    if(product!=VER_NT_WORKSTATION||architecture!=PROCESSOR_ARCHITECTURE_AMD64)return PreparationProfile::Unsupported;
    if(major==6&&minor==1)return PreparationProfile::Win7x64;
    if(major==10&&minor==0&&build>=10240&&build<22000)return PreparationProfile::Win10x64;
    return PreparationProfile::Unsupported;
}
PreparationProfile preparationProfile(){
#ifdef TCP_TESTER_TEST
    if(preparationSimulation.active)return preparationSimulation.win7?PreparationProfile::Win7x64:(preparationSimulation.win10?PreparationProfile::Win10x64:PreparationProfile::Unsupported);
#endif
    // RtlGetVersion reports the real OS, not the manifest/compatibility-lied GetVersionEx.
    typedef LONG (WINAPI *VersionFunction)(OSVERSIONINFOW*);
    FARPROC address=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion");VersionFunction version=nullptr;
    static_assert(sizeof(version)==sizeof(address),"function pointer size");memcpy(&version,&address,sizeof(version));
    OSVERSIONINFOEXW info={};info.dwOSVersionInfoSize=sizeof(info);SYSTEM_INFO system={};GetNativeSystemInfo(&system);
    return version&&version((OSVERSIONINFOW*)&info)==0?preparationProfileFor(info.dwMajorVersion,info.dwMinorVersion,info.dwBuildNumber,info.wProductType,system.wProcessorArchitecture):PreparationProfile::Unsupported;
}
void preparationCandidates(){
    const std::wstring beforeBridge=preparationText(PREP_BRIDGE),beforeApp=preparationText(PREP_APP);std::vector<std::wstring> choices;
    // Suggestions only: adjacent free candidates, not a claim that every unused COM is listed.
    // Every create is checked again against OS existence, reservations and full driver inventory.
    std::wstring error;
    for(int number=1;number<256&&choices.size()<32;++number){SerialOptions o;o.enabled=true;o.bridge=L"COM"+std::to_wstring(number);o.application=L"COM"+std::to_wstring(number+1);
        if(namesFree(o,error))for(const auto& name:{o.bridge,o.application})if(std::find(choices.begin(),choices.end(),name)==choices.end())choices.push_back(name);
    }
    for(UINT id:{PREP_BRIDGE,PREP_APP}){HWND combo=GetDlgItem(preparationWindow,(int)id);SendMessageW(combo,CB_RESETCONTENT,0,0);for(const auto& name:choices)SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)name.c_str());}
    SetWindowTextW(GetDlgItem(preparationWindow,PREP_BRIDGE),beforeBridge.empty()&&choices.size()>1?choices[0].c_str():beforeBridge.c_str());
    SetWindowTextW(GetDlgItem(preparationWindow,PREP_APP),beforeApp.empty()&&choices.size()>1?choices[1].c_str():beforeApp.c_str());
    if(choices.empty())preparationStatus(L"未找到可验证的空闲COM建议；请检查系统COM保留表权限。可手填，但不能跳过创建前检查。");
}
void checkPreparationDriver(){
    preparationCandidates();
    const auto profile=preparationProfile();
    const std::wstring advice=profile==PreparationProfile::Win7x64?L"Win7 x64可点击安装驱动。":(profile==PreparationProfile::Win10x64?L"Win10 x64试用驱动2.2.2；已有驱动优先保留。代码52停下反馈，不关闭签名或安全启动。":L"Win11及其他系统拒绝包内自动安装，请使用已获批的适配驱动。");
    preparationStatus(preparationSetup().empty()?L"未发现可信的com0com驱动管理程序。"+advice:L"已发现受管理员保护的驱动管理程序。已有串口对优先保留；新建请选两个空闲COM。");
}
void installPreparationDriver(){
    cancelStartup();
    if(driverBusy()||pairProcess||pairLaunching||pairBlocked){preparationStatus(L"系统设置尚未结束，禁止重复操作；不会强杀进程。");return;}
    if(engine.running||engine.serialOpened||engine.receiveOnline||engine.sendOnline){preparationStatus(L"请先断开桥接连接并关闭目标软件串口，再准备虚拟串口。");return;}
    const auto profile=preparationProfile();
    if(profile==PreparationProfile::Unsupported){preparationStatus(L"包内自动安装仅支持 Windows 7 x64 或 Windows 10 x64（试用）；Win11及其他系统拒绝安装。若已安装可信setupc，仍可创建新串口对。");return;}
    if(!preparationSetup().empty()){preparationStatus(L"已发现可信驱动管理程序，保留现有驱动；不重复安装。可选择空闲COM创建新对，或返回主界面核验已有对。");return;}
    const bool win10=profile==PreparationProfile::Win10x64;
    std::wstring path=directory+(win10?preparationsecurity::Win10InstallerRelative:preparationsecurity::InstallerRelative),error;bool verified=false;
#ifdef TCP_TESTER_TEST
    if(preparationSimulation.active){verified=preparationSimulation.verified;error=L"模拟签名/哈希校验失败";}
    else
#endif
    verified=win10?preparationsecurity::verifyWin10(path,installerPin,error):preparationsecurity::verify(path,installerPin,error);
    if(!verified){preparationStatus(L"安装器校验失败："+error);return;}
    driverLaunching=true;preparationEnable();bool approved=false;
    const std::wstring prompt=(win10?std::wstring(L"Win10 x64试用驱动2.2.2；已有驱动优先保留。\r\n安装向导本身未签名，包来源和固定哈希已核对，驱动目录签名已验证。\r\n旧证书不保证每个Win10安全策略可加载；代码52请停下反馈，不关闭签名或安全启动。\r\n"):std::wstring(L"包内固定Win7 x64安装器的SHA256及Authenticode已校验。\r\n"))+L"将请求Windows管理员授权，请在安装器中核对选项。已有COM对不要重复创建。\r\n安装完成后本工具只重新检查驱动，不自动创建COM，不连接TCP、不发指令。继续吗？";
#ifdef TCP_TESTER_TEST
    if(preparationSimulation.active){preparationSimulation.prompt=prompt;approved=preparationSimulation.confirm;}
    else
#endif
    approved=MessageBoxW(preparationWindow,prompt.c_str(),L"确认安装虚拟串口驱动",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)==IDYES;
    if(!approved){driverLaunching=false;installerPin.reset();preparationEnable();preparationStatus(L"已取消安装，未更改系统。");return;}
    SHELLEXECUTEINFOW start={};start.cbSize=sizeof(start);start.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;start.hwnd=preparationWindow;start.lpVerb=L"runas";start.lpFile=path.c_str();start.lpParameters=nullptr;start.nShow=SW_SHOWNORMAL;
    bool launched=false;
#ifdef TCP_TESTER_TEST
    // Tests can only return an injected event handle; never run a real installer.
    if(preparationSimulation.active){++preparationSimulation.launches;preparationSimulation.launchedPath=path;preparationSimulation.launchedArguments=start.lpParameters?start.lpParameters:L"";launched=preparationSimulation.launch;start.hProcess=preparationSimulation.child;SetLastError(ERROR_CANCELLED);}
#else
    launched=ShellExecuteExW(&start)!=FALSE;
#endif
    DWORD launchError=GetLastError();driverLaunching=false;
    if(!launched){installerPin.reset();preparationEnable();preparationStatus(L"安装器启动失败或UAC取消，Windows错误 "+std::to_wstring(launchError)+L"。未自动创建串口。");return;}
    if(!start.hProcess){driverBlocked=true;preparationEnable();preparationStatus(L"安装器没有返回可核验句柄；禁止重复安装/创建。请人工检查系统安装窗口，退出工具后再核查。");return;}
    driverProcess=start.hProcess;driverStarted=GetTickCount64();driverWarned=false;preparationEnable();preparationStatus(L"安装器已启动，请完成系统窗口。结束前禁止重复操作；不会自动创建或连接。");
}
void pollPreparationDriver(){
    if(!driverProcess)return;DWORD wait=WaitForSingleObject(driverProcess,0);
    if(wait==WAIT_TIMEOUT){if(!driverWarned&&GetTickCount64()-driverStarted>120000){driverWarned=true;preparationStatus(L"安装器仍在运行（超过120秒），请检查系统窗口；不强杀，仍禁止重复操作。");}return;}
    if(wait!=WAIT_OBJECT_0){driverBlocked=true;preparationEnable();preparationStatus(L"无法核验安装进程状态，保持锁定；请人工检查，不重复安装。");return;}
    DWORD exitCode=999;BOOL known=GetExitCodeProcess(driverProcess,&exitCode);
#ifdef TCP_TESTER_TEST
    if(preparationSimulation.active){known=TRUE;exitCode=0;} // injected event is not a process
#endif
    CloseHandle(driverProcess);driverProcess=nullptr;installerPin.reset();preparationEnable();
    const std::wstring summary=L"安装器已退出（"+(known?std::to_wstring(exitCode):std::wstring(L"未知"))+L"）。";
    preparationStatus(summary+(preparationSetup().empty()?L"未发现可信setupc；不能认定驱动已就绪。若安装要求重启，请手动重启后重新检查；不要重复盲装。":L"已发现可信setupc。未自动创建串口；请再次核对两个新COM，再点击创建串口对。实际驱动加载/签名仍需现场确认。"));
}
void preparationPairResult(bool ok,const std::wstring& error){
    preparationCreating=false;
    if(ok){set(COM_BRIDGE,pairOptions.bridge);set(COM_APP,pairOptions.application);preparationStatus(L"库存已核验，已回填主界面：桥接端 "+pairOptions.bridge+L"，软件端 "+pairOptions.application+L"。请关闭本窗口，在目标软件选择软件端，再手动保存并连接。未自动连接或发指令。");}
    else preparationStatus(L"创建未核验成功："+error+L"。可能部分生效，请检查系统库存；不会覆盖或自动重试。");
    preparationEnable();
}
void createPreparationPair(){
    cancelStartup();
    if(driverBusy()||pairProcess||pairLaunching||pairBlocked){preparationStatus(L"系统设置尚未结束，禁止重复创建。");return;}
    if(engine.running||engine.serialOpened||engine.receiveOnline||engine.sendOnline){preparationStatus(L"请先断开桥接连接，并关闭目标软件串口。");return;}
    const std::wstring setup=preparationSetup();if(setup.empty()){preparationStatus(L"请先安装适配驱动，再检查是否发现可信setupc。Win7/Win10 x64分别使用对应包；Win11拒绝包内自动安装。");return;}
    SerialOptions o=serialFromUI();o.bridge=upperCom(preparationText(PREP_BRIDGE));o.application=upperCom(preparationText(PREP_APP));std::wstring error;
    if(!validSerialOptions(o)||!namesFree(o,error)){preparationStatus(error.empty()?L"请输入两个不同的有效COM号，并核对主界面串口参数。":error);return;}
    com0com::Inventory inventory;bool captured=false;
#ifdef TCP_TESTER_TEST
    if(preparationSimulation.active){std::string detail;captured=com0com::ParseInventory(preparationSimulation.inventory,inventory,detail);error=serialbridge::wide(detail);}
    else
#endif
    {auto capture=com0com::CaptureInventory(setup,2000);captured=capture.status==com0com::CaptureStatus::Ok;inventory=capture.inventory;if(!captured)error=com0com::DescribeCaptureFailure(capture);}
    if(!captured){preparationStatus(L"不能核验现有驱动库存，拒绝创建："+error);return;}
    for(const auto& endpoint:inventory.endpoints)if(endpoint.portName==serialbridge::narrow(o.bridge)||endpoint.portName==serialbridge::narrow(o.application)){preparationStatus(L"所选COM已在驱动库存中，禁止覆盖；已有对请回主界面用配对设置核验。");return;}
    std::wstring prompt=L"创建新虚拟串口对：本工具桥接端 "+o.bridge+L" ↔ 目标软件端 "+o.application+L"。\r\n不覆盖已存在或保留的COM，需管理员授权。确认两端串口已关闭，继续吗？";
    bool approved=false;pairLaunching=true;preparationEnable();
#ifdef TCP_TESTER_TEST
    if(preparationSimulation.active)approved=preparationSimulation.confirm;
    else
#endif
    approved=MessageBoxW(preparationWindow,prompt.c_str(),L"确认创建虚拟串口",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)==IDYES;
    pairLaunching=false;
    if(!approved){preparationStatus(L"已取消创建，未更改系统。");preparationEnable();return;}
    std::wstring parameters;if(!newPairCommand(o,setup,parameters,error)){preparationStatus(error);preparationEnable();return;}
    pairOptions=o;pairSetup=setup;pairCommands.clear();pairCommands.push_back(parameters);preparationCreating=true;
    if(!launchPairCommand())preparationPairResult(false,L"管理员确认取消、路径信任失败、缺少句柄或启动失败；详情见主界面日志");
    else preparationStatus(L"已请求管理员创建。请完成系统窗口，等待真实库存回读；未自动连接TCP。");
    preparationEnable();
}
LRESULT CALLBACK preparationProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_CREATE){preparationWindow=h;
        preferenceControl(h,L"STATIC",L"1  检查 / 安装驱动",0,24,18,600,28,0);
        preferenceControl(h,L"STATIC",L"已有对/驱动优先保留；Win10 x64试用驱动2.2.2，Win11不自动安装。\r\n不保证各Win10安全策略加载；代码52停下反馈，不关闭签名或安全启动。",0,24,54,600,54,0);
        preferenceControl(h,L"BUTTON",L"检查驱动",WS_TABSTOP,24,116,125,34,PREP_CHECK);
        preferenceControl(h,L"BUTTON",L"安装驱动",WS_TABSTOP,164,116,125,34,PREP_INSTALL);
        preferenceControl(h,L"STATIC",L"2  选择两个新COM（可下拉或手填）",0,24,174,600,28,0);
        preferenceControl(h,L"STATIC",L"桥接端：本工具打开",0,24,214,270,26,0);
        preferenceControl(h,L"STATIC",L"软件端：目标软件打开",0,334,214,280,26,0);
        preferenceControl(h,L"COMBOBOX",L"",CBS_DROPDOWN|WS_VSCROLL|WS_TABSTOP,24,246,270,190,PREP_BRIDGE);
        preferenceControl(h,L"COMBOBOX",L"",CBS_DROPDOWN|WS_VSCROLL|WS_TABSTOP,334,246,270,190,PREP_APP);
        preferenceControl(h,L"STATIC",L"扫码枪 TCP → 本工具 → 桥接端 ↔ 软件端 → 目标软件\r\n创建后回填主界面，不会自动连接TCP或发送指令。",0,24,292,600,54,0);
        preferenceControl(h,L"BUTTON",L"创建串口对",WS_TABSTOP,24,354,155,36,PREP_CREATE);
        preferenceControl(h,L"STATIC",L"先检查驱动；只在明确点击并确认后进行系统设置。",0,24,406,600,60,PREP_STATUS);
        preferenceControl(h,L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,24,474,600,110,PREP_LOG);SetTimer(h,1,100,nullptr);return 0;
    }
    if(m==WM_COMMAND){if(LOWORD(w)==PREP_CHECK)checkPreparationDriver();else if(LOWORD(w)==PREP_INSTALL)installPreparationDriver();else if(LOWORD(w)==PREP_CREATE)createPreparationPair();return 0;}
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLORBTN){SetTextColor((HDC)w,operatorui::Ink);SetBkMode((HDC)w,TRANSPARENT);return (LRESULT)operatorui::paper;}
    if(m==WM_TIMER){pollPreparationDriver();pollPair();preparationEnable();return 0;}
    if(m==WM_CLOSE){if(driverProcess||driverLaunching||pairProcess||pairLaunching){preparationStatus(L"设置进程尚未结束，请完成或取消系统窗口。不会强杀进程。");return 0;}DestroyWindow(h);return 0;}
    if(m==WM_DESTROY){KillTimer(h,1);preparationWindow=nullptr;EnableWindow(window,TRUE);SetForegroundWindow(window);return 0;}
    return DefWindowProcW(h,m,w,l);
}
void openPreparation(){
    cancelStartup();if(preparationWindow){SetForegroundWindow(preparationWindow);return;}
    WNDCLASSW wc={};wc.lpfnWndProc=preparationProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"SR5000VirtualComPreparation";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=operatorui::paper;RegisterClassW(&wc);
    RECT owner;GetWindowRect(window,&owner);RECT size={0,0,operatorui::px(648),operatorui::px(608)};AdjustWindowRectEx(&size,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);
    preparationWindow=CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,L"创建虚拟串口",WS_CAPTION|WS_SYSMENU,owner.left+(owner.right-owner.left-size.right+size.left)/2,owner.top+(owner.bottom-owner.top-size.bottom+size.top)/2,size.right-size.left,size.bottom-size.top,window,nullptr,wc.hInstance,nullptr);
    if(preparationWindow){checkPreparationDriver();preparationEnable();EnableWindow(window,FALSE);ShowWindow(preparationWindow,SW_SHOW);}
}
