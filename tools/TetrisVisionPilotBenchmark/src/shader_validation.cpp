#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>

#include <cstdio>
#include <cstring>

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 2) {
        std::fwprintf(stderr, L"Usage: TetrisShaderValidation.exe <shader.hlsl>\n");
        return 2;
    }

    struct ShaderEntry {
        const char* name;
        const char* target;
    };
    static constexpr ShaderEntry entries[] = {
        {"ExpandCS", "cs_5_0"},
        {"ReduceCS", "cs_5_0"}
    };

    for (const ShaderEntry& entry : entries) {
        ID3DBlob* bytecode = nullptr;
        ID3DBlob* diagnostics = nullptr;
        const HRESULT hr = D3DCompileFromFile(
            argv[1],
            nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entry.name,
            entry.target,
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,
            &bytecode,
            &diagnostics);

        if (diagnostics && diagnostics->GetBufferSize() > 0) {
            std::fwrite(diagnostics->GetBufferPointer(), 1,
                diagnostics->GetBufferSize(), stderr);
        }

        if (FAILED(hr)) {
            std::fprintf(stderr, "FAILED: %s (HRESULT 0x%08lX)\n",
                entry.name, static_cast<unsigned long>(hr));
            if (diagnostics) diagnostics->Release();
            if (bytecode) bytecode->Release();
            return 1;
        }

        std::printf("PASS: %s compiled successfully.\n", entry.name);
        if (diagnostics) diagnostics->Release();
        if (bytecode) bytecode->Release();
    }

    return 0;
}
