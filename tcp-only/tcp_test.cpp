#define TCP_TESTER_TEST
#include "tcp_tester.cpp"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstddef>
#define CHECK(...) do{if(!(__VA_ARGS__)){fprintf(stderr,"FAILED %s:%d: %s\n",__FILE__,__LINE__,#__VA_ARGS__);std::abort();}}while(false)
struct Listener{
    SOCKET s;int port;
    explicit Listener(bool listening=true):s(socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)),port(0){
        CHECK(s!=INVALID_SOCKET);sockaddr_in a={};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=0;
        CHECK(bind(s,(sockaddr*)&a,sizeof(a))==0);int length=sizeof(a);CHECK(getsockname(s,(sockaddr*)&a,&length)==0);port=ntohs(a.sin_port);if(listening)CHECK(listen(s,4)==0);
    }
    ~Listener(){if(s!=INVALID_SOCKET)closesocket(s);}
    SOCKET acceptOne(){fd_set f;FD_ZERO(&f);FD_SET(s,&f);timeval t={3,0};CHECK(select(0,&f,nullptr,nullptr,&t)==1);SOCKET peer=accept(s,nullptr,nullptr);CHECK(peer!=INVALID_SOCKET);return peer;}
    bool hasExtra(){fd_set f;FD_ZERO(&f);FD_SET(s,&f);timeval t={0,80000};return select(0,&f,nullptr,nullptr,&t)>0;}
};
Bytes readExactly(SOCKET s,size_t length){
    Bytes out;ULONGLONG end=GetTickCount64()+2500;
    while(out.size()<length&&GetTickCount64()<end){fd_set f;FD_ZERO(&f);FD_SET(s,&f);timeval t={0,100000};int n=select(0,&f,nullptr,nullptr,&t);CHECK(n>=0);if(!n)continue;unsigned char b[64];int k=recv(s,(char*)b,(int)((std::min)(sizeof(b),length-out.size())),0);CHECK(k>0);out.insert(out.end(),b,b+k);}CHECK(out.size()==length);return out;
}
bool waitOnline(TcpEngine& e,bool rx,bool tx){ULONGLONG end=GetTickCount64()+2200;while(GetTickCount64()<end){if(e.receiveOnline==rx&&e.sendOnline==tx)return true;Sleep(5);}return false;}
Bytes collect(TcpEngine& e,const std::wstring& channel,size_t count){
    Bytes result;ULONGLONG end=GetTickCount64()+2200;
    while(result.size()<count&&GetTickCount64()<end){for(const auto& r:e.drain())if(r.payload&&r.channel==channel)result.insert(result.end(),r.bytes.begin(),r.bytes.end());Sleep(5);}CHECK(result.size()==count);return result;
}
void shared(){
    Listener listener;TcpEngine e;Settings c;c.ip=L"127.0.0.1";c.receivePort=c.sendPort=listener.port;CHECK(e.start(c));SOCKET peer=listener.acceptOne();CHECK(waitOnline(e,true,true));Sleep(10);CHECK(!listener.hasExtra());
    const Bytes unsolicited={'A',0,'B',0xFF,13,10};CHECK(send(peer,(const char*)unsolicited.data(),(int)unsolicited.size(),0)==(int)unsolicited.size());
    CHECK(collect(e,L"RX 共用连接",unsolicited.size())==unsolicited); // no COM and no prior trigger
    Bytes trigger;CHECK(tcpcodecs::parseTrigger(L"LON\\r",false,trigger));CHECK(e.queue(trigger));CHECK(readExactly(peer,trigger.size())==Bytes({'L','O','N',13}));
    fd_set f;FD_ZERO(&f);FD_SET(peer,&f);timeval t={0,50000};CHECK(select(0,&f,nullptr,nullptr,&t)==0); // no extra terminator
    const Bytes reply={'O','K',13};CHECK(send(peer,(const char*)reply.data(),(int)reply.size(),0)==(int)reply.size());CHECK(collect(e,L"RX 共用连接",reply.size())==reply);
    shutdown(peer,SD_BOTH);closesocket(peer);CHECK(waitOnline(e,false,false));CHECK(!e.queue(trigger));CHECK(!listener.hasExtra());e.stop();CHECK(e.txBytes==4&&e.rxBytes==unsolicited.size()+reply.size());
}
void separate(){
    Listener data,command;TcpEngine e;Settings c;c.ip=L"127.0.0.1";c.receivePort=data.port;c.sendPort=command.port;CHECK(e.start(c));SOCKET dp=data.acceptOne(),cp=command.acceptOne();CHECK(waitOnline(e,true,true));Sleep(10);
    const Bytes scan={'1','2','3',13,10},ack={'O','K',13};CHECK(send(dp,(const char*)scan.data(),(int)scan.size(),0)==(int)scan.size());CHECK(collect(e,L"RX 扫码端口",scan.size())==scan);
    Bytes trigger={0x4c,0};CHECK(e.queue(trigger));CHECK(readExactly(cp,2)==trigger);
    CHECK(send(cp,(const char*)ack.data(),(int)ack.size(),0)==(int)ack.size());CHECK(collect(e,L"RX 命令应答",ack.size())==ack);
    shutdown(cp,SD_BOTH);closesocket(cp);CHECK(waitOnline(e,true,false));CHECK(!e.queue(trigger));
    CHECK(send(dp,(const char*)scan.data(),(int)scan.size(),0)==(int)scan.size());CHECK(collect(e,L"RX 扫码端口",scan.size())==scan);e.stop();closesocket(dp);
}
void independentFailure(){
    Listener data,closedCommand(false);TcpEngine e;Settings c;c.ip=L"127.0.0.1";c.receivePort=data.port;c.sendPort=closedCommand.port;
    CHECK(e.start(c));SOCKET dp=data.acceptOne();CHECK(waitOnline(e,true,false));const char* code="SCAN\r";CHECK(send(dp,code,5,0)==5);CHECK(collect(e,L"RX 扫码端口",5)==Bytes({'S','C','A','N',13}));CHECK(!e.queue(Bytes({'L'})));e.stop();closesocket(dp);
    Listener closedData(false),cmd;Settings inverse;inverse.ip=L"127.0.0.1";inverse.receivePort=closedData.port;inverse.sendPort=cmd.port;
    CHECK(e.start(inverse));SOCKET cp=cmd.acceptOne();CHECK(waitOnline(e,false,true));Sleep(10);CHECK(e.queue(Bytes({'L'})));CHECK(readExactly(cp,1)==Bytes({'L'}));e.stop();closesocket(cp);
}
void uiAndConfig(){
    wchar_t base[MAX_PATH]={},folder[MAX_PATH]={};GetTempPathW(MAX_PATH,base);GetTempFileNameW(base,L"STC",0,folder);DeleteFileW(folder);CHECK(CreateDirectoryW(folder,nullptr));directory=folder;configPath=directory+L"\\tcp.ini";
    WNDCLASSW wc={};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"TCPOnlyTestWindow";CHECK(RegisterClassW(&wc));
    HWND h=CreateWindowW(wc.lpszClassName,L"TCP test",WS_OVERLAPPED,0,0,850,650,nullptr,nullptr,wc.hInstance,nullptr);CHECK(h);CHECK(GetDlgItem(h,LOG));CHECK(GetDlgItem(h,SEND));CHECK(!GetDlgItem(h,110));
    set(PAYLOAD,L"4C");hexMode=true;SendMessageW(GetDlgItem(h,ENCODING),CB_SETCURSEL,0,0);SendMessageW(h,WM_COMMAND,MAKEWPARAM(ENCODING,CBN_SELCHANGE),(LPARAM)GetDlgItem(h,ENCODING));CHECK(!hexMode&&text(PAYLOAD)==L"L");CHECK(convertEncoding(true,false)&&text(PAYLOAD)==L"4C");
    set(PAYLOAD,L"GG");CHECK(!convertEncoding(false,false)&&hexMode&&text(PAYLOAD)==L"GG");
    for(int i=0;i<256;++i){Bytes b(1,(unsigned char)i),decoded;CHECK(tcpcodecs::parseTrigger(tcpcodecs::formatAscii(b),false,decoded)&&decoded==b);CHECK(tcpcodecs::parseTrigger(tcpcodecs::formatHex(b),true,decoded)&&decoded==b);}
    Settings c;c.ip=L"127.0.0.1";c.receivePort=c.sendPort=9004;set(PAYLOAD,L"LON\\r");hexMode=false;Bytes b={'L','O','N',13};CHECK(persist(c,b));CHECK(ini(L"Encoding")==L"ASCII"&&ini(L"TriggerHex")==L"4C 4F 4E 0D");
    HANDLE lock=CreateFileW(configPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);CHECK(lock!=INVALID_HANDLE_VALUE);c.sendPort=9005;CHECK(!persist(c,b));CloseHandle(lock);CHECK(ini(L"SendPort")==L"9004");
    CHECK(SendMessageW(GetDlgItem(h,HOTKEY),BM_GETCHECK,0,0)==BST_UNCHECKED);
    // Actual GUI connect/send/timer integration without any COM endpoint.
    Listener scanner;set(IP,L"127.0.0.1");set(RXPORT,std::to_wstring(scanner.port));set(TXPORT,std::to_wstring(scanner.port));
    set(PAYLOAD,L"4C 4F 4E 0D");hexMode=true;SendMessageW(GetDlgItem(h,ENCODING),CB_SETCURSEL,1,0);
    SendMessageW(h,WM_COMMAND,CONNECT,0);SOCKET scanPeer=scanner.acceptOne();CHECK(waitOnline(engine,true,true));Sleep(10);
    CHECK(send(scanPeer,"123\r",4,0)==4);ULONGLONG until=GetTickCount64()+1200;
    while(engine.rxBytes<4&&GetTickCount64()<until)Sleep(5);CHECK(engine.rxBytes==4);SendMessageW(h,WM_TIMER,1,0);
    CHECK(text(LOG).find(L"31 32 33 0D")!=std::wstring::npos&&text(LOG).find(L"123\\r")!=std::wstring::npos);
    SendMessageW(h,WM_COMMAND,SEND,0);CHECK(readExactly(scanPeer,4)==Bytes({'L','O','N',13}));
    // Exact Win64 keyboard packet (40 bytes), not sizeof(RAWINPUT)==48.
    const UINT packetSize=(UINT)(offsetof(RAWINPUT,data)+sizeof(RAWKEYBOARD));static_assert(offsetof(RAWINPUT,data)+sizeof(RAWKEYBOARD)==40,"expected Win64 raw keyboard layout");
    RAWINPUT raw={};raw.header.dwType=RIM_TYPEKEYBOARD;raw.header.dwSize=packetSize;raw.data.keyboard.VKey=VK_RETURN;
    std::vector<BYTE> packet(packetSize);memcpy(packet.data(),&raw,packetSize);
    CHECK(!handleKeyboardPacket(packet.data(),packetSize-1,GetTickCount(),0));
    SendMessageW(GetDlgItem(h,HOTKEY),BM_SETCHECK,BST_CHECKED,0);SendMessageW(h,WM_COMMAND,HOTKEY,0);keyDown=false;
    CHECK(handleKeyboardPacket(packet.data(),packetSize,GetTickCount(),0));CHECK(readExactly(scanPeer,4)==Bytes({'L','O','N',13}));
    CHECK(handleKeyboardPacket(packet.data(),packetSize,GetTickCount(),0));
    fd_set noRepeat;FD_ZERO(&noRepeat);FD_SET(scanPeer,&noRepeat);timeval tiny={0,60000};CHECK(select(0,&noRepeat,nullptr,nullptr,&tiny)==0);
    raw.data.keyboard.Flags=RI_KEY_BREAK;memcpy(packet.data(),&raw,packetSize);CHECK(handleKeyboardPacket(packet.data(),packetSize,GetTickCount(),0));
    raw.data.keyboard.Flags=0;memcpy(packet.data(),&raw,packetSize);CHECK(handleKeyboardPacket(packet.data(),packetSize,GetTickCount(),0));CHECK(readExactly(scanPeer,4)==Bytes({'L','O','N',13}));
    raw.data.keyboard.Flags=RI_KEY_BREAK;memcpy(packet.data(),&raw,packetSize);CHECK(handleKeyboardPacket(packet.data(),packetSize,GetTickCount(),0));
    raw.data.keyboard.Flags=0;memcpy(packet.data(),&raw,packetSize);CHECK(handleKeyboardPacket(packet.data(),packetSize,GetTickCount()-1000,0));
    FD_ZERO(&noRepeat);FD_SET(scanPeer,&noRepeat);tiny.tv_sec=0;tiny.tv_usec=60000;CHECK(select(0,&noRepeat,nullptr,nullptr,&tiny)==0);
    keyDown=false;CHECK(handleKeyboardPacket(packet.data(),packetSize,GetTickCount(),GetCurrentProcessId()));
    FD_ZERO(&noRepeat);FD_SET(scanPeer,&noRepeat);tiny.tv_sec=0;tiny.tv_usec=60000;CHECK(select(0,&noRepeat,nullptr,nullptr,&tiny)==0);
    keyDown=false;SendMessageW(GetDlgItem(h,HOTKEY),BM_SETCHECK,BST_UNCHECKED,0);CHECK(handleKeyboardPacket(packet.data(),packetSize,GetTickCount(),0));
    FD_ZERO(&noRepeat);FD_SET(scanPeer,&noRepeat);tiny.tv_sec=0;tiny.tv_usec=60000;CHECK(select(0,&noRepeat,nullptr,nullptr,&tiny)==0);
    raw.header.dwType=RIM_TYPEMOUSE;memcpy(packet.data(),&raw,packetSize);CHECK(!handleKeyboardPacket(packet.data(),packetSize,GetTickCount(),0));
    refresh();CHECK(text(LOG).find(L"已捕获后台 Enter")!=std::wstring::npos);

    SendMessageW(h,WM_COMMAND,DISCONNECT,0);CHECK(!engine.receiveOnline&&!engine.sendOnline);closesocket(scanPeer);
    DestroyWindow(h);window=nullptr;DeleteFileW(configPath.c_str());DeleteFileW((directory+L"\\logs\\tcp.log").c_str());RemoveDirectoryW((directory+L"\\logs").c_str());RemoveDirectoryW(directory.c_str());
}
int main(){
    CHECK(keyboardPacketSize((UINT)(offsetof(RAWINPUT,data)+sizeof(RAWKEYBOARD))));
    WSADATA w={};CHECK(WSAStartup(MAKEWORD(2,2),&w)==0);CHECK(!requiresCom());CHECK(!validSettings(Settings{}));uiAndConfig();shared();separate();independentFailure();engine.stop();WSACleanup();
    puts("PASS: no COM, actual shared/split engine, unsolicited binary RX, exact ASCII/HEX TX, separate ACK channel, independent channel failure, disconnect/no replay, selector conversion, 256-byte roundtrips, atomic config lock failure, hotkey initially off, Win64 exact40-byte raw keyboard -> real network trigger, repeat suppression, key-up/new press, stale/off/own-window/mouse rejection");
}
