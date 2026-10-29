#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include "tcp_codecs.h"
#include "serial_forward.h"
#include "startup_settings.h"
#include "operator_ui.h"
#include "preparation_security.h"
#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shell32.lib")
namespace {
using Bytes=std::vector<unsigned char>;
using serialbridge::SerialOptions;using serialbridge::validSerialOptions;
bool requiresCom(){return false;}
bool keyboardPacketSize(UINT bytes){return bytes>=offsetof(RAWINPUT,data)+sizeof(RAWKEYBOARD)&&bytes<4096;}
struct Settings{std::wstring ip;int receivePort=0,sendPort=0;SerialOptions serial;};
struct Record{std::wstring channel,text;Bytes bytes;bool payload=false;};
struct Request{Bytes bytes;ULONGLONG when;};
int portNumber(const std::wstring& s){if(s.empty()||s.size()>5)return 0;int n=0;for(wchar_t c:s){if(c<L'0'||c>L'9')return 0;n=n*10+c-L'0';}return n>=1&&n<=65535?n:0;}
bool validSettings(const Settings& c){IN_ADDR a={};return c.receivePort>0&&c.receivePort<=65535&&c.sendPort>0&&c.sendPort<=65535&&InetPtonW(AF_INET,c.ip.c_str(),&a)==1&&validSerialOptions(c.serial)&&(!c.serial.enabled||c.receivePort!=c.sendPort||c.serial.sharedApproved);}
class TcpEngine{
    HANDLE stopping_;std::thread worker_;std::mutex mutex_;std::deque<Request> pending_;std::deque<Record> records_;
    bool logOverflow_=false;std::wstring serialMessage_;
    void serialMessage(const std::wstring& value){std::lock_guard<std::mutex> lock(mutex_);serialMessage_=value;}
    void push(Record r){std::lock_guard<std::mutex> lock(mutex_);if(records_.size()>=256){records_.pop_front();logOverflow_=true;}records_.push_back(std::move(r));}
    bool stopped(){return WaitForSingleObject(stopping_,0)==WAIT_OBJECT_0;}
    static void closeSocket(SOCKET& s){if(s!=INVALID_SOCKET){shutdown(s,SD_BOTH);closesocket(s);s=INVALID_SOCKET;}}
    SOCKET connectSocket(const std::wstring& ip,int port){
        SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==INVALID_SOCKET)return s;
        u_long nb=1;if(ioctlsocket(s,FIONBIO,&nb)){closeSocket(s);return s;}
        sockaddr_in a={};a.sin_family=AF_INET;a.sin_port=htons((u_short)port);InetPtonW(AF_INET,ip.c_str(),&a.sin_addr);
        int result=connect(s,(sockaddr*)&a,sizeof(a));
        if(result==SOCKET_ERROR){
            if(WSAGetLastError()!=WSAEWOULDBLOCK){closeSocket(s);return s;}
            const ULONGLONG end=GetTickCount64()+1200;bool ready=false;
            while(!stopped()&&GetTickCount64()<end){fd_set w,e;FD_ZERO(&w);FD_ZERO(&e);FD_SET(s,&w);FD_SET(s,&e);timeval tv={0,100000};
                int n=select(0,nullptr,&w,&e,&tv);if(n<0)break;if(n>0){int error=0,len=sizeof(error);if(!getsockopt(s,SOL_SOCKET,SO_ERROR,(char*)&error,&len)&&!error&&FD_ISSET(s,&w))ready=true;break;}}
            if(!ready){closeSocket(s);return s;}
        }
        BOOL yes=TRUE;setsockopt(s,SOL_SOCKET,SO_KEEPALIVE,(const char*)&yes,sizeof(yes));setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(const char*)&yes,sizeof(yes));
        tcp_keepalive tune={1,10000,3000};DWORD used=0;WSAIoctl(s,SIO_KEEPALIVE_VALS,&tune,sizeof(tune),nullptr,0,&used,nullptr,nullptr);
        return s;
    }
    bool transmit(SOCKET s,const Bytes& bytes,size_t& written){
        written=0;const ULONGLONG end=GetTickCount64()+1000;
        while(written<bytes.size()&&!stopped()&&GetTickCount64()<end){fd_set w;FD_ZERO(&w);FD_SET(s,&w);timeval tv={0,100000};int n=select(0,nullptr,&w,nullptr,&tv);if(n<0)return false;if(!n)continue;
            int k=send(s,(const char*)bytes.data()+written,(int)(bytes.size()-written),0);if(k<0&&WSAGetLastError()==WSAEWOULDBLOCK)continue;if(k<=0)return false;written+=(size_t)k;}
        return written==bytes.size();
    }
    void loop(Settings c){
        running=true;const bool shared=c.receivePort==c.sendPort;
        serialbridge::SerialForwarder serial;
        const auto syncSerial=[&](){
            serial.poll();serialOpened=serial.opened();serialPeer=serial.peer();serialBytes=serial.count();
            if(serial.fault()&&!serialFault.exchange(true)){clearPending();serial.discardPending();serialMessage(serial.error());info(L"串口故障",serial.error()+L" TCP接收显示继续，但不再转发、不补发。新触发已禁用。");}
        };
        if(!serial.open(c.serial)){serialMessage(serial.error());info(L"串口",serial.error());}
        else if(c.serial.enabled)info(L"串口",L"已核验虚拟串口配对并打开桥接端 "+c.serial.bridge+L"；目标软件请选择 "+c.serial.application+L"，参数须一致。未开软件端前禁止触发。RS232控制信号仅作为所配置驱动的状态线索。");
        syncSerial();
        info(L"连接",shared?L"相同端口：仅建立一条双向 TCP 连接":L"不同端口：分别连接扫码接收和触发发送服务");
        SOCKET data=connectSocket(c.ip,c.receivePort);receiveOnline=data!=INVALID_SOCKET;
        if(stopped()){closeSocket(data);running=false;return;}
        SOCKET cmd=shared?data:connectSocket(c.ip,c.sendPort);sendOnline=cmd!=INVALID_SOCKET;
        info(L"连接",receiveOnline?L"接收 TCP 已连接":L"接收 TCP 连接失败");
        info(L"连接",sendOnline?L"发送 TCP 已连接；不代表扫码枪接受了指令":L"发送 TCP 连接失败；不会妨碍独立的接收连接");
        const ULONGLONG readyAt=GetTickCount64();
        while(!stopped()&&(data!=INVALID_SOCKET||cmd!=INVALID_SOCKET)){
            syncSerial();
            Request request;bool have=false;{std::lock_guard<std::mutex> lock(mutex_);if(!pending_.empty()){request=std::move(pending_.front());pending_.pop_front();have=true;}}
            if(have){
                if(request.when<readyAt||GetTickCount64()-request.when>250||cmd==INVALID_SOCKET||(c.serial.enabled&&(!serial.ready()||!receiveOnline))){info(L"触发",L"请求已过期或发送连接已断开，丢弃且不补发");}
                else{size_t sent=0;if(transmit(cmd,request.bytes,sent)){txBytes.fetch_add(sent);push({L"TX 触发",L"本机已交给 TCP（设备是否执行看实际应答）",request.bytes,true});}
                    else{info(L"故障",L"触发写入失败，已发送 "+std::to_wstring(sent)+L" 字节，结果可能不确定；锁定此发送连接，不重发");
                        if(shared){closeSocket(data);cmd=INVALID_SOCKET;receiveOnline=false;}else closeSocket(cmd);sendOnline=false;clearPending();}}
            }
            fd_set rd;FD_ZERO(&rd);if(data!=INVALID_SOCKET)FD_SET(data,&rd);if(!shared&&cmd!=INVALID_SOCKET)FD_SET(cmd,&rd);
            if(data==INVALID_SOCKET&&cmd==INVALID_SOCKET)break;
            timeval tv={0,20000};int ready=select(0,&rd,nullptr,nullptr,&tv);if(ready<0){info(L"故障",L"TCP 监听失败，请手动重新连接");break;}
            for(int role=0;role<(shared?1:2);++role){
                SOCKET& sock=role==0?data:cmd;if(sock==INVALID_SOCKET||!ready||!FD_ISSET(sock,&rd))continue;
                char buffer[4096];int count=recv(sock,buffer,sizeof(buffer),0);
                if(count<0&&WSAGetLastError()==WSAEWOULDBLOCK)continue;
                if(count<=0){info(L"断开",role==0?L"接收连接断开；不会自动重连或重发":L"发送连接断开；独立接收通道继续监听");
                    closeSocket(sock);if(shared){cmd=INVALID_SOCKET;sendOnline=false;receiveOnline=false;clearPending();}else if(role==0)receiveOnline=false;else{sendOnline=false;clearPending();}continue;}
                Bytes bytes((unsigned char*)buffer,(unsigned char*)buffer+count);rxBytes.fetch_add((size_t)count);
                if(c.serial.enabled&&role==0){
                    if(serial.write(bytes,stopping_))push({L"COM 转发",L"已写入桥接端（不代表目标软件已处理）",bytes,true});
                    syncSerial();
                }
                push({shared?L"RX 共用连接":(role==0?L"RX 扫码端口":L"RX 命令应答"),L"TCP 字节块（不把拆包/粘包误称完整条码）",std::move(bytes),true});
            }
        }
        if(shared)cmd=INVALID_SOCKET;closeSocket(cmd);closeSocket(data);receiveOnline=false;sendOnline=false;running=false;clearPending();
        serial.close();serialOpened=false;serialPeer=false;
        info(L"状态",L"连接已停止；需要继续请手动连接，不补发旧触发或旧扫码数据");
    }
