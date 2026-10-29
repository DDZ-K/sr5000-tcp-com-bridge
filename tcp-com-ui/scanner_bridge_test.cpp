#define TCP_TESTER_TEST
#include "scanner_bridge.cpp"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstddef>
#define CHECK(...) do{if(!(__VA_ARGS__)){fprintf(stderr,"FAILED %s:%d: %s\n",__FILE__,__LINE__,#__VA_ARGS__);fflush(stderr);ExitProcess(77);}}while(false)
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

std::string goodInventory(){return "CNCA0 PortName=COM7,EmuOverrun=yes\nCNCB0 PortName=COM8,dsr=ropen,EmuBR=yes\n";}
struct SerialFixture{
    std::wstring path;
    SerialFixture(){wchar_t folder[MAX_PATH]={},file[MAX_PATH]={};GetTempPathW(MAX_PATH,folder);CHECK(GetTempFileNameW(folder,L"SCM",0,file));path=file;serialbridge::testBackend.path=path;serialbridge::testBackend.inventory=goodInventory();serialbridge::testBackend.enabled=true;serialbridge::testBackend.peer=true;serialbridge::testBackend.failWrite=false;}
    ~SerialFixture(){serialbridge::testBackend.enabled=false;DeleteFileW(path.c_str());}
    Bytes contents(){HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);CHECK(h!=INVALID_HANDLE_VALUE);DWORD size=GetFileSize(h,nullptr),count=0;CHECK(size<100000);Bytes bytes(size);if(size)CHECK(ReadFile(h,bytes.data(),size,&count,nullptr)&&count==size);CloseHandle(h);return bytes;}
};
Settings withSerial(int rx,int tx){Settings c;c.ip=L"127.0.0.1";c.receivePort=rx;c.sendPort=tx;c.serial.enabled=true;c.serial.bridge=L"COM8";c.serial.application=L"COM7";c.serial.sharedApproved=true;return c;}
bool waitCom(TcpEngine& e,unsigned long long bytes){ULONGLONG end=GetTickCount64()+1800;while(GetTickCount64()<end){if(e.serialBytes==bytes)return true;Sleep(5);}return false;}
bool waitFault(TcpEngine& e){ULONGLONG end=GetTickCount64()+1800;while(GetTickCount64()<end){if(e.serialFault)return true;Sleep(5);}return false;}
void serialShared(){
    SerialFixture f;Listener scanner;TcpEngine e;Settings c=withSerial(scanner.port,scanner.port);
    c.serial.sharedApproved=false;CHECK(!e.start(c));c.serial.sharedApproved=true;CHECK(e.start(c));SOCKET peer=scanner.acceptOne();CHECK(waitOnline(e,true,true));CHECK(e.serialOpened&&e.serialPeer);Sleep(20);
    const Bytes code={'A',0,'B',0xff,13,10};CHECK(send(peer,(const char*)code.data(),(int)code.size(),0)==(int)code.size());CHECK(waitCom(e,code.size()));CHECK(f.contents()==code); // unsolicited, no key or scan quota
    const Bytes fragmented={'X','Y',13};for(unsigned char value:fragmented){CHECK(send(peer,(const char*)&value,1,0)==1);Sleep(15);}CHECK(waitCom(e,code.size()+fragmented.size()));Bytes expected=code;expected.insert(expected.end(),fragmented.begin(),fragmented.end());CHECK(f.contents()==expected);
    CHECK(e.queue(Bytes({'L'})));CHECK(readExactly(peer,1)==Bytes({'L'}));
    serialbridge::testBackend.peer=false;CHECK(waitFault(e));CHECK(!e.queue(Bytes({'L'})));serialbridge::testBackend.peer=true;Sleep(50);CHECK(e.serialFault&&!e.queue(Bytes({'L'})));
    CHECK(send(peer,"LATE\r",5,0)==5);CHECK(collect(e,L"RX 共用连接",code.size()+fragmented.size()+5).size()==code.size()+fragmented.size()+5);CHECK(f.contents()==expected);
    e.stop();closesocket(peer);
    // Manual reconnect cannot reuse old pairing evidence, resend old code or trigger.
    serialbridge::testBackend.inventory="CNCA0 PortName=COM7,EmuOverrun=yes\nCNCB0 PortName=COM9,dsr=ropen,EmuBR=yes\n";
    CHECK(e.start(c));peer=scanner.acceptOne();CHECK(waitOnline(e,true,true));CHECK(waitFault(e));CHECK(!e.serialOpened&&!e.queue(Bytes({'L'})));CHECK(f.contents()==expected);e.stop();closesocket(peer);
    serialbridge::testBackend.inventory=goodInventory();CHECK(e.start(c));peer=scanner.acceptOne();CHECK(waitOnline(e,true,true));Sleep(40);CHECK(f.contents().empty());CHECK(!scanner.hasExtra());
    fd_set no;FD_ZERO(&no);FD_SET(peer,&no);timeval tv={0,80000};CHECK(select(0,&no,nullptr,nullptr,&tv)==0);CHECK(send(peer,"NEW\r",4,0)==4);CHECK(waitCom(e,4));CHECK(f.contents()==Bytes({'N','E','W',13}));e.stop();closesocket(peer);
}
void serialSplitAndFaults(){
    SerialFixture f;Listener data,command;TcpEngine e;Settings c=withSerial(data.port,command.port);CHECK(e.start(c));SOCKET dp=data.acceptOne(),cp=command.acceptOne();CHECK(waitOnline(e,true,true));Sleep(20);
    CHECK(send(cp,"OK\r",3,0)==3);CHECK(collect(e,L"RX 命令应答",3)==Bytes({'O','K',13}));CHECK(e.serialBytes==0&&f.contents().empty());
    CHECK(send(dp,"SCAN\r",5,0)==5);CHECK(waitCom(e,5));CHECK(f.contents()==Bytes({'S','C','A','N',13}));
    shutdown(cp,SD_BOTH);closesocket(cp);CHECK(waitOnline(e,true,false));CHECK(send(dp,"NEXT\r",5,0)==5);CHECK(waitCom(e,10));CHECK(f.contents()==Bytes({'S','C','A','N',13,'N','E','X','T',13}));
    serialbridge::testBackend.failWrite=true;CHECK(send(dp,"FAIL\r",5,0)==5);CHECK(waitFault(e));CHECK(e.receiveOnline);serialbridge::testBackend.failWrite=false;CHECK(send(dp,"DROP\r",5,0)==5);CHECK(collect(e,L"RX 扫码端口",20).size()==20);CHECK(f.contents().size()==10);e.stop();closesocket(dp);
    Listener offline(false);c=withSerial(data.port,offline.port);CHECK(e.start(c));dp=data.acceptOne();CHECK(waitOnline(e,true,false));CHECK(send(dp,"NO-CMD\r",7,0)==7);CHECK(waitCom(e,7));CHECK(f.contents()==Bytes({'N','O','-','C','M','D',13}));e.stop();closesocket(dp);
    serialbridge::testBackend.peer=false;c=withSerial(data.port,data.port);CHECK(e.start(c));dp=data.acceptOne();CHECK(waitOnline(e,true,true));CHECK(!e.serialPeer&&!e.queue(Bytes({'L'})));CHECK(send(dp,"EARLY\r",6,0)==6);CHECK(waitFault(e));CHECK(f.contents().empty());serialbridge::testBackend.peer=true;Sleep(50);CHECK(e.serialFault&&!e.queue(Bytes({'L'})));e.stop();closesocket(dp);
}


