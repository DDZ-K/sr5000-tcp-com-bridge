#define BRIDGE_UNIT_TEST
#include "bridge.cpp"
#include <cstdlib>
#define CHECK(...) do { if(!(__VA_ARGS__)){fprintf(stderr,"CHECK FAILED: %s (line %d)\n",#__VA_ARGS__,__LINE__);std::abort();} } while(false)
#include <cstdio>
int testRecv(SOCKET socket,char* data,int length,int flags=0){
    fd_set read;FD_ZERO(&read);FD_SET(socket,&read);timeval wait={2,0};
    int ready=select(0,&read,nullptr,nullptr,&wait);
    if(ready<=0)return SOCKET_ERROR;
    return recv(socket,data,length,flags);
}
void workerRejectsInvalidPair(){
    g_testSerial.enabled=true;g_testSerial.inventory="CNCA0 PortName=COM7,EmuOverrun=yes\nCNCB0 PortName=COM8,dsr=ropen,EmuBR=yes\n";
    SOCKET server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);CHECK(server!=INVALID_SOCKET);
    sockaddr_in a={};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=0;
    CHECK(bind(server,(sockaddr*)&a,sizeof(a))==0&&listen(server,1)==0);int n=sizeof(a);CHECK(getsockname(server,(sockaddr*)&a,&n)==0);
    Config c={};c.ip=L"127.0.0.1";c.dataPort=c.commandPort=ntohs(a.sin_port);c.com=L"COM8";c.packCom=L"COM9";c.trigger={'L'};c.key=L"Enter";
    g_faultLatched=false;startWorker(c);Sleep(40);
    fd_set f;FD_ZERO(&f);FD_SET(server,&f);timeval wait={0,150000};CHECK(select(0,&f,nullptr,nullptr,&wait)==0);
    stopWorker();CHECK(!g_pairVerified&&!g_ser&&!g_net);closeSock(server);g_testSerial.enabled=false;
}
void workerRevalidatesRetry(){
    wchar_t dir[MAX_PATH]={},file[MAX_PATH]={};GetTempPathW(MAX_PATH,dir);GetTempFileNameW(dir,L"SRV",0,file);
    g_testSerial.enabled=true;g_testSerial.file=file;g_testSerial.peer=true;g_testSerial.inventoryCalls=0;g_testSerial.invalidOnRetry=true;
    g_testSerial.inventory="CNCA0 PortName=COM7,EmuOverrun=yes\nCNCB0 PortName=COM8,dsr=ropen,EmuBR=yes\n";
    SOCKET server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);CHECK(server!=INVALID_SOCKET);
    sockaddr_in a={};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=0;
    CHECK(bind(server,(sockaddr*)&a,sizeof(a))==0);int n=sizeof(a);CHECK(getsockname(server,(sockaddr*)&a,&n)==0);
    Config c={};c.ip=L"127.0.0.1";c.dataPort=c.commandPort=ntohs(a.sin_port);c.com=L"COM8";c.packCom=L"COM7";c.trigger={'L'};c.key=L"Enter";
    g_faultLatched=false;startWorker(c);ULONGLONG until=GetTickCount64()+2500;bool failed=false;
    while(GetTickCount64()<until){{std::lock_guard<std::mutex> lock(g_statusMutex);failed=g_status.find(L"2 秒后重连")!=std::wstring::npos;}if(failed)break;Sleep(5);}CHECK(failed);
    CHECK(listen(server,1)==0);fd_set f;FD_ZERO(&f);FD_SET(server,&f);timeval wait={3,0};CHECK(select(0,&f,nullptr,nullptr,&wait)==0);
    stopWorker();CHECK(g_testSerial.inventoryCalls>=2&&!g_pairVerified&&!g_net);
    closeSock(server);g_testSerial.enabled=false;g_testSerial.invalidOnRetry=false;DeleteFileW(file);
}
void workerCommandFailureRegression(){
    wchar_t temp[MAX_PATH]={},file[MAX_PATH]={};GetTempPathW(MAX_PATH,temp);GetTempFileNameW(temp,L"SRW",0,file);
    g_testSerial.enabled=true;g_testSerial.file=file;g_testSerial.peer=true;g_testSerial.writes=0;
    g_testSerial.inventory="CNCA0 PortName=COM7,EmuOverrun=yes\nCNCB0 PortName=COM8,dsr=ropen,EmuBR=yes\n";
    SOCKET dataServer=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP),closedCommand=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    CHECK(dataServer!=INVALID_SOCKET&&closedCommand!=INVALID_SOCKET);
    sockaddr_in a={};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=0;
    CHECK(bind(dataServer,(sockaddr*)&a,sizeof(a))==0&&listen(dataServer,2)==0);
    int n=sizeof(a);CHECK(getsockname(dataServer,(sockaddr*)&a,&n)==0);int dataPort=ntohs(a.sin_port);
    a.sin_port=0;CHECK(bind(closedCommand,(sockaddr*)&a,sizeof(a))==0);n=sizeof(a);CHECK(getsockname(closedCommand,(sockaddr*)&a,&n)==0);
    Config c={};c.ip=L"127.0.0.1";c.dataPort=dataPort;c.commandPort=ntohs(a.sin_port);c.com=L"COM8";c.packCom=L"COM7";c.trigger={'L'};c.key=L"Enter";
    g_faultLatched=false;g_keyboardOK=true;startWorker(c);
    fd_set fd;FD_ZERO(&fd);FD_SET(dataServer,&fd);timeval tv={3,0};CHECK(select(0,&fd,nullptr,nullptr,&tv)>0);
    SOCKET peer=accept(dataServer,nullptr,nullptr);CHECK(peer!=INVALID_SOCKET);
    const char scan[]={'A',0,'B','\r'};CHECK(send(peer,scan,sizeof(scan),0)==sizeof(scan));
    ULONGLONG end=GetTickCount64()+1800;while(!g_testSerial.writes.load()&&GetTickCount64()<end)Sleep(10);
    const bool received=g_testSerial.writes.load()>0;
    stopWorker();closeSock(peer);closeSock(dataServer);closeSock(closedCommand);g_testSerial.enabled=false;DeleteFileW(file);
    CHECK(received&&"actual worker must receive scans even when command connection fails");
}
void workerSharedRegression(){
    wchar_t dir[MAX_PATH]={},file[MAX_PATH]={};GetTempPathW(MAX_PATH,dir);GetTempFileNameW(dir,L"SRS",0,file);
    g_testSerial.enabled=true;g_testSerial.file=file;g_testSerial.peer=true;g_testSerial.writes=0;
    g_testSerial.inventory="CNCA0 PortName=COM7,EmuOverrun=yes\nCNCB0 PortName=COM8,dsr=ropen,EmuBR=yes\n";
    SOCKET server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);CHECK(server!=INVALID_SOCKET);
    sockaddr_in a={};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=0;
    CHECK(bind(server,(sockaddr*)&a,sizeof(a))==0&&listen(server,2)==0);int len=sizeof(a);CHECK(getsockname(server,(sockaddr*)&a,&len)==0);
    Config c={};c.ip=L"127.0.0.1";c.dataPort=c.commandPort=ntohs(a.sin_port);c.com=L"COM8";c.packCom=L"COM7";c.trigger={'L','O','N',13};c.key=L"Enter";
    g_faultLatched=false;g_keyboardOK=true;startWorker(c);
    fd_set fd;FD_ZERO(&fd);FD_SET(server,&fd);timeval wait={3,0};CHECK(select(0,&fd,nullptr,nullptr,&wait)>0);
    SOCKET peer=accept(server,nullptr,nullptr);CHECK(peer!=INVALID_SOCKET);
    ULONGLONG until=GetTickCount64()+1500;while(!g_peerOnline&&GetTickCount64()<until)Sleep(5);CHECK(g_peerOnline);
    FD_ZERO(&fd);FD_SET(server,&fd);wait.tv_sec=0;wait.tv_usec=50000;CHECK(select(0,&fd,nullptr,nullptr,&wait)==0); // no second socket to same port
    const char scan[]={ 'S',0,'C',13,10 };CHECK(send(peer,scan,sizeof(scan),0)==sizeof(scan));
    until=GetTickCount64()+1200;while(!g_testSerial.writes&&GetTickCount64()<until)Sleep(5);CHECK(g_testSerial.writes>0); // unsolicited scan, no trigger pressed
    queueInput({GetTickCount()-1000,g_inputGeneration.load()});
    FD_ZERO(&fd);FD_SET(peer,&fd);wait.tv_sec=0;wait.tv_usec=50000;CHECK(select(0,&fd,nullptr,nullptr,&wait)==0);
    queueInput({GetTickCount(),g_inputGeneration.load()});
    unsigned char command[4]={};size_t received=0;
    while(received<sizeof(command)){int n=testRecv(peer,(char*)command+received,(int)(sizeof(command)-received));CHECK(n>0);received+=n;}
    CHECK(memcmp(command,c.trigger.data(),4)==0);
    // A real key-up/key-down pair <300ms apart is not suppressed by a fixed throttle.
    queueInput({GetTickCount(),g_inputGeneration.load()});received=0;
    while(received<sizeof(command)){int n=testRecv(peer,(char*)command+received,(int)(sizeof(command)-received));CHECK(n>0);received+=n;}
    CHECK(memcmp(command,c.trigger.data(),4)==0);
    // Drop must latch: no automatic connection recreation or old-trigger replay.
    closeSock(peer);until=GetTickCount64()+1500;while(!g_faultLatched&&GetTickCount64()<until)Sleep(5);CHECK(g_faultLatched);
    FD_ZERO(&fd);FD_SET(server,&fd);wait.tv_sec=0;wait.tv_usec=100000;CHECK(select(0,&fd,nullptr,nullptr,&wait)==0);
    stopWorker();g_testSerial.enabled=false;closeSock(server);
    HANDLE copied=CreateFileW(file,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);CHECK(copied!=INVALID_HANDLE_VALUE);
    char result[sizeof(scan)]={};DWORD read=0;CHECK(ReadFile(copied,result,sizeof(result),&read,nullptr)&&read==sizeof(scan));CHECK(memcmp(scan,result,sizeof(scan))==0);CloseHandle(copied);DeleteFileW(file);
}
int main(){
    wchar_t base[MAX_PATH]={},folder[MAX_PATH]={};GetTempPathW(MAX_PATH,base);GetTempFileNameW(base,L"SRT",0,folder);DeleteFileW(folder);CHECK(CreateDirectoryW(folder,nullptr));
    g_dir=folder;g_ini=g_dir+L"\\bridge.ini";
    std::vector<unsigned char> b;
    wchar_t originalPF[32768]={},originalPFx86[32768]={};
    DWORD originalPFlen=GetEnvironmentVariableW(L"ProgramFiles",originalPF,32768),originalPF86len=GetEnvironmentVariableW(L"ProgramFiles(x86)",originalPFx86,32768);
    SetEnvironmentVariableW(L"ProgramFiles",g_dir.c_str());SetEnvironmentVariableW(L"ProgramFiles(x86)",g_dir.c_str());
    std::wstring fakeDir=g_dir+L"\\com0com";CHECK(CreateDirectoryW(fakeDir.c_str(),nullptr));
    std::wstring fakePath=fakeDir+L"\\setupc.exe";HANDLE fakeFile=CreateFileW(fakePath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);CHECK(fakeFile!=INVALID_HANDLE_VALUE);CloseHandle(fakeFile);
    CHECK(findSetupc()!=fakePath);
    SetEnvironmentVariableW(L"ProgramFiles",originalPFlen?originalPF:nullptr);SetEnvironmentVariableW(L"ProgramFiles(x86)",originalPF86len?originalPFx86:nullptr);
    DeleteFileW(fakePath.c_str());RemoveDirectoryW(fakeDir.c_str());

    std::wstring pairReason;
    g_testSerial.enabled=true;g_testSerial.inventory="CNCA0 PortName=COM7,EmuOverrun=yes\nCNCB0 PortName=COM8,dsr=ropen,EmuBR=yes\n";
    CHECK(verifyConfiguredPair(L"COM7",L"COM8",pairReason));
    CHECK(!verifyConfiguredPair(L"COM9",L"COM8",pairReason));
    CHECK(!verifyConfiguredPair(L"COM8",L"COM7",pairReason));
    g_testSerial.inventory="CNCA0 PortName=COM7\nCNCB0 PortName=COM8,dsr=rdtr,EmuBR=yes\n";
    CHECK(!verifyConfiguredPair(L"COM7",L"COM8",pairReason));g_testSerial.enabled=false;

    // HEX_REVIEW_REGRESSION: do not join two incomplete byte tokens across spaces.
    CHECK(!triggerBytes(L"hex:4 C",b));
    std::wstring reservedReason;g_reservedPortsForTest={L"COM8"};
    CHECK(!creationNamesFree(L"COM7",L"COM8",reservedReason));
    g_reservedPortsForTest.clear();CHECK(creationNamesFree(L"COM7",L"COM8",reservedReason));
    CHECK(!creationNamesFree(L"COM07",L"COM8",reservedReason));

    HANDLE installFake=CreateEventW(nullptr,TRUE,FALSE,nullptr);CHECK(installFake);
    g_pairInstallProcess=installFake;g_pairInstallStarted=GetTickCount64()-31000;g_pairInstallWarned=false;
    pollPairInstall();CHECK(g_pairInstallProcess==installFake&&g_pairInstallWarned);
    SetEvent(installFake);pollPairInstall();CHECK(g_pairInstallProcess==nullptr);

    // Input captured before reconnect, from an old generation, or delayed by UI blocking must never fire.
    CHECK(inputFresh(100,1,150,1,50));
    CHECK(!inputFresh(100,1,600,1,50));
    CHECK(!inputFresh(100,1,150,2,50));
    CHECK(!inputFresh(100,1,150,1,110));
    CHECK(inputFresh(0xFFFFFFF0u,3,0x10u,3,0xFFFFFF00u));
    invalidateInputs();DWORD nowInput=GetTickCount();DWORD generation=g_inputGeneration;
    queueInput({nowInput,generation});queueInput({nowInput,generation});
    TriggerInput next={};CHECK(nextInput(next));CHECK(nextInput(next));CHECK(!nextInput(next));
    queueInput({GetTickCount(),g_inputGeneration.load()});invalidateInputs();CHECK(!nextInput(next));


    DCB serialConfig={};serialConfig.DCBlength=sizeof(serialConfig);serialConfig.fInX=TRUE;serialConfig.fOutX=TRUE;serialConfig.fNull=TRUE;serialConfig.fErrorChar=TRUE;
    configureSerial(serialConfig);
    CHECK(serialConfig.fBinary&&!serialConfig.fInX&&!serialConfig.fOutX&&!serialConfig.fNull&&!serialConfig.fErrorChar&&!serialConfig.fDsrSensitivity&&!serialConfig.fAbortOnError);

    CHECK(triggerBytes(L"text:T",b)&&b==std::vector<unsigned char>({'t','e','x','t',':','T'}));
    CHECK(triggerBytes(L"L",b)&&b==std::vector<unsigned char>({'L'}));
    CHECK(triggerBytes(L"4C",b)&&b==std::vector<unsigned char>({'4','C'})); // legacy v2 had no encoding selector
    CHECK(parseTrigger(L"4C",true,b)&&b==std::vector<unsigned char>({'L'}));
    CHECK(parseTrigger(L"4C",false,b)&&b==std::vector<unsigned char>({'4','C'}));
    g_ioCancelTest=true;g_stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    wchar_t ioDir[MAX_PATH]={},ioFile[MAX_PATH]={};GetTempPathW(MAX_PATH,ioDir);GetTempFileNameW(ioDir,L"SRQ",0,ioFile);
    HANDLE ioTarget=CreateFileW(ioFile,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_FLAG_OVERLAPPED,nullptr);CHECK(ioTarget!=INVALID_HANDLE_VALUE);
    ULONGLONG began=GetTickCount64();CHECK(!writeCom(ioTarget,std::vector<unsigned char>({'A',0,'B'})));
    CHECK(GetTickCount64()-began<2200&&g_ioStalled.load());
    CHECK(g_unfinishedWrites.size()==1&&g_unfinishedWrites[0]->bytes==std::vector<unsigned char>({'A',0,'B'}));
    CHECK(WaitForSingleObject(g_unfinishedWrites[0]->ov.hEvent,0)==WAIT_TIMEOUT);
    SetEvent(g_unfinishedWrites[0]->ov.hEvent);reapWrites();CHECK(g_unfinishedWrites.empty()&&!g_ioStalled);
    g_ioCancelTest=false;CloseHandle(ioTarget);DeleteFileW(ioFile);CloseHandle(g_stop);g_stop=nullptr;

    CHECK(triggerBytes(L"hex:4C 4F 4E 0D",b)&&b.size()==4&&b[0]=='L'&&b[3]=='\r');
    CHECK(triggerBytes(L"4C 4F 4E 0D",b)&&b.size()==4&&b[0]==0x4C&&b[1]==0x4F&&b[2]==0x4E&&b[3]==0x0D);
    CHECK(!triggerBytes(L"hex:4C F",b));
    CHECK(!triggerBytes(L"hex:GG",b));
    CHECK(triggerBytes(L"HELLO WORLD",b)&&b.size()==11&&b[0]=='H');
    CHECK(triggerBytes(L"hex:AA-BB-00",b)&&b.size()==3&&b[0]==0xAA&&b[1]==0xBB&&b[2]==0x00);
    CHECK(!triggerBytes(L"",b));
    CHECK(keyCode(L"Enter")==VK_RETURN&&keyCode(L"Space")==VK_SPACE);
    CHECK(keyCode(L"F1")==VK_F1&&keyCode(L"f12")==VK_F12);
    CHECK(keyCode(L"F13")==0&&keyCode(L"Escape")==0);
    CHECK(number(L"9004")==9004);
    CHECK(number(L"65536")==0);
    CHECK(number(L"12x")==0);
    CHECK(comName(L"COM7")&&comName(L"COM19")&&!comName(L"COM0")&&!comName(L"COM7evil")&&!comName(L"COM\\7"));
    CHECK(!comExists(L"COM999"));
    // Packaging executable foreground selection is removed: only physical key / connection state gates trigger.
    g_window=CreateWindowW(L"STATIC",L"Test picker",WS_OVERLAPPED,0,0,300,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    CHECK(g_window!=nullptr);
    HWND picker=keyPicker(g_window,35,L"Space");
    CHECK(picker&&((GetWindowLongPtrW(picker,GWL_STYLE)&CBS_DROPDOWNLIST)==CBS_DROPDOWNLIST));
    CHECK(SendMessageW(picker,CB_GETCOUNT,0,0)==14);
    CHECK(getText(ID_KEY)==L"Space");
    CHECK(keyCode(getText(ID_KEY))==VK_SPACE);
    CHECK(SendMessageW(picker,CB_GETLBTEXTLEN,0,0)==5);
    CHECK(SendMessageW(picker,CB_SELECTSTRING,(WPARAM)-1,(LPARAM)L"F12")!=CB_ERR);
    CHECK(getText(ID_KEY)==L"F12");
    DestroyWindow(g_window);g_window=nullptr;
    // UI smoke: new packaging COM field and create-pair button exist; EXE foreground selector is absent.
    WNDCLASSW wc={};wc.lpfnWndProc=wndProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"BridgeV2Smoke";
    CHECK(RegisterClassW(&wc));
    HWND ui=CreateWindowW(wc.lpszClassName,L"Bridge smoke",WS_OVERLAPPED,0,0,510,520,nullptr,nullptr,wc.hInstance,nullptr);
    CHECK(ui);CHECK(GetDlgItem(ui,ID_PACK_COM));CHECK(GetDlgItem(ui,CMD_CREATE_PAIR));
    // New requirement: explicit ASCII/HEX selector, never guess from typed characters.
    CHECK(GetDlgItem(ui,111)!=nullptr);
    CHECK(SendMessageW(GetDlgItem(ui,ID_ENCODING),CB_GETCOUNT,0,0)==2);
    RECT modeRect={},contentRect={},keyRect={},buttonRect={};
    GetWindowRect(GetDlgItem(ui,ID_ENCODING),&modeRect);GetWindowRect(GetDlgItem(ui,ID_TRIGGER),&contentRect);
    GetWindowRect(GetDlgItem(ui,ID_KEY),&keyRect);GetWindowRect(GetDlgItem(ui,CMD_SAVE),&buttonRect);
    CHECK(modeRect.top<contentRect.top&&modeRect.left==contentRect.left&&keyRect.top<buttonRect.top);

    SetWindowTextW(GetDlgItem(ui,ID_TRIGGER),L"4C");g_hexMode=true;
    CHECK(changeEncoding(false,false));CHECK(getText(ID_TRIGGER)==L"L");
    CHECK(changeEncoding(true,false));CHECK(getText(ID_TRIGGER)==L"4C");
    // Exercise the actual combo-box notification, not only the conversion helper.
    SendMessageW(GetDlgItem(ui,ID_ENCODING),CB_SETCURSEL,0,0);
    SendMessageW(ui,WM_COMMAND,MAKEWPARAM(ID_ENCODING,CBN_SELCHANGE),(LPARAM)GetDlgItem(ui,ID_ENCODING));
    CHECK(!g_hexMode&&getText(ID_TRIGGER)==L"L");
    SendMessageW(GetDlgItem(ui,ID_ENCODING),CB_SETCURSEL,1,0);
    SendMessageW(ui,WM_COMMAND,MAKEWPARAM(ID_ENCODING,CBN_SELCHANGE),(LPARAM)GetDlgItem(ui,ID_ENCODING));
    CHECK(g_hexMode&&getText(ID_TRIGGER)==L"4C");

    SetWindowTextW(GetDlgItem(ui,ID_TRIGGER),L"4C 4F 4E 0D 0A");
    CHECK(changeEncoding(false,false));CHECK(getText(ID_TRIGGER)==L"LON\\r\\n");
    CHECK(changeEncoding(true,false));CHECK(getText(ID_TRIGGER)==L"4C 4F 4E 0D 0A");
    SetWindowTextW(GetDlgItem(ui,ID_TRIGGER),L"4 G");
    CHECK(!changeEncoding(false,false));CHECK(g_hexMode&&getText(ID_TRIGGER)==L"4 G");
    CHECK(SendMessageW(GetDlgItem(ui,ID_ENCODING),CB_GETCURSEL,0,0)==1);
    SetWindowTextW(GetDlgItem(ui,ID_TRIGGER),L"");CHECK(changeEncoding(false,false));
    CHECK(!g_hexMode&&getText(ID_TRIGGER).empty());
    SetWindowTextW(GetDlgItem(ui,ID_TRIGGER),L"4C");CHECK(changeEncoding(true,false));
    CHECK(getText(ID_TRIGGER)==L"34 43"); // ASCII '4C' means two literal characters, not byte 4C.
    CHECK(changeEncoding(false,false)&&getText(ID_TRIGGER)==L"4C");
    SetWindowTextW(GetDlgItem(ui,ID_TRIGGER),L"\\xGG");
    CHECK(!changeEncoding(true,false)&&!g_hexMode&&getText(ID_TRIGGER)==L"\\xGG");

    for(int value=0;value<256;++value){
        std::vector<unsigned char> original(1,(unsigned char)value),decoded;
        CHECK(parseTrigger(formatAscii(original),false,decoded)&&decoded==original);
        CHECK(parseTrigger(formatHex(original),true,decoded)&&decoded==original);
    }

    CHECK(GetDlgItem(ui,107)==nullptr);
    DestroyWindow(ui);g_window=nullptr;
    WSADATA wsa={};CHECK(WSAStartup(MAKEWORD(2,2),&wsa)==0);
    wchar_t tempDir[MAX_PATH]={},oldIni[MAX_PATH]={};CHECK(GetTempPathW(MAX_PATH,tempDir)>0);
    CHECK(GetTempFileNameW(tempDir,L"SRB",0,oldIni));
    g_ini=oldIni;
    CHECK(WritePrivateProfileStringW(L"Reader",L"ReaderIp",L"127.0.0.1",g_ini.c_str()));
    CHECK(WritePrivateProfileStringW(L"Reader",L"DataPort",L"39001",g_ini.c_str()));
    CHECK(WritePrivateProfileStringW(L"Reader",L"CommandMode",L"Separate",g_ini.c_str()));
    CHECK(WritePrivateProfileStringW(L"Reader",L"CommandPort",L"39002",g_ini.c_str()));
    CHECK(WritePrivateProfileStringW(L"Reader",L"TriggerHex",L"T",g_ini.c_str()));
    CHECK(WritePrivateProfileStringW(L"Serial",L"BridgeCom",L"COM19",g_ini.c_str()));
    Config config={};std::wstring reason;
    // Direct INI edits must not bypass validation of the packaging endpoint.
    CHECK(!loadConfig(config,reason));
    CHECK(WritePrivateProfileStringW(L"Serial",L"PackagingCom",L"COM19",g_ini.c_str()));
    reason.clear();CHECK(!loadConfig(config,reason));
    CHECK(WritePrivateProfileStringW(L"Serial",L"PackagingCom",L"COM18",g_ini.c_str()));
    reason.clear();CHECK(loadConfig(config,reason)&&config.dataPort==39001&&config.commandPort==39002);
    CHECK(WritePrivateProfileStringW(L"Serial",L"PackagingCom",L"COM00018",g_ini.c_str()));
    reason.clear();CHECK(!loadConfig(config,reason));
    CHECK(WritePrivateProfileStringW(L"Serial",L"PackagingCom",L"COM18",g_ini.c_str()));
    reason.clear();CHECK(loadConfig(config,reason));
    CHECK(WritePrivateProfileStringW(L"Reader",L"TcpPort",L"39003",g_ini.c_str()));
    reason.clear();CHECK(loadConfig(config,reason)&&config.dataPort==39001&&config.commandPort==39002);
    CHECK(WritePrivateProfileStringW(L"Reader",L"CommandPort",L"39001",g_ini.c_str()));
    reason.clear();CHECK(loadConfig(config,reason)&&config.dataPort==config.commandPort);
    CHECK(WritePrivateProfileStringW(L"Reader",L"DataPort",L"9004",g_ini.c_str()));
    CHECK(WritePrivateProfileStringW(L"Reader",L"CommandPort",L"09004",g_ini.c_str()));
    reason.clear();CHECK(loadConfig(config,reason)&&config.dataPort==config.commandPort);
    CHECK(WritePrivateProfileStringW(L"Reader",L"DataPort",L"39001",g_ini.c_str()));
    CHECK(WritePrivateProfileStringW(L"Reader",L"CommandPort",nullptr,g_ini.c_str()));
    reason.clear();CHECK(!loadConfig(config,reason));
    CHECK(WritePrivateProfileStringW(L"Reader",L"CommandPort",L"39002",g_ini.c_str()));
    reason.clear();CHECK(loadConfig(config,reason));
    // Explicit mode survives restart/config load; same displayed text means different bytes in each mode.
    CHECK(WritePrivateProfileStringW(L"Reader",L"TriggerEncoding",L"ASCII",g_ini.c_str()));
    CHECK(WritePrivateProfileStringW(L"Reader",L"TriggerText",L"LON\\r",g_ini.c_str()));
    reason.clear();CHECK(loadConfig(config,reason));
    CHECK(config.trigger==std::vector<unsigned char>({'L','O','N',13}));
    CHECK(WritePrivateProfileStringW(L"Reader",L"TriggerEncoding",L"HEX",g_ini.c_str()));
    CHECK(WritePrivateProfileStringW(L"Reader",L"TriggerText",L"4C 4F 4E 0D",g_ini.c_str()));
    reason.clear();CHECK(loadConfig(config,reason)&&config.trigger==std::vector<unsigned char>({'L','O','N',13}));
    CHECK(WritePrivateProfileStringW(L"Reader",L"TriggerEncoding",L"ASCII",g_ini.c_str()));
    CHECK(WritePrivateProfileStringW(L"Reader",L"TriggerText",L"4C",g_ini.c_str()));
    reason.clear();CHECK(loadConfig(config,reason)&&config.trigger==std::vector<unsigned char>({'4','C'}));
    CHECK(storeTrigger(L" L ",false));
    reason.clear();CHECK(loadConfig(config,reason)&&config.trigger==std::vector<unsigned char>({' ','L',' '}));
    CHECK(storeTrigger(L"\"L\"",false));
    reason.clear();CHECK(loadConfig(config,reason)&&config.trigger==std::vector<unsigned char>({'"','L','"'}));
    CHECK(storeTrigger(L"4C 4F 4E 0D",true));
    reason.clear();CHECK(loadConfig(config,reason)&&config.trigger==std::vector<unsigned char>({'L','O','N',13}));

    CHECK(WritePrivateProfileStringW(L"Reader",L"TriggerEncoding",L"UTF8",g_ini.c_str()));
    reason.clear();CHECK(!loadConfig(config,reason));
    // Full configuration commit must be all-or-nothing, including an INI locked against replacement.
    std::vector<std::wstring> values={L"127.0.0.1",L"39001",L"39002",L"LON\\r",L"COM19",L"COM18",L"Enter"};
    Config saved={};std::wstring saveError;
    if(!saveConfigAtomic(values,false,saved,saveError)){int n=WideCharToMultiByte(CP_UTF8,0,saveError.c_str(),-1,nullptr,0,nullptr,nullptr);std::vector<char> message(n);WideCharToMultiByte(CP_UTF8,0,saveError.c_str(),-1,message.data(),n,nullptr,nullptr);fprintf(stderr,"ATOMIC_SAVE_ERROR: %s\n",message.data());CHECK(false);}
    CHECK(saved.trigger==std::vector<unsigned char>({'L','O','N',13}));
    CHECK(ini(L"Reader",L"DataPort",L"")==L"39001");
    HANDLE locked=CreateFileW(g_ini.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);CHECK(locked!=INVALID_HANDLE_VALUE);
    values[1]=L"39005";
    CHECK(!saveConfigAtomic(values,false,saved,saveError));
    CloseHandle(locked);
    CHECK(ini(L"Reader",L"DataPort",L"")==L"39001");
    values[1]=L"invalid";
    CHECK(!saveConfigAtomic(values,false,saved,saveError));
    CHECK(ini(L"Reader",L"DataPort",L"")==L"39001");
    DeleteFileW(g_ini.c_str());g_ini.clear();
    g_stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    SOCKET server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    CHECK(server!=INVALID_SOCKET);
    sockaddr_in addr={};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=0;
    CHECK(bind(server,(sockaddr*)&addr,sizeof(addr))==0);
    CHECK(listen(server,1)==0);
    int len=sizeof(addr);CHECK(getsockname(server,(sockaddr*)&addr,&len)==0);
    SOCKET client=connectTo(L"127.0.0.1",ntohs(addr.sin_port));CHECK(client!=INVALID_SOCKET);
    SOCKET accepted=accept(server,nullptr,nullptr);CHECK(accepted!=INVALID_SOCKET);
    SOCKET commandServer=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);CHECK(commandServer!=INVALID_SOCKET);
    sockaddr_in commandAddr={};commandAddr.sin_family=AF_INET;commandAddr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);commandAddr.sin_port=0;
    CHECK(bind(commandServer,(sockaddr*)&commandAddr,sizeof(commandAddr))==0&&listen(commandServer,1)==0);
    int commandLen=sizeof(commandAddr);CHECK(getsockname(commandServer,(sockaddr*)&commandAddr,&commandLen)==0);
    CHECK(ntohs(commandAddr.sin_port)!=ntohs(addr.sin_port));
    SOCKET commandClient=connectTo(L"127.0.0.1",ntohs(commandAddr.sin_port));CHECK(commandClient!=INVALID_SOCKET);
    SOCKET commandPeer=accept(commandServer,nullptr,nullptr);CHECK(commandPeer!=INVALID_SOCKET);

    std::vector<unsigned char> asciiBytes,hexBytes;
    CHECK(parseTrigger(L"LON\\r",false,asciiBytes));
    CHECK(parseTrigger(L"4C 4F 4E 0D",true,hexBytes)&&asciiBytes==hexBytes);
    CHECK(sendAll(commandClient,asciiBytes));
    unsigned char packet[4]={};size_t packetDone=0;
    while(packetDone<4){int k=testRecv(commandPeer,(char*)packet+packetDone,(int)(4-packetDone),0);CHECK(k>0);packetDone+=k;}
    CHECK(memcmp(packet,asciiBytes.data(),4)==0);
    u_long commandNB=1;CHECK(ioctlsocket(commandPeer,FIONBIO,&commandNB)==0);
    char unexpected=0;CHECK(recv(commandPeer,&unexpected,1,0)==SOCKET_ERROR&&WSAGetLastError()==WSAEWOULDBLOCK);
    commandNB=0;CHECK(ioctlsocket(commandPeer,FIONBIO,&commandNB)==0);
    CHECK(parseTrigger(L"T",false,b));CHECK(sendAll(commandClient,b));
    char v[3]={};CHECK(testRecv(commandPeer,v,sizeof(v),0)==1&&v[0]=='T');
    u_long nonblocking=1;CHECK(ioctlsocket(accepted,FIONBIO,&nonblocking)==0);
    CHECK(recv(accepted,v,sizeof(v),0)==SOCKET_ERROR&&WSAGetLastError()==WSAEWOULDBLOCK);
    // Simulate unsolicited scanner data: no key press, binary-safe, terminator preserved.
    wchar_t dir[MAX_PATH]={},name[MAX_PATH]={};CHECK(GetTempPathW(MAX_PATH,dir)>0);
    CHECK(GetTempFileNameW(dir,L"SRT",0,name));
    HANDLE target=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_OVERLAPPED,nullptr);
    CHECK(target!=INVALID_HANDLE_VALUE);
    const char scanner[]={ 'A',0,'B',static_cast<char>(-1),'\r','\n' };
    CHECK(send(accepted,scanner,(int)sizeof(scanner),0)==sizeof(scanner));
    char received[sizeof(scanner)]={};size_t done=0;
    while(done<sizeof(scanner)){int got=testRecv(client,received+done,(int)(sizeof(scanner)-done),0);CHECK(got>0);CHECK(relayChunk(target,received+done,got));done+=got;}
    CHECK(FlushFileBuffers(target));CloseHandle(target);
    target=CreateFileW(name,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);CHECK(target!=INVALID_HANDLE_VALUE);
    char copied[sizeof(scanner)]={};DWORD count=0;CHECK(ReadFile(target,copied,sizeof(copied),&count,nullptr)&&count==sizeof(scanner));
    CHECK(memcmp(scanner,copied,sizeof(scanner))==0);CloseHandle(target);DeleteFileW(name);
    SOCKET sharedClient=connectTo(L"127.0.0.1",ntohs(addr.sin_port));CHECK(sharedClient!=INVALID_SOCKET);
    SOCKET sharedPeer=accept(server,nullptr,nullptr);CHECK(sharedPeer!=INVALID_SOCKET);
    CHECK(sendAll(sharedClient,b));char sharedByte=0;
    CHECK(testRecv(sharedPeer,&sharedByte,1,0)==1&&sharedByte=='T');
    CHECK(send(sharedPeer,scanner,(int)sizeof(scanner),0)==sizeof(scanner));
    char sharedReceived[sizeof(scanner)]={};size_t sharedDone=0;
    while(sharedDone<sizeof(scanner)){int k=testRecv(sharedClient,sharedReceived+sharedDone,(int)(sizeof(scanner)-sharedDone),0);CHECK(k>0);sharedDone+=k;}
    CHECK(memcmp(sharedReceived,scanner,sizeof(scanner))==0);
    closeSock(sharedPeer);closeSock(sharedClient);
    closeSock(commandPeer);closeSock(commandClient);closeSock(commandServer);
    closeSock(accepted);closeSock(client);closeSock(server);
    g_trigger=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    workerRejectsInvalidPair();
    workerRevalidatesRetry();
    workerCommandFailureRegression();
    workerSharedRegression();
    CloseHandle(g_trigger);g_trigger=nullptr;
    CloseHandle(g_stop);g_stop=nullptr;WSACleanup();
    DeleteFileW((g_dir+L"\\logs\\bridge.log").c_str());DeleteFileW((g_dir+L"\\logs\\bridge.previous.log").c_str());RemoveDirectoryW((g_dir+L"\\logs").c_str());RemoveDirectoryW(g_dir.c_str());
    puts("PASS: ASCII/HEX selector notifications, L<->4C, control escapes, all 256 byte conversions, invalid-input rollback, mode and whitespace persistence, exact TCP bytes, shared/separate TCP helpers, config validation and binary serial flags. Actual worker: invalid pair blocks scanner connection, command-port unavailable keeps receiving, shared mode single socket, stale input dropped, valid trigger bytes, disconnect locks without replay. Atomic-save locked-file rollback and delayed-cancel resource lifetime pass. No physical driver/scanner validation.");
}
