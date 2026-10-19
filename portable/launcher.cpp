// Aeonis portátil em arquivo único: extrai o ZIP embutido para uma pasta temporária própria,
// abre o Aeonis.exe e apaga a pasta quando ele fecha. Configurações e vídeos ficam nos lugares de
// sempre (%LOCALAPPDATA%\Aeonis e Vídeos\Aeonis).

#include <windows.h>
#include <objbase.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static DWORD run(const fs::path &exe, const std::wstring &args, const fs::path &cwd, bool hidden)
{
    std::wstring cmd = L"\"" + exe.wstring() + L"\" " + args;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, hidden ? CREATE_NO_WINDOW : 0, nullptr,
                        cwd.c_str(), &startup, &info))
        throw std::runtime_error("Não foi possível iniciar o Aeonis.");
    CloseHandle(info.hThread);
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    return code;
}

int WINAPI wWinMain(HINSTANCE module, HINSTANCE, PWSTR arguments, int)
{
    fs::path root;
    int result = 1;
    try {
        wchar_t temp[MAX_PATH + 1]{};
        if (!GetTempPathW(MAX_PATH, temp))
            throw std::runtime_error("Pasta temporária indisponível.");
        GUID id{};
        if (FAILED(CoCreateGuid(&id)))
            throw std::runtime_error("Não foi possível criar a sessão portátil.");
        wchar_t guid[40]{};
        StringFromGUID2(id, guid, 40);
        root = fs::path(temp) / (std::wstring(L"Aeonis-") + guid);
        if (!fs::create_directory(root))
            throw std::runtime_error("Não foi possível criar a pasta portátil.");
        const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(101), RT_RCDATA);
        const DWORD size = resource ? SizeofResource(module, resource) : 0;
        const void *data = resource ? LockResource(LoadResource(module, resource)) : nullptr;
        if (!data || !size)
            throw std::runtime_error("Pacote portátil inválido.");
        {
            std::ofstream zip(root / L"payload.zip", std::ios::binary);
            zip.write(static_cast<const char *>(data), size);
            if (!zip)
                throw std::runtime_error("Não foi possível extrair o pacote portátil.");
        }
        // Nenhum caminho controlado pelo usuário entra no código do PowerShell.
        {
            std::ofstream script(root / L"expand.ps1");
            script << "$ErrorActionPreference='Stop'\n"
                      "Expand-Archive -LiteralPath (Join-Path $PSScriptRoot 'payload.zip') -DestinationPath $PSScriptRoot\n";
        }
        wchar_t system[MAX_PATH + 1]{};
        GetSystemDirectoryW(system, MAX_PATH);
        if (run(fs::path(system) / L"WindowsPowerShell/v1.0/powershell.exe",
                L"-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + (root / L"expand.ps1").wstring() + L"\"",
                root, true))
            throw std::runtime_error("A extração falhou.");
        fs::remove(root / L"payload.zip");
        const fs::path app = root / L"Aeonis";
        result = int(run(app / L"Aeonis.exe", arguments, app, false));
    } catch (const std::exception &) {
        MessageBoxW(nullptr, L"Não foi possível abrir o Aeonis portátil. Verifique o espaço livre na pasta temporária e tente de novo.",
                    L"Aeonis", MB_OK | MB_ICONERROR);
    }
    if (!root.empty()) {
        std::error_code error;
        fs::remove_all(root, error); // pasta criada por esta execução
    }
    return result;
}