void unrelatedUnassignedPairMustNotBlockRelay(){
    // Configuration-only transcription of the field screenshot; no production IP.
    const std::string listing=
        "CNCA2 PortName=COM#,EmuBR=no,EmuOverrun=no,dsr=rdtr\n"
        "CNCB2 PortName=COM#,EmuBR=no,EmuOverrun=no,dsr=rdtr\n"
        "CNCA0 PortName=COM9,EmuBR=yes,EmuOverrun=yes,dsr=rdtr\n"
        "CNCB0 PortName=COM10,EmuBR=yes,EmuOverrun=yes,dsr=rdtr\n"
        "CNCA1 PortName=COM#,RealPortName=COM7,EmuBR=no,EmuOverrun=yes,dsr=rdtr\n"
        "CNCB1 PortName=COM#,RealPortName=COM8,EmuBR=yes,EmuOverrun=no,dsr=ropen\n";
    com0com::Inventory inventory;std::string error;CHECK(com0com::ParseInventory(listing,inventory,error));
    CHECK(inventory.endpoints.size()==6&&inventory.endpoints[0].portName=="CNCA2"&&inventory.endpoints[1].portName=="CNCB2");
    CHECK(com0com::ValidatePair(inventory,"COM7","COM8").ok());CHECK(!com0com::ValidatePair(inventory,"COM7","COM9").identityVerified);CHECK(!com0com::ValidatePair(inventory,"COM11","COM12").identityVerified);
    CHECK(!com0com::ParseInventory(listing+"CNCA3 PortName=COM#,RealPortName=COM7\n",inventory,error));
    CHECK(!com0com::ParseInventory("CNCA2 PortName=COM#,RealPortName=COM00\n",inventory,error));
    CHECK(!com0com::ParseInventory("CNCA2 PortName=COM#\nCNCA2 PortName=COM#\n",inventory,error));
    SerialFixture fixture;serialbridge::testBackend.inventory=listing;Listener scanner;TcpEngine e;Settings c=withSerial(scanner.port,scanner.port);
    CHECK(e.start(c));SOCKET peer=scanner.acceptOne();CHECK(waitOnline(e,true,true));CHECK(e.serialOpened&&e.serialPeer&&!e.serialFault);Sleep(20);
    const Bytes barcode={'F','I','E','L','D','-','C','O','D','E',13,10};CHECK(send(peer,(const char*)barcode.data(),(int)barcode.size(),0)==(int)barcode.size());CHECK(waitCom(e,barcode.size()));CHECK(fixture.contents()==barcode);
    CHECK(e.queue(Bytes({'L','O','N',13})));CHECK(readExactly(peer,4)==Bytes({'L','O','N',13}));e.stop();closesocket(peer);
    puts("[TEST] field COM7/COM8 aliases + unrelated unresolved COM# pair -> actual TCP worker relay; no guessed COM aliases or cross-pair acceptance");
}

