// UI/lifecycle only. Communication engine and wire/serial codecs remain in the proven baseline.
HWND preferencesWindow=nullptr;startup::Preferences startupPrefs;bool trayInstalled=false,startupPending=false,startupConsumed=false,rawRegistered=true;Settings startupSnapshot;std::wstring executablePath,lastTrayText,lastBalloon;HICON applicationIcon=nullptr;UINT taskbarRestartMessage=0;
const UINT WM_TRAY=WM_APP+41,WM_STARTUP_BEGIN=WM_APP+42,WM_STARTUP_CONNECT=WM_APP+43;
const UINT PREF_RUN=5001,PREF_CONNECT=5002,PREF_HOTKEY=5003,PREF_HIDDEN=5004,PREF_SHARED=5005,PREF_DELAY=5006,PREF_SAVE=5007,PREF_CANCEL=5008;
#ifdef TCP_TESTER_TEST
std::wstring startupTestKey;
#endif
const wchar_t* startupKeyForUI(){
#ifdef TCP_TESTER_TEST
    return startupTestKey.empty()?L"Software\\SR5000BridgeTestsNeverAutoRun":startupTestKey.c_str();
#else
    return startup::RunKey;
#endif
}
std::wstring preferencePath(){return directory+L"\\startup.ini";}
Settings currentFormSettings(){Settings c;c.ip=text(IP);c.receivePort=portNumber(text(RXPORT));c.sendPort=c.receivePort;c.serial=serialFromUI();return c;}
Settings savedFormSettings(){
    Settings c;c.ip=ini(L"ReaderIp");c.receivePort=portNumber(communicationPortText());c.sendPort=c.receivePort;c.serial.sharedApproved=true;c.serial.enabled=true;
    c.serial.bridge=upperCom(serialIni(L"BridgeCom"));c.serial.application=upperCom(serialIni(L"ApplicationCom"));c.serial.baud=(DWORD)_wtoi(serialIni(L"Baud",L"9600").c_str());c.serial.bits=(BYTE)_wtoi(serialIni(L"Bits",L"8").c_str());c.serial.parity=(BYTE)_wtoi(serialIni(L"Parity",L"0").c_str());c.serial.stopBits=(BYTE)_wtoi(serialIni(L"StopBits",L"0").c_str());return c;
}
void cancelStartup(){KillTimer(window,2);startupPending=false;}
void restoreMainWindow(){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);}
void refreshTray(){
    if(!trayInstalled)return;
    std::wstring state=L"SR-5000 | RX "+std::wstring(engine.receiveOnline?L"在线":L"离线")+L" | TX "+(engine.sendOnline?L"在线":L"离线")+L" | COM "+(engine.serialFault?L"故障":(engine.serialPeer?L"就绪":L"未就绪"));
    if(state!=lastTrayText){NOTIFYICONDATAW data={};data.cbSize=sizeof(data);data.hWnd=window;data.uID=1;data.uFlags=NIF_TIP;wcsncpy_s(data.szTip,state.c_str(),_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&data);lastTrayText=state;}
    std::wstring failure=engine.serialFault?engine.serialStatus():L"";
    if(!failure.empty()&&failure!=lastBalloon){NOTIFYICONDATAW data={};data.cbSize=sizeof(data);data.hWnd=window;data.uID=1;data.uFlags=NIF_INFO;data.dwInfoFlags=NIIF_ERROR;wcscpy_s(data.szInfoTitle,L"串口转发已锁定");wcsncpy_s(data.szInfo,failure.c_str(),_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&data);}
    lastBalloon=failure;
}
void addTray(){
    NOTIFYICONDATAW data={};data.cbSize=sizeof(data);data.hWnd=window;data.uID=1;data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;data.uCallbackMessage=WM_TRAY;data.hIcon=applicationIcon?applicationIcon:LoadIconW(nullptr,IDI_APPLICATION);wcscpy_s(data.szTip,L"SR-5000 扫码串口桥接");trayInstalled=Shell_NotifyIconW(NIM_ADD,&data)!=FALSE;lastTrayText.clear();
}
void removeTray(){if(trayInstalled){NOTIFYICONDATAW data={};data.cbSize=sizeof(data);data.hWnd=window;data.uID=1;Shell_NotifyIconW(NIM_DELETE,&data);trayInstalled=false;}}
void showTrayMenu(){HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,6001,L"显示主界面");AppendMenuW(menu,MF_STRING,6002,L"断开连接");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,6003,L"退出程序");POINT p;GetCursorPos(&p);SetForegroundWindow(window);UINT command=(UINT)TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr);DestroyMenu(menu);if(command==6001)restoreMainWindow();else if(command==6002)SendMessageW(window,WM_COMMAND,DISCONNECT,0);else if(command==6003)PostMessageW(window,WM_CLOSE,0,0);}
void updateDashboard(){
    set(UI_RX,engine.receiveOnline&&engine.sendOnline?L"已连接":L"未连接");
    set(UI_COM,engine.serialFault?L"故障锁定":(engine.serialPeer?L"软件端就绪":(engine.serialEnabled?L"等待软件端":L"待连接")));
    set(UI_RX_BYTES,L"RX  "+std::to_wstring(engine.rxBytes.load())+L" B");set(UI_TX_BYTES,L"TX  "+std::to_wstring(engine.txBytes.load())+L" B");set(UI_COM_BYTES,L"COM  "+std::to_wstring(engine.serialBytes.load())+L" B");refreshTray();
}
void runStartupOnce(){
    if(startupConsumed||!startupPending)return;startupConsumed=true;cancelStartup();
    if(!startupPrefs.connect)return;
    if(pairProcess||pairLaunching||pairBlocked||driverBusy()||!validSettings(startupSnapshot)){engine.info(L"启动",L"自动连接未执行：保存的IP、通讯端口或串口参数无效。请检查配置后手动连接。");restoreMainWindow();refresh();return;}
    engine.start(startupSnapshot);hotkeyBoundary=GetTickCount();keyDown=false;engine.info(L"启动",L"已按保存配置尝试连接一次；没有发送触发、没有重放旧扫码数据，失败后不自动循环重连。");refresh();
}
void beginStartup(){
    startupSnapshot=savedFormSettings();
    SendMessageW(GetDlgItem(window,HOTKEY),BM_SETCHECK,startupPrefs.hotkey&&rawRegistered?BST_CHECKED:BST_UNCHECKED,0);hotkeyBoundary=GetTickCount();keyDown=false;
    if(startupPrefs.connect){startupPending=true;SetTimer(window,2,startupPrefs.delay?startupPrefs.delay*1000:1,nullptr);engine.info(L"启动",L"将在 "+std::to_wstring(startupPrefs.delay)+L" 秒后尝试连接一次；手动连接/断开会取消该启动任务。");}
    if(startupPrefs.hidden&&trayInstalled)ShowWindow(window,SW_HIDE);refresh();
}
HWND preferenceControl(HWND h,const wchar_t* cls,const wchar_t* label,DWORD style,int x,int y,int w,int height,UINT id){HWND c=CreateWindowW(cls,label,WS_CHILD|WS_VISIBLE|style,operatorui::px(x),operatorui::px(y),operatorui::px(w),operatorui::px(height),h,(HMENU)(UINT_PTR)id,nullptr,nullptr);SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;}
LRESULT CALLBACK preferencesProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_CREATE){
        auto p=startup::Load(preferencePath());std::wstring run;startup::ReadRun(run,startupKeyForUI());
        preferenceControl(h,L"STATIC",L"启动与后台",0,24,18,500,32,0);
        for(const auto& item:std::vector<std::pair<UINT,const wchar_t*>>{{PREF_RUN,L"开机自启动（当前用户登录 Windows 后）"},{PREF_CONNECT,L"启动时自动连接已保存配置（仅尝试一次）"},{PREF_HOTKEY,L"启动时启用所选后台按键"},{PREF_HIDDEN,L"启动后收起到系统托盘"}})preferenceControl(h,L"BUTTON",item.second,BS_AUTOCHECKBOX|WS_TABSTOP,24,64+(int)(item.first-PREF_RUN)*40,510,30,item.first);
        SendMessageW(GetDlgItem(h,PREF_RUN),BM_SETCHECK,!run.empty()&&run==startup::CommandFor(executablePath)?BST_CHECKED:BST_UNCHECKED,0);
        for(const auto& item:std::vector<std::pair<UINT,bool>>{{PREF_CONNECT,p.connect},{PREF_HOTKEY,p.hotkey},{PREF_HIDDEN,p.hidden}})SendMessageW(GetDlgItem(h,(int)item.first),BM_SETCHECK,item.second?BST_CHECKED:BST_UNCHECKED,0);
        preferenceControl(h,L"STATIC",L"延迟连接",0,24,235,110,28,0);HWND delay=preferenceControl(h,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,145,232,100,180,PREF_DELAY);for(const wchar_t* value:{L"0",L"5",L"10",L"20",L"30"})SendMessageW(delay,CB_ADDSTRING,0,(LPARAM)value);std::wstring seconds=std::to_wstring(p.delay);LRESULT index=SendMessageW(delay,CB_FINDSTRINGEXACT,(WPARAM)-1,(LPARAM)seconds.c_str());SendMessageW(delay,CB_SETCURSEL,index==CB_ERR?2:index,0);preferenceControl(h,L"STATIC",L"秒",0,255,235,40,28,0);
        preferenceControl(h,L"STATIC",L"先保存连接参数，再应用启动设置。不会自动安装驱动或发送触发。\r\n登录后启动，不是登录前服务；不自动提权。最小化可收起到托盘。",0,24,278,515,56,0);
        preferenceControl(h,L"BUTTON",L"应用设置",WS_TABSTOP,300,348,115,36,PREF_SAVE);preferenceControl(h,L"BUTTON",L"取消",WS_TABSTOP,430,348,100,36,PREF_CANCEL);return 0;
    }
    if(m==WM_COMMAND){
        if(LOWORD(w)==PREF_CANCEL){DestroyWindow(h);return 0;}
        if(LOWORD(w)==PREF_SAVE){
            startup::Preferences p;const auto checked=[h](UINT id){return SendMessageW(GetDlgItem(h,(int)id),BM_GETCHECK,0,0)==BST_CHECKED;};p.connect=checked(PREF_CONNECT);p.hotkey=checked(PREF_HOTKEY);p.hidden=checked(PREF_HIDDEN);wchar_t b[32]={};GetWindowTextW(GetDlgItem(h,PREF_DELAY),b,32);p.delay=(DWORD)_wtoi(b);if(p.delay>30)p.delay=10;
            const Settings saved=savedFormSettings();if(p.connect&&!validSettings(saved)){MessageBoxW(h,L"自动连接需要有效且已保存的IP、通讯端口和串口参数。",L"启动参数未就绪",MB_ICONWARNING);return 0;}
            bool enable=checked(PREF_RUN);std::wstring before,error;LONG rc=startup::ReadRun(before,startupKeyForUI());if(rc!=ERROR_SUCCESS){MessageBoxW(h,L"无法读取现有自启动设置，未修改。",L"保存失败",MB_ICONERROR);return 0;}
            bool replaceOther=!before.empty()&&before!=startup::CommandFor(executablePath);if(replaceOther&&MessageBoxW(h,L"检测到另一个目录的本工具自启动。确认用当前目录替换（或按本次选择关闭）吗？",L"确认修改已有自启动",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES)return 0;
            startup::Preferences old=startup::Load(preferencePath());if(!startup::Save(directory,preferencePath(),p)){MessageBoxW(h,L"启动配置写入失败；自启动注册未修改。",L"保存失败",MB_ICONERROR);return 0;}
            if(!startup::SetRun(enable,executablePath,replaceOther,error,startupKeyForUI())){startup::Save(directory,preferencePath(),old);MessageBoxW(h,error.c_str(),L"自启动设置未完成",MB_ICONERROR);return 0;}
            startupPrefs=p;engine.info(L"启动设置",enable?L"已注册当前用户登录自启动，并回读核对。请保留当前程序目录，不要移动EXE。":L"当前工具的登录自启动已关闭并核对。");refresh();MessageBoxW(h,L"已保存。启动选项在下次打开程序时生效；当前通信连接不变。",L"设置已应用",MB_OK);DestroyWindow(h);return 0;
        }
    }
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLORBTN){SetTextColor((HDC)w,operatorui::Ink);SetBkMode((HDC)w,TRANSPARENT);return (LRESULT)operatorui::paper;}
    if(m==WM_ERASEBKGND){RECT r;GetClientRect(h,&r);FillRect((HDC)w,&r,operatorui::paper);return 1;}
    if(m==WM_CLOSE){DestroyWindow(h);return 0;}if(m==WM_DESTROY){preferencesWindow=nullptr;EnableWindow(window,TRUE);SetForegroundWindow(window);return 0;}
    return DefWindowProcW(h,m,w,l);
}
void openPreferences(){
    if(preferencesWindow){SetForegroundWindow(preferencesWindow);return;}WNDCLASSW wc={};wc.lpfnWndProc=preferencesProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"SR5000StartupPreferences";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=operatorui::paper;RegisterClassW(&wc);
    RECT owner;GetWindowRect(window,&owner);RECT size={0,0,operatorui::px(554),operatorui::px(402)};AdjustWindowRectEx(&size,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);int width=size.right-size.left,height=size.bottom-size.top;preferencesWindow=CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,L"启动设置",WS_CAPTION|WS_SYSMENU,owner.left+(owner.right-owner.left-width)/2,owner.top+(owner.bottom-owner.top-height)/2,width,height,window,nullptr,wc.hInstance,nullptr);if(preferencesWindow){EnableWindow(window,FALSE);ShowWindow(preferencesWindow,SW_SHOW);}
}
void createOperatorControls(HWND h){
    RECT r;GetClientRect(h,&r);operatorui::initialize(r.right,r.bottom);font=operatorui::regular;startupPrefs=startup::Load(preferencePath());startupPending=false;startupConsumed=false;
    control(L"BUTTON",L"启动设置",WS_TABSTOP,828,18,108,34,UI_SETTINGS);
    for(UINT id:{UI_RX,UI_COM}){int x=id==UI_RX?44:512;HWND c=control(L"STATIC",L"未连接",0,x,126,170,32,id);SendMessageW(c,WM_SETFONT,(WPARAM)operatorui::large,TRUE);}
    control(L"STATIC",L"RX  0 B",0,254,140,90,25,UI_RX_BYTES);control(L"STATIC",L"TX  0 B",0,354,140,90,25,UI_TX_BYTES);control(L"STATIC",L"COM  0 B",0,804,140,112,25,UI_COM_BYTES);
    control(L"STATIC",L"未连接",SS_ENDELLIPSIS,40,194,880,25,STATE);
    control(L"EDIT",ini(L"ReaderIp").c_str(),WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,146,276,298,30,IP);
    control(L"EDIT",communicationPortText().c_str(),WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,146,318,298,30,RXPORT);
    HWND mode=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,146,360,90,150,ENCODING);for(const wchar_t* name:{L"ASCII",L"HEX"})SendMessageW(mode,CB_ADDSTRING,0,(LPARAM)name);hexMode=ini(L"Encoding",L"HEX")!=L"ASCII";SendMessageW(mode,CB_SETCURSEL,hexMode?1:0,0);
    HWND payload=control(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,250,360,194,30,PAYLOAD);SendMessageW(payload,EM_SETLIMITTEXT,512,0);Bytes bytes;if(tcpcodecs::hex(ini(L"TriggerHex"),bytes))set(PAYLOAD,hexMode?tcpcodecs::formatHex(bytes):tcpcodecs::formatAscii(bytes));
    control(L"BUTTON",L"后台按键触发",BS_AUTOCHECKBOX|WS_TABSTOP,44,402,196,28,HOTKEY);HWND keys=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,262,399,182,230,KEY);for(const wchar_t* name:{L"Enter",L"Space"})SendMessageW(keys,CB_ADDSTRING,0,(LPARAM)name);for(int i=1;i<=12;++i){std::wstring name=L"F"+std::to_wstring(i);SendMessageW(keys,CB_ADDSTRING,0,(LPARAM)name.c_str());}LRESULT keyIndex=SendMessageW(keys,CB_FINDSTRINGEXACT,(WPARAM)-1,(LPARAM)ini(L"TriggerKey",L"Enter").c_str());SendMessageW(keys,CB_SETCURSEL,keyIndex==CB_ERR?0:keyIndex,0);
    control(L"BUTTON",L"创建虚拟串口",WS_TABSTOP,620,238,142,28,311);control(L"BUTTON",L"刷新",WS_TABSTOP,772,238,58,28,COM_REFRESH);control(L"BUTTON",L"配对设置",WS_TABSTOP,840,238,76,28,COM_PAIR);
    control(L"COMBOBOX",L"",CBS_DROPDOWN|WS_VSCROLL|WS_TABSTOP,590,276,106,180,COM_BRIDGE);control(L"COMBOBOX",L"",CBS_DROPDOWN|WS_VSCROLL|WS_TABSTOP,800,276,116,180,COM_APP);set(COM_BRIDGE,serialIni(L"BridgeCom"));set(COM_APP,serialIni(L"ApplicationCom"));
    HWND baud=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,512,343,112,200,COM_BAUD);for(const wchar_t* v:{L"1200",L"2400",L"4800",L"9600",L"19200",L"38400",L"57600",L"115200"})SendMessageW(baud,CB_ADDSTRING,0,(LPARAM)v);LRESULT bi=SendMessageW(baud,CB_FINDSTRINGEXACT,(WPARAM)-1,(LPARAM)serialIni(L"Baud",L"9600").c_str());SendMessageW(baud,CB_SETCURSEL,bi==CB_ERR?3:bi,0);
    HWND bits=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,638,343,62,130,COM_BITS);for(const wchar_t* v:{L"5",L"6",L"7",L"8"})SendMessageW(bits,CB_ADDSTRING,0,(LPARAM)v);SendMessageW(bits,CB_SETCURSEL,_wtoi(serialIni(L"Bits",L"8").c_str())-5,0);
    HWND parity=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,714,343,114,130,COM_PARITY);for(const wchar_t* v:{L"无 None",L"奇 Odd",L"偶 Even"})SendMessageW(parity,CB_ADDSTRING,0,(LPARAM)v);SendMessageW(parity,CB_SETCURSEL,_wtoi(serialIni(L"Parity",L"0").c_str()),0);
    HWND stop=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,842,343,74,110,COM_STOP);for(const wchar_t* v:{L"1",L"2"})SendMessageW(stop,CB_ADDSTRING,0,(LPARAM)v);SendMessageW(stop,CB_SETCURSEL,serialIni(L"StopBits",L"0")==L"2"?1:0,0);
    control(L"BUTTON",L"保存并连接",WS_TABSTOP,24,464,176,36,CONNECT);control(L"BUTTON",L"断开",WS_TABSTOP,214,464,100,36,DISCONNECT);control(L"BUTTON",L"发送触发",WS_TABSTOP,328,464,128,36,SEND);control(L"STATIC",L"最小化到托盘，关闭按钮退出",0,638,470,298,26,UI_TRAY_HINT);
    control(L"BUTTON",L"清空",WS_TABSTOP,840,516,76,28,CLEAR);HWND log=control(L"EDIT",L"",ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY|WS_VSCROLL|WS_TABSTOP,40,550,880,76,LOG);SendMessageW(log,WM_SETFONT,(WPARAM)operatorui::mono,TRUE);
    refreshComs();SetTimer(h,1,100,nullptr);engine.info(L"状态",L"界面版v1.3.1-field.1：单一通讯端口全双工；收到的数据原样转发（包括可能的设备应答）。自启动须由启动设置明确开启；打开程序不安装驱动，不自动发送指令。");refresh();
}
