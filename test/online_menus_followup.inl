// Coordinator review regressions; included in the owned-PE fixture namespace.
std::filesystem::path recent_file() { return root/L"local/XML2 Fix/online/last-game.txt"; }
std::string disk_recent() {
    std::ifstream file(recent_file(),std::ios::binary);
    return {(std::istreambuf_iterator<char>(file)),{}};
}
void follow_prepare() {
    if(mode=="follow-lifecycle" || mode.starts_with("follow-storage-")) {
        std::filesystem::create_directories(recent_file().parent_path());
        auto bytes=encode_recent({"23456789","Previous fixture",123});
        if(mode=="follow-storage-directory") {
            std::filesystem::create_directory(recent_file());
            std::ofstream(recent_file()/"owned-marker")<<"fixture";return;
        }
        if(mode=="follow-storage-128" || mode=="follow-storage-129") bytes.resize(mode.ends_with("128") ? 128 : 129,'\0');
        if(mode=="follow-storage-crlf") {
            std::string crlf;for(char c:bytes) { if(c=='\n') crlf+='\r';crlf+=c; }bytes=crlf;
        }
        std::ofstream file(recent_file(),std::ios::binary);file.write(bytes.data(),bytes.size());file.close();
        if(mode=="follow-storage-readonly") assert(SetFileAttributesW(recent_file().c_str(),FILE_ATTRIBUTE_READONLY));
    }
}
BOOL (WINAPI* real_write_file)(HANDLE,LPCVOID,DWORD,LPDWORD,LPOVERLAPPED)=&WriteFile;
BOOL WINAPI partial_recent_write(HANDLE file,LPCVOID data,DWORD size,LPDWORD written,LPOVERLAPPED overlapped) {
    assert(size>1);return real_write_file(file,data,size-1,written,overlapped);
}
unsigned recent_flush_calls=0;
BOOL WINAPI failed_recent_flush(HANDLE) { ++recent_flush_calls;SetLastError(ERROR_WRITE_FAULT);return FALSE; }
DWORD WINAPI foreign_keyboard(LPVOID) { std::array<unsigned char,256> keys{};online_menus::keyboard(keys.data());return 0; }
void follow_tests(const script_functions_rules::func_entry* registration) {
    auto tick=*reinterpret_cast<void(__fastcall**)(void*,void*)>(0x69e454);
    auto deactivate=*reinterpret_cast<void(__fastcall**)(void*,void*)>(0x69e438);
    auto request=*reinterpret_cast<void(__fastcall**)(void*,void*,const char*,const char*)>(0x6a23d8);
    const auto queue_target=reinterpret_cast<void*>(0x5bc3d5+*reinterpret_cast<unsigned*>(0x5bc3d1));
    auto post=[&](intent action) { int_value[1]=unsigned(action);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(nullptr); };
    auto open_code=[&]() { post(intent::code);tick(hub.data(),nullptr);set_current(child.data());tick(child.data(),nullptr); };
    auto hub_again=[&]() { deactivate(child.data(),nullptr);set_current(hub.data());tick(hub.data(),nullptr); };
    auto fill_code=[&]() { for(unsigned i=0;i<8;++i) { int_value[1]=32+i;reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(nullptr);tick(child.data(),nullptr); }tick(child.data(),nullptr); };
    if(mode=="follow-lifecycle") {
        post(intent::rejoin);tick(hub.data(),nullptr);const auto requests=calls.size();
        tick(hub.data(),nullptr);tick(hub.data(),nullptr);assert(calls.size()==requests);
        set_current(child.data());tick(child.data(),nullptr);assert(texts["code_value"]=="2345-6789");hub_again();
        for(auto action:{intent::host,intent::back}) {
            post(intent::edit_name);tick(hub.data(),nullptr);assert(native_tag==0);
            if(action==intent::host) result("NeverCommit");
            const auto consumed=consumes;
            post(action);tick(hub.data(),nullptr);
            assert(native_tag==4 && consumes==consumed+(action==intent::host ? 1 : 0));
            assert(calls.back()==(action==intent::host ? "request:lc_host" : "request:main"));
            set_current(hub.data());result("LateName");tick(hub.data(),nullptr);assert(texts["local_name"]=="100??????openmenu bad");native_ready=false;native_tag=4;
        }
        std::cout<<"follow lifecycle PASS Rejoin survives two queued Hub ticks; Host/Back drain queued name; late name cannot commit\n";return;
    }
    if(mode=="follow-name-faults") {
        for(int stage:{1,3,4,5,6,7,12,13}) {
            result("stale");name_fault_stage=stage;post(intent::edit_name);tick(hub.data(),nullptr);
            assert(name_fault_stage==0 && !visibility["lc_notice_name_open"]);
            post(intent::edit_name);tick(hub.data(),nullptr);assert(native_tag==0);post(intent::back);tick(hub.data(),nullptr);set_current(hub.data());
        }
        for(int invalid:{1,2,3}) {
            name_bad_object=invalid==3 ? 0 : invalid;if(invalid==3) put(reinterpret_cast<void*>(0xa3e168),0,0);
            post(intent::edit_name);tick(hub.data(),nullptr);assert(!visibility["lc_notice_name_open"]);
            name_bad_object=0;put(reinterpret_cast<void*>(0xa3e168),0,0x6a4730);
        }
        for(int stage:{1,2,7,8,9,10,11,13}) {
            post(intent::edit_name);tick(hub.data(),nullptr);deactivate(hub.data(),nullptr);set_current(hub.data());result("ChangedName");
            name_fault_stage=stage;tick(hub.data(),nullptr);
            assert(!name_fault_stage && !visibility["lc_notice_name_open"] && texts["local_name"]=="100??????openmenu bad");
        }
        post(intent::edit_name);tick(hub.data(),nullptr);name_fault_stage=2;post(intent::host);tick(hub.data(),nullptr);
        set_current(hub.data());tick(hub.data(),nullptr);assert(!visibility["lc_notice_name_open"]);
        std::cout<<"follow name PASS SEH getter/ready/tag/seed/title/max/consume/null-result/request/save faults, validated singleton, bounded abandon and recovery\n";return;
    }
    if(mode=="follow-keyboard") {
        std::array<unsigned char,256> keys{};online_menus::keyboard(keys.data());
        const auto reads=manager_reads;put(reinterpret_cast<void*>(0x8aff18),0,0);keys[0x1e]=128;online_menus::keyboard(keys.data());
        assert(manager_reads==reads && !*reinterpret_cast<void**>(0x8aff18));
        put(reinterpret_cast<void*>(0x8aff18),0,1);online_menus::keyboard(keys.data());assert(manager_reads==reads);
        put(reinterpret_cast<void*>(0x8aff18),0,unsigned(reinterpret_cast<std::uintptr_t>(mgr.data())));
        set_current(reinterpret_cast<void*>(1));online_menus::keyboard(keys.data());assert(manager_reads==reads);set_current(hub.data());
        open_code();keys.fill(0);online_menus::keyboard(keys.data());
        keys[0x1e]=keys[0x30]=128;online_menus::keyboard(keys.data());tick(child.data(),nullptr);assert(texts["code_value"]=="AB__-____");
        keys.fill(0);online_menus::keyboard(keys.data());keys[0x2e]=keys[0x18]=128;online_menus::keyboard(keys.data());tick(child.data(),nullptr);
        assert(texts["code_value"]=="ABC_-____" && visibility["lc_notice_code_invalid"]);
        keys.fill(0);online_menus::keyboard(keys.data());keys[0x38]=keys[0x20]=128;online_menus::keyboard(keys.data());tick(child.data(),nullptr);assert(texts["code_value"]=="ABC_-____");
        keys.fill(0);online_menus::keyboard(keys.data());keys[0x50]=keys[0x49]=128;online_menus::keyboard(keys.data());tick(child.data(),nullptr);assert(texts["code_value"]=="ABC9-2___");
        keys.fill(0);online_menus::keyboard(keys.data());keys[0x12]=128;online_menus::keyboard(keys.data());
        keys[0x21]=128;online_menus::keyboard(keys.data());tick(child.data(),nullptr);assert(texts["code_value"]=="ABC9-2EF_");
        const auto before_worker=manager_reads;
        auto thread=CreateThread(nullptr,0,foreign_keyboard,nullptr,0,nullptr);assert(thread && WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);CloseHandle(thread);
        assert(manager_reads==before_worker && once_logs["online:thread-agreement"].find("matched")!=std::string::npos && once_logs["online:thread-refused"].find("refused outside game thread")!=std::string::npos);
        const auto before=manager_reads;tick(child.data(),nullptr);assert(texts["code_value"]=="ABC9-2EF_" && manager_reads>=before);
        keys.fill(0);online_menus::keyboard(keys.data());keys[0x23]=keys[0x1c]=128;online_menus::keyboard(keys.data());
        assert(keys[0x1c]==0);tick(child.data(),nullptr);assert(texts["code_value"]=="ABC9-2EFH");
        keys.fill(0);online_menus::keyboard(keys.data());keys[0x23]=128;online_menus::keyboard(keys.data());deactivate(child.data(),nullptr);set_current(child.data());tick(child.data(),nullptr);
        assert(texts["code_value"]=="____-____");
        std::cout<<"follow keyboard PASS null manager never calls lazy getter; ordered multi-edge/valid-over-invalid/Alt/numpad; game-thread refusal; close invalidation\n";return;
    }
    if(mode=="follow-queue") {
        for(auto* menu:{main_menu.data(),hub.data()}) {
            set_current(menu);if(menu==hub.data()) hub[4]=0;
            const auto before=calls.size();invoke_usecmd(queue_target,nullptr,"runscript lcOnlineIntent(2)");
            assert(calls.size()==before+1 && calls.back()=="queue:runscript lcOnlineIntent(2)");
        }
        hub[4]=1;set_current(hub.data());const auto before=calls.size();
        invoke_usecmd(queue_target,nullptr,"runscript lcOnlineIntent(15)");assert(calls.size()==before+1 && calls.back()=="queue:runscript lcOnlineIntent(15)");
        int_value[1]=15;reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(nullptr);tick(hub.data(),nullptr);assert(visibility["lc_notice_code_invalid"]);
        set_current(main_menu.data());request(mgr.data(),nullptr,"online",nullptr);assert(!visibility["lc_online_continue"]);
        request(mgr.data(),nullptr,"options",nullptr);assert(visibility["lc_online_continue"]);
        std::cout<<"follow queue PASS native forwarding with no custom/active source; 15 unpublished and handler-refused; stale main hint dismissed\n";return;
    }
    if(mode=="follow-clipboard") {
        open_code();fake_clipboard_imports();
        clipboard_text("ABCDEFGH");clip_missing=true;post(intent::paste);tick(child.data(),nullptr);assert(clip_closed==1 && !clip_locked && texts["code_value"]=="____-____");
        clipboard_text("ABCDEFGH");clip_zero_size=true;post(intent::paste);tick(child.data(),nullptr);assert(clip_closed==1 && !clip_locked);
        clipboard_text(std::string(248,' ')+"ABCDEFGH");post(intent::paste);tick(child.data(),nullptr);assert(texts["code_value"]=="ABCD-EFGH" && clip_unlocked==1);
        post(intent::erase);tick(child.data(),nullptr);
        clipboard_text(std::string("ABCDEFGH\0junk",13));post(intent::paste);tick(child.data(),nullptr);
        assert(texts["code_value"]=="ABCD-EFG_" && clip_unlocked==1 && !visibility["lc_notice_paste_invalid"]);
        clipboard_text(std::string("ABC\0DEFGH",9));post(intent::paste);tick(child.data(),nullptr);assert(texts["code_value"]=="ABCD-EFG_");
        std::cout<<"follow clipboard PASS NULL handle/zero size, exactly 256 characters with whitespace, embedded NUL suffix rejection, balanced release\n";return;
    }
    if(mode.starts_with("follow-storage-")) {
        if(mode=="follow-storage-128" || mode=="follow-storage-129" || mode=="follow-storage-crlf") {
            assert(!enabled_items["rejoin"]);const auto before=calls.size();post(intent::rejoin);tick(hub.data(),nullptr);assert(calls.size()==before);
            std::cout<<"follow storage PASS bounded read/CRLF refusal "<<mode<<"\n";return;
        }
        const auto before=mode=="follow-storage-directory" ? std::string() : disk_recent();
        if(mode=="follow-storage-partial") import_stub("WriteFile",reinterpret_cast<void*>(&partial_recent_write));
        if(mode=="follow-storage-flush") import_stub("FlushFileBuffers",reinterpret_cast<void*>(&failed_recent_flush));
        open_code();fill_code();
        const bool success=mode=="follow-storage-ok" || mode=="follow-storage-flush";
        if(mode=="follow-storage-flush") assert(recent_flush_calls==0); // no synchronous durability flush on the game thread
        assert(visibility["lc_notice_recent_save"]==success && visibility["lc_notice_fixture"]!=success);
        if(mode=="follow-storage-directory") assert(std::filesystem::is_directory(recent_file()));
        else if(success) assert(decode_recent(disk_recent())->code=="ABCDEFGH");
        else assert(disk_recent()==before);
        hub_again();
        if(mode=="follow-storage-directory") assert(!enabled_items["rejoin"]);
        else assert(enabled_items["rejoin"] && texts["last_code"].starts_with(success ? "Code ABCD-EFGH" : "Code 2345-6789"));
        for(const auto& entry:std::filesystem::directory_iterator(recent_file().parent_path())) assert(entry.path().filename().wstring().find(L".tmp.")==std::wstring::npos);
        if(mode=="follow-storage-readonly") assert(SetFileAttributesW(recent_file().c_str(),FILE_ATTRIBUTE_NORMAL));
        std::cout<<"follow storage PASS atomic checked commit/publish and temp cleanup "<<mode<<"\n";return;
    }
    assert(false && "unknown coordinator follow-up mode");
}
