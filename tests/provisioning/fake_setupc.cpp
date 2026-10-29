#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
int main(int argc,char** argv) {
    if (argc!=4 || std::strcmp(argv[1],"--silent") || std::strcmp(argv[2],"--detail-prms") || std::strcmp(argv[3],"list")) {
        std::fprintf(stderr,"FAIL unsafe argv");return 90;
    }
    char sentinel[64]={};
    if (GetEnvironmentVariableA("SR5000_FAKE_SENTINEL",sentinel,sizeof(sentinel))) {
        HANDLE h=reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(std::strtoull(sentinel,nullptr,10)));
        // The parent uses an event. Reuse of the numeric value for a different
        // child handle cannot make SetEvent succeed.
        if (SetEvent(h)) return 91;
    }
    char mode[64]={}; GetEnvironmentVariableA("SR5000_FAKE_SETUPC_MODE",mode,sizeof(mode));
    if (!std::strcmp(mode,"exit")) return 23;
    if (!std::strcmp(mode,"malformed")) { std::puts("DIALOG: failed"); return 0; }
    if (!std::strcmp(mode,"sleep")) { Sleep(10000); return 0; }
    if (!std::strcmp(mode,"flood")) { for (int i=0;i<10000;++i) std::puts("0123456789abcdef0123456789abcdef"); return 0; }
    std::printf(" CNCA0 PortName=COM7,EmuOverrun=yes\n CNCB0 PortName=COM8,dsr=ropen,EmuBR=yes\n");
    return 0;
}
