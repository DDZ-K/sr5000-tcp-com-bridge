#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <shlobj.h>
#include <aclapi.h>
#include <sddl.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cwctype>
#include <cstdio>
#include <deque>
#include "com0com_inventory.h"
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace {
const UINT WM_BRIDGE_STATUS = WM_APP + 10;
const UINT WM_BRIDGE_TRAY = WM_APP + 11;
const UINT CMD_SAVE = 201, CMD_STARTUP = 202, CMD_HIDE = 203, CMD_TRAY_SHOW = 204, CMD_TRAY_EXIT = 205, CMD_CREATE_PAIR = 207;
const UINT ID_IP=101, ID_DATA=102, ID_CMD=103, ID_TRIGGER=105, ID_COM=106, ID_STATE=108, ID_KEY=109, ID_PACK_COM=110, ID_ENCODING=111;
struct Config {
    std::wstring ip, port, com, packCom, key;
    int dataPort, commandPort;
    std::vector<unsigned char> trigger;
};
HWND g_window=nullptr;
HANDLE g_stop=nullptr, g_trigger=nullptr, g_serial=INVALID_HANDLE_VALUE;
std::thread g_worker;
std::atomic<bool> g_net(false),g_ser(false),g_pending(false),g_running(false),g_faultLatched(false);
std::atomic<bool> g_dataOnline(false),g_commandOnline(false),g_peerOnline(false),g_pairVerified(false);
std::mutex g_logMutex, g_statusMutex;
std::wstring g_dir,g_ini,g_status=L"未连接";
struct TriggerInput{DWORD captured,generation;};
std::atomic<DWORD> g_inputGeneration(1),g_inputBoundary(0);
std::mutex g_inputMutex;std::deque<TriggerInput> g_inputs;
bool inputFresh(DWORD captured,DWORD generation,DWORD now,DWORD currentGeneration,DWORD boundary){
    return generation==currentGeneration&&(LONG)(captured-boundary)>=0&&(DWORD)(now-captured)<=250;
}
void invalidateInputs(){
    std::lock_guard<std::mutex> lock(g_inputMutex);
    g_inputBoundary=GetTickCount();++g_inputGeneration;g_inputs.clear();g_pending=false;
    if(g_trigger)ResetEvent(g_trigger);
}
void queueInput(TriggerInput input){
    std::lock_guard<std::mutex> lock(g_inputMutex);
    if(!inputFresh(input.captured,input.generation,GetTickCount(),g_inputGeneration.load(),g_inputBoundary.load())||g_inputs.size()>=16)return;
    g_inputs.push_back(input);if(g_trigger)SetEvent(g_trigger);
}
bool nextInput(TriggerInput& input){
    std::lock_guard<std::mutex> lock(g_inputMutex);
    while(!g_inputs.empty()){
        input=g_inputs.front();g_inputs.pop_front();
        if(inputFresh(input.captured,input.generation,GetTickCount(),g_inputGeneration.load(),g_inputBoundary.load()))return true;
    }return false;
}
bool g_keyDown=false;
bool g_keyboardOK=false;
UINT g_triggerVK=VK_RETURN;
HFONT g_font=nullptr;
HWND g_edit[7]={};
bool g_hexMode=true;
HANDLE g_pairInstallProcess=nullptr;ULONGLONG g_pairInstallStarted=0;bool g_pairInstallWarned=false;
std::wstring g_pairInstallPack,g_pairInstallBridge;
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
int number(const std::wstring& s){ if(s.empty()||s.size()>5)return 0;int n=0;
    for(wchar_t c:s){if(c<L'0'||c>L'9')return 0;n=n*10+(c-L'0');}return n>0&&n<=65535?n:0; }
int digit(wchar_t c){if(c>=L'0'&&c<=L'9')return c-L'0';c=(wchar_t)towupper(c);if(c>=L'A'&&c<=L'F')return c-L'A'+10;return -1;}
bool hex(const std::wstring& s,std::vector<unsigned char>& out){
    out.clear();std::vector<unsigned char> parsed;int hi=-1;
    for(wchar_t ch:s){
        if(ch==L' '||ch==L'\t'||ch==L'-'){
            if(hi>=0)return false;
            continue;
        }
        int d=digit(ch);if(d<0)return false;
        if(hi<0)hi=d;else{parsed.push_back((unsigned char)(hi*16+d));hi=-1;}
        if(parsed.size()>64)return false;
    }
    if(hi>=0||parsed.empty())return false;
    out.swap(parsed);return true;
}
bool triggerBytes(const std::wstring& s,std::vector<unsigned char>& out){
    out.clear();
    if(s.size()>=4&&_wcsnicmp(s.c_str(),L"hex:",4)==0)return hex(s.substr(4),out);
    // Only legacy INIs without an explicit mode use the former v2 interpretation.
    if(s.find_first_of(L" -\t")!=std::wstring::npos&&s.size()>=5){
        bool allHex=true;for(wchar_t ch:s)if(ch!=L' '&&ch!=L'-'&&ch!=L'\t'&&digit(ch)<0){allHex=false;break;}
        if(allHex)return hex(s,out);
    }
    if(s.empty()||s.size()>64)return false;
    for(wchar_t ch:s){if(ch<32||ch>126){out.clear();return false;}out.push_back((unsigned char)ch);}return true;
}
std::wstring formatHex(const std::vector<unsigned char>& bytes){
    const wchar_t* digits=L"0123456789ABCDEF";std::wstring result;
    for(unsigned char b:bytes){if(!result.empty())result+=L' ';result+=digits[b>>4];result+=digits[b&15];}return result;
}
std::wstring formatAscii(const std::vector<unsigned char>& bytes){
    const wchar_t* digits=L"0123456789ABCDEF";std::wstring result;
    for(unsigned char b:bytes){
        if(b=='\\')result+=L"\\\\";
        else if(b=='\r')result+=L"\\r";
        else if(b=='\n')result+=L"\\n";
        else if(b=='\t')result+=L"\\t";
        else if(b==0)result+=L"\\0";
        else if(b>=32&&b<=126)result+=(wchar_t)b;
        else{result+=L"\\x";result+=digits[b>>4];result+=digits[b&15];}
    }return result;
}
bool parseAscii(const std::wstring& text,std::vector<unsigned char>& out){
    out.clear();std::vector<unsigned char> parsed;
    for(size_t i=0;i<text.size();++i){
        wchar_t ch=text[i];
        if(ch==L'\\'){
            if(++i==text.size())return false;ch=text[i];
            if(ch==L'r')ch=L'\r';else if(ch==L'n')ch=L'\n';else if(ch==L't')ch=L'\t';else if(ch==L'0')ch=0;
            else if(ch==L'x'){
                if(i+2>=text.size())return false;int high=digit(text[i+1]),low=digit(text[i+2]);
                if(high<0||low<0)return false;ch=(wchar_t)(high*16+low);i+=2;
            }else if(ch!=L'\\')return false;
        }else if(ch<32||ch>126)return false;
        parsed.push_back((unsigned char)ch);if(parsed.size()>64)return false;
    }
    if(parsed.empty())return false;out.swap(parsed);return true;
}
bool parseTrigger(const std::wstring& text,bool hexMode,std::vector<unsigned char>& out){
    return hexMode?hex(text,out):parseAscii(text,out);
}
bool storeTrigger(const std::wstring& text,bool hexMode){
    std::vector<unsigned char> bytes;if(!parseTrigger(text,hexMode,bytes))return false;
    // INI APIs trim unquoted whitespace: quotes preserve ASCII leading/trailing spaces.
    std::wstring quoted=L"\""+text+L"\"";
    return WritePrivateProfileStringW(L"Reader",L"TriggerText",quoted.c_str(),g_ini.c_str())&&
        WritePrivateProfileStringW(L"Reader",L"TriggerHex",(L"hex:"+formatHex(bytes)).c_str(),g_ini.c_str())&&
        WritePrivateProfileStringW(L"Reader",L"TriggerEncoding",hexMode?L"HEX":L"ASCII",g_ini.c_str());
}
UINT keyCode(const std::wstring& name){
    if(!_wcsicmp(name.c_str(),L"Enter"))return VK_RETURN;
    if(!_wcsicmp(name.c_str(),L"Space"))return VK_SPACE;
    if(name.size()>=2&&(name[0]==L'F'||name[0]==L'f')){
        int n=0;for(size_t i=1;i<name.size();++i){if(name[i]<L'0'||name[i]>L'9')return 0;n=n*10+(name[i]-L'0');}
        if(n>=1&&n<=12)return VK_F1+n-1;
    }return 0;
}
bool comName(const std::wstring& x);
bool loadConfig(Config& c,std::wstring& err){
    err.clear();
    c.ip=ini(L"Reader",L"ReaderIp",L"");
    // Old single-port configuration can supply the data port only; the command port must be entered explicitly.
    std::wstring data=ini(L"Reader",L"DataPort",L"");
    if(data.empty())data=ini(L"Reader",L"TcpPort",L"");
    c.dataPort=number(data);c.commandPort=number(ini(L"Reader",L"CommandPort",L""));
    c.com=ini(L"Serial",L"BridgeCom",L"");c.packCom=ini(L"Serial",L"PackagingCom",L"");
    c.port=ini(L"Reader",L"TriggerHex",L"");
    const std::wstring encoding=ini(L"Reader",L"TriggerEncoding",L"");
    bool triggerOK=false;
    if(encoding.empty())triggerOK=triggerBytes(c.port,c.trigger);
    else if(encoding==L"HEX"||encoding==L"ASCII")triggerOK=parseTrigger(ini(L"Reader",L"TriggerText",L""),encoding==L"HEX",c.trigger);
    c.key=ini(L"Keyboard",L"TriggerKey",L"Enter");
    if(!c.dataPort||!c.commandPort)err=L"请填写扫码接收端口和触发发送端口（1..65535）";
    else if(!comName(c.com))err=L"桥接端 COM 无效（例如 COM8）";
    else if(!comName(c.packCom))err=L"包装软件端 COM 无效（例如 COM7）";
    else if(number(c.packCom.substr(3))==number(c.com.substr(3)))err=L"包装软件和桥接工具必须使用配对的两个不同 COM";
    else if(!triggerOK)err=L"触发内容与所选 ASCII/HEX 编码不符，或超过 64 字节";
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
bool sendAll(SOCKET s,const std::vector<unsigned char>& bytes){
    size_t sent=0;const ULONGLONG deadline=GetTickCount64()+1000;
    while(sent<bytes.size()&&!stopped()){
        ULONGLONG now=GetTickCount64();if(now>=deadline)return false;
        DWORD remaining=(DWORD)(deadline-now);fd_set wf;FD_ZERO(&wf);FD_SET(s,&wf);
        timeval tv={(long)(remaining/1000),(long)((remaining%1000)*1000)};
        if(select(0,nullptr,&wf,nullptr,&tv)<=0)return false;
        int n=send(s,(const char*)bytes.data()+sent,(int)(bytes.size()-sent),0);
        if(n<0&&WSAGetLastError()==WSAEWOULDBLOCK)continue;
        if(n<=0)return false;sent+=(size_t)n;
    }return sent==bytes.size();
}
#ifdef BRIDGE_UNIT_TEST
struct TestSerialHooks{bool enabled=false;std::wstring file;std::string inventory;std::atomic<bool> peer;std::atomic<unsigned> writes,inventoryCalls;bool invalidOnRetry=false;TestSerialHooks():peer(true),writes(0),inventoryCalls(0){}};
TestSerialHooks g_testSerial;
#endif
std::wstring findSetupc();
bool verifyConfiguredPair(const std::wstring& pack,const std::wstring& bridge,std::wstring& error){
    error.clear();com0com::Inventory inventory;std::string failure;
#ifdef BRIDGE_UNIT_TEST
    if(g_testSerial.enabled){
        const unsigned calls=g_testSerial.inventoryCalls.fetch_add(1);
        if(g_testSerial.invalidOnRetry&&calls>0){error=L"模拟重试库存已失效";return false;}
        if(!com0com::ParseInventory(g_testSerial.inventory,inventory,failure)){error=L"模拟库存解析失败："+narrowAsWide(failure);return false;}
    }else
#endif
    {
        const std::wstring setup=findSetupc();
        if(setup.empty()){error=L"未找到受管理员保护的标准 com0com/setupc.exe，或其所有者/权限/路径检查失败；请管理员核实安装目录，未启动桥接。";return false;}
        com0com::CaptureResult captured=com0com::CaptureInventory(setup,2000);
        if(captured.status!=com0com::CaptureStatus::Ok){
            error=L"只读配对检查失败（Windows 错误 "+std::to_wstring(captured.win32Error)+L"，退出码 "+std::to_wstring(captured.exitCode)+L"）："+narrowAsWide(captured.error)+
                L"。若错误为740/5，请管理员核实后以管理员身份打开本工具；不会自动提权或安装。";return false;
        }
        inventory=captured.inventory;
    }
    const auto check=com0com::ValidatePair(inventory,ascii(pack),ascii(bridge));
    if(!check.identityVerified){error=L"配对未验证：所填包装 COM 与桥接 COM 不是库存中的同一串口对，拒绝连接。";return false;}
    if(!check.pinsVerified){error=L"配对存在，但安全映射未满足：包装端需 EmuOverrun=yes；桥接端需 EmuBR=yes、dsr=ropen。未自动修改现有设备，请管理员核实设置。";return false;}
    return true;
}
bool serialPresent(const std::wstring& name){
#ifdef BRIDGE_UNIT_TEST
    if(g_testSerial.enabled)return true;
#endif
    wchar_t target[1024]={};return QueryDosDeviceW(name.c_str(),target,1024)!=0;
}
void configureSerial(DCB& d){
    d.BaudRate=115200;d.ByteSize=8;d.Parity=NOPARITY;d.StopBits=ONESTOPBIT;
    d.fBinary=TRUE;d.fParity=FALSE;d.fInX=FALSE;d.fOutX=FALSE;d.fNull=FALSE;d.fErrorChar=FALSE;
    d.fDsrSensitivity=FALSE;d.fAbortOnError=FALSE;d.fTXContinueOnXoff=TRUE;
    d.fOutxCtsFlow=FALSE;d.fOutxDsrFlow=FALSE;d.fDtrControl=DTR_CONTROL_ENABLE;d.fRtsControl=RTS_CONTROL_ENABLE;
}
HANDLE openCom(const std::wstring& name){
#ifdef BRIDGE_UNIT_TEST
    if(g_testSerial.enabled)return CreateFileW(g_testSerial.file.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_FLAG_OVERLAPPED,nullptr);
#endif
    std::wstring path=L"\\\\.\\"+name;
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
    if(h==INVALID_HANDLE_VALUE)return h;
    if(!SetupComm(h,4096,4096)){DWORD code=GetLastError();CloseHandle(h);SetLastError(code);return INVALID_HANDLE_VALUE;}
    DCB d={};d.DCBlength=sizeof(d);
    if(!GetCommState(h,&d)){DWORD code=GetLastError();CloseHandle(h);SetLastError(code);return INVALID_HANDLE_VALUE;}
    configureSerial(d);
    if(!SetCommState(h,&d)){DWORD code=GetLastError();CloseHandle(h);SetLastError(code);return INVALID_HANDLE_VALUE;}
    PurgeComm(h,PURGE_RXCLEAR|PURGE_TXCLEAR|PURGE_RXABORT|PURGE_TXABORT);
    return h;
}
// Requires com0com bridge-side pin mapping dsr=ropen.
bool peerOpened(HANDLE h){
#ifdef BRIDGE_UNIT_TEST
    if(g_testSerial.enabled)return g_testSerial.peer.load();
#endif
    DWORD signals=0;return GetCommModemStatus(h,&signals)&&((signals&MS_DSR_ON)!=0);
}
struct PendingWrite{
    OVERLAPPED ov;std::vector<unsigned char> bytes;HANDLE owned;
    PendingWrite(const std::vector<unsigned char>& data):ov(),bytes(data),owned(INVALID_HANDLE_VALUE){}
};
std::mutex g_writeMutex;std::vector<PendingWrite*> g_unfinishedWrites;std::atomic<bool> g_ioStalled(false);
#ifdef BRIDGE_UNIT_TEST
bool g_ioCancelTest=false;
#endif
void destroyWrite(PendingWrite* op){CloseHandle(op->ov.hEvent);CloseHandle(op->owned);delete op;}
void reapWrites(){
    std::lock_guard<std::mutex> lock(g_writeMutex);
    for(auto it=g_unfinishedWrites.begin();it!=g_unfinishedWrites.end();){
        if(WaitForSingleObject((*it)->ov.hEvent,0)==WAIT_OBJECT_0){destroyWrite(*it);it=g_unfinishedWrites.erase(it);}else ++it;
    }
    g_ioStalled=!g_unfinishedWrites.empty();
}
bool writeCom(HANDLE h,const std::vector<unsigned char>& bytes){
    if(bytes.empty())return true;if(g_ioStalled)return false;
    PendingWrite* op=new PendingWrite(bytes);
    if(!DuplicateHandle(GetCurrentProcess(),h,GetCurrentProcess(),&op->owned,0,FALSE,DUPLICATE_SAME_ACCESS)){delete op;return false;}
    op->ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!op->ov.hEvent){CloseHandle(op->owned);delete op;return false;}
    DWORD wrote=0;BOOL ok=FALSE;
#ifdef BRIDGE_UNIT_TEST
    if(g_ioCancelTest)SetLastError(ERROR_IO_PENDING);else
#endif
    ok=WriteFile(op->owned,op->bytes.data(),(DWORD)op->bytes.size(),&wrote,&op->ov);
    if(!ok&&GetLastError()==ERROR_IO_PENDING){
        HANDLE events[2]={op->ov.hEvent,g_stop};DWORD wait=WaitForMultipleObjects(2,events,FALSE,1000);
        if(wait==WAIT_OBJECT_0)ok=GetOverlappedResult(op->owned,&op->ov,&wrote,FALSE);
        else{
            BOOL cancelled=CancelIoEx(op->owned,&op->ov);DWORD cancelError=cancelled?0:GetLastError();
            if(WaitForSingleObject(op->ov.hEvent,500)!=WAIT_OBJECT_0){
                // Driver may complete later: retain both byte buffer and OVERLAPPED and handle, never free stack storage.
                std::lock_guard<std::mutex> lock(g_writeMutex);g_unfinishedWrites.push_back(op);g_ioStalled=true;
                status(L"严重故障：串口驱动未完成取消（错误 "+std::to_wstring(cancelError)+L"）；已保留写入资源，桥接锁定，请检查驱动；不会自动补发");
                return false;
            }
            ok=FALSE;
        }
    }
    const bool result=ok&&wrote==op->bytes.size();destroyWrite(op);return result;
}
// No protocol framing: TCP payload (including scanner's own terminator) is unchanged.
bool relayChunk(HANDLE serial,const char* buf,int count){
    if(count<=0)return false;
    bool ok=writeCom(serial,std::vector<unsigned char>(buf,buf+count));
#ifdef BRIDGE_UNIT_TEST
    if(ok&&g_testSerial.enabled)g_testSerial.writes.fetch_add(1);
#endif
    return ok;
}
void run(Config c){
    logLine(L"工作线程启动；扫码端口="+std::to_wstring(c.dataPort)+L"；触发端口="+std::to_wstring(c.commandPort)+L"；断线不补发；串口="+c.com);
    g_running=true;g_pairVerified=false;
    while(!stopped()){
        reapWrites();if(g_faultLatched||g_ioStalled){WaitForSingleObject(g_stop,500);continue;}
        g_pairVerified=false;invalidateInputs();g_net=false;g_ser=false;
        g_dataOnline=false;g_commandOnline=false;g_peerOnline=false;
        std::wstring pairError;
        if(!verifyConfiguredPair(c.packCom,c.com,pairError)){
            status(pairError);g_running=false;return;
        }
        if(stopped()){g_running=false;return;}
        g_pairVerified=true;
        if(!serialPresent(c.com)){
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
        if(data==INVALID_SOCKET){
            g_net=false;status(L"扫码 TCP 未连接；禁止触发；2 秒后重连");
        }else{
            invalidateInputs();g_net=(cmd!=INVALID_SOCKET);
            bool hadPeer=peerOpened(serial);
            g_peerOnline=hadPeer;
            status(cmd==INVALID_SOCKET?L"扫码接收在线，命令连接失败；持续接收扫码，触发禁用，核对后手动重连":
                !g_keyboardOK?L"后台键盘输入失败；仍在监听数据，触发禁用":
                peerOpened(serial)?(shared?L"单连接在线：同一端口发送和接收；请确认扫码枪不会把指令应答当扫码数据":L"双连接在线：所选按键可触发（不限制前台程序）"):
                L"TCP 已连接；包装软件串口未打开，触发禁用");
            bool good=true;
            while(!stopped()&&good){
                bool peerNow=peerOpened(serial);
                g_peerOnline=peerNow;
                if(hadPeer&&!peerNow){g_faultLatched=true;good=false;status(L"严重故障：包装软件关闭串口；请核对实物和旧数据后手动重连");break;}
                if(peerNow&&!hadPeer){invalidateInputs();status(g_commandOnline?L"对端开启信号有效，持续监听数据":L"对端开启信号有效，持续接收；命令未连接，触发禁用");}
                hadPeer=peerNow;
                TriggerInput input={};
                if(nextInput(input)){
                    if(g_pairVerified&&g_ser&&g_net&&g_keyboardOK&&peerOpened(serial)&&inputFresh(input.captured,input.generation,GetTickCount(),g_inputGeneration.load(),g_inputBoundary.load())){
                        if(!sendAll(cmd,c.trigger)){
                            g_net=false;g_commandOnline=false;invalidateInputs();
                            if(!shared)closeSock(cmd);
                            status(L"严重故障：触发发送结果不确定；触发已锁定，不重发；扫码通道继续接收，请人工核查");
                        }
                        else logLine(L"已发送触发命令 "+std::to_wstring(c.trigger.size())+L" 字节");
                    }else {status(L"触发已拒绝：输入过期、对端未打开或连接状态异常；不会补发");}
                }
                if(!good)break;
                fd_set rd;FD_ZERO(&rd);FD_SET(data,&rd);if(!shared&&cmd!=INVALID_SOCKET)FD_SET(cmd,&rd);
                timeval tv={0,200000};int n=select(0,&rd,nullptr,nullptr,&tv);
                if(n==SOCKET_ERROR){g_faultLatched=true;good=false;status(L"严重故障：TCP 监听失败；人工核查后才能恢复");break;}
                if(!shared&&cmd!=INVALID_SOCKET&&n>0&&FD_ISSET(cmd,&rd)){
                    char response[512];int k=recv(cmd,response,sizeof(response),0);
                    if(k<=0){
                        if(k==0||WSAGetLastError()!=WSAEWOULDBLOCK){
                            g_net=false;g_commandOnline=false;invalidateInputs();closeSock(cmd);
                            status(L"触发端口已断开，触发已锁定；扫码数据通道继续接收，核对后手动重连");
                        }
                    }else logLine(L"收到触发端口响应 "+std::to_wstring(k)+L" 字节（不转串口）");
                }
                if(good&&n>0&&FD_ISSET(data,&rd)){
                    char buf[4096];int k=recv(data,buf,sizeof(buf),0);
                    if(k<=0){if(k<0&&WSAGetLastError()==WSAEWOULDBLOCK)continue;g_faultLatched=true;good=false;g_dataOnline=false;status(L"严重故障：扫码端口断开；人工核查后才能恢复");break;}
                    if(!peerOpened(serial)){g_faultLatched=true;good=false;status(L"严重故障：包装软件串口未打开；本批数据丢弃，禁止补发");break;}
                    if(!relayChunk(serial,buf,k)){g_faultLatched=true;good=false;status(L"严重故障：串口写入失败；本批可能部分写入，人工核查后才能恢复");break;}
                    logLine(L"TCP 转 COM "+std::to_wstring(k)+L" 字节（原样转发）");
                }
            }
        }
        invalidateInputs();g_net=false;g_ser=false;g_dataOnline=false;g_commandOnline=false;g_peerOnline=false;
        if(shared)cmd=INVALID_SOCKET;
        closeSock(cmd);closeSock(data);PurgeComm(serial,PURGE_RXCLEAR|PURGE_TXCLEAR|PURGE_RXABORT|PURGE_TXABORT);
        CloseHandle(serial);g_serial=INVALID_HANDLE_VALUE;
        if(!stopped()&&!g_faultLatched)WaitForSingleObject(g_stop,2000);
    }
    g_running=false;g_pairVerified=false;g_dataOnline=false;g_commandOnline=false;g_peerOnline=false;status(L"桥接已停止");
}
void stopWorker(){g_pairVerified=false;invalidateInputs();if(g_stop)SetEvent(g_stop);if(g_worker.joinable())g_worker.join();g_net=false;g_ser=false;g_pending=false;g_dataOnline=false;g_commandOnline=false;g_peerOnline=false;ResetEvent(g_trigger);}
void startWorker(const Config& c){invalidateInputs();ResetEvent(g_stop);g_worker=std::thread(run,c);}
void setText(int id,const std::wstring& s){SetWindowTextW(GetDlgItem(g_window,id),s.c_str());}
std::wstring getText(int id){wchar_t b[512]={};GetWindowTextW(GetDlgItem(g_window,id),b,512);return b;}
bool changeEncoding(bool newHex,bool showError=true){
    HWND picker=GetDlgItem(g_window,ID_ENCODING);
    if(newHex==g_hexMode)return true;
    const std::wstring before=getText(ID_TRIGGER);std::vector<unsigned char> bytes;
    if(!before.empty()&&!parseTrigger(before,g_hexMode,bytes)){
        SendMessageW(picker,CB_SETCURSEL,g_hexMode?1:0,0);
        if(showError)MessageBoxW(g_window,L"当前内容不能按原编码解析，未切换、未修改内容。HEX 需完整字节；ASCII 控制字符用 \\r、\\n、\\xNN，反斜杠用 \\\\。",L"无法转换编码",MB_ICONERROR);
        return false;
    }
    setText(ID_TRIGGER,newHex?formatHex(bytes):formatAscii(bytes));g_hexMode=newHex;
    SendMessageW(picker,CB_SETCURSEL,newHex?1:0,0);return true;
}
void refresh(){std::wstring s;{std::lock_guard<std::mutex> lock(g_statusMutex);s=g_status;}setText(ID_STATE,s);}
void drawLinks(HDC dc){
    SetBkMode(dc,TRANSPARENT);
    HFONT oldFont=(HFONT)SelectObject(dc,g_font);
    const wchar_t* labels[]={L"扫码接收 TCP",L"触发发送 TCP",L"包装软件 COM"};
    bool connected[]={g_dataOnline.load()&&!g_faultLatched.load(),g_commandOnline.load()&&!g_faultLatched.load(),g_peerOnline.load()&&g_pairVerified.load()&&!g_faultLatched.load()};
    for(int i=0;i<3;++i){
        const int x=18+i*154,y=397;
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
    if(x.size()<4||x.size()>8||_wcsnicmp(x.c_str(),L"COM",3)!=0||x[3]==L'0')return false;
    return number(x.substr(3))>0&&x.find_first_not_of(L"0123456789",3)==std::wstring::npos;
}
bool comExists(const std::wstring& x){
    wchar_t target[1024]={};return QueryDosDeviceW(x.c_str(),target,1024)!=0;
}
bool privilegedSid(PSID sid){
    if(!sid||!IsValidSid(sid))return false;
    BYTE system[SECURITY_MAX_SID_SIZE]={},admins[SECURITY_MAX_SID_SIZE]={};DWORD systemSize=sizeof(system),adminSize=sizeof(admins);
    if(CreateWellKnownSid(WinLocalSystemSid,nullptr,system,&systemSize)&&EqualSid(sid,system))return true;
    if(CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,admins,&adminSize)&&EqualSid(sid,admins))return true;
    PSID installer=nullptr;bool trusted=false;
    if(ConvertStringSidToSidW(L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464",&installer)){
        trusted=EqualSid(sid,installer)!=FALSE;LocalFree(installer);
    }return trusted;
}
bool protectedInstallPath(const std::wstring& path,bool directory){
    DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT)||
       ((attributes&FILE_ATTRIBUTE_DIRECTORY)!=0)!=directory)return false;
    PSECURITY_DESCRIPTOR descriptor=nullptr;PSID owner=nullptr;PACL acl=nullptr;
    DWORD result=GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&acl,nullptr,&descriptor);
    if(result!=ERROR_SUCCESS||!descriptor){if(descriptor)LocalFree(descriptor);return false;}
    bool trusted=privilegedSid(owner)&&acl!=nullptr;
    const DWORD writable=GENERIC_ALL|GENERIC_WRITE|WRITE_DAC|WRITE_OWNER|DELETE|
        FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES|FILE_DELETE_CHILD;
    for(DWORD i=0;trusted&&i<acl->AceCount;++i){
        void* item=nullptr;if(!GetAce(acl,i,&item)){trusted=false;break;}
        ACE_HEADER* header=static_cast<ACE_HEADER*>(item);
        if(header->AceFlags&INHERIT_ONLY_ACE)continue;
        if(header->AceType==ACCESS_ALLOWED_ACE_TYPE){
            ACCESS_ALLOWED_ACE* ace=static_cast<ACCESS_ALLOWED_ACE*>(item);
            if((ace->Mask&writable)&&!privilegedSid(&ace->SidStart))trusted=false;
        }else if(header->AceType!=ACCESS_DENIED_ACE_TYPE){
            // Do not guess the semantics of conditional/object allow ACEs on an elevated executable.
            trusted=false;
        }
    }
    LocalFree(descriptor);return trusted;
}
std::wstring findSetupc(){
    // Resolve OS-known default installation locations, never inherited ProgramFiles/PATH values.
    const KNOWNFOLDERID* folders[]={&FOLDERID_ProgramFiles,&FOLDERID_ProgramFilesX86};
    for(const KNOWNFOLDERID* id:folders){
        PWSTR known=nullptr;HRESULT result=SHGetKnownFolderPath(*id,KF_FLAG_DEFAULT_PATH,nullptr,&known);
        if(FAILED(result)||!known){if(known)CoTaskMemFree(known);continue;}
        const std::wstring base=known;CoTaskMemFree(known);
        const std::wstring directory=base+L"\\com0com",candidate=directory+L"\\setupc.exe";
        if(protectedInstallPath(base,true)&&protectedInstallPath(directory,true)&&protectedInstallPath(candidate,false))return candidate;
    }return L"";
}
#ifdef BRIDGE_UNIT_TEST
std::vector<std::wstring> g_reservedPortsForTest;
#endif
bool creationNamesFree(const std::wstring& pack,const std::wstring& bridge,std::wstring& error){
    if(!comName(pack)||!comName(bridge)||_wcsicmp(pack.c_str(),bridge.c_str())==0){error=L"需要两个不同的规范 COM 号";return false;}
#ifdef BRIDGE_UNIT_TEST
    for(const auto& name:g_reservedPortsForTest)if(name==pack||name==bridge){error=L"该 COM 已保留，禁止覆盖";return false;}
    return true;
#else
    if(comExists(pack)||comExists(bridge)){error=L"所选端口已经存在，禁止覆盖";return false;}
    HKEY key=nullptr;LONG rc=RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\COM Name Arbiter",0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&key);
    if(rc!=ERROR_SUCCESS){error=L"无法只读检查 COM 保留表（错误 "+std::to_wstring(rc)+L"），禁止自动创建。";return false;}
    DWORD type=0,size=0;rc=RegQueryValueExW(key,L"ComDB",nullptr,&type,nullptr,&size);
    if(rc!=ERROR_SUCCESS||type!=REG_BINARY||size==0||size>65536){RegCloseKey(key);error=L"COM 保留表不可用，禁止自动创建。";return false;}
    std::vector<BYTE> bitmap(size);rc=RegQueryValueExW(key,L"ComDB",nullptr,&type,bitmap.data(),&size);RegCloseKey(key);
    if(rc!=ERROR_SUCCESS){error=L"读取 COM 保留表失败，禁止自动创建。";return false;}
    for(const auto& name:{pack,bridge}){
        const size_t index=(size_t)number(name.substr(3))-1;
        if(index>=bitmap.size()*8){error=L"所选端口号超出系统保留表可验证范围。";return false;}
        if(bitmap[index/8]&(1u<<(index%8))){error=name+L" 已被系统保留（可能为离线设备），禁止重复创建。";return false;}
    }
    return true;
#endif
}
void pollPairInstall(){
    if(!g_pairInstallProcess)return;
    DWORD wait=WaitForSingleObject(g_pairInstallProcess,0);
    if(wait==WAIT_TIMEOUT){
        if(!g_pairInstallWarned&&GetTickCount64()-g_pairInstallStarted>=30000){
            g_pairInstallWarned=true;status(L"串口创建仍在运行，禁止重复创建；请检查安装窗口和设备管理器。");
        }return;
    }
    DWORD code=999;BOOL gotCode=GetExitCodeProcess(g_pairInstallProcess,&code);
    CloseHandle(g_pairInstallProcess);g_pairInstallProcess=nullptr;
    if(g_window){EnableWindow(GetDlgItem(g_window,CMD_CREATE_PAIR),TRUE);EnableWindow(GetDlgItem(g_window,CMD_SAVE),TRUE);}
    if(wait==WAIT_OBJECT_0&&gotCode&&code==0&&comExists(g_pairInstallPack)&&comExists(g_pairInstallBridge)){
        std::wstring error;
        if(verifyConfiguredPair(g_pairInstallPack,g_pairInstallBridge,error))status(L"库存确认同一对 COM 且安全映射符合；仍需目标电脑实际收发验收，再保存重连。");
        else status(L"创建进程结束，但配对验证失败："+error);
    }
    else status(L"串口创建未验证成功（退出码 "+std::to_wstring(code)+L"），未自动重启桥接；请检查驱动和设备管理器。");
}
void createPair(){
    if(g_pairInstallProcess){MessageBoxW(g_window,L"串口安装进程尚未结束，不能重复创建。",L"安装仍在运行",MB_ICONWARNING);return;}
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
    std::wstring prompt=L"即将以管理员权限创建一对虚拟串口：包装软件 "+pack+L" ↔ 桥接工具 "+bridge+
        L"。将启动系统 UAC 确认，不会覆盖现有端口；请在非生产状态先核对端口空闲。继续吗？";
    if(MessageBoxW(g_window,prompt.c_str(),L"确认创建虚拟串口对",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
    std::wstring freeError;
    if(!creationNamesFree(pack,bridge,freeError)){status(L"创建未执行："+freeError);return;}
    const auto before=com0com::CaptureInventory(setup,2000);
    if(before.status!=com0com::CaptureStatus::Ok){status(L"无法核对创建前的串口库存，拒绝安装；请管理员检查 setupc list。");return;}
    for(const auto& endpoint:before.inventory.endpoints)if(endpoint.portName==ascii(pack)||endpoint.portName==ascii(bridge)){
        status(L"库存中已有所选 COM，禁止覆盖或重复创建。");return;
    }
    if(g_running.load())stopWorker(); // only after operator confirms; Cancel leaves retry worker untouched

    com0com::AdminCreateCommand command;std::string commandError;
    if(!com0com::BuildAdminCreateCommand(setup,ascii(pack),ascii(bridge),command,commandError)){
        status(L"创建参数校验失败，未运行安装："+narrowAsWide(commandError));return;
    }
    SHELLEXECUTEINFOW info={};info.cbSize=sizeof(info);info.fMask=SEE_MASK_NOCLOSEPROCESS;
    info.hwnd=g_window;info.lpVerb=command.verb.c_str();info.lpFile=command.file.c_str();info.lpParameters=command.parameters.c_str();info.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&info)){
        status(L"创建虚拟串口对未执行：管理员授权被拒绝或启动失败，Windows 错误 "+std::to_wstring(GetLastError()));return;
    }
    if(!info.hProcess){status(L"创建进程无可验证的进程句柄，未重连；请人工检查，禁止重复创建。");return;}
    g_pairInstallProcess=info.hProcess;g_pairInstallStarted=GetTickCount64();g_pairInstallWarned=false;
    g_pairInstallPack=pack;g_pairInstallBridge=bridge;
    EnableWindow(GetDlgItem(g_window,CMD_CREATE_PAIR),FALSE);EnableWindow(GetDlgItem(g_window,CMD_SAVE),FALSE);
    status(L"管理员串口创建已启动；界面保持响应，完成前禁止重复创建或重连。");
}
bool saveConfigAtomic(const std::vector<std::wstring>& values,bool hexMode,Config& result,std::wstring& error){
    error.clear();if(values.size()!=7){error=L"配置字段数不正确";return false;}
    const std::wstring original=g_ini,folder=dirOf(original);wchar_t tempPath[MAX_PATH]={};
    if(!GetTempFileNameW(folder.c_str(),L"SRB",0,tempPath)){error=L"无法创建临时配置，Windows 错误 "+std::to_wstring(GetLastError());return false;}
    const std::wstring temporary=tempPath;bool ok=true;
    const DWORD attributes=GetFileAttributesW(original.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES){
        if(attributes&FILE_ATTRIBUTE_READONLY){DeleteFileW(tempPath);error=L"配置文件为只读，未修改原配置";return false;}
        ok=CopyFileW(original.c_str(),tempPath,FALSE)!=FALSE;
    }
    if(ok){
        g_ini=temporary;
        const wchar_t* sections[]={L"Reader",L"Reader",L"Reader",L"Reader",L"Serial",L"Serial",L"Keyboard"};
        const wchar_t* keys[]={L"ReaderIp",L"DataPort",L"CommandPort",L"TriggerHex",L"BridgeCom",L"PackagingCom",L"TriggerKey"};
        for(size_t i=0;ok&&i<7;++i)if(i!=3){ok=WritePrivateProfileStringW(sections[i],keys[i],values[i].c_str(),tempPath)!=FALSE;if(!ok)error=L"write field "+std::to_wstring(i)+L" failed "+std::to_wstring(GetLastError());}
        if(ok){ok=storeTrigger(values[3],hexMode);if(!ok)error=L"trigger store failed "+std::to_wstring(GetLastError());}
        for(const auto& pair:std::vector<std::pair<const wchar_t*,const wchar_t*>>{{L"Reader",L"TcpPort"},{L"Reader",L"CommandMode"},{L"Keyboard",L"PackagingExe"}})
            if(ok){ok=WritePrivateProfileStringW(pair.first,pair.second,nullptr,tempPath)!=FALSE;if(!ok)error=L"delete legacy failed "+std::to_wstring(GetLastError());}
        if(ok)WritePrivateProfileStringW(nullptr,nullptr,nullptr,tempPath); // cache flush returns zero by contract; verify via readback + FlushFileBuffers
        Config check={};std::wstring validation;std::vector<unsigned char> expected;
        if(ok)ok=loadConfig(check,validation)&&parseTrigger(values[3],hexMode,expected)&&check.trigger==expected&&
            check.ip==values[0]&&check.dataPort==number(values[1])&&check.commandPort==number(values[2])&&
            check.com==values[4]&&check.packCom==values[5]&&check.key==values[6];
        g_ini=original;
        if(!ok&&error.empty())error=validation.empty()?L"临时配置回读校验失败；原配置保持不变":validation;
        if(ok){
            HANDLE file=CreateFileW(tempPath,GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            ok=file!=INVALID_HANDLE_VALUE&&FlushFileBuffers(file)!=FALSE;
            if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
        }
        if(ok){
            // Same-volume commit: no partial per-key writes to the live INI.
            ok=MoveFileExW(tempPath,original.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
            if(ok){WritePrivateProfileStringW(nullptr,nullptr,nullptr,original.c_str());result=check;}
        }
    }
    if(!ok){DWORD code=GetLastError();DeleteFileW(tempPath);if(error.empty())error=L"配置提交失败，原配置未改动；Windows 错误 "+std::to_wstring(code);}
    return ok;
}
void save(){
    if(g_pairInstallProcess){status(L"串口安装仍在运行，禁止保存重连。");return;}
    const wchar_t* sections[]={L"Reader",L"Reader",L"Reader",L"Reader",L"Serial",L"Serial",L"Keyboard"};
    const wchar_t* keys[]={L"ReaderIp",L"DataPort",L"CommandPort",L"TriggerHex",L"BridgeCom",L"PackagingCom",L"TriggerKey"};
    const int ids[]={ID_IP,ID_DATA,ID_CMD,ID_TRIGGER,ID_COM,ID_PACK_COM,ID_KEY};
    std::wstring vals[7];for(int i=0;i<7;++i)vals[i]=getText(ids[i]);
    // Validate before touching the existing INI; no half-applied invalid settings.
    IN_ADDR a;std::vector<unsigned char> bytes;std::wstring err;
    if(InetPtonW(AF_INET,vals[0].c_str(),&a)!=1)err=L"ReaderIp 需填写 IPv4 地址";
    else if(!number(vals[1])||!number(vals[2]))err=L"请填写两个有效的扫码枪端口（1..65535）";
    else if(!parseTrigger(vals[3],g_hexMode,bytes))err=L"触发内容无效：请按选择的 ASCII 或 HEX 格式输入，最多 64 字节";
    else if(!comName(vals[4]))err=L"桥接端 COM 无效（例如 COM8）";
    else if(!comName(vals[5]))err=L"包装软件 COM 无效（例如 COM7）";
    else if(number(vals[4].substr(3))==number(vals[5].substr(3)))err=L"包装软件与桥接端必须用不同 COM 号";
    else if(!keyCode(vals[6]))err=L"触发按键只能选 Enter、Space 或 F1 到 F12";
    if(g_ioStalled)err=L"串口驱动尚未完成取消，禁止重连；请检查驱动并等待写入结束。";
    if(!err.empty()){MessageBoxW(g_window,err.c_str(),L"配置未保存",MB_ICONERROR);return;}
    if(number(vals[1])==number(vals[2])&&
       MessageBoxW(g_window,L"接收与发送设置为同一个扫码枪端口，只建立一条 TCP 连接。若扫码枪返回指令应答，该应答会作为扫码数据转入包装软件；未核对真实协议前禁止生产使用。确认继续保存吗？",L"同端口风险确认",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
    stopWorker();g_faultLatched=true;
    Config c;std::wstring why;
    if(!saveConfigAtomic(std::vector<std::wstring>(vals,vals+7),g_hexMode,c,why)){status(L"配置未保存，桥接保持停止："+why);return;}
    g_triggerVK=keyCode(c.key);g_keyDown=false;
    g_faultLatched=false;logLine(L"配置完整保存并回读通过，操作员解除故障锁定");startWorker(c);
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
        const wchar_t* labels[]={L"扫码枪 IPv4",L"扫码接收端口",L"触发发送端口",L"发送内容",L"桥接端 COM",L"包装软件 COM",L"触发按键"};
        const int ids[]={ID_IP,ID_DATA,ID_CMD,ID_TRIGGER,ID_COM,ID_PACK_COM,ID_KEY};
        const wchar_t* sections[]={L"Reader",L"Reader",L"Reader",L"Reader",L"Serial",L"Serial",L"Keyboard"};
        const wchar_t* keys[]={L"ReaderIp",L"DataPort",L"CommandPort",L"TriggerHex",L"BridgeCom",L"PackagingCom",L"TriggerKey"};
        std::wstring encoding=ini(L"Reader",L"TriggerEncoding",L"");
        g_hexMode=(encoding!=L"ASCII");
        HWND modeLabel=CreateWindowW(L"STATIC",L"发送编码",WS_CHILD|WS_VISIBLE,18,124,135,22,h,nullptr,nullptr,nullptr);
        HWND mode=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,155,121,305,120,h,(HMENU)(UINT_PTR)ID_ENCODING,nullptr,nullptr);
        SendMessageW(modeLabel,WM_SETFONT,(WPARAM)g_font,TRUE);SendMessageW(mode,WM_SETFONT,(WPARAM)g_font,TRUE);
        SendMessageW(mode,CB_ADDSTRING,0,(LPARAM)L"ASCII");SendMessageW(mode,CB_ADDSTRING,0,(LPARAM)L"HEX");
        SendMessageW(mode,CB_SETCURSEL,g_hexMode?1:0,0);
        for(int i=0;i<7;++i){
            std::wstring value=ini(sections[i],keys[i],i==6?L"Enter":L"");
            if(i==1&&value.empty())value=ini(L"Reader",L"TcpPort",L"");

            if(i==3){
                if(!encoding.empty())value=ini(L"Reader",L"TriggerText",L"");
                else{std::vector<unsigned char> bytes;if(triggerBytes(value,bytes))value=formatHex(bytes);}
            }
            int rowY=16+35*i+(i>=3?35:0);
            if(i==6){g_edit[i]=keyPicker(h,rowY,value);continue;}
            g_edit[i]=field(labels[i],ids[i],rowY,value);
        }
        HWND encodingHint=CreateWindowW(L"STATIC",LR"(ASCII: 
=回车  
=换行  \=反斜杠；切换不改变发送字节)",WS_CHILD|WS_VISIBLE,18,296,465,18,h,nullptr,nullptr,nullptr);
        SendMessageW(encodingHint,WM_SETFONT,(WPARAM)g_font,TRUE);
        HWND pairButton=CreateWindowW(L"BUTTON",L"创建虚拟串口对（需驱动/管理员）",WS_CHILD|WS_VISIBLE|WS_TABSTOP,18,358,340,30,h,(HMENU)(UINT_PTR)CMD_CREATE_PAIR,nullptr,nullptr);
        SendMessageW(pairButton,WM_SETFONT,(WPARAM)g_font,TRUE);
        HWND b=CreateWindowW(L"BUTTON",L"保存配置并重连",WS_CHILD|WS_VISIBLE|WS_TABSTOP,18,322,142,29,h,(HMENU)(UINT_PTR)CMD_SAVE,nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)g_font,TRUE);
        b=CreateWindowW(L"BUTTON",L"安装登录自启",WS_CHILD|WS_VISIBLE,167,322,142,29,h,(HMENU)(UINT_PTR)CMD_STARTUP,nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)g_font,TRUE);
        b=CreateWindowW(L"BUTTON",L"隐藏到托盘",WS_CHILD|WS_VISIBLE,316,322,144,29,h,(HMENU)(UINT_PTR)CMD_HIDE,nullptr,nullptr);SendMessageW(b,WM_SETFONT,(WPARAM)g_font,TRUE);
        HWND s=CreateWindowW(L"STATIC",L"未连接",WS_CHILD|WS_VISIBLE,18,427,460,65,h,(HMENU)(UINT_PTR)ID_STATE,nullptr,nullptr);SendMessageW(s,WM_SETFONT,(WPARAM)g_font,TRUE);

        SetTimer(h,1,500,nullptr);
        tray(true);return 0;
    }
    if(msg==WM_TIMER&&w==1){pollPairInstall();reapWrites();RECT area={15,394,481,417};InvalidateRect(h,&area,FALSE);updateTrayTip();return 0;}
    if(msg==WM_PAINT){PAINTSTRUCT paint={};HDC dc=BeginPaint(h,&paint);drawLinks(dc);EndPaint(h,&paint);return 0;}
    if(msg==WM_COMMAND){
        if(LOWORD(w)==ID_ENCODING&&HIWORD(w)==CBN_SELCHANGE){changeEncoding(SendMessageW(GetDlgItem(h,ID_ENCODING),CB_GETCURSEL,0,0)==1);return 0;}
        switch(LOWORD(w)){
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
                    else if(!g_keyDown){
                        g_keyDown=true;DWORD captured=(DWORD)GetMessageTime();
                        if(inputFresh(captured,g_inputGeneration.load(),GetTickCount(),g_inputGeneration.load(),g_inputBoundary.load()))
                            PostMessageW(h,WM_APP+12,g_inputGeneration.load(),(LPARAM)captured);
                    }
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
        if(!g_peerOnline||!g_pairVerified)return 0;
        TriggerInput input={(DWORD)l,(DWORD)w};queueInput(input);return 0;
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
    if(msg==WM_DESTROY){if(g_pairInstallProcess){CloseHandle(g_pairInstallProcess);g_pairInstallProcess=nullptr;}KillTimer(h,1);tray(false);stopWorker();DeleteObject(g_font);PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,msg,w,l);
}
}
#ifndef BRIDGE_UNIT_TEST
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR args,int){
    HANDLE single=CreateMutexW(nullptr,TRUE,L"Local\\SR5000_TCP_COM_Bridge_Instance");
    if(GetLastError()==ERROR_ALREADY_EXISTS){MessageBoxW(nullptr,L"已有桥接程序正在运行（可能是旧版本）。请先在旧程序托盘中退出，再打开新版；本次未启动新的桥接实例。",L"桥接已在运行",MB_ICONINFORMATION);if(single)CloseHandle(single);return 0;}
    wchar_t path[4096];GetModuleFileNameW(nullptr,path,4096);g_dir=dirOf(path);g_ini=g_dir+L"\\bridge.ini";
    g_stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);g_trigger=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    WSADATA wsa={};if(WSAStartup(MAKEWORD(2,2),&wsa)!=0)return 1;
    WNDCLASSW wc={};wc.lpfnWndProc=wndProc;wc.hInstance=instance;wc.lpszClassName=L"SR5000BridgeWindow";wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    RegisterClassW(&wc);
    HWND window=CreateWindowW(wc.lpszClassName,L"SR-5000 TCP-COM 桥接",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,505,575,nullptr,nullptr,instance,nullptr);
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
