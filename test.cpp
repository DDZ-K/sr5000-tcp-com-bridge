#define BRIDGE_UNIT_TEST
#include "bridge.cpp"
#include <cassert>
#include <cstdio>
int main(){
    std::vector<unsigned char> b;
    assert(triggerBytes(L"T",b)&&b.size()==1&&b[0]=='T');
    assert(triggerBytes(L"hex:4C 4F 4E 0D",b)&&b.size()==4&&b[0]=='L'&&b[3]=='\r');
    assert(triggerBytes(L"4C 4F 4E 0D",b)&&b.size()==4&&b[0]==0x4C&&b[1]==0x4F&&b[2]==0x4E&&b[3]==0x0D);
    assert(!triggerBytes(L"hex:4C F",b));
    assert(!triggerBytes(L"hex:GG",b));
    assert(triggerBytes(L"HELLO WORLD",b)&&b.size()==11&&b[0]=='H');
    assert(triggerBytes(L"hex:AA-BB-00",b)&&b.size()==3&&b[0]==0xAA&&b[1]==0xBB&&b[2]==0x00);
    assert(!triggerBytes(L"",b));
    assert(keyCode(L"Enter")==VK_RETURN&&keyCode(L"Space")==VK_SPACE);
    assert(keyCode(L"F1")==VK_F1&&keyCode(L"f12")==VK_F12);
    assert(keyCode(L"F13")==0&&keyCode(L"Escape")==0);
    assert(number(L"9004")==9004);
    assert(number(L"65536")==0);
    assert(number(L"12x")==0);
    assert(comName(L"COM7")&&comName(L"COM19")&&!comName(L"COM0")&&!comName(L"COM7evil")&&!comName(L"COM\\7"));
    assert(!comExists(L"COM999"));
    // Packaging executable foreground selection is removed: only physical key / connection state gates trigger.
    g_window=CreateWindowW(L"STATIC",L"Test picker",WS_OVERLAPPED,0,0,300,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    assert(g_window!=nullptr);
    HWND picker=keyPicker(g_window,35,L"Space");
    assert(picker&&((GetWindowLongPtrW(picker,GWL_STYLE)&CBS_DROPDOWNLIST)==CBS_DROPDOWNLIST));
    assert(SendMessageW(picker,CB_GETCOUNT,0,0)==14);
    assert(getText(ID_KEY)==L"Space");
    assert(keyCode(getText(ID_KEY))==VK_SPACE);
    assert(SendMessageW(picker,CB_GETLBTEXTLEN,0,0)==5);
    assert(SendMessageW(picker,CB_SELECTSTRING,(WPARAM)-1,(LPARAM)L"F12")!=CB_ERR);
    assert(getText(ID_KEY)==L"F12");
    DestroyWindow(g_window);g_window=nullptr;
    // UI smoke: new packaging COM field and create-pair button exist; EXE foreground selector is absent.
    WNDCLASSW wc={};wc.lpfnWndProc=wndProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"BridgeV2Smoke";
    assert(RegisterClassW(&wc));
    HWND ui=CreateWindowW(wc.lpszClassName,L"Bridge smoke",WS_OVERLAPPED,0,0,510,520,nullptr,nullptr,wc.hInstance,nullptr);
    assert(ui);assert(GetDlgItem(ui,ID_PACK_COM));assert(GetDlgItem(ui,CMD_CREATE_PAIR));
    assert(GetDlgItem(ui,107)==nullptr);
    DestroyWindow(ui);g_window=nullptr;
    WSADATA wsa={};assert(WSAStartup(MAKEWORD(2,2),&wsa)==0);
    wchar_t tempDir[MAX_PATH]={},oldIni[MAX_PATH]={};assert(GetTempPathW(MAX_PATH,tempDir)>0);
    assert(GetTempFileNameW(tempDir,L"SRB",0,oldIni));
    g_ini=oldIni;
    assert(WritePrivateProfileStringW(L"Reader",L"ReaderIp",L"127.0.0.1",g_ini.c_str()));
    assert(WritePrivateProfileStringW(L"Reader",L"DataPort",L"39001",g_ini.c_str()));
    assert(WritePrivateProfileStringW(L"Reader",L"CommandMode",L"Separate",g_ini.c_str()));
    assert(WritePrivateProfileStringW(L"Reader",L"CommandPort",L"39002",g_ini.c_str()));
    assert(WritePrivateProfileStringW(L"Reader",L"TriggerHex",L"T",g_ini.c_str()));
    assert(WritePrivateProfileStringW(L"Serial",L"BridgeCom",L"COM19",g_ini.c_str()));
    Config config={};std::wstring reason;assert(loadConfig(config,reason)&&config.dataPort==39001&&config.commandPort==39002);
    assert(WritePrivateProfileStringW(L"Reader",L"TcpPort",L"39003",g_ini.c_str()));
    reason.clear();assert(loadConfig(config,reason)&&config.dataPort==39001&&config.commandPort==39002);
    assert(WritePrivateProfileStringW(L"Reader",L"CommandPort",L"39001",g_ini.c_str()));
    reason.clear();assert(loadConfig(config,reason)&&config.dataPort==config.commandPort);
    assert(WritePrivateProfileStringW(L"Reader",L"DataPort",L"9004",g_ini.c_str()));
    assert(WritePrivateProfileStringW(L"Reader",L"CommandPort",L"09004",g_ini.c_str()));
    reason.clear();assert(loadConfig(config,reason)&&config.dataPort==config.commandPort);
    assert(WritePrivateProfileStringW(L"Reader",L"DataPort",L"39001",g_ini.c_str()));
    assert(WritePrivateProfileStringW(L"Reader",L"CommandPort",nullptr,g_ini.c_str()));
    reason.clear();assert(!loadConfig(config,reason));
    assert(WritePrivateProfileStringW(L"Reader",L"CommandPort",L"39002",g_ini.c_str()));
    reason.clear();assert(loadConfig(config,reason));
    DeleteFileW(g_ini.c_str());g_ini.clear();
    g_stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    SOCKET server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    assert(server!=INVALID_SOCKET);
    sockaddr_in addr={};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=0;
    assert(bind(server,(sockaddr*)&addr,sizeof(addr))==0);
    assert(listen(server,1)==0);
    int len=sizeof(addr);assert(getsockname(server,(sockaddr*)&addr,&len)==0);
    SOCKET client=connectTo(L"127.0.0.1",ntohs(addr.sin_port));assert(client!=INVALID_SOCKET);
    SOCKET accepted=accept(server,nullptr,nullptr);assert(accepted!=INVALID_SOCKET);
    SOCKET commandServer=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(commandServer!=INVALID_SOCKET);
    sockaddr_in commandAddr={};commandAddr.sin_family=AF_INET;commandAddr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);commandAddr.sin_port=0;
    assert(bind(commandServer,(sockaddr*)&commandAddr,sizeof(commandAddr))==0&&listen(commandServer,1)==0);
    int commandLen=sizeof(commandAddr);assert(getsockname(commandServer,(sockaddr*)&commandAddr,&commandLen)==0);
    assert(ntohs(commandAddr.sin_port)!=ntohs(addr.sin_port));
    SOCKET commandClient=connectTo(L"127.0.0.1",ntohs(commandAddr.sin_port));assert(commandClient!=INVALID_SOCKET);
    SOCKET commandPeer=accept(commandServer,nullptr,nullptr);assert(commandPeer!=INVALID_SOCKET);

    assert(triggerBytes(L"T",b));assert(sendAll(commandClient,b));
    char v[3]={};assert(recv(commandPeer,v,sizeof(v),0)==1&&v[0]=='T');
    u_long nonblocking=1;assert(ioctlsocket(accepted,FIONBIO,&nonblocking)==0);
    assert(recv(accepted,v,sizeof(v),0)==SOCKET_ERROR&&WSAGetLastError()==WSAEWOULDBLOCK);
    // Simulate unsolicited scanner data: no key press, binary-safe, terminator preserved.
    wchar_t dir[MAX_PATH]={},name[MAX_PATH]={};assert(GetTempPathW(MAX_PATH,dir)>0);
    assert(GetTempFileNameW(dir,L"SRT",0,name));
    HANDLE target=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_OVERLAPPED,nullptr);
    assert(target!=INVALID_HANDLE_VALUE);
    const char scanner[]={ 'A',0,'B',static_cast<char>(-1),'\r','\n' };
    assert(send(accepted,scanner,(int)sizeof(scanner),0)==sizeof(scanner));
    char received[sizeof(scanner)]={};size_t done=0;
    while(done<sizeof(scanner)){int got=recv(client,received+done,(int)(sizeof(scanner)-done),0);assert(got>0);assert(relayChunk(target,received+done,got));done+=got;}
    assert(FlushFileBuffers(target));CloseHandle(target);
    target=CreateFileW(name,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);assert(target!=INVALID_HANDLE_VALUE);
    char copied[sizeof(scanner)]={};DWORD count=0;assert(ReadFile(target,copied,sizeof(copied),&count,nullptr)&&count==sizeof(scanner));
    assert(memcmp(scanner,copied,sizeof(scanner))==0);CloseHandle(target);DeleteFileW(name);
    SOCKET sharedClient=connectTo(L"127.0.0.1",ntohs(addr.sin_port));assert(sharedClient!=INVALID_SOCKET);
    SOCKET sharedPeer=accept(server,nullptr,nullptr);assert(sharedPeer!=INVALID_SOCKET);
    assert(sendAll(sharedClient,b));char sharedByte=0;
    assert(recv(sharedPeer,&sharedByte,1,0)==1&&sharedByte=='T');
    assert(send(sharedPeer,scanner,(int)sizeof(scanner),0)==sizeof(scanner));
    char sharedReceived[sizeof(scanner)]={};size_t sharedDone=0;
    while(sharedDone<sizeof(scanner)){int k=recv(sharedClient,sharedReceived+sharedDone,(int)(sizeof(scanner)-sharedDone),0);assert(k>0);sharedDone+=k;}
    assert(memcmp(sharedReceived,scanner,sizeof(scanner))==0);
    closeSock(sharedPeer);closeSock(sharedClient);
    closeSock(commandPeer);closeSock(commandClient);closeSock(commandServer);
    closeSock(accepted);closeSock(client);closeSock(server);
    CloseHandle(g_stop);g_stop=nullptr;WSACleanup();
    puts("PASS: distinct and shared TCP port; trigger and data exchange; data relayed unchanged; COM creation requires signed driver");
}
