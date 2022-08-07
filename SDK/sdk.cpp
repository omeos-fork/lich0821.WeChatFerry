﻿#include "Shlwapi.h"
#include "framework.h"
#include <process.h>
#include <queue>
#include <stdio.h>
#include <stdlib.h>
#include <tlhelp32.h>
#include <vector>

#include "injector.h"
#include "rpc_client.h"
#include "sdk.h"
#include "util.h"

std::function<int(WxMessage_t)> g_cbReceiveTextMsg;

int WxInitSDK()
{
    unsigned long ulCode    = 0;
    DWORD status            = 0;
    DWORD pid               = 0;
    WCHAR DllPath[MAX_PATH] = { 0 };

    GetModuleFileName(GetModuleHandle(WECHATSDKDLL), DllPath, MAX_PATH);
    PathRemoveFileSpec(DllPath);
    PathAppend(DllPath, WECHATINJECTDLL);

    if (!PathFileExists(DllPath)) {
        return ERROR_FILE_NOT_FOUND;
    }

    status = OpenWeChat(&pid);
    if (status != 0) {
        return status;
    }
    Sleep(2000); // 等待微信打开
    if (!InjectDll(pid, DllPath)) {
        return -1;
    }

    RpcConnectServer();

    while (!RpcIsLogin()) {
        Sleep(1000);
    }

    return ERROR_SUCCESS;
}

int WxSetTextMsgCb(const std::function<int(WxMessage_t)> &onMsg)
{
    if (onMsg) {
        HANDLE msgThread;
        g_cbReceiveTextMsg = onMsg;

        msgThread = (HANDLE)_beginthreadex(NULL, 0, RpcSetTextMsgCb, NULL, 0, NULL);
        if (msgThread == NULL) {
            printf("Failed to create innerWxRecvTextMsg.\n");
            return -2;
        }
        CloseHandle(msgThread);

        return 0;
    }

    printf("Empty Callback.\n");
    return -1;
}

int WxSendTextMsg(wstring wxid, wstring at_wxid, wstring msg)
{
    return RpcSendTextMsg(wxid.c_str(), at_wxid.c_str(), msg.c_str());
}

int WxSendImageMsg(wstring wxid, wstring path) { return RpcSendImageMsg(wxid.c_str(), path.c_str()); }

static int getAddrHandle(DWORD *addr, HANDLE *handle)
{
    DWORD processID     = 0;
    wstring processName = L"WeChat.exe";
    wstring moduleName  = L"WeChatWin.dll";

    HANDLE hSnapshot    = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe32 = { sizeof(PROCESSENTRY32) };
    while (Process32Next(hSnapshot, &pe32)) {
        wstring strProcess = pe32.szExeFile;
        if (strProcess == processName) {
            processID = pe32.th32ProcessID;
            break;
        }
    }
    CloseHandle(hSnapshot);
    if (processID == 0) {
        printf("Failed to get Process ID\r\n");
        return -1;
    }

    HANDLE hProcessSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processID);
    if (hProcessSnapshot == INVALID_HANDLE_VALUE) {
        printf("Failed to get Process Snapshot\r\n");
        return -2;
    }

    MODULEENTRY32 me32;
    SecureZeroMemory(&me32, sizeof(MODULEENTRY32));
    me32.dwSize = sizeof(MODULEENTRY32);
    while (Module32Next(hProcessSnapshot, &me32)) {
        me32.dwSize = sizeof(MODULEENTRY32);

        if (!wcscmp(me32.szModule, moduleName.c_str())) {
            *addr = (DWORD)me32.modBaseAddr;
            break;
        }
    }

    CloseHandle(hProcessSnapshot);
    if (*addr == 0) {
        printf("Failed to get Module Address\r\n");
        return -3;
    }

    *handle = OpenProcess(PROCESS_VM_READ, FALSE, processID);
    if (*handle == 0) {
        printf("Failed to open Process\r\n");
        return -4;
    }

    return 0;
}

MsgTypesMap_t WxGetMsgTypes()
{
    static MsgTypesMap_t WxMsgTypes;
    if (WxMsgTypes.empty()) {
        int size            = 0;
        PPRpcIntBstrPair pp = RpcGetMsgTypes(&size);
        for (int i = 0; i < size; i++) {
            WxMsgTypes.insert(make_pair(pp[i]->key, GetWstringFromBstr(pp[i]->value)));
            midl_user_free(pp[i]);
        }
        if (pp) {
            midl_user_free(pp);
        }
    }

    return WxMsgTypes;
}

ContactMap_t WxGetContacts()
{
    ContactMap_t mContact;
    int size        = 0;
    PPRpcContact pp = RpcGetContacts(&size);
    for (int i = 0; i < size; i++) {
        WxContact_t contact;
        contact.wxId       = GetWstringFromBstr(pp[i]->wxId);
        contact.wxCode     = GetWstringFromBstr(pp[i]->wxCode);
        contact.wxName     = GetWstringFromBstr(pp[i]->wxName);
        contact.wxCountry  = GetWstringFromBstr(pp[i]->wxCountry);
        contact.wxProvince = GetWstringFromBstr(pp[i]->wxProvince);
        contact.wxCity     = GetWstringFromBstr(pp[i]->wxCity);
        contact.wxGender   = GetWstringFromBstr(pp[i]->wxGender);

        mContact.insert(make_pair(contact.wxId, contact));
        midl_user_free(pp[i]);
    }

    if (pp) {
        midl_user_free(pp);
    }

    return mContact;
}
