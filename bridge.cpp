#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cwctype>
#include <cstdio>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace {
const UINT WM_BRIDGE_STATUS = WM_APP + 10;
const UINT WM_BRIDGE_TRAY = WM_APP + 11;
const UINT CMD_SAVE = 201, CMD_STARTUP = 202, CMD_HIDE = 203, CMD_TRAY_SHOW = 204, CMD_TRAY_EXIT = 205, CMD_CREATE_PAIR = 207;
const UINT ID_IP=101, ID_DATA=102, ID_CMD=103, ID_TRIGGER=105, ID_COM=106, ID_STATE=108, ID_KEY=109, ID_PACK_COM=110;
struct Config {
    std::wstring ip, port, com, packCom, key;
    int dataPort, commandPort;
    std::vector<unsigned char> trigger;
};
HWND g_window=nullptr;
HANDLE g_stop=nullptr, g_trigger=nullptr, g_serial=INVALID_HANDLE_VALUE;
std::thread g_worker;
std::atomic<bool> g_net(false),g_ser(false),g_pending(false),g_running(false),g_faultLatched(false);
std::atomic<bool> g_dataOnline(false),g_commandOnline(false),g_peerOnline(false);
std::mutex g_logMutex, g_statusMutex;
std::wstring g_dir,g_ini,g_status=L"未连接";
ULONGLONG g_lastEnter=0;
bool g_keyDown=false;
bool g_keyboardOK=false;
UINT g_triggerVK=VK_RETURN;
HFONT g_font=nullptr;
HWND g_edit[7]={};
std::wstring dirOf(std::wstring s){ size_t i=s.find_last_of(L"\\/"); return i==std::wstring::npos?L".":s.substr(0,i); }
std::wstring narrowAsWide(const std::string& x){ return std::wstring(x.begin(),x.end()); }
std::string ascii(const std::wstring& x){ return std::string(x.begin(),x.end()); }
std::wstring stamp(){ SYSTEMTIME t; GetLocalTime(&t); wchar_t b[40];
    swprintf_s(b, L"%04u-%02u-%02u %02u:%02u:%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond); return b; }
void logLine(const std::wstring& s){
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::wstring folder=g_dir+L"\\logs"; CreateDirectoryW(folder.c_str(),nullptr);
    std::wstring file=folder+L"\\bridge.log",old=folder+L"\\bridge.previous.log";
    WIN32_FILE_ATTRIBUTE_DATA a;
    if(GetFileAttributesExW(file.c_str(),GetFileExInfoStandard,&a) && a.nFileSizeLow>=2*1024*1024){
        DeleteFileW(old.c_str()); MoveFileW(file.c_str(),old.c_str());
    }
    HANDLE h=CreateFileW(file.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return;
    std::wstring t=stamp()+L" "+s+L"\r\n";
    int n=WideCharToMultiByte(CP_UTF8,0,t.c_str(),(int)t.size(),nullptr,0,nullptr,nullptr);
    std::string b((size_t)n,0);
    WideCharToMultiByte(CP_UTF8,0,t.c_str(),(int)t.size(),&b[0],n,nullptr,nullptr);
    DWORD w=0;WriteFile(h,b.data(),(DWORD)b.size(),&w,nullptr);CloseHandle(h);
}
void status(const std::wstring& s){
    {std::lock_guard<std::mutex> lock(g_statusMutex);if(g_status==s)return;g_status=s;}
    logLine(s); if(g_window)PostMessageW(g_window,WM_BRIDGE_STATUS,0,0);
}
std::wstring ini(const wchar_t* sec,const wchar_t* key,const wchar_t* def){
    wchar_t b[512]={};GetPrivateProfileStringW(sec,key,def,b,512,g_ini.c_str()); return b;
}
void put(const wchar_t* sec,const wchar_t* key,const std::wstring& val){ WritePrivateProfileStringW(sec,key,val.c_str(),g_ini.c_str()); }
int number(const std::wstring& s){ if(s.empty()||s.size()>5)return 0;int n=0;
    for(wchar_t c:s){if(c<L'0'||c>L'9')return 0;n=n*10+(c-L'0');}return n>0&&n<=65535?n:0; }
int digit(wchar_t c){if(c>=L'0'&&c<=L'9')return c-L'0';c=(wchar_t)towupper(c);if(c>=L'A'&&c<=L'F')return c-L'A'+10;return -1;}
bool hex(const std::wstring& s,std::vector<unsigned char>& out){out.clear();int hi=-1;
    for(wchar_t c:s){if(c==L' '||c==L'\t'||c==L'-')continue;int d=digit(c);if(d<0)return false;
        if(hi<0)hi=d;else{out.push_back((unsigned char)(hi*16+d));hi=-1;}}
    return hi<0&&!out.empty()&&out.size()<=64;
}
bool triggerBytes(const std::wstring& s,std::vector<unsigned char>& out){
    if(s.size()>=4&&_wcsnicmp(s.c_str(),L"hex:",4)==0)return hex(s.substr(4),out);
    // Bare bytes separated by spaces/dashes are treated as HEX; ordinary printable text remains literal.
    if(s.find_first_of(L" -\t")!=std::wstring::npos&&s.size()>=5){
        // Interpret only a fully-HEX token sequence as bytes; "HELLO WORLD" remains text.
        bool allHex=true, hasSeparator=false;
        for(wchar_t ch:s){if(ch==L' '||ch==L'-'||ch==L'\t')hasSeparator=true;
            else if(digit(ch)<0){allHex=false;break;}}
        if(allHex&&hasSeparator)return hex(s,out);
    }
    out.clear();if(s.empty()||s.size()>64)return false;
    for(wchar_t c:s){if(c<32||c>126)return false;out.push_back((unsigned char)c);}
    return true;
}
UINT keyCode(const std::wstring& name){
    if(!_wcsicmp(name.c_str(),L"Enter"))return VK_RETURN;
    if(!_wcsicmp(name.c_str(),L"Space"))return VK_SPACE;
    if(name.size()>=2&&(name[0]==L'F'||name[0]==L'f')){
        int n=0;for(size_t i=1;i<name.size();++i){if(name[i]<L'0'||name[i]>L'9')return 0;n=n*10+(name[i]-L'0');}
        if(n>=1&&n<=12)return VK_F1+n-1;
    }return 0;
}
bool loadConfig(Config& c,std::wstring& err){
    c.ip=ini(L"Reader",L"ReaderIp",L"");
    // Old single-port configuration can supply the data port only; the command port must be entered explicitly.
    std::wstring data=ini(L"Reader",L"DataPort",L"");
    if(data.empty())data=ini(L"Reader",L"TcpPort",L"");
    c.dataPort=number(data);c.commandPort=number(ini(L"Reader",L"CommandPort",L""));
    c.com=ini(L"Serial",L"BridgeCom",L"");c.packCom=ini(L"Serial",L"PackagingCom",L"");
    c.port=ini(L"Reader",L"TriggerHex",L"");
    c.key=ini(L"Keyboard",L"TriggerKey",L"Enter");
    if(!c.dataPort||!c.commandPort)err=L"请填写扫码接收端口和触发发送端口（1..65535）";
    else if(c.com.size()<4||_wcsnicmp(c.com.c_str(),L"COM",3)!=0||!number(c.com.substr(3))||c.com.find_first_not_of(L"0123456789",3)!=std::wstring::npos)err=L"BridgeCom 需为 COM 数字（如 COM8）";
    else if(!triggerBytes(c.port,c.trigger))err=L"触发指令需为英文文字或 HEX 字节（如 4C 4F 4E 0D）";
    else if(!keyCode(c.key))err=L"TriggerKey 只能填写 Enter、Space 或 F1 到 F12";
    else {IN_ADDR x;if(InetPtonW(AF_INET,c.ip.c_str(),&x)!=1)err=L"ReaderIp 必须是有效 IPv4 地址";}
    return err.empty();
}
void closeSock(SOCKET& s){if(s!=INVALID_SOCKET){shutdown(s,SD_BOTH);closesocket(s);s=INVALID_SOCKET;}}
bool stopped(){return WaitForSingleObject(g_stop,0)==WAIT_OBJECT_0;}
SOCKET connectTo(const std::wstring& ip,int port){
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==INVALID_SOCKET)return s;
    u_long nb=1;ioctlsocket(s,FIONBIO,&nb);
    sockaddr_in addr={};addr.sin_family=AF_INET;addr.sin_port=htons((u_short)port);
    if(InetPtonW(AF_INET,ip.c_str(),&addr.sin_addr)!=1){closeSock(s);return INVALID_SOCKET;}
    int r=connect(s,(sockaddr*)&addr,sizeof(addr));
    if(r==SOCKET_ERROR&&WSAGetLastError()!=WSAEWOULDBLOCK){closeSock(s);return INVALID_SOCKET;}
    if(r==SOCKET_ERROR){fd_set wf;FD_ZERO(&wf);FD_SET(s,&wf);timeval tv={1,0};
        if(select(0,nullptr,&wf,nullptr,&tv)<=0){closeSock(s);return INVALID_SOCKET;}
        int error=0;int len=sizeof(error);
        if(getsockopt(s,SOL_SOCKET,SO_ERROR,(char*)&error,&len)!=0||error){closeSock(s);return INVALID_SOCKET;}
    }
    BOOL keep=TRUE;setsockopt(s,SOL_SOCKET,SO_KEEPALIVE,(const char*)&keep,sizeof(keep));
    tcp_keepalive tuning={};tuning.onoff=1;tuning.keepalivetime=10000;tuning.keepaliveinterval=3000;
    DWORD used=0;if(WSAIoctl(s,SIO_KEEPALIVE_VALS,&tuning,sizeof(tuning),nullptr,0,&used,nullptr,nullptr)!=0){closeSock(s);return INVALID_SOCKET;}
    BOOL nd=TRUE;setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(const char*)&nd,sizeof(nd));
    return s;
}
bool sendAll(SOCKET s,const std::vector<unsigned char>& bytes){size_t sent=0;
    while(sent<bytes.size()&&!stopped()){
        fd_set wf;FD_ZERO(&wf);FD_SET(s,&wf);timeval tv={1,0};if(select(0,nullptr,&wf,nullptr,&tv)<=0)return false;
        int n=send(s,(const char*)bytes.data()+sent,(int)(bytes.size()-sent),0);
        if(n<0&&WSAGetLastError()==WSAEWOULDBLOCK)continue;
        if(n<=0)return false;sent+=(size_t)n;
    }return sent==bytes.size();
}
HANDLE openCom(const std::wstring& name){
    std::wstring path=L"\\\\.\\"+name;
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
    if(h==INVALID_HANDLE_VALUE)return h;
    if(!SetupComm(h,4096,4096)){DWORD code=GetLastError();CloseHandle(h);SetLastError(code);return INVALID_HANDLE_VALUE;}
    DCB d={};d.DCBlength=sizeof(d);
    if(!GetCommState(h,&d)){DWORD code=GetLastError();CloseHandle(h);SetLastError(code);return INVALID_HANDLE_VALUE;}
    d.BaudRate=115200;d.ByteSize=8;d.Parity=NOPARITY;d.StopBits=ONESTOPBIT;
    d.fOutxCtsFlow=FALSE;d.fOutxDsrFlow=FALSE;d.fDtrControl=DTR_CONTROL_ENABLE;d.fRtsControl=RTS_CONTROL_ENABLE;
    if(!SetCommState(h,&d)){DWORD code=GetLastError();CloseHandle(h);SetLastError(code);return INVALID_HANDLE_VALUE;}
    PurgeComm(h,PURGE_RXCLEAR|PURGE_TXCLEAR|PURGE_RXABORT|PURGE_TXABORT);
    return h;
}
// Requires com0com bridge-side pin mapping dsr=ropen.
bool peerOpened(HANDLE h){DWORD signals=0;return GetCommModemStatus(h,&signals)&&((signals&MS_DSR_ON)!=0);}
bool writeCom(HANDLE h,const std::vector<unsigned char>& bytes){
    if(bytes.empty())return true;
    OVERLAPPED ov={};ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!ov.hEvent)return false;
    DWORD wrote=0;BOOL ok=WriteFile(h,bytes.data(),(DWORD)bytes.size(),&wrote,&ov);
    if(!ok&&GetLastError()==ERROR_IO_PENDING){
        HANDLE ev[2]={ov.hEvent,g_stop};DWORD wait=WaitForMultipleObjects(2,ev,FALSE,1000);
        if(wait==WAIT_OBJECT_0)ok=GetOverlappedResult(h,&ov,&wrote,FALSE);
        else {CancelIoEx(h,&ov);WaitForSingleObject(ov.hEvent,INFINITE);ok=FALSE;}
    }
    bool result=ok&&wrote==bytes.size();CloseHandle(ov.hEvent);return result;
}
// No protocol framing: TCP payload (including scanner's own terminator) is unchanged.
bool relayChunk(HANDLE serial,const char* buf,int count){
    if(count<=0)return false;
    return writeCom(serial,std::vector<unsigned char>(buf,buf+count));
}
void run(Config c){
    logLine(L"工作线程启动；扫码端口="+std::to_wstring(c.dataPort)+L"；触发端口="+std::to_wstring(c.commandPort)+L"；断线不补发；串口="+c.com);
    g_running=true;
    while(!stopped()){
        if(g_faultLatched){WaitForSingleObject(g_stop,500);continue;}
        g_dataOnline=false;g_commandOnline=false;g_peerOnline=false;
        wchar_t target[1024]={};
        if(!QueryDosDeviceW(c.com.c_str(),target,1024)){
            g_ser=false;g_net=false;status(L"故障："+c.com+L" 不存在。请选择已安装 com0com 的桥接端，或由管理员点击“创建虚拟串口对”；禁止触发");
            WaitForSingleObject(g_stop,2000);continue;
        }
        HANDLE serial=openCom(c.com);
        if(serial==INVALID_HANDLE_VALUE){
            DWORD code=GetLastError();g_ser=false;g_net=false;
            status(L"故障："+c.com+L" 存在但打不开（Windows 错误 "+std::to_wstring(code)+L"）；检查占用、驱动和串口配置；禁止触发");
            WaitForSingleObject(g_stop,2000);continue;
        }
        g_serial=serial;g_ser=true;status(L"串口已打开，正在连接扫码枪...");
        SOCKET data=connectTo(c.ip,c.dataPort);
        const bool shared=(c.dataPort==c.commandPort);
        SOCKET cmd=shared?data:(data==INVALID_SOCKET?INVALID_SOCKET:connectTo(c.ip,c.commandPort));
        g_dataOnline=(data!=INVALID_SOCKET);g_commandOnline=(cmd!=INVALID_SOCKET);
        if(data==INVALID_SOCKET||cmd==INVALID_SOCKET){
            g_net=false;status(L"故障：扫码或触发 TCP 未连接；禁止触发；2 秒后重连");
        }else{
            g_net=true;
            bool hadPeer=peerOpened(serial);
            g_peerOnline=hadPeer;
            status(!g_keyboardOK?L"后台键盘输入失败；仍在监听数据，触发禁用":
                peerOpened(serial)?(shared?L"单连接在线：同一端口发送和接收；请确认扫码枪不会把指令应答当扫码数据":L"双连接在线：所选按键可触发（不限制前台程序）"):
                L"TCP 已连接；包装软件串口未打开，触发禁用");
            bool good=true;
            while(!stopped()&&good){
                bool peerNow=peerOpened(serial);
                g_peerOnline=peerNow;
                if(hadPeer&&!peerNow){g_faultLatched=true;good=false;status(L"严重故障：包装软件关闭串口；请核对实物和旧数据后手动重连");break;}
                if(peerNow&&!hadPeer)status(L"包装软件串口已打开，持续监听数据");
                hadPeer=peerNow;
                if(WaitForSingleObject(g_trigger,0)==WAIT_OBJECT_0){
                    if(g_pending.exchange(false)){
                        if(g_ser&&g_net&&g_keyboardOK&&peerOpened(serial)){
                            if(!sendAll(cmd,c.trigger)){g_faultLatched=true;status(L"严重故障：触发发送结果不确定；人工核查后才能恢复");good=false;}
                            else logLine(L"已发送触发命令 "+std::to_wstring(c.trigger.size())+L" 字节");
                        }else {status(L"故障：包装软件串口未打开、按键或网络状态异常；触发已拒绝");}
                    }
                }
                if(!good)break;
                fd_set rd;FD_ZERO(&rd);FD_SET(data,&rd);if(!shared)FD_SET(cmd,&rd);
                timeval tv={0,200000};int n=select(0,&rd,nullptr,nullptr,&tv);
                if(n==SOCKET_ERROR){g_faultLatched=true;good=false;status(L"严重故障：TCP 监听失败；人工核查后才能恢复");break;}
                if(!shared&&n>0&&FD_ISSET(cmd,&rd)){
                    char response[512];int k=recv(cmd,response,sizeof(response),0);
                    if(k<=0){g_faultLatched=true;good=false;g_commandOnline=false;status(L"严重故障：触发端口断开；人工核查后才能恢复");break;}
                    logLine(L"收到触发端口响应 "+std::to_wstring(k)+L" 字节（不转串口）");
                }
                if(good&&n>0&&FD_ISSET(data,&rd)){
                    char buf[4096];int k=recv(data,buf,sizeof(buf),0);
                    if(k<=0){g_faultLatched=true;good=false;g_dataOnline=false;status(L"严重故障：扫码端口断开；人工核查后才能恢复");break;}
                    if(!peerOpened(serial)){g_faultLatched=true;good=false;status(L"严重故障：包装软件串口未打开；本批数据丢弃，禁止补发");break;}
                    if(!relayChunk(serial,buf,k)){g_faultLatched=true;good=false;status(L"严重故障：串口写入失败；本批可能部分写入，人工核查后才能恢复");break;}
                    logLine(L"TCP 转 COM "+std::to_wstring(k)+L" 字节（原样转发）");
                }
            }
        }
        g_net=false;g_ser=false;g_pending=false;g_dataOnline=false;g_commandOnline=false;g_peerOnline=false;ResetEvent(g_trigger);
        if(shared)cmd=INVALID_SOCKET;
        closeSock(cmd);closeSock(data);PurgeComm(serial,PURGE_RXCLEAR|PURGE_TXCLEAR|PURGE_RXABORT|PURGE_TXABORT);
        CloseHandle(serial);g_serial=INVALID_HANDLE_VALUE;
        if(!stopped()&&!g_faultLatched)WaitForSingleObject(g_stop,2000);
    }
    g_running=false;g_dataOnline=false;g_commandOnline=false;g_peerOnline=false;status(L"桥接已停止");
}
void stopWorker(){if(g_stop)SetEvent(g_stop);if(g_worker.joinable())g_worker.join();g_net=false;g_ser=false;g_pending=false;g_dataOnline=false;g_commandOnline=false;g_peerOnline=false;ResetEvent(g_trigger);}
void startWorker(const Config& c){ResetEvent(g_stop);g_worker=std::thread(run,c);}
void setText(int id,const std::wstring& s){SetWindowTextW(GetDlgItem(g_window,id),s.c_str());}
std::wstring getText(int id){wchar_t b[512]={};GetWindowTextW(GetDlgItem(g_window,id),b,512);return b;}
void refresh(){std::wstring s;{std::lock_guard<std::mutex> lock(g_statusMutex);s=g_status;}setText(ID_STATE,s);}
void drawLinks(HDC dc){
    SetBkMode(dc,TRANSPARENT);
    HFONT oldFont=(HFONT)SelectObject(dc,g_font);
    const wchar_t* labels[]={L"扫码接收 TCP",L"触发发送 TCP",L"包装软件 COM"};
    bool connected[]={g_dataOnline.load()&&!g_faultLatched.load(),g_commandOnline.load()&&!g_faultLatched.load(),g_peerOnline.load()&&!g_faultLatched.load()};
    for(int i=0;i<3;++i){
        const int x=18+i*154,y=345;
        HBRUSH brush=CreateSolidBrush(connected[i]?RGB(0,145,80):RGB(192,0,0));
        HGDIOBJ oldBrush=SelectObject(dc,brush);HGDIOBJ oldPen=SelectObject(dc,GetStockObject(NULL_PEN));
        Ellipse(dc,x,y,x+14,y+14);SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(brush);
        SetTextColor(dc,RGB(32,32,32));TextOutW(dc,x+19,y-3,labels[i],(int)wcslen(labels[i]));
    }
    SelectObject(dc,oldFont);
}
void show(){ShowWindow(g_window,SW_SHOW);SetForegroundWindow(g_window);}
bool startup(bool enable){
    HKEY key=nullptr;LONG r=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
    if(r!=ERROR_SUCCESS)return false;
    if(enable){wchar_t path[4096];GetModuleFileNameW(nullptr,path,4096);std::wstring cmd=L"\""+std::wstring(path)+L"\" --hidden";
        r=RegSetValueExW(key,L"SR5000ComBridge",0,REG_SZ,(const BYTE*)cmd.c_str(),(DWORD)((cmd.size()+1)*sizeof(wchar_t)));
    }else r=RegDeleteValueW(key,L"SR5000ComBridge");RegCloseKey(key);return r==ERROR_SUCCESS||(!enable&&r==ERROR_FILE_NOT_FOUND);
}
void tray(bool add){NOTIFYICONDATAW x={};x.cbSize=sizeof(x);x.hWnd=g_window;x.uID=1;x.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;x.uCallbackMessage=WM_BRIDGE_TRAY;
    x.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wcscpy_s(x.szTip,L"SR-5000 TCP-COM 桥接");Shell_NotifyIconW(add?NIM_ADD:NIM_DELETE,&x);
}
void updateTrayTip(){
    NOTIFYICONDATAW x={};x.cbSize=sizeof(x);x.hWnd=g_window;x.uID=1;x.uFlags=NIF_TIP;
    std::wstring tip=L"扫码TCP:"+std::wstring(g_dataOnline&&!g_faultLatched?L"已连":L"未连")+
        L" | 触发TCP:"+(g_commandOnline&&!g_faultLatched?L"已连":L"未连")+
        L" | 包装COM:"+(g_peerOnline&&!g_faultLatched?L"已开":L"未开");
    if(g_faultLatched)tip+=L" | 故障锁定";
    static std::wstring previous;
    if(tip==previous)return;
    previous=tip;
    wcsncpy_s(x.szTip,tip.c_str(),_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&x);
}
bool comName(const std::wstring& x){
    if(x.size()<4||_wcsnicmp(x.c_str(),L"COM",3)!=0)return false;
    return number(x.substr(3))>0&&x.find_first_not_of(L"0123456789",3)==std::wstring::npos;
}
bool comExists(const std::wstring& x){
    wchar_t target[1024]={};return QueryDosDeviceW(x.c_str(),target,1024)!=0;
}
std::wstring findSetupc(){
    wchar_t p[MAX_PATH]={};
    for(const wchar_t* v:{L"ProgramFiles",L"ProgramFiles(x86)"}){
        DWORD n=GetEnvironmentVariableW(v,p,MAX_PATH);
        if(n&&n<MAX_PATH){std::wstring candidate=std::wstring(p)+L"\\com0com\\setupc.exe";
            if(GetFileAttributesW(candidate.c_str())!=INVALID_FILE_ATTRIBUTES)return candidate;}
    }
    return L"";
}
void createPair(){
    const std::wstring bridge=getText(ID_COM),pack=getText(ID_PACK_COM);
    if(!comName(bridge)||!comName(pack)||_wcsicmp(bridge.c_str(),pack.c_str())==0){
        MessageBoxW(g_window,L"请填写两个不同且有效的 COM 号（包装软件端、桥接端）。",L"无法创建串口对",MB_ICONERROR);return;
    }
    const std::wstring setup=findSetupc();
    if(comExists(bridge)||comExists(pack)){
        if(setup.empty()){
            MessageBoxW(g_window,L"所选端口已存在，不能覆盖。需要先从设备管理器核对两端是否为同一 com0com 串口对；此电脑尚未找到 setupc.exe。",L"已有串口",MB_ICONWARNING);return;
        }
        std::wstring hint=L"所选端口已存在，禁止重复创建。请管理员在命令提示符运行 setupc list，核对 "+pack+L" 与 "+bridge+L" 是否同一对；确认后直接保存配置。";
        MessageBoxW(g_window,hint.c_str(),L"核对现有虚拟串口",MB_ICONINFORMATION);return;
    }
    if(setup.empty()){
        MessageBoxW(g_window,L"本机尚未安装 com0com 驱动／找不到 setupc.exe，程序无法凭空创建内核虚拟串口。请由管理员安装适用于当前 Windows 且签名验证通过的驱动；Win10/11 不得盲装 Win7 驱动或关闭安全启动。",L"缺少虚拟串口驱动",MB_ICONERROR);return;
    }
    if(g_ser.load()||g_net.load()||g_peerOnline.load()){
        MessageBoxW(g_window,L"当前桥接正在收发。创建新串口需要先停止桥接，以免关联错误产品。请退出后再打开尚未配置连接的新副本创建。",L"桥接正在收发",MB_ICONWARNING);return;
    }
    if(g_running.load())stopWorker(); // only an offline/missing-COM retry worker; never disrupt an active bridge
    std::wstring prompt=L"即将以管理员权限创建一对虚拟串口：包装软件 "+pack+L" ↔ 桥接工具 "+bridge+
        L"。将启动系统 UAC 确认，不会覆盖现有端口；请在非生产状态先核对端口空闲。继续吗？";
    if(MessageBoxW(g_window,prompt.c_str(),L"确认创建虚拟串口对",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
    std::wstring args=L"install PortName="+pack+L",EmuOverrun=yes PortName="+bridge+L",dsr=ropen,EmuBR=yes";
    SHELLEXECUTEINFOW info={};info.cbSize=sizeof(info);info.fMask=SEE_MASK_NOCLOSEPROCESS;
    info.hwnd=g_window;info.lpVerb=L"runas";info.lpFile=setup.c_str();info.lpParameters=args.c_str();info.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&info)){
        status(L"创建虚拟串口对未执行：管理员授权被拒绝或启动失败，Windows 错误 "+std::to_wstring(GetLastError()));return;
    }
    DWORD wait=WaitForSingleObject(info.hProcess,30000);
    if(wait==WAIT_TIMEOUT){
        status(L"创建串口超时：安装器可能仍在运行，请检查设备管理器；不要再次点击创建");
    }else{
        DWORD rc=999;GetExitCodeProcess(info.hProcess,&rc);
        if(wait==WAIT_OBJECT_0&&rc==0&&comExists(pack)&&comExists(bridge)){
            status(L"已检测到 "+pack+L" 与 "+bridge+L"；请用 setupc list 和串口收发测试确认是同一对，再保存配置重连");
        }else{
            status(L"创建未验证成功（退出码 "+std::to_wstring(rc)+L"）：请检查驱动签名、设备管理器和 setupc list；未自动修改原配置");
        }
    }
    CloseHandle(info.hProcess);
}
void save(){
    const wchar_t* sections[]={L"Reader",L"Reader",L"Reader",L"Reader",L"Serial",L"Serial",L"Keyboard"};
    const wchar_t* keys[]={L"ReaderIp",L"DataPort",L"CommandPort",L"TriggerHex",L"BridgeCom",L"PackagingCom",L"TriggerKey"};
    const int ids[]={ID_IP,ID_DATA,ID_CMD,ID_TRIGGER,ID_COM,ID_PACK_COM,ID_KEY};
    std::wstring vals[7];for(int i=0;i<7;++i)vals[i]=getText(ids[i]);
    // Validate before touching the existing INI; no half-applied invalid settings.
    IN_ADDR a;std::vector<unsigned char> bytes;std::wstring err;
    if(InetPtonW(AF_INET,vals[0].c_str(),&a)!=1)err=L"ReaderIp 需填写 IPv4 地址";
    else if(!number(vals[1])||!number(vals[2]))err=L"请填写两个有效的扫码枪端口（1..65535）";
    else if(!triggerBytes(vals[3],bytes))err=L"触发内容无效（示例：T 或 hex:4C 4F 4E 0D）";
    else if(!comName(vals[4]))err=L"桥接端 COM 无效（例如 COM8）";
    else if(!comName(vals[5]))err=L"包装软件 COM 无效（例如 COM7）";
    else if(number(vals[4].substr(3))==number(vals[5].substr(3)))err=L"包装软件与桥接端必须用不同 COM 号";
    else if(!keyCode(vals[6]))err=L"触发按键只能选 Enter、Space 或 F1 到 F12";
    if(!err.empty()){MessageBoxW(g_window,err.c_str(),L"配置未保存",MB_ICONERROR);return;}
    if(number(vals[1])==number(vals[2])&&
       MessageBoxW(g_window,L"接收与发送设置为同一个扫码枪端口，只建立一条 TCP 连接。若扫码枪返回指令应答，该应答会作为扫码数据转入包装软件；未核对真实协议前禁止生产使用。确认继续保存吗？",L"同端口风险确认",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
    stopWorker();for(int i=0;i<7;++i)put(sections[i],keys[i],vals[i]);
    WritePrivateProfileStringW(L"Reader",L"TcpPort",nullptr,g_ini.c_str());
    WritePrivateProfileStringW(L"Reader",L"CommandMode",nullptr,g_ini.c_str());
    WritePrivateProfileStringW(L"Keyboard",L"PackagingExe",nullptr,g_ini.c_str());
    g_triggerVK=keyCode(vals[6]);g_keyDown=false;
    g_faultLatched=false;logLine(L"操作员确认配置和实物一致，手动解除故障锁定");
    Config c;std::wstring why;if(loadConfig(c,why)){logLine(L"配置已保存；重启工作线程");startWorker(c);}
    else status(L"配置错误："+why);
}
HWND field(const wchar_t* name,UINT id,int y,const std::wstring& val){
    HWND label=CreateWindowW(L"STATIC",name,WS_CHILD|WS_VISIBLE,18,y+3,135,22,g_window,nullptr,nullptr,nullptr);
    HWND edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",val.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,155,y,305,25,g_window,(HMENU)(UINT_PTR)id,nullptr,nullptr);
    SendMessageW(label,WM_SETFONT,(WPARAM)g_font,TRUE);SendMessageW(edit,WM_SETFONT,(WPARAM)g_font,TRUE);return edit;
}
HWND keyPicker(HWND parent,int y,const std::wstring& saved){
    HWND label=CreateWindowW(L"STATIC",L"触发按键",WS_CHILD|WS_VISIBLE,18,y+3,135,22,parent,nullptr,nullptr,nullptr);
    HWND picker=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST,155,y,305,260,parent,(HMENU)(UINT_PTR)ID_KEY,nullptr,nullptr);
    SendMessageW(label,WM_SETFONT,(WPARAM)g_font,TRUE);SendMessageW(picker,WM_SETFONT,(WPARAM)g_font,TRUE);
    SendMessageW(picker,CB_ADDSTRING,0,(LPARAM)L"Enter");
    SendMessageW(picker,CB_ADDSTRING,0,(LPARAM)L"Space");
    for(int i=1;i<=12;++i){std::wstring name=L"F"+std::to_wstring(i);SendMessageW(picker,CB_ADDSTRING,0,(LPARAM)name.c_str());}
    LRESULT index=SendMessageW(picker,CB_FINDSTRINGEXACT,(WPARAM)-1,(LPARAM)saved.c_str());
    SendMessageW(picker,CB_SETCURSEL,index==CB_ERR?0:index,0);
    return picker;
}
LRESULT CALLBACK wndProc(HWND h,UINT msg,WPARAM w,LPARAM l){
    if(msg==WM_CREATE){g_window=h;g_font=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        const wchar_t* labels[]={L"扫码枪 IPv4",L"扫码接收端口",L"触发发送端口",L"触发内容 HEX",L"桥接端 COM",L"包装软件 COM",L"触发按键"};
        const int ids[]={ID_IP,ID_DATA,ID_CMD,ID_TRIGGER,ID_COM,ID_PACK_COM,ID_KEY};
        const wchar_t* sections[]={L"Reader",L"Reader",L"Reader",L"Reader",L"Serial",L"Serial",L"Keyboard"};
        const wchar_t* keys[]={L"ReaderIp",L"DataPort",L"CommandPort",L"TriggerHex",L"BridgeCom",L"PackagingCom",L"TriggerKey"};
        for(int i=0;i<7;++i){
            std::wstring value=ini(sections[i],keys[i],i==6?L"Enter":L"");
            if(i==1&&value.empty())value=ini(L"Reader",L"TcpPort",L"");

            if(i==6){g_edit[i]=keyPicker(h,16+35*i,value);continue;}
            g_edit[i]=field(labels[i],ids[i],16+35*i,value);
        }
        HWND pairButton=CreateWindowW(L"BUTTON",L"创建虚拟串口对（需驱动/管理员）",WS_CHILD|WS_VISIBLE|WS_TABSTOP,18,306,340,30,h,(HMENU)(UINT_PTR)CMD_CREATE_PAIR,nullptr,nullptr);
        SendMessageW(pairButton,WM_SETFONT,(WPARAM)g_font,TRUE);
        HWND b=CreateWindowW(L"BUTTON",L"保存配置并重连",WS_CHILD|WS_VISIBLE|WS_TABSTOP,18,270,142,29,h,(HMENU)(UINT_PTR)CMD_SAVE,nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)g_font,TRUE);
        b=CreateWindowW(L"BUTTON",L"安装登录自启",WS_CHILD|WS_VISIBLE,167,270,142,29,h,(HMENU)(UINT_PTR)CMD_STARTUP,nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)g_font,TRUE);
        b=CreateWindowW(L"BUTTON",L"隐藏到托盘",WS_CHILD|WS_VISIBLE,316,270,144,29,h,(HMENU)(UINT_PTR)CMD_HIDE,nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)g_font,TRUE);
        HWND s=CreateWindowW(L"STATIC",L"未连接",WS_CHILD|WS_VISIBLE,18,375,460,55,h,(HMENU)(UINT_PTR)ID_STATE,nullptr,nullptr);SendMessageW(s,WM_SETFONT,(WPARAM)g_font,TRUE);

        SetTimer(h,1,500,nullptr);
        tray(true);return 0;
    }
    if(msg==WM_TIMER&&w==1){RECT area={15,342,481,365};InvalidateRect(h,&area,FALSE);updateTrayTip();return 0;}
    if(msg==WM_PAINT){PAINTSTRUCT paint={};HDC dc=BeginPaint(h,&paint);drawLinks(dc);EndPaint(h,&paint);return 0;}
    if(msg==WM_COMMAND){switch(LOWORD(w)){
        case CMD_SAVE:save();return 0;
        case CMD_CREATE_PAIR:createPair();return 0;
        case CMD_STARTUP:if(startup(true))MessageBoxW(h,L"已设置当前用户登录后自动运行。若无人登录，按键监控不会运行。",L"登录自启",MB_OK);else MessageBoxW(h,L"设置失败，请检查权限",L"错误",MB_ICONERROR);return 0;
        case CMD_HIDE:ShowWindow(h,SW_HIDE);return 0;
        case CMD_TRAY_SHOW:show();return 0;
        case CMD_TRAY_EXIT:DestroyWindow(h);return 0;
    }}
    if(msg==WM_INPUT){
        UINT size=0;
        if(GetRawInputData((HRAWINPUT)l,RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER))==0&&size>=sizeof(RAWINPUT)&&size<4096){
            std::vector<BYTE> bytes(size);
            if(GetRawInputData((HRAWINPUT)l,RID_INPUT,bytes.data(),&size,sizeof(RAWINPUTHEADER))!=UINT(-1)){
                RAWINPUT* r=reinterpret_cast<RAWINPUT*>(bytes.data());
                if(r->header.dwType==RIM_TYPEKEYBOARD&&r->data.keyboard.VKey==g_triggerVK){
                    if(r->data.keyboard.Flags&RI_KEY_BREAK)g_keyDown=false;
                    else if(!g_keyDown){g_keyDown=true;PostMessageW(h,WM_APP+12,0,0);}
                }
            }
        }
        return DefWindowProcW(h,msg,w,l);
    }
    if(msg==WM_APP+12){
        // Do not trigger while configuring this bridge, even though other foreground apps are unrestricted.
        DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
        if(foregroundPid==GetCurrentProcessId())return 0;
        Config c;std::wstring error;if(!loadConfig(c,error))return 0;
        if(g_faultLatched)return 0;
        if(!g_ser||!g_net||!g_keyboardOK){status(L"故障：TCP/串口/按键监控未在线，回车触发已拒绝");return 0;}
        ULONGLONG now=GetTickCount64();if(now-g_lastEnter<300)return 0;g_lastEnter=now;
        if(!g_pending.exchange(true))SetEvent(g_trigger);return 0;
    }
    if(msg==WM_BRIDGE_STATUS){
        refresh();std::wstring current;{std::lock_guard<std::mutex> lock(g_statusMutex);current=g_status;}
        if(current.find(L"严重故障")!=std::wstring::npos){
            NOTIFYICONDATAW notice={};notice.cbSize=sizeof(notice);notice.hWnd=h;notice.uID=1;notice.uFlags=NIF_INFO;
            wcscpy_s(notice.szInfoTitle,L"扫码桥接严重故障");wcsncpy_s(notice.szInfo,current.c_str(),_TRUNCATE);notice.dwInfoFlags=NIIF_ERROR;
            Shell_NotifyIconW(NIM_MODIFY,&notice);
        }return 0;
    }
    if(msg==WM_BRIDGE_TRAY){if(l==WM_LBUTTONDBLCLK){show();return 0;}
        if(l==WM_RBUTTONUP){POINT p;GetCursorPos(&p);HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,CMD_TRAY_SHOW,L"显示状态与设置");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,CMD_TRAY_EXIT,L"退出桥接（停止扫码）");SetForegroundWindow(h);TrackPopupMenu(menu,TPM_RIGHTBUTTON,p.x,p.y,0,h,nullptr);DestroyMenu(menu);return 0;}}
    if(msg==WM_CLOSE){ShowWindow(h,SW_HIDE);return 0;}
    if(msg==WM_DESTROY){KillTimer(h,1);tray(false);stopWorker();DeleteObject(g_font);PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,msg,w,l);
}
}
#ifndef BRIDGE_UNIT_TEST
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR args,int){
    HANDLE single=CreateMutexW(nullptr,TRUE,L"Local\\SR5000_TCP_COM_Bridge_Instance");
    if(GetLastError()==ERROR_ALREADY_EXISTS){if(single)CloseHandle(single);return 0;}
    wchar_t path[4096];GetModuleFileNameW(nullptr,path,4096);g_dir=dirOf(path);g_ini=g_dir+L"\\bridge.ini";
    g_stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);g_trigger=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    WSADATA wsa={};if(WSAStartup(MAKEWORD(2,2),&wsa)!=0)return 1;
    WNDCLASSW wc={};wc.lpfnWndProc=wndProc;wc.hInstance=instance;wc.lpszClassName=L"SR5000BridgeWindow";wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    RegisterClassW(&wc);
    HWND window=CreateWindowW(wc.lpszClassName,L"SR-5000 TCP-COM 桥接",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,505,510,nullptr,nullptr,instance,nullptr);
    if(!window)return 2;
    RAWINPUTDEVICE rid={};rid.usUsagePage=0x01;rid.usUsage=0x06;rid.dwFlags=RIDEV_INPUTSINK;rid.hwndTarget=window;
    g_keyboardOK=(RegisterRawInputDevices(&rid,1,sizeof(rid))!=FALSE);
    if(!g_keyboardOK)status(L"故障：无法注册后台键盘输入；无法触发扫码");
    Config c;std::wstring why;
    if(loadConfig(c,why)){g_triggerVK=keyCode(c.key);startWorker(c);}else status(L"配置未完成："+why);
    if(!wcsstr(args,L"--hidden")&&ini(L"Startup",L"StartHidden",L"0")!=L"1")ShowWindow(window,SW_SHOW);
    MSG m;while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}
    WSACleanup();CloseHandle(g_stop);CloseHandle(g_trigger);CloseHandle(single);return (int)m.wParam;
}
#endif