void nativeRegistryEvidence(){
    const std::wstring path=L"Software\\SR5000NativeTest_"+std::to_wstring(GetCurrentProcessId());HKEY parameters=nullptr,a=nullptr,b=nullptr;
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,0,KEY_READ|KEY_WRITE,nullptr,&parameters,nullptr)==ERROR_SUCCESS);
    CHECK(RegCreateKeyExW(parameters,L"CNCA1",0,nullptr,0,KEY_READ|KEY_WRITE,nullptr,&a,nullptr)==ERROR_SUCCESS);
    CHECK(RegCreateKeyExW(parameters,L"CNCB1",0,nullptr,0,KEY_READ|KEY_WRITE,nullptr,&b,nullptr)==ERROR_SUCCESS);
    const auto putString=[](HKEY key,const wchar_t* name,const std::wstring& value){CHECK(RegSetValueExW(key,name,0,REG_SZ,(const BYTE*)value.c_str(),(DWORD)((value.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS);};
    const auto putNumber=[](HKEY key,const wchar_t* name,DWORD value){CHECK(RegSetValueExW(key,name,0,REG_DWORD,(const BYTE*)&value,sizeof(value))==ERROR_SUCCESS);};
    putString(a,L"PortName",L"COM7");putString(b,L"PortName",L"COM8");com0com::Endpoint ea,eb;std::wstring error;
    CHECK(com0com::NativeEndpointFromEvidence(parameters,L"COM7",LR"(\Device\com0com11)",false,ea,error));CHECK(com0com::NativeEndpointFromEvidence(parameters,L"COM8",LR"(\Device\com0com21)",false,eb,error));
    com0com::Inventory inventory;inventory.endpoints={ea,eb};auto pair=com0com::ValidatePair(inventory,"COM7","COM8");CHECK(pair.identityVerified&&!pair.pinsVerified); // existing pair, defaults are not safety-ready
    putNumber(a,L"EmuOverrun",1);putNumber(b,L"EmuBR",1);putNumber(b,L"dsr",0x80);
    CHECK(com0com::NativeEndpointFromEvidence(parameters,L"COM7",LR"(\Device\com0com11)",false,ea,error));CHECK(com0com::NativeEndpointFromEvidence(parameters,L"COM8",LR"(\Device\com0com21)",false,eb,error));inventory.endpoints={ea,eb};CHECK(com0com::ValidatePair(inventory,"COM7","COM8").ok());
    CHECK(!com0com::NativeEndpointFromEvidence(parameters,L"COM9",LR"(\Device\com0com21)",false,eb,error));CHECK(!com0com::NativeEndpointFromEvidence(parameters,L"COM8",LR"(\Device\Serial0)",false,eb,error));
    putString(b,L"PortName",L"COM#");CHECK(!com0com::NativeEndpointFromEvidence(parameters,L"COM8",LR"(\Device\com0com21)",false,eb,error));CHECK(com0com::NativeEndpointFromEvidence(parameters,L"COM8",LR"(\Device\com0com21)",true,eb,error));
    putNumber(b,L"dsr",0x80000080);CHECK(com0com::NativeEndpointFromEvidence(parameters,L"COM8",LR"(\Device\com0com21)",true,eb,error));CHECK(eb.parameters.at("dsr")!="ropen");putString(b,L"EmuBR",L"1");CHECK(!com0com::NativeEndpointFromEvidence(parameters,L"COM8",LR"(\Device\com0com21)",true,eb,error));
    RegCloseKey(a);RegCloseKey(b);CHECK(RegDeleteKeyW(parameters,L"CNCA1")==ERROR_SUCCESS);CHECK(RegDeleteKeyW(parameters,L"CNCB1")==ERROR_SUCCESS);RegCloseKey(parameters);CHECK(RegDeleteKeyW(HKEY_CURRENT_USER,path.c_str())==ERROR_SUCCESS);
    std::wstring id;CHECK(!com0com::EndpointIdFromTarget(LR"(\Device\com0com201)",id));CHECK(!com0com::EndpointIdFromTarget(LR"(\Device\com0com321)",id));
    com0com::CaptureResult failure;failure.status=com0com::CaptureStatus::NonzeroExit;failure.exitCode=23;failure.output="Access denied";CHECK(com0com::DescribeCaptureFailure(failure).find(L"23")!=std::wstring::npos);CHECK(com0com::DescribeCaptureFailure(failure).find(L"740")==std::wstring::npos);
    failure.status=com0com::CaptureStatus::Win32Error;failure.win32Error=740;CHECK(com0com::DescribeCaptureFailure(failure).find(L"管理员")!=std::wstring::npos);
    puts("[TEST] native selected-pair registry evidence, COM# OS-alias gate, parameter freshness/types, exact CLI failure stage (HKCU fixtures only)");
}

void uiComRoundtrip(){
    SerialFixture f;wchar_t base[MAX_PATH]={},folder[MAX_PATH]={};GetTempPathW(MAX_PATH,base);GetTempFileNameW(base,L"SCU",0,folder);DeleteFileW(folder);CHECK(CreateDirectoryW(folder,nullptr));directory=folder;configPath=directory+L"\\tcp.ini";
    WNDCLASSW wc={};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"TCPComTestWindow";CHECK(RegisterClassW(&wc));
    HWND h=CreateWindowW(wc.lpszClassName,L"COM UI test",WS_OVERLAPPED,0,0,850,715,nullptr,nullptr,wc.hInstance,nullptr);CHECK(h);CHECK(GetDlgItem(h,COM_BRIDGE)&&GetDlgItem(h,COM_PAIR));CHECK(SendMessageW(GetDlgItem(h,COM_ENABLE),BM_GETCHECK,0,0)==BST_UNCHECKED);
    set(COM_BRIDGE,L"com8");set(COM_APP,L"COM7");SendMessageW(GetDlgItem(h,COM_ENABLE),BM_SETCHECK,BST_CHECKED,0);SendMessageW(GetDlgItem(h,COM_SHARED),BM_SETCHECK,BST_CHECKED,0);
    SendMessageW(GetDlgItem(h,COM_BAUD),CB_SETCURSEL,7,0);SendMessageW(GetDlgItem(h,COM_PARITY),CB_SETCURSEL,2,0);SendMessageW(GetDlgItem(h,COM_STOP),CB_SETCURSEL,1,0);
    SerialOptions check=serialFromUI();CHECK(validSerialOptions(check)&&check.bridge==L"COM8"&&check.application==L"COM7"&&check.baud==115200&&check.parity==EVENPARITY&&check.stopBits==TWOSTOPBITS);
    Listener scan;set(IP,L"127.0.0.1");set(RXPORT,std::to_wstring(scan.port));set(TXPORT,std::to_wstring(scan.port));set(PAYLOAD,L"4C 0D");hexMode=true;
    SendMessageW(h,WM_COMMAND,CONNECT,0);SOCKET peer=scan.acceptOne();CHECK(waitOnline(engine,true,true));CHECK(engine.serialEnabled&&engine.serialOpened&&engine.serialPeer);Sleep(30);
    CHECK(serialIni(L"Enabled")==L"1"&&serialIni(L"BridgeCom")==L"COM8"&&serialIni(L"ApplicationCom")==L"COM7"&&serialIni(L"Baud")==L"115200"&&serialIni(L"Parity")==L"2"&&serialIni(L"StopBits")==L"2");
    SendMessageW(GetDlgItem(h,HOTKEY),BM_SETCHECK,BST_CHECKED,0);SendMessageW(h,WM_COMMAND,HOTKEY,0);
    RAWINPUT raw={};const UINT size=(UINT)(offsetof(RAWINPUT,data)+sizeof(RAWKEYBOARD));raw.header.dwType=RIM_TYPEKEYBOARD;raw.header.dwSize=size;raw.data.keyboard.VKey=VK_RETURN;Bytes packet(size);memcpy(packet.data(),&raw,size);
    CHECK(handleKeyboardPacket(packet.data(),size,GetTickCount(),0));CHECK(readExactly(peer,2)==Bytes({'L',13}));CHECK(send(peer,"GUI-CODE\r\n",10,0)==10);CHECK(waitCom(engine,10));CHECK(f.contents()==Bytes({'G','U','I','-','C','O','D','E',13,10}));refresh();CHECK(text(STATE).find(L"10")!=std::wstring::npos);
    SendMessageW(h,WM_COMMAND,DISCONNECT,0);closesocket(peer);
    testPairLaunch=true;testPairTrust=true;testPairLaunchResult=false;pairSetup=L"C:\\Program Files\\com0com\\setupc.exe";
    pairCommands.push_back(L"change CNCA0 EmuOverrun=yes");EnableWindow(GetDlgItem(h,CONNECT),FALSE);EnableWindow(GetDlgItem(h,COM_PAIR),FALSE);
    CHECK(!launchPairCommand());CHECK(!pairLaunching&&!pairProcess&&pairCommands.empty());CHECK(IsWindowEnabled(GetDlgItem(h,CONNECT))&&IsWindowEnabled(GetDlgItem(h,COM_PAIR)));
    testPairTrust=false;pairCommands.push_back(L"change CNCB0 dsr=ropen,EmuBR=yes");EnableWindow(GetDlgItem(h,CONNECT),FALSE);CHECK(!launchPairCommand());CHECK(pairCommands.empty()&&IsWindowEnabled(GetDlgItem(h,CONNECT)));
    testPairTrust=true;testPairLaunchResult=true;pairCommands.push_back(L"install PortName=COM7 PortName=COM8");CHECK(!launchPairCommand());CHECK(pairBlocked&&!IsWindowEnabled(GetDlgItem(h,CONNECT)));CHECK(!pairProcess);
    pairBlocked=false;testPairLaunch=false;pairCommands.clear();pairSetup.clear();DestroyWindow(h);window=nullptr;
    h=CreateWindowW(wc.lpszClassName,L"COM restore test",WS_OVERLAPPED,0,0,850,715,nullptr,nullptr,wc.hInstance,nullptr);CHECK(h);check=serialFromUI();CHECK(check.enabled&&check.bridge==L"COM8"&&check.application==L"COM7"&&check.baud==115200&&check.parity==EVENPARITY&&check.stopBits==TWOSTOPBITS&&!check.sharedApproved);DestroyWindow(h);window=nullptr;
    DeleteFileW(configPath.c_str());DeleteFileW((directory+L"\\logs\\tcp.log").c_str());RemoveDirectoryW((directory+L"\\logs").c_str());RemoveDirectoryW(directory.c_str());
}

void startupPersistenceAndOwnership(){
    wchar_t base[MAX_PATH]={},folder[MAX_PATH]={};GetTempPathW(MAX_PATH,base);CHECK(GetTempFileNameW(base,L"STP",0,folder));DeleteFileW(folder);CHECK(CreateDirectoryW(folder,nullptr));std::wstring directoryForTest=folder,path=directoryForTest+L"\\startup.ini";
    startup::Preferences preferences;CHECK(!preferences.connect&&!preferences.hotkey&&!preferences.hidden&&!preferences.shared);preferences.connect=true;preferences.hotkey=true;preferences.hidden=true;preferences.shared=true;preferences.delay=20;preferences.signature=L"127.0.0.1|39000|39000|1|COM8|COM7|115200|8|0|0";
    CHECK(startup::Save(directoryForTest,path,preferences));auto loaded=startup::Load(path);CHECK(loaded.connect&&loaded.hotkey&&loaded.hidden&&loaded.shared&&loaded.delay==20&&loaded.signature==preferences.signature);CHECK(startup::SharedConsentMatches(loaded,preferences.signature));CHECK(!startup::SharedConsentMatches(loaded,preferences.signature+L"changed"));
    HANDLE lock=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);CHECK(lock!=INVALID_HANDLE_VALUE);preferences.delay=30;CHECK(!startup::Save(directoryForTest,path,preferences));CloseHandle(lock);CHECK(startup::Load(path).delay==20);
    std::wstring key=L"Software\\SR5000StartupTest_"+std::to_wstring(GetCurrentProcessId()),runKey=key+L"\\Run",error,actual;const std::wstring exe=LR"(E:\Tools\扫码 工具\SR5000TcpComBridge.exe)";
    CHECK(startup::ReadRun(actual,runKey.c_str())==ERROR_SUCCESS&&actual.empty());CHECK(startup::SetRun(true,exe,false,error,runKey.c_str()));CHECK(startup::ReadRun(actual,runKey.c_str())==ERROR_SUCCESS&&actual==startup::CommandFor(exe));
    CHECK(!startup::SetRun(true,LR"(E:\Other\SR5000TcpComBridge.exe)",false,error,runKey.c_str()));CHECK(!startup::SetRun(false,LR"(E:\Other\SR5000TcpComBridge.exe)",false,error,runKey.c_str()));CHECK(startup::ReadRun(actual,runKey.c_str())==ERROR_SUCCESS&&actual==startup::CommandFor(exe));
    CHECK(startup::SetRun(false,exe,false,error,runKey.c_str()));CHECK(startup::ReadRun(actual,runKey.c_str())==ERROR_SUCCESS&&actual.empty());CHECK(startup::CommandFor(L"not-absolute.exe").empty());CHECK(startup::CommandFor(L"E:\\x\\broken\".exe").empty());
    CHECK(RegDeleteKeyW(HKEY_CURRENT_USER,runKey.c_str())==ERROR_SUCCESS);CHECK(RegDeleteKeyW(HKEY_CURRENT_USER,key.c_str())==ERROR_SUCCESS);DeleteFileW(path.c_str());CHECK(RemoveDirectoryW(directoryForTest.c_str()));
    puts("[TEST] opt-in startup quotes Unicode paths, isolated HKCU registration/readback/removal, foreign ownership protection, atomic prefs failure, changed-config consent rejection; actual Windows Run key untouched");
}
void startupConnectsWithoutSending(){
    SerialFixture fixture;wchar_t base[MAX_PATH]={},folder[MAX_PATH]={};GetTempPathW(MAX_PATH,base);CHECK(GetTempFileNameW(base,L"STB",0,folder));DeleteFileW(folder);CHECK(CreateDirectoryW(folder,nullptr));directory=folder;configPath=directory+L"\\tcp.ini";
    WNDCLASSW wc={};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"StartupIntegrationWindow";CHECK(RegisterClassW(&wc));HWND h=CreateWindowW(wc.lpszClassName,L"startup test",WS_OVERLAPPED,0,0,976,699,nullptr,nullptr,wc.hInstance,nullptr);CHECK(h);
    Listener listener;Settings c=withSerial(listener.port,listener.port);set(IP,c.ip);set(RXPORT,std::to_wstring(c.receivePort));set(TXPORT,std::to_wstring(c.sendPort));set(COM_BRIDGE,L"COM8");set(COM_APP,L"COM7");SendMessageW(GetDlgItem(h,COM_ENABLE),BM_SETCHECK,BST_CHECKED,0);SendMessageW(GetDlgItem(h,COM_SHARED),BM_SETCHECK,BST_CHECKED,0);set(PAYLOAD,L"4C 0D");hexMode=true;
    c=currentFormSettings();CHECK(persist(c,Bytes({'L',13})));startupPrefs.connect=true;startupPrefs.hotkey=true;startupPrefs.shared=true;startupPrefs.signature=settingsSignature(c);startupPrefs.delay=0;beginStartup();CHECK(startupPending);SendMessageW(h,WM_COMMAND,DISCONNECT,0);CHECK(!startupPending);SendMessageW(h,WM_TIMER,2,0);CHECK(!engine.running&&!startupConsumed&&!listener.hasExtra());beginStartup();CHECK(startupPending);CHECK(SendMessageW(GetDlgItem(h,HOTKEY),BM_GETCHECK,0,0)==BST_CHECKED);runStartupOnce();SOCKET peer=listener.acceptOne();CHECK(waitOnline(engine,true,true));Sleep(35);
    fd_set set;FD_ZERO(&set);FD_SET(peer,&set);timeval wait={0,100000};CHECK(select(0,&set,nullptr,nullptr,&wait)==0);CHECK(engine.txBytes==0);CHECK(send(peer,"BOOT-CODE\r",10,0)==10);CHECK(waitCom(engine,10));CHECK(fixture.contents()==Bytes({'B','O','O','T','-','C','O','D','E',13}));runStartupOnce();CHECK(!listener.hasExtra());CHECK(engine.txBytes==0);
    SendMessageW(h,WM_COMMAND,DISCONNECT,0);closesocket(peer);startupPrefs.connect=false;DestroyWindow(h);window=nullptr;DeleteFileW(configPath.c_str());DeleteFileW((directory+L"\\logs\\tcp.log").c_str());RemoveDirectoryW((directory+L"\\logs").c_str());CHECK(RemoveDirectoryW(directory.c_str()));
    puts("[TEST] configured delayed startup connects once and forwards unsolicited bytes; no lifecycle trigger/no reconnect replay; user consent and keyboard options restored");
}

int main(){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);setvbuf(stdout,nullptr,_IONBF,0);setvbuf(stderr,nullptr,_IONBF,0);
    std::wstring liveRunBefore;CHECK(startup::ReadRun(liveRunBefore)==ERROR_SUCCESS);
    CHECK(keyboardPacketSize((UINT)(offsetof(RAWINPUT,data)+sizeof(RAWKEYBOARD))));
    SerialOptions options;options.enabled=true;options.bridge=L"COM8";options.application=L"COM7";CHECK(validSerialOptions(options));options.application=L"COM8";CHECK(!validSerialOptions(options));
    std::wstring nativeId;CHECK(com0com::EndpointIdFromTarget(LR"(\Device\com0com11)",nativeId)&&nativeId==L"CNCA1");CHECK(com0com::EndpointIdFromTarget(LR"(\Device\com0com21)",nativeId)&&nativeId==L"CNCB1");CHECK(!com0com::EndpointIdFromTarget(LR"(\Device\Serial0)",nativeId));
    com0com::CaptureResult diagnostic;diagnostic.status=com0com::CaptureStatus::ParseError;diagnostic.exitCode=0;diagnostic.error="malformed parameter";diagnostic.output="CNCA1 PortName=COM7,dsr=";
    std::wstring message=com0com::DescribeCaptureFailure(diagnostic);CHECK(message.find(L"解析")!=std::wstring::npos&&message.find(L"740")==std::wstring::npos&&message.find(L"malformed parameter")!=std::wstring::npos);
    CHECK(startup::CommandFor(L"E:\\Tools\\扫码工具\\SR5000TcpComBridge.exe")==L"\"E:\\Tools\\扫码工具\\SR5000TcpComBridge.exe\" --autostart");
    WSADATA w={};CHECK(WSAStartup(MAKEWORD(2,2),&w)==0);CHECK(!requiresCom());CHECK(!validSettings(Settings{}));puts("[TEST] UI");uiAndConfig();puts("[TEST] shared TCP");shared();puts("[TEST] split TCP");separate();puts("[TEST] independent failure");independentFailure();puts("[TEST] COM shared");serialShared();puts("[TEST] COM split faults");serialSplitAndFaults();puts("[TEST] COM-enabled GUI Enter -> TCP -> serial; persistence");uiComRoundtrip();unrelatedUnassignedPairMustNotBlockRelay();nativeRegistryEvidence();startupPersistenceAndOwnership();startupConnectsWithoutSending();engine.stop();WSACleanup();std::wstring liveRunAfter;CHECK(startup::ReadRun(liveRunAfter)==ERROR_SUCCESS&&liveRunBefore==liveRunAfter);
    puts("PASS: COM optional; actual TCP worker -> explicit fake serial file, binary/fragmented unsolicited forwarding, shared ACK consent, split ACK excluded, command-failure RX relay, peer close fault latch, failed write latch, fresh pair recheck on reconnect, no old-code replay; no real driver installed. Original TCP/hotkey tests: actual shared/split engine, unsolicited binary RX, exact ASCII/HEX TX, separate ACK channel, independent channel failure, disconnect/no replay, selector conversion, 256-byte roundtrips, atomic config lock failure, hotkey initially off, Win64 exact40-byte raw keyboard -> real network trigger, repeat suppression, key-up/new press, stale/off/own-window/mouse rejection");
}
