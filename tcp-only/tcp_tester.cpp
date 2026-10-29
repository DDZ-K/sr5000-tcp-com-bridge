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
#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shell32.lib")
namespace {
using Bytes=std::vector<unsigned char>;
bool requiresCom(){return false;}
bool keyboardPacketSize(UINT bytes){return bytes>=offsetof(RAWINPUT,data)+sizeof(RAWKEYBOARD)&&bytes<4096;}
struct Settings{std::wstring ip;int receivePort=0,sendPort=0;};
struct Record{std::wstring channel,text;Bytes bytes;bool payload=false;};
struct Request{Bytes bytes;ULONGLONG when;};
int portNumber(const std::wstring& s){if(s.empty()||s.size()>5)return 0;int n=0;for(wchar_t c:s){if(c<L'0'||c>L'9')return 0;n=n*10+c-L'0';}return n>=1&&n<=65535?n:0;}
bool validSettings(const Settings& c){IN_ADDR a={};return c.receivePort>0&&c.receivePort<=65535&&c.sendPort>0&&c.sendPort<=65535&&InetPtonW(AF_INET,c.ip.c_str(),&a)==1;}
class TcpEngine{
    HANDLE stopping_;std::thread worker_;std::mutex mutex_;std::deque<Request> pending_;std::deque<Record> records_;
    bool logOverflow_=false;
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
        info(L"连接",shared?L"相同端口：仅建立一条双向 TCP 连接":L"不同端口：分别连接扫码接收和触发发送服务");
        SOCKET data=connectSocket(c.ip,c.receivePort);receiveOnline=data!=INVALID_SOCKET;
        if(stopped()){closeSocket(data);running=false;return;}
        SOCKET cmd=shared?data:connectSocket(c.ip,c.sendPort);sendOnline=cmd!=INVALID_SOCKET;
        info(L"连接",receiveOnline?L"接收 TCP 已连接":L"接收 TCP 连接失败");
        info(L"连接",sendOnline?L"发送 TCP 已连接；不代表扫码枪接受了指令":L"发送 TCP 连接失败；不会妨碍独立的接收连接");
        const ULONGLONG readyAt=GetTickCount64();
        while(!stopped()&&(data!=INVALID_SOCKET||cmd!=INVALID_SOCKET)){
            Request request;bool have=false;{std::lock_guard<std::mutex> lock(mutex_);if(!pending_.empty()){request=std::move(pending_.front());pending_.pop_front();have=true;}}
            if(have){
                if(request.when<readyAt||GetTickCount64()-request.when>250||cmd==INVALID_SOCKET){info(L"触发",L"请求已过期或发送连接已断开，丢弃且不补发");}
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
                push({shared?L"RX 共用连接":(role==0?L"RX 扫码端口":L"RX 命令应答"),L"TCP 字节块（不把拆包/粘包误称完整条码）",std::move(bytes),true});
            }
        }
        if(shared)cmd=INVALID_SOCKET;closeSocket(cmd);closeSocket(data);receiveOnline=false;sendOnline=false;running=false;clearPending();
        info(L"状态",L"连接已停止；需要继续请手动连接，不补发旧触发");
    }
public:
    std::atomic<bool> running,receiveOnline,sendOnline,sharedConnection;std::atomic<unsigned long long> txBytes,rxBytes;
    TcpEngine():stopping_(CreateEventW(nullptr,TRUE,FALSE,nullptr)),running(false),receiveOnline(false),sendOnline(false),sharedConnection(false),txBytes(0),rxBytes(0){}
    ~TcpEngine(){stop();CloseHandle(stopping_);}
    void info(const std::wstring& channel,const std::wstring& text){push({channel,text,Bytes{},false});}
    void clearPending(){std::lock_guard<std::mutex> lock(mutex_);pending_.clear();}
    void stop(){SetEvent(stopping_);if(worker_.joinable())worker_.join();running=false;receiveOnline=false;sendOnline=false;clearPending();}
    bool start(const Settings& c){if(!validSettings(c))return false;stop();ResetEvent(stopping_);sharedConnection=c.receivePort==c.sendPort;txBytes=0;rxBytes=0;running=true;worker_=std::thread(&TcpEngine::loop,this,c);return true;}
    bool queue(const Bytes& bytes){if(bytes.empty()||bytes.size()>64||!sendOnline||stopped())return false;std::lock_guard<std::mutex> lock(mutex_);if(pending_.size()>=8)return false;pending_.push_back({bytes,GetTickCount64()});return true;}
    std::vector<Record> drain(){std::lock_guard<std::mutex> lock(mutex_);std::vector<Record> result;if(logOverflow_){result.push_back({L"显示",L"接收过快，显示队列已舍弃旧记录；计数仍持续更新",Bytes{},false});logOverflow_=false;}while(!records_.empty()){result.push_back(std::move(records_.front()));records_.pop_front();}return result;}
};
TcpEngine engine;
const UINT IP=101,RXPORT=102,TXPORT=103,ENCODING=104,PAYLOAD=105,KEY=106,CONNECT=201,DISCONNECT=202,SEND=203,CLEAR=204,HOTKEY=205,STATE=206,LOG=207;
HWND window=nullptr;HFONT font=nullptr;bool hexMode=true,keyDown=false;std::wstring directory,configPath;DWORD hotkeyBoundary=0;
std::wstring text(UINT id){int n=GetWindowTextLengthW(GetDlgItem(window,(int)id));if(n>1024)n=1024;std::vector<wchar_t> b((size_t)n+1);GetWindowTextW(GetDlgItem(window,(int)id),b.data(),n+1);return b.data();}
void set(UINT id,const std::wstring& s){SetWindowTextW(GetDlgItem(window,(int)id),s.c_str());}
std::wstring ini(const wchar_t* key,const wchar_t* fallback=L""){wchar_t b[1024]={};GetPrivateProfileStringW(L"TCP",key,fallback,b,1024,configPath.c_str());return b;}
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
    const bool shared=engine.sharedConnection.load();
    set(STATE,L"    接收 TCP："+std::wstring(engine.receiveOnline?L"已连接":L"未连接")+L"              发送 TCP："+(engine.sendOnline?L"已连接":L"未连接")+
        (shared?L"    同端口共用连接":L"    不同端口独立连接")+L"\r\nTX "+std::to_wstring(engine.txBytes.load())+L" 字节    RX "+std::to_wstring(engine.rxBytes.load())+L" 字节；无需 COM，收到即显示（含命令应答）");
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
    wchar_t tmp[MAX_PATH]={};if(!GetTempFileNameW(directory.c_str(),L"TCP",0,tmp))return false;
    std::wstring doc=L"[TCP]\r\nReaderIp="+c.ip+L"\r\nReceivePort="+std::to_wstring(c.receivePort)+L"\r\nSendPort="+std::to_wstring(c.sendPort)+
        L"\r\nEncoding="+(hexMode?std::wstring(L"HEX"):std::wstring(L"ASCII"))+L"\r\nTriggerHex="+tcpcodecs::formatHex(bytes)+L"\r\nTriggerKey="+text(KEY)+L"\r\n";
    HANDLE h=CreateFileW(tmp,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE){DeleteFileW(tmp);return false;}
    const wchar_t bom=0xFEFF;DWORD count=0;bool ok=WriteFile(h,&bom,2,&count,nullptr)&&count==2;DWORD n=(DWORD)(doc.size()*sizeof(wchar_t));if(ok)ok=WriteFile(h,doc.data(),n,&count,nullptr)&&count==n;if(ok)ok=FlushFileBuffers(h)!=FALSE;CloseHandle(h);
    if(ok)ok=MoveFileExW(tmp,configPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;if(!ok)DeleteFileW(tmp);WritePrivateProfileStringW(nullptr,nullptr,nullptr,configPath.c_str());return ok;
}
void connectClicked(){
    Settings c;c.ip=text(IP);c.receivePort=portNumber(text(RXPORT));c.sendPort=portNumber(text(TXPORT));Bytes bytes;
    if(!validSettings(c)){MessageBoxW(window,L"请输入扫码枪真实IPv4和1至65535的接收/发送设备端口。",L"连接参数无效",MB_ICONERROR);return;}
    if(!tcpcodecs::parseTrigger(text(PAYLOAD),hexMode,bytes)){MessageBoxW(window,L"请按所选ASCII/HEX格式填写触发内容，最多64字节；不会自动加回车换行。",L"发送内容无效",MB_ICONERROR);return;}
    if(!persist(c,bytes)){MessageBoxW(window,L"配置保存失败，未改变当前连接；请把文件夹放在可写位置。",L"保存失败",MB_ICONERROR);return;}
    hotkeyBoundary=GetTickCount();keyDown=false;engine.start(c);engine.info(L"配置",L"已保存并开始连接；不会自动发送触发指令");refresh();
}
void sendClicked(){Bytes bytes;if(!tcpcodecs::parseTrigger(text(PAYLOAD),hexMode,bytes)){engine.info(L"拒绝",L"输入不是所选编码的有效数据，未发送");}
    else if(!engine.queue(bytes))engine.info(L"拒绝",L"发送连接未就绪或队列已满，未发送、不会补发");refresh();}
