// Socket-free handshake ownership regression. The real client executes with
// synthetic SSPI/socket imports; each context owns a real kernel event handle.
namespace tls_lifetime {
enum class ending { send_failure, receive_eof, context_failure, no_context, query_failure, incomplete, success };
constexpr const char* pin="5accba244809bb6c9a2c2f5bdb4e5d30fb5ec737d59bf87a8fc9031d81cf8a5a";
ending how=ending::send_failure;
unsigned made=0,deleted=0,acquired=0,released=0,initializes=0,receives=0;
std::vector<HANDLE> outstanding;
unsigned char token=0x16;
SECURITY_STATUS SEC_ENTRY acquire(SEC_CHAR*,SEC_CHAR*,unsigned long,void*,void*,SEC_GET_KEY_FN,void*,PCredHandle out,PTimeStamp) {
    ++acquired;out->dwLower=123;out->dwUpper=456;return SEC_E_OK;
}
SECURITY_STATUS SEC_ENTRY release(PCredHandle) { ++released;return SEC_E_OK; }
SECURITY_STATUS SEC_ENTRY initialize(PCredHandle,PCtxtHandle old,SEC_CHAR*,unsigned long,unsigned long,unsigned long,
    PSecBufferDesc,unsigned long,PCtxtHandle out,PSecBufferDesc output,unsigned long*,PTimeStamp) {
    ++initializes;
    if(how==ending::no_context) return SEC_E_INTERNAL_ERROR;
    if(!old) {
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(event);
        outstanding.push_back(event);++made;out->dwLower=reinterpret_cast<ULONG_PTR>(event);out->dwUpper=789;
    }
    output->pBuffers[0].pvBuffer=&token;output->pBuffers[0].cbBuffer=1;
    if(how==ending::context_failure) return SEC_E_INTERNAL_ERROR;
    if(how==ending::query_failure || how==ending::success) return SEC_E_OK;
    if(how==ending::incomplete && old) return SEC_E_INCOMPLETE_MESSAGE;
    return SEC_I_CONTINUE_NEEDED;
}
SECURITY_STATUS SEC_ENTRY destroy(PCtxtHandle context) {
    auto event=reinterpret_cast<HANDLE>(context->dwLower);
    const auto it=std::find(outstanding.begin(),outstanding.end(),event);assert(it!=outstanding.end());
    assert(CloseHandle(event));outstanding.erase(it);++deleted;return SEC_E_OK;
}
SECURITY_STATUS SEC_ENTRY free_buffer(void* p) { assert(p==&token);return SEC_E_OK; }
SECURITY_STATUS SEC_ENTRY query(PCtxtHandle,unsigned long attribute,void* out) {
    if(how!=ending::success) return SEC_E_INTERNAL_ERROR;
    if(attribute==SECPKG_ATTR_REMOTE_CERT_CONTEXT) {
        auto certificate=CertCreateCertificateContext(X509_ASN_ENCODING|PKCS_7_ASN_ENCODING,certificate_der,sizeof(certificate_der));assert(certificate);
        *static_cast<PCCERT_CONTEXT*>(out)=certificate;return SEC_E_OK;
    }
    assert(attribute==SECPKG_ATTR_STREAM_SIZES);
    *static_cast<SecPkgContext_StreamSizes*>(out)={1,1,256,1,1};return SEC_E_OK;
}
int WSAAPI send_bytes(SOCKET,const char*,int size,int) { return how==ending::send_failure ? SOCKET_ERROR : size; }
int WSAAPI receive_bytes(SOCKET,char* out,int,int) {
    if(how==ending::incomplete && receives++==0) { *out=0;return 1; }return 0;
}
int WSAAPI ioctl_socket(SOCKET,long,u_long*) { return 0; }
int WSAAPI socket_option(SOCKET,int,int,const char*,int) { return 0; }

void replace_import(const char* name,const void* replacement) {
    auto* base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    auto* directory=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for(;directory->Name;++directory) {
        auto* names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+directory->OriginalFirstThunk);
        auto* slots=reinterpret_cast<IMAGE_THUNK_DATA*>(base+directory->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots) {
            bool matches=false;
            if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
                const auto module=GetModuleHandleA(reinterpret_cast<const char*>(base+directory->Name));
                matches=module && slots->u1.Function==reinterpret_cast<ULONG_PTR>(GetProcAddress(module,name));
            } else {
                auto* entry=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
                matches=std::strcmp(reinterpret_cast<const char*>(entry->Name),name)==0;
            }
            if(!matches) continue;
            DWORD old=0;assert(VirtualProtect(&slots->u1.Function,sizeof(slots->u1.Function),PAGE_READWRITE,&old));
            slots->u1.Function=reinterpret_cast<ULONG_PTR>(replacement);
            DWORD ignored=0;assert(VirtualProtect(&slots->u1.Function,sizeof(slots->u1.Function),old,&ignored));return;
        }
    }
    std::printf("missing handshake import: %s\n",name);std::fflush(stdout);
    assert(false && "missing handshake import");
}
int run() {
    SetPriorityClass(GetCurrentProcess(),IDLE_PRIORITY_CLASS);
    // Warm only local certificate hashing, never a chain/hostname lookup.
    auto certificate=CertCreateCertificateContext(X509_ASN_ENCODING|PKCS_7_ASN_ENCODING,certificate_der,sizeof(certificate_der));assert(certificate);
    std::string trust_error;assert(broker_tls::verify_certificate(certificate,L"broker.invalid",pin,trust_error));CertFreeCertificateContext(certificate);
    replace_import("AcquireCredentialsHandleA",reinterpret_cast<const void*>(&acquire));
    replace_import("FreeCredentialsHandle",reinterpret_cast<const void*>(&release));
    replace_import("InitializeSecurityContextA",reinterpret_cast<const void*>(&initialize));
    replace_import("DeleteSecurityContext",reinterpret_cast<const void*>(&destroy));
    replace_import("FreeContextBuffer",reinterpret_cast<const void*>(&free_buffer));
    replace_import("QueryContextAttributesA",reinterpret_cast<const void*>(&query));
    replace_import("send",reinterpret_cast<const void*>(&send_bytes));
    replace_import("recv",reinterpret_cast<const void*>(&receive_bytes));
    replace_import("ioctlsocket",reinterpret_cast<const void*>(&ioctl_socket));
    replace_import("setsockopt",reinterpret_cast<const void*>(&socket_option));
    DWORD before=0,after=0;assert(GetProcessHandleCount(GetCurrentProcess(),&before));
    for(int cycle=0;cycle<100;++cycle) {
        // Reuse a client, then destroy it: reset and the destructor are both idempotent.
        {
        broker_tls::client client;
        for(const auto end:{ending::send_failure,ending::receive_eof,ending::context_failure,ending::no_context,ending::query_failure,ending::incomplete}) {
            how=end;initializes=receives=0;std::string error;
            assert(!client.handshake(SOCKET(42),"broker.invalid",std::string(64,'0'),error));
            assert(!error.empty());
            if(!outstanding.empty()) {
                std::printf("FAIL TLS context ownership: cycle %d exit %d made %u deleted %u outstanding %zu\n",cycle,int(end),made,deleted,outstanding.size());
                for(const auto handle:outstanding) CloseHandle(handle);return 1;
            }
            assert(acquired==released && made==deleted);
        }
        how=ending::success;initializes=receives=0;std::string error;
        assert(client.handshake(SOCKET(42),"broker.invalid",pin,error));
        assert(outstanding.size()==1 && made==deleted+1 && acquired==released+1);
        if(cycle%2) {
            how=ending::no_context;
            assert(!client.handshake(SOCKET(42),"broker.invalid",pin,error));
            assert(outstanding.empty() && made==deleted && acquired==released);
        }
        }
        assert(outstanding.empty() && made==deleted && acquired==released);
    }
    assert(GetProcessHandleCount(GetCurrentProcess(),&after));
    std::printf("TLS context ownership: contexts %u deleted %u credentials %u released %u handles %lu -> %lu\n",made,deleted,acquired,released,before,after);
    assert(before==after);
    std::puts("PASS 750 socket-free handshakes, 600 contexts released once, success/reuse/destruction, no handle growth");return 0;
}
}
