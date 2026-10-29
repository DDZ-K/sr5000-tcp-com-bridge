#define _WIN32_WINNT 0x0601
#include "../../com0com_inventory.h"
#include <cstdio>
#include <cstdlib>
static int checks=0;
static void check(bool value,const char* name) {
    if (!value) { std::fprintf(stderr,"FAIL %s\n",name); std::exit(1); }
    ++checks;
}
int wmain(int argc,wchar_t** argv) {
    (void)argc;(void)argv;
    com0com::Inventory inv;
    std::string error;
    const char* documented="       CNCA0 PortName=COM#,RealPortName=COM3\r\n"
                           "       CNCB0 PortName=COM#,RealPortName=COM4\r\n";
    check(com0com::ParseInventory(documented,inv,error)&&inv.endpoints.size()==2&&inv.endpoints[0].portName=="COM3"&&inv.endpoints[1].pairId=="0","upstream literal fixture");
    auto r=com0com::ValidatePair(inv,"COM3","COM4");
    check(r.identityVerified&&!r.pinsVerified&&!r.ok(),"missing mappings fail closed");
    const char* configured=" CNCA0 PortName=COM7,EmuOverrun=yes\n CNCB0 PortName=COM8,dsr=ropen,EmuBR=yes\n";
    check(com0com::ParseInventory(configured,inv,error),"source grammar configured fixture");
    r=com0com::ValidatePair(inv,"COM7","COM8");
    check(r.ok()&&r.packagingId=="CNCA0"&&r.bridgeId=="CNCB0","pair identity and explicit mapping");
    check(!com0com::ValidatePair(inv,"COM8","COM7").ok(),"reversed roles mapping denied");
    check(!com0com::ValidatePair(inv,"COM7","COM7").ok(),"same endpoint denied");
    for (const char* bad:{"COM0","COM01","com7","COM7&whoami","COM7,dsr=on","COM7\"","COM7 ","\\\\.\\COM7","COM123456"}) check(!com0com::IsCanonicalCom(bad),"noncanonical/injection COM denied");
    for (const char* text:{"CNCA0 PortName=COM7\nCNCB1 PortName=COM8,dsr=ropen,EmuBR=yes\n","CNCA0 PortName=COM7,EmuOverrun=yes\nCNCB0 PortName=COM8,dsr=!ropen,EmuBR=yes\n","CNCA0 PortName=COM7,EmuOverrun=no\nCNCB0 PortName=COM8,dsr=ropen,EmuBR=yes\n"}) {
        check(com0com::ParseInventory(text,inv,error)&&!com0com::ValidatePair(inv,"COM7","COM8").ok(),"wrong pair or mapping denied");
    }
    for (const char* text:{"CNCA0 PortName=COM7\nCNCA0 PortName=COM8\n","CNCA0 PortName=COM7\nCNCB0 PortName=COM7\n","CNCA0 PortName=COM7,dsr=ropen,DSR=on\n","CNCA0 PortName=COM#\n","CNCA0 PortName=COM7,RealPortName=COM8\n","CNCA00 PortName=COM7\n","CNCA? PortName=COM7\n","CNCA0 PortName=COM7,\n","CNCA0 PortName=COM7\nDIALOG: failed\n","CNCA0 PortName=COM01\n"}) {
        check(!com0com::ParseInventory(text,inv,error)&&inv.endpoints.empty(),"ambiguous/malformed inventory denied atomically");
    }
    check(com0com::ParseInventory("",inv,error)&&!com0com::ValidatePair(inv,"COM7","COM8").ok(),"empty inventory does not prove pair");
    std::puts("PASS parser: documented fixture, pair identity, role mappings and malformed/injection inputs");
    check(!com0com::ParseInventory(std::string("CNCA0 PortName=COM7\n\0",sizeof("CNCA0 PortName=COM7\n\0")-1),inv,error),"embedded NUL rejected");
    check(!com0com::ParseInventory(std::string(1024*1024+1,'x'),inv,error),"total text limit");
    check(!com0com::ParseInventory("CNCA2147483648 PortName=COM7\n",inv,error),"pair index signed range");
    check(!com0com::ParseInventory("CNCA0 PortName=COM7;calc\n",inv,error),"bad visible COM name denied");
    check(com0com::ParseInventory("CNCA0 PortName=-\nCNCB0 PortName=CNCB0\n",inv,error)&&!com0com::ValidatePair(inv,"COM7","COM8").ok(),"unassigned/custom endpoints never prove COM identity");
    com0com::AdminCreateCommand command;
    check(com0com::BuildAdminCreateCommand(L"C:\\Program Files\\com0com\\setupc.exe","COM7","COM8",command,error),"build administrator creation command only");
    check(command.verb==L"runas"&&command.file==L"C:\\Program Files\\com0com\\setupc.exe"&&command.parameters==L"install PortName=COM7,EmuOverrun=yes PortName=COM8,dsr=ropen,EmuBR=yes","exact fixed install argument contract");
    for (const char* name:{"COM0","COM01","com7","COM7 & calc","COM7,dsr=on","COM7\""}) {
        check(!com0com::BuildAdminCreateCommand(L"C:\\Program Files\\com0com\\setupc.exe",name,"COM8",command,error)&&command.file.empty()&&command.parameters.empty(),"command injection denied atomically");
    }
    check(!com0com::BuildAdminCreateCommand(L"C:\\Windows\\System32\\cmd.exe","COM7","COM8",command,error),"wrong administrator executable basename denied");
    check(!com0com::BuildAdminCreateCommand(L"setupc.exe","COM7","COM8",command,error),"administrator PATH lookup denied");
    check(!com0com::BuildAdminCreateCommand(L"C:\\x\\setupc.exe","COM7","COM7",command,error),"administrator same endpoints denied");
    std::puts("PASS administrator command builder: fixed runas/file/parameters; never executed");

    // The fake executable is the only executable the test runner passes here.
    if (argc>1) {
        SECURITY_ATTRIBUTES eventAttributes={sizeof(eventAttributes),nullptr,TRUE};
        HANDLE sentinel=CreateEventW(&eventAttributes,TRUE,FALSE,nullptr);
        check(sentinel!=nullptr,"inheritable sentinel created");
        const std::wstring sentinelValue=std::to_wstring(reinterpret_cast<ULONG_PTR>(sentinel));
        SetEnvironmentVariableW(L"SR5000_FAKE_SENTINEL",sentinelValue.c_str());
        auto cap=com0com::CaptureInventory(argv[1],2000,65536);
        SetEnvironmentVariableW(L"SR5000_FAKE_SENTINEL",nullptr);
        check(WaitForSingleObject(sentinel,0)==WAIT_TIMEOUT,"unrelated inheritable event is not inherited");
        CloseHandle(sentinel);
        if (cap.status!=com0com::CaptureStatus::Ok) std::fprintf(stderr,"capture status=%d win32=%lu error=%s output=%s\n",static_cast<int>(cap.status),cap.win32Error,cap.error.c_str(),cap.output.c_str());
        check(cap.status==com0com::CaptureStatus::Ok&&com0com::ValidatePair(cap.inventory,"COM7","COM8").ok(),"exact fake setupc capture");
        std::puts("PASS read-only capture: fake setupc fixed argv and pair mappings");
        check(cap.output.find("CNCA0 PortName=COM7,EmuOverrun=yes")!=std::string::npos,"captured source grammar sample");
        check(com0com::CaptureInventory(L"setupc.exe").status==com0com::CaptureStatus::InvalidInput,"reject PATH lookup");
        check(com0com::CaptureInventory(L"C:\\x.exe\" & calc").status==com0com::CaptureStatus::InvalidInput,"reject quoted executable injection");
        check(com0com::CaptureInventory(argv[1],0).status==com0com::CaptureStatus::InvalidInput,"reject unbounded wait");
        check(com0com::CaptureInventory(argv[1],30001).status==com0com::CaptureStatus::InvalidInput,"reject excessive wait");
        std::wstring nulpath=argv[1];nulpath.push_back(0);nulpath+=L".exe";
        check(com0com::CaptureInventory(nulpath).status==com0com::CaptureStatus::InvalidInput,"embedded NUL executable denied");
        SetEnvironmentVariableW(L"SR5000_FAKE_SETUPC_MODE",L"exit");
        cap=com0com::CaptureInventory(argv[1],2000,65536);
        check(cap.status==com0com::CaptureStatus::NonzeroExit&&cap.exitCode==23&&cap.inventory.endpoints.empty(),"nonzero exit cannot verify inventory");
        SetEnvironmentVariableW(L"SR5000_FAKE_SETUPC_MODE",L"malformed");
        cap=com0com::CaptureInventory(argv[1],2000,65536);
        check(cap.status==com0com::CaptureStatus::ParseError&&cap.inventory.endpoints.empty(),"diagnostic stdout cannot verify inventory");
        SetEnvironmentVariableW(L"SR5000_FAKE_SETUPC_MODE",L"flood");
        cap=com0com::CaptureInventory(argv[1],2000,4096);
        check(cap.status==com0com::CaptureStatus::OutputLimit&&cap.output.size()<=4096&&cap.inventory.endpoints.empty(),"bounded pipe output");
        SetEnvironmentVariableW(L"SR5000_FAKE_SETUPC_MODE",L"sleep");
        ULONGLONG start=GetTickCount64();
        cap=com0com::CaptureInventory(argv[1],150,65536);
        check(cap.status==com0com::CaptureStatus::Timeout&&GetTickCount64()-start<1500&&cap.inventory.endpoints.empty(),"bounded wait kills only fake list job");
        SetEnvironmentVariableW(L"SR5000_FAKE_SETUPC_MODE",nullptr);
        DWORD before=0,after=0;
        GetProcessHandleCount(GetCurrentProcess(),&before);
        for (int i=0;i<12;++i) check(com0com::CaptureInventory(argv[1],2000,65536).status==com0com::CaptureStatus::Ok,"repeated capture");
        GetProcessHandleCount(GetCurrentProcess(),&after);
        check(after<=before+1,"no accumulating process/pipe/job handle leaks");
        std::puts("PASS capture bounds: nonzero, malformed, flood, timeout, handles and shell metacharacters in path");

    }
    std::printf("PASS %d checks\n",checks);
}