public:
    std::atomic<bool> running,receiveOnline,sendOnline,sharedConnection;std::atomic<unsigned long long> txBytes,rxBytes;
    std::atomic<bool> serialEnabled{false},serialOpened{false},serialPeer{false},serialFault{false};std::atomic<unsigned long long> serialBytes{0};
    TcpEngine():stopping_(CreateEventW(nullptr,TRUE,FALSE,nullptr)),running(false),receiveOnline(false),sendOnline(false),sharedConnection(false),txBytes(0),rxBytes(0){}
    ~TcpEngine(){stop();CloseHandle(stopping_);}
    void info(const std::wstring& channel,const std::wstring& text){push({channel,text,Bytes{},false});}
    void clearPending(){std::lock_guard<std::mutex> lock(mutex_);pending_.clear();}
    void stop(){SetEvent(stopping_);if(worker_.joinable())worker_.join();running=false;receiveOnline=false;sendOnline=false;serialOpened=false;serialPeer=false;clearPending();}
    std::wstring serialStatus(){std::lock_guard<std::mutex> lock(mutex_);return serialMessage_;}
    bool start(const Settings& c){if(!validSettings(c))return false;stop();ResetEvent(stopping_);sharedConnection=c.receivePort==c.sendPort;txBytes=0;rxBytes=0;serialBytes=0;serialEnabled=c.serial.enabled;serialFault=false;serialMessage(L"");running=true;worker_=std::thread(&TcpEngine::loop,this,c);return true;}
    bool queue(const Bytes& bytes){if(bytes.empty()||bytes.size()>64||!sendOnline||stopped()||(serialEnabled&&(!serialOpened||!serialPeer||serialFault||!receiveOnline)))return false;std::lock_guard<std::mutex> lock(mutex_);if(pending_.size()>=8)return false;pending_.push_back({bytes,GetTickCount64()});return true;}
    std::vector<Record> drain(){std::lock_guard<std::mutex> lock(mutex_);std::vector<Record> result;if(logOverflow_){result.push_back({L"显示",L"接收过快，显示队列已舍弃旧记录；计数仍持续更新",Bytes{},false});logOverflow_=false;}while(!records_.empty()){result.push_back(std::move(records_.front()));records_.pop_front();}return result;}
};
TcpEngine engine;
const UINT IP=101,RXPORT=102,TXPORT=103,ENCODING=104,PAYLOAD=105,KEY=106,CONNECT=201,DISCONNECT=202,SEND=203,CLEAR=204,HOTKEY=205,STATE=206,LOG=207,COM_ENABLE=301,COM_BRIDGE=302,COM_APP=303,COM_BAUD=304,COM_BITS=305,COM_PARITY=306,COM_STOP=307,COM_REFRESH=308,COM_PAIR=309,COM_SHARED=310,UI_SETTINGS=401,UI_RX=410,UI_TX=411,UI_COM=412,UI_RX_BYTES=413,UI_TX_BYTES=414,UI_COM_BYTES=415,UI_TRAY_HINT=416;
HWND window=nullptr;HFONT font=nullptr;bool hexMode=true,keyDown=false;std::wstring directory,configPath;DWORD hotkeyBoundary=0;HANDLE pairProcess=nullptr;ULONGLONG pairStarted=0;bool pairWarned=false,pairLaunching=false,pairBlocked=false;std::deque<std::wstring> pairCommands;SerialOptions pairOptions;std::wstring pairSetup;
HANDLE driverProcess=nullptr;ULONGLONG driverStarted=0;bool driverLaunching=false,driverBlocked=false,driverWarned=false;
bool driverBusy(){return driverProcess||driverLaunching||driverBlocked;}
std::wstring text(UINT id){int n=GetWindowTextLengthW(GetDlgItem(window,(int)id));if(n>1024)n=1024;std::vector<wchar_t> b((size_t)n+1);GetWindowTextW(GetDlgItem(window,(int)id),b.data(),n+1);return b.data();}
void set(UINT id,const std::wstring& s){if(text(id)!=s)SetWindowTextW(GetDlgItem(window,(int)id),s.c_str());}
void updateDashboard();void cancelStartup();
std::wstring ini(const wchar_t* key,const wchar_t* fallback=L""){wchar_t b[1024]={};GetPrivateProfileStringW(L"TCP",key,fallback,b,1024,configPath.c_str());return b;}
std::wstring communicationPortText(){
    std::wstring single=ini(L"CommunicationPort",L"#absent#");
    if(single!=L"#absent#")return portNumber(single)?single:L"";
    std::wstring receive=ini(L"ReceivePort"),send=ini(L"SendPort");
    return portNumber(receive)&&portNumber(receive)==portNumber(send)?std::to_wstring(portNumber(receive)):L"";
}
std::wstring serialIni(const wchar_t* key,const wchar_t* fallback=L""){wchar_t b[1024]={};GetPrivateProfileStringW(L"Serial",key,fallback,b,1024,configPath.c_str());return b;}
std::wstring upperCom(std::wstring name){for(auto& c:name)if(c>=L'a'&&c<=L'z')c=(wchar_t)(c-L'a'+L'A');return name;}
SerialOptions serialFromUI(){
    SerialOptions o;o.enabled=true; // This tool always forwards TCP payloads to the configured virtual COM pair.
    o.sharedApproved=true; // Explicit single full-duplex scanner-server topology; all RX bytes are forwarded.
    o.bridge=upperCom(text(COM_BRIDGE));o.application=upperCom(text(COM_APP));o.baud=(DWORD)_wtoi(text(COM_BAUD).c_str());
    o.bits=(BYTE)_wtoi(text(COM_BITS).c_str());LRESULT parity=SendMessageW(GetDlgItem(window,COM_PARITY),CB_GETCURSEL,0,0);
    o.parity=parity==1?ODDPARITY:(parity==2?EVENPARITY:NOPARITY);o.stopBits=text(COM_STOP)==L"2"?TWOSTOPBITS:ONESTOPBIT;return o;
}
void refreshComs(){
    std::wstring beforeBridge=text(COM_BRIDGE),beforeApp=text(COM_APP);std::vector<std::wstring> names;
    std::vector<wchar_t> buffer(262144);DWORD used=QueryDosDeviceW(nullptr,buffer.data(),(DWORD)buffer.size());
    if(used)for(const wchar_t* at=buffer.data();*at;at+=wcslen(at)+1)if(serialbridge::comName(at))names.push_back(at);
    std::sort(names.begin(),names.end(),[](const std::wstring& a,const std::wstring& b){return _wtoi(a.c_str()+3)<_wtoi(b.c_str()+3);});
    for(UINT id:{COM_BRIDGE,COM_APP}){HWND combo=GetDlgItem(window,(int)id);SendMessageW(combo,CB_RESETCONTENT,0,0);for(const auto& name:names)SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)name.c_str());}
    set(COM_BRIDGE,beforeBridge);set(COM_APP,beforeApp);engine.info(L"串口列表",L"已刷新，发现 "+std::to_wstring(names.size())+L" 个系统COM；仅显示存在，不等于已核验配对。可手填新端口号后点配对设置。");
}
void pairClicked();void pollPair();
std::wstring timestamp(){SYSTEMTIME t;GetLocalTime(&t);wchar_t b[32];swprintf_s(b,L"%02u:%02u:%02u.%03u",t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);return b;}
void appendLog(const std::wstring& line){
    HWND edit=GetDlgItem(window,LOG);if(GetWindowTextLengthW(edit)>120000)SetWindowTextW(edit,L"[为防长时间运行占用内存，旧显示已清理]\r\n");
    SendMessageW(edit,EM_SETSEL,(WPARAM)-1,(LPARAM)-1);SendMessageW(edit,EM_REPLACESEL,FALSE,(LPARAM)line.c_str());SendMessageW(edit,EM_SCROLLCARET,0,0);
    std::wstring folder=directory+L"\\logs";CreateDirectoryW(folder.c_str(),nullptr);std::wstring file=folder+L"\\tcp.log",previous=folder+L"\\tcp.previous.log";
    WIN32_FILE_ATTRIBUTE_DATA a;if(GetFileAttributesExW(file.c_str(),GetFileExInfoStandard,&a)&&a.nFileSizeLow>2*1024*1024){DeleteFileW(previous.c_str());MoveFileW(file.c_str(),previous.c_str());}
    HANDLE h=CreateFileW(file.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return;
    int length=WideCharToMultiByte(CP_UTF8,0,line.c_str(),(int)line.size(),nullptr,0,nullptr,nullptr);std::string bytes((size_t)length,0);WideCharToMultiByte(CP_UTF8,0,line.c_str(),(int)line.size(),&bytes[0],length,nullptr,nullptr);DWORD written;WriteFile(h,bytes.data(),(DWORD)bytes.size(),&written,nullptr);CloseHandle(h);
}
void refresh(){
    for(const auto& r:engine.drain()){
        std::wstring line=timestamp()+L" ["+r.channel+L"] "+r.text;
        if(r.payload)line+=L"\r\n  HEX: "+tcpcodecs::formatHex(r.bytes)+L"\r\n  ASCII: "+tcpcodecs::formatAscii(r.bytes);
        appendLog(line+L"\r\n");
    }
    std::wstring summary=engine.serialFault?L"串口故障："+engine.serialStatus():L"接收原字节转发；TCP TX "+std::to_wstring(engine.txBytes.load())+L" / RX "+std::to_wstring(engine.rxBytes.load())+L" B；COM "+std::to_wstring(engine.serialBytes.load())+L" B";
    set(STATE,summary);updateDashboard();
}
bool convertEncoding(bool next,bool showError=true){
    if(next==hexMode)return true;std::wstring before=text(PAYLOAD);Bytes bytes;
    if(!before.empty()&&!tcpcodecs::parseTrigger(before,hexMode,bytes)){
        SendMessageW(GetDlgItem(window,ENCODING),CB_SETCURSEL,hexMode?1:0,0);
        if(showError)MessageBoxW(window,L"当前内容不能按原编码解析，未切换且未更改输入。HEX需完整字节；ASCII控制字符用反斜杠转义。",L"编码未切换",MB_ICONWARNING);return false;
    }
    set(PAYLOAD,next?tcpcodecs::formatHex(bytes):tcpcodecs::formatAscii(bytes));hexMode=next;SendMessageW(GetDlgItem(window,ENCODING),CB_SETCURSEL,next?1:0,0);return true;
}
bool persist(const Settings& c,const Bytes& bytes){
    if(c.receivePort!=c.sendPort)return false;
    wchar_t tmp[MAX_PATH]={};if(!GetTempFileNameW(directory.c_str(),L"TCP",0,tmp))return false;
    std::wstring doc=L"[TCP]\r\nReaderIp="+c.ip+L"\r\nCommunicationPort="+std::to_wstring(c.receivePort)+L"\r\nReceivePort="+std::to_wstring(c.receivePort)+L"\r\nSendPort="+std::to_wstring(c.sendPort)+
        L"\r\nEncoding="+(hexMode?std::wstring(L"HEX"):std::wstring(L"ASCII"))+L"\r\nTriggerHex="+tcpcodecs::formatHex(bytes)+L"\r\nTriggerKey="+text(KEY)+L"\r\n[Serial]\r\nEnabled="+std::wstring(c.serial.enabled?L"1":L"0")+L"\r\nBridgeCom="+c.serial.bridge+L"\r\nApplicationCom="+c.serial.application+
        L"\r\nBaud="+std::to_wstring(c.serial.baud)+L"\r\nBits="+std::to_wstring(c.serial.bits)+L"\r\nParity="+std::to_wstring(c.serial.parity)+L"\r\nStopBits="+std::to_wstring(c.serial.stopBits)+L"\r\n";
    HANDLE h=CreateFileW(tmp,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE){DeleteFileW(tmp);return false;}
    const wchar_t bom=0xFEFF;DWORD count=0;bool ok=WriteFile(h,&bom,2,&count,nullptr)&&count==2;DWORD n=(DWORD)(doc.size()*sizeof(wchar_t));if(ok)ok=WriteFile(h,doc.data(),n,&count,nullptr)&&count==n;if(ok)ok=FlushFileBuffers(h)!=FALSE;CloseHandle(h);
    if(ok)ok=MoveFileExW(tmp,configPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;if(!ok)DeleteFileW(tmp);WritePrivateProfileStringW(nullptr,nullptr,nullptr,configPath.c_str());return ok;
}
void connectClicked(){
    cancelStartup();
    if(pairProcess||pairLaunching||pairBlocked||driverBusy()){engine.info(L"拒绝",L"串口配对设置尚未结束，不允许重连");refresh();return;}
    Settings c;c.ip=text(IP);c.receivePort=portNumber(text(RXPORT));c.sendPort=c.receivePort;c.serial=serialFromUI();Bytes bytes;
    if(!validSerialOptions(c.serial)){MessageBoxW(window,L"串口启用时，两个COM必须有效且不同，串口参数须与目标软件一致。",L"串口参数无效",MB_ICONERROR);return;}

    if(!validSettings(c)){MessageBoxW(window,L"请输入扫码枪真实IPv4和有效通讯端口（1至65535）。",L"连接参数无效",MB_ICONERROR);return;}
    if(!tcpcodecs::parseTrigger(text(PAYLOAD),hexMode,bytes)){MessageBoxW(window,L"请按所选ASCII/HEX格式填写触发内容，最多64字节；不会自动加回车换行。",L"发送内容无效",MB_ICONERROR);return;}
    if(!persist(c,bytes)){MessageBoxW(window,L"配置保存失败，未改变当前连接；请把文件夹放在可写位置。",L"保存失败",MB_ICONERROR);return;}
    engine.start(c);hotkeyBoundary=GetTickCount();keyDown=false;engine.info(L"配置",L"已保存并开始连接；不会自动发送触发指令");refresh();
}
bool preparationCreating=false;
void preparationPairResult(bool ok,const std::wstring& error);
std::wstring preparationSetup();
bool comExists(const std::wstring& name){
#ifdef TCP_TESTER_TEST
    if(preparationSimulation.active)return std::find(preparationSimulation.existing.begin(),preparationSimulation.existing.end(),name)!=preparationSimulation.existing.end();
#endif
    wchar_t target[4096];return QueryDosDeviceW(name.c_str(),target,4096)!=0;
}
bool namesFree(const SerialOptions& o,std::wstring& error){
    if(!serialbridge::comName(o.bridge)||!serialbridge::comName(o.application)||o.bridge==o.application){error=L"请填写两个不同的规范COM号";return false;}
    if(comExists(o.bridge)||comExists(o.application)){error=L"所选COM已存在，禁止创建覆盖";return false;}
#ifdef TCP_TESTER_TEST
    if(preparationSimulation.active){if(!preparationSimulation.arbiterValid){error=L"模拟COM保留证据不可读";return false;}for(const auto& name:preparationSimulation.reserved)if(name==o.bridge||name==o.application){error=name+L" 已保留，不能覆盖";return false;}return true;}
#endif
    HKEY key=nullptr;LONG rc=RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\COM Name Arbiter",0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&key);
    if(rc!=ERROR_SUCCESS){error=L"无法只读检查系统COM保留表，禁止自动创建";return false;}
    DWORD type=0,size=0;rc=RegQueryValueExW(key,L"ComDB",nullptr,&type,nullptr,&size);
    if(rc!=ERROR_SUCCESS||type!=REG_BINARY||!size||size>65536){RegCloseKey(key);error=L"系统COM保留表格式不可验证";return false;}
    std::vector<BYTE> bitmap(size);rc=RegQueryValueExW(key,L"ComDB",nullptr,&type,bitmap.data(),&size);RegCloseKey(key);
    if(rc!=ERROR_SUCCESS){error=L"读取系统COM保留表失败";return false;}
    for(const auto& name:{o.bridge,o.application}){size_t index=(size_t)_wtoi(name.c_str()+3)-1;if(index>=bitmap.size()*8||(bitmap[index/8]&(1u<<(index%8)))){error=name+L" 已保留或超出可验证范围，不能覆盖";return false;}}
    return true;
}
#ifdef TCP_TESTER_TEST
bool testPairLaunch=false,testPairLaunchResult=false,testPairTrust=false;
#endif
bool startPairProcess(SHELLEXECUTEINFOW& info){
#ifdef TCP_TESTER_TEST
    if(preparationSimulation.active){++preparationSimulation.launches;preparationSimulation.launchedPath=info.lpFile;preparationSimulation.launchedArguments=info.lpParameters?info.lpParameters:L"";info.hProcess=preparationSimulation.child;return preparationSimulation.launch;}
    if(testPairLaunch){if(!testPairLaunchResult)SetLastError(ERROR_CANCELLED);return testPairLaunchResult;}
#endif
    return ShellExecuteExW(&info)!=FALSE;
}
bool pairPathTrusted(){
#ifdef TCP_TESTER_TEST
    if(preparationSimulation.active)return pairSetup==preparationSimulation.setup&&!pairSetup.empty();
    if(testPairLaunch)return testPairTrust;
#endif
    return !pairSetup.empty()&&serialbridge::findSetupc()==pairSetup;
}
bool launchPairCommand(){
    if(pairCommands.empty()||pairProcess||pairLaunching||pairBlocked)return false;
    const auto unlockPairUI=[](){EnableWindow(GetDlgItem(window,COM_PAIR),TRUE);EnableWindow(GetDlgItem(window,CONNECT),TRUE);};
    // Revalidate path ownership/ACLs before every elevated operation; never use PATH or cmd.exe.
    if(!pairPathTrusted()){engine.info(L"配对设置",L"安装路径信任核验失败，设置停止");pairCommands.clear();unlockPairUI();return false;}
    SHELLEXECUTEINFOW start={};start.cbSize=sizeof(start);start.fMask=SEE_MASK_NOCLOSEPROCESS;start.hwnd=window;start.lpVerb=L"runas";start.lpFile=pairSetup.c_str();start.lpParameters=pairCommands.front().c_str();start.nShow=SW_SHOWNORMAL;
    pairLaunching=true;EnableWindow(GetDlgItem(window,COM_PAIR),FALSE);EnableWindow(GetDlgItem(window,CONNECT),FALSE);
    BOOL launched=startPairProcess(start);pairLaunching=false;
    if(!launched){engine.info(L"配对设置",L"未执行：管理员确认取消或启动失败，Windows错误 "+std::to_wstring(GetLastError()));pairCommands.clear();unlockPairUI();return false;}
    if(!start.hProcess){pairBlocked=true;engine.info(L"配对设置",L"无可核验的设置进程句柄；请退出本工具并人工检查，禁止重复创建");pairCommands.clear();EnableWindow(GetDlgItem(window,COM_PAIR),FALSE);EnableWindow(GetDlgItem(window,CONNECT),FALSE);return false;}
    pairProcess=start.hProcess;pairStarted=GetTickCount64();pairWarned=false;pairCommands.pop_front();EnableWindow(GetDlgItem(window,COM_PAIR),FALSE);EnableWindow(GetDlgItem(window,CONNECT),FALSE);
    engine.info(L"配对设置",L"管理员设置已启动；请完成系统确认，结束前不允许重连或重复设置");return true;
}
void pollPair(){
    if(!pairProcess)return;DWORD wait=WaitForSingleObject(pairProcess,0);
    if(wait==WAIT_TIMEOUT){if(!pairWarned&&GetTickCount64()-pairStarted>30000){pairWarned=true;engine.info(L"配对设置",L"设置仍在运行；界面保持响应，但禁止重复创建。请检查安装窗口，不强杀驱动进程。");}return;}
    DWORD exitCode=999;bool exited=wait==WAIT_OBJECT_0&&GetExitCodeProcess(pairProcess,&exitCode);
#ifdef TCP_TESTER_TEST
    if(preparationSimulation.active&&wait==WAIT_OBJECT_0){exited=true;exitCode=0;}
#endif
    CloseHandle(pairProcess);pairProcess=nullptr;
    if(!exited||exitCode!=0){pairCommands.clear();engine.info(L"配对设置",L"设置未验证成功，退出码 "+std::to_wstring(exitCode)+L"；部分设置可能已生效，请检查库存。未自动连接。");if(preparationCreating)preparationPairResult(false,L"设置进程未成功，退出码 "+std::to_wstring(exitCode));}
    else if(!pairCommands.empty()){launchPairCommand();return;}
    else{std::wstring error;bool ok=serialbridge::inspectPair(pairOptions,error);engine.info(L"配对设置",ok?L"库存已核验：同一对COM且映射设置正确。还需目标电脑真实收发测试；请先打开软件端，再点击保存并连接。":L"进程结束，但配对检查失败："+error);if(preparationCreating)preparationPairResult(ok,error);}
    EnableWindow(GetDlgItem(window,COM_PAIR),TRUE);EnableWindow(GetDlgItem(window,CONNECT),TRUE);refreshComs();
}
bool newPairCommand(const SerialOptions& o,const std::wstring& setup,std::wstring& parameters,std::wstring& error){
    if(!namesFree(o,error))return false;
    com0com::AdminCreateCommand command;std::string detail;
    if(!com0com::BuildAdminCreateCommand(setup,serialbridge::narrow(o.application),serialbridge::narrow(o.bridge),command,detail)){error=L"创建参数拒绝："+serialbridge::wide(detail);return false;}
    parameters=command.parameters;return true;
}
void pairClicked(){
    if(pairProcess||pairLaunching||pairBlocked||driverBusy()){engine.info(L"配对设置",L"设置尚未结束，不能重复运行");return;}
    if(engine.running||engine.serialOpened||engine.receiveOnline||engine.sendOnline){MessageBoxW(window,L"请先点击“断开”，并关闭目标软件所选串口，再进行配对设置；不在生产收发中修改驱动。",L"请先停止连接",MB_ICONWARNING);return;}
    SerialOptions o=serialFromUI();o.enabled=true;if(!validSerialOptions(o)){MessageBoxW(window,L"请填写两个不同的COM号，并核对参数。",L"COM设置无效",MB_ICONWARNING);return;}
    std::wstring setup=serialbridge::findSetupc();if(setup.empty()){MessageBoxW(window,L"尚未找到受管理员保护的com0com/setupc.exe。先安装包内 drivers/win7-x64 驱动（仅Win7 x64），或使用已经获批的适配驱动。不要在Win10/11盲装Win7驱动，不关闭签名/安全启动。已有COM请先核验，勿重复安装。",L"需要虚拟串口驱动",MB_ICONWARNING);return;}
    com0com::Inventory inventory;std::wstring inventoryError;
    bool existing=comExists(o.bridge)||comExists(o.application);
    if(existing){if(!serialbridge::readSelectedInventory(o,inventory,inventoryError)){engine.info(L"配对设置",L"不能核验现有配对，拒绝修改："+inventoryError);return;}}
    else{auto capture=com0com::CaptureInventory(setup,2000);if(capture.status!=com0com::CaptureStatus::Ok){engine.info(L"配对设置",com0com::DescribeCaptureFailure(capture));return;}inventory=capture.inventory;}
    auto pair=com0com::ValidatePair(inventory,serialbridge::narrow(o.application),serialbridge::narrow(o.bridge));pairCommands.clear();
    if(pair.identityVerified){
        if(pair.pinsVerified){engine.info(L"配对设置",L"现有虚拟串口对及方向设置已正确，不需要重复创建或修改。");return;}
        std::wstring prompt=L"现有同一对：软件端 "+o.application+L" ↔ 桥接端 "+o.bridge+L"。仅设置软件端EmuOverrun=yes，桥接端dsr=ropen、EmuBR=yes；不改端口号码。请先关闭两端串口。需系统管理员确认，继续吗？";
        if(MessageBoxW(window,prompt.c_str(),L"确认现有配对设置",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
        pairCommands.push_back(L"change "+serialbridge::wide(pair.packagingId)+L" EmuOverrun=yes");pairCommands.push_back(L"change "+serialbridge::wide(pair.bridgeId)+L" dsr=ropen,EmuBR=yes");
    }else{
        for(const auto& ep:inventory.endpoints)if(ep.portName==serialbridge::narrow(o.bridge)||ep.portName==serialbridge::narrow(o.application)){engine.info(L"配对设置",L"所选COM已存在但不是同一虚拟串口对，禁止覆盖。请重新选择。");return;}
        std::wstring error;if(!namesFree(o,error)){engine.info(L"配对设置",error);return;}
        std::wstring prompt=L"将创建新虚拟串口对：软件端 "+o.application+L" ↔ 桥接端 "+o.bridge+L"。不覆盖已存在或保留的COM，需系统管理员授权。继续吗？";
        if(MessageBoxW(window,prompt.c_str(),L"确认创建虚拟串口",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
        if(!namesFree(o,error)){engine.info(L"配对设置",error);return;}
        std::wstring parameters;if(!newPairCommand(o,setup,parameters,error)){engine.info(L"配对设置",error);return;}pairCommands.push_back(parameters);
    }
    engine.stop();pairOptions=o;pairSetup=setup;launchPairCommand();
}
void sendClicked(){Bytes bytes;if(!tcpcodecs::parseTrigger(text(PAYLOAD),hexMode,bytes)){engine.info(L"拒绝",L"输入不是所选编码的有效数据，未发送");}
    else if(!engine.queue(bytes))engine.info(L"拒绝",L"连接未就绪/串口未启用就绪/故障锁定/队列已满，未发送、不会补发；看状态栏");refresh();}
HWND control(const wchar_t* cls,const wchar_t* label,DWORD style,int x,int y,int w,int h,UINT id){
    if(!wcscmp(cls,L"BUTTON")&&((style&BS_TYPEMASK)==BS_PUSHBUTTON))style|=BS_OWNERDRAW;
    HWND c=CreateWindowW(cls,label,WS_CHILD|WS_VISIBLE|style,operatorui::px(x),operatorui::px(y),operatorui::px(w),operatorui::px(h),window,(HMENU)(UINT_PTR)id,nullptr,nullptr);SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;
}
bool handleKeyboardPacket(const void* packet,UINT count,DWORD captured,DWORD foregroundPid){
    if(!packet||!keyboardPacketSize(count))return false;
    RAWINPUTHEADER header={};memcpy(&header,packet,sizeof(header));
    if(header.dwType!=RIM_TYPEKEYBOARD||header.dwSize<offsetof(RAWINPUT,data)+sizeof(RAWKEYBOARD)||header.dwSize>count)return false;
    RAWKEYBOARD key={};memcpy(&key,(const BYTE*)packet+offsetof(RAWINPUT,data),sizeof(key));
    const UINT selected=tcpcodecs::keyCode(text(KEY));if(!selected||key.VKey!=selected)return false;
    if(key.Flags&RI_KEY_BREAK){keyDown=false;return true;}
    if(keyDown)return true;keyDown=true;
    if(SendMessageW(GetDlgItem(window,HOTKEY),BM_GETCHECK,0,0)!=BST_CHECKED)return true;
    if((LONG)(captured-hotkeyBoundary)<0||(DWORD)(GetTickCount()-captured)>250){engine.info(L"按键",L"已捕获按键，但输入已过期，丢弃且不补发");return true;}
    if(foregroundPid==GetCurrentProcessId()){engine.info(L"按键",L"已捕获 "+text(KEY)+L"；调试窗口在前台，未发送。切到记事本/包装程序测试后台按键");return true;}
    engine.info(L"按键",L"已捕获后台 "+text(KEY)+L"，提交当前触发内容");sendClicked();return true;
}
#include "operator_runtime.h"
#include "virtual_com_preparation.h"
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_CREATE){window=h;createOperatorControls(h);return 0;}
    if(m==WM_STARTUP_BEGIN){beginStartup();return 0;}if(m==WM_STARTUP_CONNECT){runStartupOnce();return 0;}
    if(m==WM_TIMER){if(w==2){runStartupOnce();return 0;}pollPreparationDriver();pollPair();refresh();RECT cards=operatorui::box(24,86,912,134);InvalidateRect(h,&cards,FALSE);return 0;}
    if(m==WM_SIZE&&w==SIZE_MINIMIZED&&trayInstalled){ShowWindow(h,SW_HIDE);return 0;}
    if(taskbarRestartMessage&&m==taskbarRestartMessage){trayInstalled=false;addTray();return 0;}
    if(m==WM_TRAY){if(l==WM_LBUTTONDBLCLK||l==WM_LBUTTONUP)restoreMainWindow();else if(l==WM_RBUTTONUP)showTrayMenu();return 0;}
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_DRAWITEM&&operatorui::drawButton((DRAWITEMSTRUCT*)l,CONNECT))return TRUE;
    if(m==WM_PAINT){PAINTSTRUCT paint={};HDC screen=BeginPaint(h,&paint);RECT r;GetClientRect(h,&r);HDC buffer=CreateCompatibleDC(screen);HBITMAP image=CreateCompatibleBitmap(screen,r.right,r.bottom);HGDIOBJ old=SelectObject(buffer,image);operatorui::paint(buffer,r.right,r.bottom,engine.serialFault);operatorui::dot(buffer,434,107,engine.receiveOnline&&engine.sendOnline?operatorui::Green:operatorui::Muted);operatorui::dot(buffer,902,107,engine.serialFault?operatorui::Red:(engine.serialPeer?operatorui::Green:operatorui::Muted));BitBlt(screen,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(image);DeleteDC(buffer);EndPaint(h,&paint);return 0;}
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLORBTN||m==WM_CTLCOLOREDIT){UINT id=(UINT)GetDlgCtrlID((HWND)l);COLORREF color=operatorui::Ink;if(id==UI_RX)color=engine.receiveOnline?operatorui::Green:operatorui::Muted;if(id==UI_TX)color=engine.sendOnline?operatorui::Green:operatorui::Muted;if(id==UI_COM)color=engine.serialFault?operatorui::Red:(engine.serialPeer?operatorui::Green:operatorui::Muted);if(id==STATE&&engine.serialFault)color=operatorui::Red;SetTextColor((HDC)w,color);COLORREF background=id==STATE?(engine.serialFault?RGB(255,242,242):RGB(235,240,248)):(id==UI_TRAY_HINT?operatorui::Background:operatorui::Paper);SetBkColor((HDC)w,background);SetDCBrushColor((HDC)w,background);return (LRESULT)GetStockObject(DC_BRUSH);}
    if(m==WM_COMMAND){UINT changed=LOWORD(w);bool editable=(changed>=IP&&changed<=KEY)||(changed>=COM_BRIDGE&&changed<=COM_STOP);bool toggle=(changed==COM_ENABLE||changed==COM_SHARED||changed==HOTKEY)&&HIWORD(w)==BN_CLICKED;if(startupPending&&((editable&&(HIWORD(w)==EN_CHANGE||HIWORD(w)==CBN_SELCHANGE))||toggle))cancelStartup();if(LOWORD(w)==KEY&&HIWORD(w)==CBN_SELCHANGE){hotkeyBoundary=GetTickCount();keyDown=false;return 0;}if(LOWORD(w)==ENCODING&&HIWORD(w)==CBN_SELCHANGE){convertEncoding(SendMessageW(GetDlgItem(h,ENCODING),CB_GETCURSEL,0,0)==1);return 0;}
        switch(LOWORD(w)){case COM_PREPARE:openPreparation();return 0;case UI_SETTINGS:openPreferences();return 0;case COM_REFRESH:refreshComs();refresh();return 0;case COM_PAIR:pairClicked();refresh();return 0;case CONNECT:connectClicked();return 0;case DISCONNECT:cancelStartup();engine.stop();hotkeyBoundary=GetTickCount();engine.info(L"用户",L"已手动断开");refresh();return 0;case SEND:sendClicked();return 0;case CLEAR:set(LOG,L"");return 0;case HOTKEY:hotkeyBoundary=GetTickCount();keyDown=false;engine.info(L"按键",SendMessageW(GetDlgItem(h,HOTKEY),BM_GETCHECK,0,0)==BST_CHECKED?L"后台按键触发已开启；请切到其他窗口测试，原按键不会被吞掉":L"后台按键触发已关闭");refresh();return 0;}}
    if(m==WM_INPUT){
        UINT size=0;
        if(GetRawInputData((HRAWINPUT)l,RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER))==0&&keyboardPacketSize(size)){
            std::vector<BYTE> packet(size);UINT capacity=size;
            UINT copied=GetRawInputData((HRAWINPUT)l,RID_INPUT,packet.data(),&capacity,sizeof(RAWINPUTHEADER));
            if(copied!=UINT(-1)&&copied<=packet.size()){
                DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
                handleKeyboardPacket(packet.data(),copied,(DWORD)GetMessageTime(),pid);
            }else engine.info(L"按键",L"读取键盘数据失败，Windows错误 "+std::to_wstring(GetLastError()));
        }
        return DefWindowProcW(h,m,w,l);
    }
    if(m==WM_CLOSE){if(pairProcess||pairLaunching||driverProcess||driverLaunching){MessageBoxW(h,L"配对设置进程尚未结束，请先完成或取消系统安装窗口。不会强杀驱动设置。",L"请等待设置完成",MB_ICONWARNING);return 0;}DestroyWindow(h);return 0;}
    if(m==WM_DESTROY){KillTimer(h,1);KillTimer(h,2);removeTray();if(preferencesWindow)DestroyWindow(preferencesWindow);engine.stop();operatorui::release();font=nullptr;PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,m,w,l);
}
}
#ifndef TCP_TESTER_TEST
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    SetProcessDPIAware();
    HANDLE single=CreateMutexW(nullptr,TRUE,L"Local\\SR5000_TCP_COM_BRIDGE");if(GetLastError()==ERROR_ALREADY_EXISTS){HWND existing=FindWindowW(L"SR5000TcpComOperator",nullptr);if(existing){ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);}else MessageBoxW(nullptr,L"桥接工具已经在运行，请先退出旧实例。",L"已有实例",MB_OK);CloseHandle(single);return 0;}
    wchar_t exe[4096];GetModuleFileNameW(nullptr,exe,4096);executablePath=exe;directory=executablePath.substr(0,executablePath.find_last_of(L"\\/"));configPath=directory+L"\\tcp.ini";
    WSADATA wsa={};if(WSAStartup(MAKEWORD(2,2),&wsa)){CloseHandle(single);return 1;}
    WNDCLASSW wc={};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.lpszClassName=L"SR5000TcpComOperator";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);applicationIcon=LoadIconW(instance,MAKEINTRESOURCEW(201));wc.hIcon=applicationIcon;RegisterClassW(&wc);
    RECT work={};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);HDC dc=GetDC(nullptr);double density=GetDeviceCaps(dc,LOGPIXELSX)/96.0;ReleaseDC(nullptr,dc);double ratio=(std::min)(1.5,(std::min)(density,(std::min)((work.right-work.left-24)/980.0,(work.bottom-work.top-24)/700.0)));if(ratio<.65)ratio=.65;
    DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN;RECT size={0,0,(int)(960*ratio),(int)(660*ratio)};AdjustWindowRect(&size,style,FALSE);int width=size.right-size.left,height=size.bottom-size.top;
    HWND h=CreateWindowW(wc.lpszClassName,L"SR-5000 扫码串口桥接 v1.3.0-field.1",style,work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,nullptr,nullptr,instance,nullptr);if(!h){WSACleanup();CloseHandle(single);return 2;}
    RAWINPUTDEVICE rid={};rid.usUsagePage=1;rid.usUsage=6;rid.dwFlags=RIDEV_INPUTSINK;rid.hwndTarget=h;rawRegistered=RegisterRawInputDevices(&rid,1,sizeof(rid))!=FALSE;
    if(!rawRegistered){EnableWindow(GetDlgItem(h,HOTKEY),FALSE);engine.info(L"按键",L"后台键盘监听注册失败，Windows错误 "+std::to_wstring(GetLastError()));}
    taskbarRestartMessage=RegisterWindowMessageW(L"TaskbarCreated");addTray();ShowWindow(h,startupPrefs.hidden&&trayInstalled?SW_HIDE:SW_SHOW);PostMessageW(h,WM_STARTUP_BEGIN,0,0);
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(preparationWindow&&IsDialogMessageW(preparationWindow,&msg))continue;if(preferencesWindow&&IsDialogMessageW(preferencesWindow,&msg))continue;if(msg.message==WM_INPUT||!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    engine.stop();WSACleanup();CloseHandle(single);return 0;
}
#endif
