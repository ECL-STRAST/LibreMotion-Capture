#include "windows/WinHttpTransport.h"
#include "platform/Log.h"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace lmc {

std::vector<std::pair<std::string, std::string>> buildSupabaseHeaders(const std::string& apiKey) {
    return { {"Content-Type", "application/json"},
             {"apikey", apiKey},
             {"Authorization", std::string("Bearer ") + apiKey},
             {"Prefer", "return=representation"} };
}

#ifdef _WIN32

static std::wstring toWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out;
    out.resize((size_t)wideLen);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], wideLen);
    return out;
}

bool WinHttpTransport::postJson(const std::string& url,
                                const std::string& body,
                                const std::vector<std::pair<std::string, std::string>>& headers) {
    // 1) Crack the URL into host / path / port / scheme.
    const std::wstring wUrl = toWide(url);
    wchar_t hostBuf[256];
    wchar_t pathBuf[2048];
    URL_COMPONENTS uc;
    ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = hostBuf;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = pathBuf;
    uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &uc)) {
        Log::err("WinHttpCrackUrl failed url=%.200s err=%lu", url.c_str(), GetLastError());
        return false;
    }
    if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS) {
        Log::err("unsupported URL scheme %lu url=%.200s", (unsigned long)uc.nScheme, url.c_str());
        return false;
    }

    // 2) Session with system default proxy settings.
    HINTERNET hSession = WinHttpOpen(L"telemetria/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        Log::err("WinHttpOpen failed err=%lu", GetLastError());
        return false;
    }
    // resolve / connect / send / receive timeouts (ms): connect 15 s, read 30 s (matches AyudanteHttp).
    WinHttpSetTimeouts(hSession, 15000, 15000, 30000, 30000);

    // 3) Connect.
    HINTERNET hConnect = WinHttpConnect(hSession, uc.lpszHostName, uc.nPort, 0);
    if (!hConnect) {
        Log::err("WinHttpConnect failed err=%lu", GetLastError());
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 4) POST request; TLS when the URL scheme was https.
    const wchar_t* path = (uc.dwUrlPathLength == 0) ? L"/" : uc.lpszUrlPath;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"POST", path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        Log::err("WinHttpOpenRequest failed err=%lu", GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    bool ok = false;
    bool headersOk = true;

    // 5) One call per header, "Key: Value\r\n".
    for (size_t i = 0; i < headers.size(); ++i) {
        const std::wstring wLine = toWide(headers[i].first + ": " + headers[i].second + "\r\n");
        if (!WinHttpAddRequestHeaders(hRequest, wLine.c_str(), (DWORD)wLine.length(), WINHTTP_ADDREQ_FLAG_ADD)) {
            Log::err("WinHttpAddRequestHeaders failed header=%.80s err=%lu",
                     headers[i].first.c_str(), GetLastError());
            headersOk = false;
            break;
        }
    }

    // 6) Send the body and wait for the response.
    if (headersOk) {
        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               const_cast<char*>(body.data()), (DWORD)body.size(), (DWORD)body.size(), 0) &&
            WinHttpReceiveResponse(hRequest, nullptr)) {
            // 7) Status code, true only on 2xx.
            DWORD statusCode = 0;
            DWORD size = sizeof(statusCode);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size,
                                    WINHTTP_NO_HEADER_INDEX)) {
                ok = (statusCode >= 200 && statusCode < 300);
                if (!ok) Log::err("postJson: HTTP status %lu url=%.200s", (unsigned long)statusCode, url.c_str());
            } else {
                Log::err("WinHttpQueryHeaders failed err=%lu", GetLastError());
            }
        } else {
            Log::err("WinHttpSendRequest/ReceiveResponse failed err=%lu", GetLastError());
        }
    }

    // 8) Close every handle on every exit path.
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

#endif // _WIN32

} // namespace lmc