HWND control(const wchar_t* cls,const wchar_t* label,DWORD style,int x,int y,int w,int h,UINT id){HWND c=CreateWindowW(cls,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,(HMENU)(UINT_PTR)id,nullptr,nullptr);SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;}
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
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_CREATE){window=h;font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        control(L"STATIC",L"扫码枪 IPv4",0,16,18,130,24,0);control(L"EDIT",ini(L"ReaderIp").c_str(),WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,145,14,235,28,IP);
        control(L"STATIC",L"扫码接收端口",0,405,18,130,24,0);control(L"EDIT",ini(L"ReceivePort").c_str(),WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,540,14,150,28,RXPORT);
        control(L"STATIC",L"触发发送端口",0,16,57,130,24,0);control(L"EDIT",ini(L"SendPort").c_str(),WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,145,53,140,28,TXPORT);
        control(L"STATIC",L"发送编码",0,315,57,90,24,0);HWND mode=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,405,53,135,100,ENCODING);SendMessageW(mode,CB_ADDSTRING,0,(LPARAM)L"ASCII");SendMessageW(mode,CB_ADDSTRING,0,(LPARAM)L"HEX");hexMode=ini(L"Encoding",L"HEX")!=L"ASCII";SendMessageW(mode,CB_SETCURSEL,hexMode?1:0,0);
        control(L"STATIC",L"发送内容",0,16,96,130,24,0);HWND payload=control(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,145,92,640,28,PAYLOAD);SendMessageW(payload,EM_SETLIMITTEXT,512,0);Bytes bytes;if(tcpcodecs::hex(ini(L"TriggerHex"),bytes))set(PAYLOAD,hexMode?tcpcodecs::formatHex(bytes):tcpcodecs::formatAscii(bytes));
        control(L"STATIC",LR"(ASCII控制字符: \r=回车 \n=换行 \\=反斜杠；编码切换只转换内容，不发送)",0,145,126,650,25,0);
        control(L"BUTTON",L"连接 / 保存并重连",WS_TABSTOP,16,165,190,32,CONNECT);control(L"BUTTON",L"断开",WS_TABSTOP,220,165,90,32,DISCONNECT);control(L"BUTTON",L"发送触发",WS_TABSTOP,324,165,130,32,SEND);control(L"BUTTON",L"清空显示",WS_TABSTOP,468,165,120,32,CLEAR);
        control(L"BUTTON",L"启用后台按键触发",BS_AUTOCHECKBOX|WS_TABSTOP,16,213,220,28,HOTKEY);HWND keys=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,245,212,130,250,KEY);
        SendMessageW(keys,CB_ADDSTRING,0,(LPARAM)L"Enter");SendMessageW(keys,CB_ADDSTRING,0,(LPARAM)L"Space");for(int i=1;i<=12;++i){std::wstring key=L"F"+std::to_wstring(i);SendMessageW(keys,CB_ADDSTRING,0,(LPARAM)key.c_str());}LRESULT k=SendMessageW(keys,CB_FINDSTRINGEXACT,(WPARAM)-1,(LPARAM)ini(L"TriggerKey",L"Enter").c_str());SendMessageW(keys,CB_SETCURSEL,k==CB_ERR?0:k,0);
        control(L"STATIC",L"默认关闭：先用发送按钮测试；勾选后其他程序前台也可触发",0,390,217,425,36,0);
        control(L"STATIC",L"未连接",0,16,260,790,55,STATE);
        control(L"EDIT",L"",WS_BORDER|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY|WS_VSCROLL|WS_TABSTOP,16,321,790,275,LOG);
        SetTimer(h,1,50,nullptr);engine.info(L"状态",L"仅扫码枪 TCP 调试：没有 COM、驱动、包装程序前台条件；填参数后点击连接。收到的数据和命令应答均显示 ASCII/HEX。");return 0;}
    if(m==WM_TIMER){refresh();RECT indicatorArea={12,258,807,281};InvalidateRect(h,&indicatorArea,FALSE);return 0;}
    if(m==WM_PAINT){PAINTSTRUCT paint={};HDC dc=BeginPaint(h,&paint);for(int i=0;i<2;++i){bool online=i==0?engine.receiveOnline.load():engine.sendOnline.load();HBRUSH brush=CreateSolidBrush(online?RGB(0,145,80):RGB(192,0,0));HGDIOBJ old=SelectObject(dc,brush);HGDIOBJ pen=SelectObject(dc,GetStockObject(NULL_PEN));int x=i==0?16:315;Ellipse(dc,x,263,x+12,275);SelectObject(dc,pen);SelectObject(dc,old);DeleteObject(brush);}EndPaint(h,&paint);return 0;}
    if(m==WM_COMMAND){if(LOWORD(w)==ENCODING&&HIWORD(w)==CBN_SELCHANGE){convertEncoding(SendMessageW(GetDlgItem(h,ENCODING),CB_GETCURSEL,0,0)==1);return 0;}
        switch(LOWORD(w)){case CONNECT:connectClicked();return 0;case DISCONNECT:engine.stop();hotkeyBoundary=GetTickCount();engine.info(L"用户",L"已手动断开");refresh();return 0;case SEND:sendClicked();return 0;case CLEAR:set(LOG,L"");return 0;case HOTKEY:hotkeyBoundary=GetTickCount();keyDown=false;engine.info(L"按键",SendMessageW(GetDlgItem(h,HOTKEY),BM_GETCHECK,0,0)==BST_CHECKED?L"后台按键触发已开启；请切到其他窗口测试，原按键不会被吞掉":L"后台按键触发已关闭");refresh();return 0;}}
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
    if(m==WM_CLOSE){DestroyWindow(h);return 0;}
    if(m==WM_DESTROY){KillTimer(h,1);engine.stop();DeleteObject(font);PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,m,w,l);
}
}
#ifndef TCP_TESTER_TEST
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    HANDLE single=CreateMutexW(nullptr,TRUE,L"Local\\SR5000_TCP_ONLY_TESTER");if(GetLastError()==ERROR_ALREADY_EXISTS){MessageBoxW(nullptr,L"TCP调试版已在运行，请使用已有窗口。",L"已有实例",MB_OK);CloseHandle(single);return 0;}
    wchar_t exe[4096];GetModuleFileNameW(nullptr,exe,4096);directory=exe;directory=directory.substr(0,directory.find_last_of(L"\\/"));configPath=directory+L"\\tcp.ini";
    WSADATA wsa={};if(WSAStartup(MAKEWORD(2,2),&wsa)){CloseHandle(single);return 1;}
    WNDCLASSW wc={};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.lpszClassName=L"SR5000TcpOnlyWindow";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&wc);
    HWND h=CreateWindowW(wc.lpszClassName,L"SR-5000 扫码枪 TCP 调试 v1.0.1（不使用 COM）",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,850,650,nullptr,nullptr,instance,nullptr);if(!h){WSACleanup();CloseHandle(single);return 2;}
    RAWINPUTDEVICE rid={};rid.usUsagePage=1;rid.usUsage=6;rid.dwFlags=RIDEV_INPUTSINK;rid.hwndTarget=h;
    if(!RegisterRawInputDevices(&rid,1,sizeof(rid))){EnableWindow(GetDlgItem(h,HOTKEY),FALSE);engine.info(L"按键",L"后台键盘注册失败，Windows错误 "+std::to_wstring(GetLastError())+L"；仍可用按钮收发TCP");}else engine.info(L"按键",L"后台键盘监听已注册（v1.0.1：兼容40字节键盘报文），勾选后可用");
    ShowWindow(h,SW_SHOW);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(msg.message==WM_INPUT||!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    engine.stop();WSACleanup();CloseHandle(single);return 0;
}
#endif
