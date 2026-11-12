// Dedicated CPU/GPU benchmark backends. Both search the same exhaustive
// three-piece tree and use the same lexicographic objective.
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <intrin.h>

struct CpuGpuBenchmarkInfo {
    std::wstring backend;
    std::wstring device;
    double computeMs{};
    std::uint64_t candidateChecks{};
    unsigned int cpuThreads{};
    double initializationMs{};
};

bool ChooseCpuParallelAction(const Board& board, char piece, int currentRotation,
                             int currentX, const std::vector<char>& nextQueue,
                             ExhaustiveDecision& decision,
                             CpuGpuBenchmarkInfo& info) {
    const auto rotationIt = g_rotations.find(piece);
    if (rotationIt == g_rotations.end() || rotationIt->second.empty()) return false;
    std::vector<char> sequence{piece};
    const int futureCount = (std::min)(2, static_cast<int>(nextQueue.size()));
    for (int i = 0; i < futureCount; ++i) sequence.push_back(nextQueue[static_cast<size_t>(i)]);

    LARGE_INTEGER frequency{}, started{}, finished{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);

    const std::vector<ActionCandidate> roots = EnumerateActions(board, piece);
    if (roots.empty()) return false;

    const unsigned int hw = (std::max)(1u, std::thread::hardware_concurrency());
    const unsigned int workers = (std::min)(hw, static_cast<unsigned int>(roots.size()));
    std::vector<ExhaustiveSearchState> localBest(roots.size());
    std::vector<std::uint64_t> localNodes(roots.size(), 0);
    std::atomic<size_t> cursor{0};

    auto worker = [&]() {
        for (;;) {
            const size_t index = cursor.fetch_add(1, std::memory_order_relaxed);
            if (index >= roots.size()) break;
            const ActionCandidate& root = roots[index];
            const int rotationCount = static_cast<int>(rotationIt->second.size());
            const int rotationTaps = rotationCount > 0
                ? (root.move.rotation - currentRotation + rotationCount) % rotationCount : 0;
            const int horizontalTaps = std::abs(root.move.x - currentX);
            const int rootMultiEvents = root.linesCleared >= 2 ? 1 : 0;
            const int rootMultiLines = root.linesCleared >= 2 ? root.linesCleared : 0;
            ExhaustiveSearch(root.nextBoard, sequence, 1, 1, root.linesCleared,
                rootMultiEvents, rootMultiLines, root.move, root.nextBoard,
                root.linesCleared, rotationTaps, horizontalTaps,
                currentRotation, currentX, localBest[index], localNodes[index]);
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(workers > 0 ? workers - 1 : 0);
    for (unsigned int i = 1; i < workers; ++i) pool.emplace_back(worker);
    worker();
    for (auto& thread : pool) if (thread.joinable()) thread.join();
    QueryPerformanceCounter(&finished);

    ExhaustiveSearchState best{};
    std::uint64_t nodes = roots.size();
    for (size_t i = 0; i < roots.size(); ++i) {
        nodes += localNodes[i];
        if (localBest[i].valid &&
            (!best.valid || BetterExhaustive(localBest[i].evaluation, best.evaluation))) {
            best = localBest[i];
        }
    }
    if (!best.valid) return false;

    const double elapsedMs = frequency.QuadPart > 0
        ? 1000.0 * static_cast<double>(finished.QuadPart - started.QuadPart) /
          static_cast<double>(frequency.QuadPart) : 0.0;
    decision.move = best.firstMove;
    decision.expectedBoard = best.firstBoard;
    decision.predictedLines = best.firstLines;
    decision.searchedNextPieces = futureCount;
    decision.nodes = nodes;
    decision.elapsedMs = elapsedMs;
    decision.rotationTaps = best.evaluation.rotationTaps;
    decision.horizontalTaps = best.evaluation.horizontalTaps;
    decision.keyTaps = best.evaluation.keyTaps;
    decision.multiClearEvents = best.evaluation.multiClearEvents;
    decision.multiClearLines = best.evaluation.multiClearLines;
    info.backend = L"CPU 多核穷举";
    info.device = Format(L"%u 个逻辑处理器；本次使用 %u 个工作线程", hw, workers);
    info.computeMs = elapsedMs;
    info.candidateChecks = nodes;
    info.cpuThreads = workers;
    return true;
}

struct GpuNode {
    std::uint32_t rows[20]{};
    std::uint32_t firstRows[20]{};
    std::int32_t placed{};
    std::int32_t lines{};
    std::int32_t firstX{};
    std::int32_t firstRotation{};
    std::int32_t rotationTaps{};
    std::int32_t horizontalTaps{};
    std::int32_t firstLines{};
    std::int32_t multiClearEvents{};
    std::int32_t multiClearLines{};
    std::uint32_t valid{};
    std::uint32_t terminal{};
};
struct GpuShape {
    std::int32_t count{};
    std::int32_t maxX{};
    std::int32_t maxY{};
    std::int32_t pad{};
    std::int32_t cells[8]{};
};
struct GpuParams {
    std::uint32_t inputCount{};
    std::uint32_t pieceId{};
    std::uint32_t depth{};
    std::int32_t currentRotation{};
    std::int32_t currentX{};
    std::uint32_t pad[3]{};
};
static_assert(sizeof(GpuNode) == 204, "GPU node layout must match HLSL");
static_assert(sizeof(GpuShape) == 48, "GPU shape layout must match HLSL");
static_assert(sizeof(GpuParams) == 32, "GPU constant buffer must be 16-byte aligned");

class GpuExhaustiveSolver {
public:
    bool Initialize(std::wstring& error) {
        if (initialized_) return true;
        LARGE_INTEGER initFrequency{}, initStart{}, initFinish{};
        QueryPerformanceFrequency(&initFrequency);
        QueryPerformanceCounter(&initStart);
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL selected{};
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels, 1, D3D11_SDK_VERSION, device_.put(), &selected, context_.put());
        if (FAILED(hr)) {
            error = Format(L"GPU 硬件设备创建失败（HRESULT 0x%08X）。本模式不会回退到 CPU/WARP。",
                static_cast<unsigned int>(hr));
            return false;
        }
        winrt::com_ptr<IDXGIDevice> dxgiDevice;
        hr = device_->QueryInterface(__uuidof(IDXGIDevice), dxgiDevice.put_void());
        if (FAILED(hr)) { error = L"无法读取 GPU 的 DXGI 设备信息。"; return false; }
        winrt::com_ptr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter(adapter.put());
        if (FAILED(hr)) { error = L"无法读取实际 GPU 适配器。"; return false; }
        winrt::com_ptr<IDXGIAdapter1> adapter1;
        hr = adapter->QueryInterface(__uuidof(IDXGIAdapter1), adapter1.put_void());
        if (FAILED(hr)) { error = L"当前 DXGI 适配器不支持 DXGI 1.1 描述接口。"; return false; }
        DXGI_ADAPTER_DESC1 adapterDesc{};
        hr = adapter1->GetDesc1(&adapterDesc);
        if (FAILED(hr)) { error = L"无法读取 GPU 名称。"; return false; }
        if ((adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            error = L"检测到软件图形适配器；GPU 测试拒绝使用软件模拟器。";
            return false;
        }
        deviceName_ = adapterDesc.Description;

        wchar_t modulePath[32768]{};
        const DWORD pathLength = GetModuleFileNameW(nullptr, modulePath, _countof(modulePath));
        if (pathLength == 0 || pathLength >= _countof(modulePath)) {
            error = L"无法定位程序目录，不能加载 GPU 计算着色器。";
            return false;
        }
        const std::filesystem::path shaderPath =
            std::filesystem::path(modulePath).parent_path() / L"shaders" / L"exhaustive_search.hlsl";
        ID3DBlob* expandBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;
        hr = D3DCompileFromFile(shaderPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            "ExpandCS", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0, &expandBlob, &errorBlob);
        if (FAILED(hr)) {
            error = L"GPU 扩展着色器编译失败：";
            if (errorBlob) {
                error += Utf8ToWide(std::string(static_cast<const char*>(errorBlob->GetBufferPointer()),
                    errorBlob->GetBufferSize()));
                errorBlob->Release();
            } else error += Format(L"HRESULT 0x%08X", static_cast<unsigned int>(hr));
            if (expandBlob) expandBlob->Release();
            return false;
        }
        if (errorBlob) { errorBlob->Release(); errorBlob = nullptr; }
        hr = device_->CreateComputeShader(expandBlob->GetBufferPointer(),
            expandBlob->GetBufferSize(), nullptr, expandShader_.put());
        expandBlob->Release();
        if (FAILED(hr)) { error = L"无法创建 GPU 扩展着色器。"; return false; }

        ID3DBlob* reduceBlob = nullptr;
        hr = D3DCompileFromFile(shaderPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            "ReduceCS", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0, &reduceBlob, &errorBlob);
        if (FAILED(hr)) {
            error = L"GPU 归约着色器编译失败：";
            if (errorBlob) {
                error += Utf8ToWide(std::string(static_cast<const char*>(errorBlob->GetBufferPointer()),
                    errorBlob->GetBufferSize()));
                errorBlob->Release();
            } else error += Format(L"HRESULT 0x%08X", static_cast<unsigned int>(hr));
            if (reduceBlob) reduceBlob->Release();
            return false;
        }
        if (errorBlob) { errorBlob->Release(); errorBlob = nullptr; }
        hr = device_->CreateComputeShader(reduceBlob->GetBufferPointer(),
            reduceBlob->GetBufferSize(), nullptr, reduceShader_.put());
        reduceBlob->Release();
        if (FAILED(hr)) { error = L"无法创建 GPU 归约着色器。"; return false; }

        if (!CreateStaticShapes(error) || !CreateWorkingBuffers(error)) return false;
        QueryPerformanceCounter(&initFinish);
        initializationMs_ = initFrequency.QuadPart > 0
            ? 1000.0 * static_cast<double>(initFinish.QuadPart - initStart.QuadPart) /
              static_cast<double>(initFrequency.QuadPart) : 0.0;
        initialized_ = true;
        return true;
    }

    const std::wstring& DeviceName() const { return deviceName_; }
    double InitializationMs() const { return initializationMs_; }

    bool Solve(const Board& board, char piece, int currentRotation, int currentX,
               const std::vector<char>& nextQueue, ExhaustiveDecision& decision,
               CpuGpuBenchmarkInfo& info, std::wstring& error) {
        if (!Initialize(error)) return false;
        const int pieceId = PieceId(piece);
        if (pieceId < 0) { error = L"GPU 搜索不认识当前方块类型。"; return false; }
        const int sequenceLength = 1 + (std::min)(2, static_cast<int>(nextQueue.size()));
        std::uint32_t pieceIds[3]{static_cast<std::uint32_t>(pieceId),0,0};
        for (int i = 1; i < sequenceLength; ++i) {
            const int id = PieceId(nextQueue[static_cast<size_t>(i - 1)]);
            if (id < 0) { error = L"GPU 搜索遇到未知 NEXT 方块类型。"; return false; }
            pieceIds[i] = static_cast<std::uint32_t>(id);
        }

        GpuNode root{};
        for (int y = 0; y < ROWS; ++y) {
            for (int x = 0; x < COLS; ++x) if (board[y][x]) root.rows[y] |= (1u << x);
        }
        root.valid = 1;
        D3D11_BOX firstNodeBox{};
        firstNodeBox.left = 0;
        firstNodeBox.right = sizeof(GpuNode);
        firstNodeBox.top = 0;
        firstNodeBox.bottom = 1;
        firstNodeBox.front = 0;
        firstNodeBox.back = 1;

        LARGE_INTEGER frequency{}, started{}, finished{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&started);
        context_->UpdateSubresource(bufferA_.get(), 0, &firstNodeBox, &root, 0, 0);
        ID3D11Buffer* constants = paramsBuffer_.get();
        context_->CSSetConstantBuffers(0, 1, &constants);
        context_->CSSetShader(expandShader_.get(), nullptr, 0);

        UINT inputCount = 1;
        bool useAAsInput = true;
        for (int depth = 0; depth < sequenceLength; ++depth) {
            GpuParams params{};
            params.inputCount = inputCount;
            params.pieceId = pieceIds[depth];
            params.depth = static_cast<std::uint32_t>(depth);
            params.currentRotation = currentRotation;
            params.currentX = currentX;
            context_->UpdateSubresource(paramsBuffer_.get(), 0, nullptr, &params, 0, 0);

            ID3D11ShaderResourceView* srvs[2] = {
                useAAsInput ? srvA_.get() : srvB_.get(), shapeSrv_.get()
            };
            ID3D11UnorderedAccessView* uav = useAAsInput ? uavB_.get() : uavA_.get();
            context_->CSSetShaderResources(0, 2, srvs);
            context_->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
            context_->Dispatch(inputCount, 1, 1);

            ID3D11ShaderResourceView* nullSrvs[2]{};
            ID3D11UnorderedAccessView* nullUav[1]{};
            context_->CSSetShaderResources(0, 2, nullSrvs);
            context_->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
            inputCount *= 40;
            useAAsInput = !useAAsInput;
        }
        if (inputCount > kCapacity) { error = L"GPU 搜索节点超过预设容量，拒绝截断搜索。"; return false; }

        GpuParams reduceParams{};
        reduceParams.inputCount = inputCount;
        context_->UpdateSubresource(paramsBuffer_.get(), 0, nullptr, &reduceParams, 0, 0);
        ID3D11ShaderResourceView* reduceSrv =
            useAAsInput ? srvA_.get() : srvB_.get();
        ID3D11UnorderedAccessView* bestUav = bestUav_.get();
        context_->CSSetShaderResources(0, 1, &reduceSrv);
        context_->CSSetUnorderedAccessViews(1, 1, &bestUav, nullptr);
        context_->CSSetShader(reduceShader_.get(), nullptr, 0);
        context_->Dispatch(1, 1, 1);
        ID3D11ShaderResourceView* nullReduceSrv[1]{};
        ID3D11UnorderedAccessView* nullBestUav[1]{};
        context_->CSSetShaderResources(0, 1, nullReduceSrv);
        context_->CSSetUnorderedAccessViews(1, 1, nullBestUav, nullptr);
        context_->CopyResource(bestStaging_.get(), bestBuffer_.get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = context_->Map(bestStaging_.get(), 0, D3D11_MAP_READ, 0, &mapped);
        QueryPerformanceCounter(&finished);
        if (FAILED(hr)) { error = Format(L"GPU 计算结果回读失败（HRESULT 0x%08X）。", static_cast<unsigned int>(hr)); return false; }
        GpuNode result = *static_cast<const GpuNode*>(mapped.pData);
        context_->Unmap(bestStaging_.get(), 0);
        if (result.valid == 0 || result.firstX < 0 || result.firstX >= COLS) {
            error = L"GPU 穷举未找到合法落点；棋盘可能已到顶部或识别结果不正确。";
            return false;
        }

        const auto rotations = g_rotations.find(piece);
        if (rotations == g_rotations.end() ||
            result.firstRotation < 0 ||
            result.firstRotation >= static_cast<int>(rotations->second.size())) {
            error = L"GPU 返回的旋转编号超出规则形状表。";
            return false;
        }
        Board firstBoard{};
        for (int y = 0; y < ROWS; ++y)
            for (int x = 0; x < COLS; ++x)
                firstBoard[y][x] = (result.firstRows[y] & (1u << x)) ? 1 : 0;
        const int firstLines = result.firstLines;
        if (firstLines < 0 || firstLines > 4) {
            error = L"GPU 返回的首步消行数超出有效范围；已中止。";
            return false;
        }

        const double elapsedMs = frequency.QuadPart > 0
            ? 1000.0 * static_cast<double>(finished.QuadPart - started.QuadPart) /
              static_cast<double>(frequency.QuadPart) : 0.0;
        decision.move.rotation = result.firstRotation;
        decision.move.x = result.firstX;
        decision.expectedBoard = firstBoard;
        decision.predictedLines = firstLines;
        decision.searchedNextPieces = sequenceLength - 1;
        std::uint64_t candidateChecks = 0;
        std::uint64_t levelParents = 1;
        for (int depth = 0; depth < sequenceLength; ++depth) {
            candidateChecks += levelParents * 40ull;
            levelParents *= 40ull;
        }
        decision.nodes = candidateChecks;
        decision.elapsedMs = elapsedMs;
        decision.rotationTaps = result.rotationTaps;
        decision.horizontalTaps = result.horizontalTaps;
        decision.keyTaps = result.rotationTaps + result.horizontalTaps;
        decision.multiClearEvents = result.multiClearEvents;
        decision.multiClearLines = result.multiClearLines;

        info.backend = L"GPU Direct3D 11 Compute";
        info.device = deviceName_;
        info.computeMs = elapsedMs;
        info.candidateChecks = decision.nodes;
        info.cpuThreads = 0;
        info.initializationMs = initializationMs_;
        return true;
    }

private:
    static constexpr UINT kCapacity = 64000;
    static int PieceId(char piece) {
        static constexpr char pieces[] = {'I','O','T','S','Z','J','L'};
        for (int i = 0; i < 7; ++i) if (pieces[i] == piece) return i;
        return -1;
    }

    static std::wstring Utf8ToWide(const std::string& text) {
        if (text.empty()) return {};
        const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(),
            static_cast<int>(text.size()), nullptr, 0);
        if (count <= 0) return L"（无法解码着色器错误信息）";
        std::wstring result(static_cast<size_t>(count), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
            result.data(), count);
        return result;
    }

    bool CreateStaticShapes(std::wstring& error) {
        std::array<GpuShape, 28> shapes{};
        static constexpr char pieces[] = {'I','O','T','S','Z','J','L'};
        for (int p = 0; p < 7; ++p) {
            const auto it = g_rotations.find(pieces[p]);
            if (it == g_rotations.end()) continue;
            for (size_t r = 0; r < it->second.size() && r < 4; ++r) {
                GpuShape& dst = shapes[static_cast<size_t>(p) * 4 + r];
                const auto& source = it->second[r];
                dst.count = static_cast<int>(source.size());
                dst.pad = static_cast<int>(it->second.size()); // unique rotation count
                dst.maxX = 0;
                dst.maxY = 0;
                for (size_t c = 0; c < source.size() && c < 4; ++c) {
                    dst.cells[c * 2] = source[c].x;
                    dst.cells[c * 2 + 1] = source[c].y;
                    dst.maxX = (std::max)(dst.maxX, source[c].x);
                    dst.maxY = (std::max)(dst.maxY, source[c].y);
                }
            }
        }
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(shapes);
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(GpuShape);
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = shapes.data();
        HRESULT hr = device_->CreateBuffer(&desc, &initial, shapeBuffer_.put());
        if (FAILED(hr)) { error = L"无法创建 GPU 方块形状缓冲区。"; return false; }
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = static_cast<UINT>(shapes.size());
        hr = device_->CreateShaderResourceView(shapeBuffer_.get(), &srvDesc, shapeSrv_.put());
        if (FAILED(hr)) { error = L"无法创建 GPU 方块形状 SRV。"; return false; }
        return true;
    }

    bool CreateNodeBuffer(UINT elements, UINT bindFlags,
                         winrt::com_ptr<ID3D11Buffer>& buffer,
                         winrt::com_ptr<ID3D11ShaderResourceView>* srv,
                         winrt::com_ptr<ID3D11UnorderedAccessView>* uav,
                         std::wstring& error, D3D11_USAGE usage = D3D11_USAGE_DEFAULT,
                         UINT cpuAccess = 0) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = elements * sizeof(GpuNode);
        desc.Usage = usage;
        desc.BindFlags = bindFlags;
        desc.CPUAccessFlags = cpuAccess;
        if (usage != D3D11_USAGE_STAGING) {
            desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            desc.StructureByteStride = sizeof(GpuNode);
        }
        HRESULT hr = device_->CreateBuffer(&desc, nullptr, buffer.put());
        if (FAILED(hr)) { error = L"无法创建 GPU 棋盘节点缓冲区。"; return false; }
        if (srv) {
            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = DXGI_FORMAT_UNKNOWN;
            sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            sd.Buffer.NumElements = elements;
            hr = device_->CreateShaderResourceView(buffer.get(), &sd, srv->put());
            if (FAILED(hr)) { error = L"无法创建 GPU 节点 SRV。"; return false; }
        }
        if (uav) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
            ud.Format = DXGI_FORMAT_UNKNOWN;
            ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            ud.Buffer.NumElements = elements;
            hr = device_->CreateUnorderedAccessView(buffer.get(), &ud, uav->put());
            if (FAILED(hr)) { error = L"无法创建 GPU 节点 UAV。"; return false; }
        }
        return true;
    }

    bool CreateWorkingBuffers(std::wstring& error) {
        if (!CreateNodeBuffer(kCapacity, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
                bufferA_, &srvA_, &uavA_, error) ||
            !CreateNodeBuffer(kCapacity, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
                bufferB_, &srvB_, &uavB_, error) ||
            !CreateNodeBuffer(1, D3D11_BIND_UNORDERED_ACCESS,
                bestBuffer_, nullptr, &bestUav_, error) ||
            !CreateNodeBuffer(1, 0, bestStaging_, nullptr, nullptr, error,
                D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ)) return false;

        D3D11_BUFFER_DESC paramsDesc{};
        paramsDesc.ByteWidth = sizeof(GpuParams);
        paramsDesc.Usage = D3D11_USAGE_DEFAULT;
        paramsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        HRESULT hr = device_->CreateBuffer(&paramsDesc, nullptr, paramsBuffer_.put());
        if (FAILED(hr)) { error = L"无法创建 GPU 搜索参数缓冲区。"; return false; }
        return true;
    }

    bool initialized_{};
    std::wstring deviceName_;
    double initializationMs_{};
    winrt::com_ptr<ID3D11Device> device_;
    winrt::com_ptr<ID3D11DeviceContext> context_;
    winrt::com_ptr<ID3D11ComputeShader> expandShader_;
    winrt::com_ptr<ID3D11ComputeShader> reduceShader_;
    winrt::com_ptr<ID3D11Buffer> shapeBuffer_;
    winrt::com_ptr<ID3D11ShaderResourceView> shapeSrv_;
    winrt::com_ptr<ID3D11Buffer> bufferA_, bufferB_, bestBuffer_, bestStaging_, paramsBuffer_;
    winrt::com_ptr<ID3D11ShaderResourceView> srvA_, srvB_;
    winrt::com_ptr<ID3D11UnorderedAccessView> uavA_, uavB_, bestUav_;
};


// Game-session board memory: once a locked mino has been observed or predicted,
// it remains part of the model even when the game's late-marathon visual effect
// makes its pixels faint or transparent. A lock event applies line clears to the
// remembered board atomically, so old coordinates are never blindly retained.
class MarathonBoardMemory {
public:
    void SetPath(const std::wstring& path) { path_ = path; }

    bool Initialized() const { return initialized_; }

    int RememberedCells() const {
        int count = 0;
        for (int y = 0; y < ROWS; ++y)
            for (int x = 0; x < COLS; ++x) if (board_[y][x]) ++count;
        return count;
    }

    std::size_t HistoryCount() const { return history_.size(); }

    void ResetForNewGame() {
        board_ = {};
        confidence_ = {};
        initialized_ = false;
        sequence_ = 0;
        lastReportedRecovery_ = -1;
        history_.clear();
        AppendSessionMarker("NEW_GAME");
    }

    Board Reconcile(const VisionResult& vision, int& recoveredCells, int& unexpectedCells) {
        recoveredCells = 0;
        unexpectedCells = 0;
        if (!vision.hasActive) return board_; // never mistake an untracked active piece for fixed cells

        const Board observed = BoardWithoutActive(vision);
        if (!initialized_) {
            Seed(observed);
            return board_;
        }

        Board merged = board_;
        for (int y = 0; y < ROWS; ++y) {
            for (int x = 0; x < COLS; ++x) {
                if (observed[y][x]) {
                    if (!board_[y][x]) ++unexpectedCells;
                    merged[y][x] = 1;
                    confidence_[y][x] = 255;
                } else if (board_[y][x]) {
                    // It was previously visible or committed by a verified
                    // lock. Keep it as a remembered transparent mino.
                    ++recoveredCells;
                    confidence_[y][x] = (std::max)(static_cast<unsigned char>(32),
                        static_cast<unsigned char>(confidence_[y][x] > 1 ? confidence_[y][x] - 1 : 1));
                }
            }
        }
        board_ = merged;
        return board_;
    }

    bool ShouldReportRecovery(int recoveredCells) {
        if (recoveredCells == lastReportedRecovery_) return false;
        lastReportedRecovery_ = recoveredCells;
        return recoveredCells > 0;
    }

    void Seed(const Board& board) {
        board_ = board;
        initialized_ = true;
        for (int y = 0; y < ROWS; ++y)
            for (int x = 0; x < COLS; ++x)
                confidence_[y][x] = board[y][x] ? 255 : 0;
        lastReportedRecovery_ = -1;
    }

    void Commit(const Board& expectedBoard, char piece, int rotation, int x,
                int clearedLines, int multiClearEvents, double computeMs,
                double inputMs, double endToEndMs, const Board& sourceBoard,
                std::uint64_t candidateChecks, int backendCode) {
        board_ = expectedBoard;
        initialized_ = true;
        lastReportedRecovery_ = -1;
        for (int y = 0; y < ROWS; ++y)
            for (int col = 0; col < COLS; ++col)
                confidence_[y][col] = expectedBoard[y][col] ? 255 : 0;

        MarathonMoveRecord record;
        record.sequence = ++sequence_;
        record.piece = piece;
        record.rotation = rotation;
        record.x = x;
        record.clearedLines = clearedLines;
        record.multiClearEvents = multiClearEvents;
        record.computeMs = computeMs;
        record.inputMs = inputMs;
        record.endToEndMs = endToEndMs;
        record.boardHash = HashBoard(expectedBoard);
        record.sourceBoardHash = HashBoard(sourceBoard);
        record.candidateChecks = candidateChecks;
        record.backendCode = backendCode;
        history_.push_back(record);
        if (history_.size() > 4096) history_.erase(history_.begin());

        if (!path_.empty()) {
            const std::filesystem::path filePath(path_);
            const bool writeHeader = !std::filesystem::exists(filePath);
            std::ofstream output{filePath, std::ios::out | std::ios::app};
            if (output) {
                if (writeHeader)
                    output << "sequence,piece,rotation,x,cleared_lines,multi_clear_events,compute_ms,input_ms,end_to_end_ms,board_hash,source_board_hash,backend_code,candidate_checks,rows_hex\n";
                output << record.sequence << ',' << record.piece << ',' << record.rotation << ','
                       << record.x << ',' << record.clearedLines << ',' << record.multiClearEvents << ','
                       << std::fixed << std::setprecision(3) << record.computeMs << ','
                       << record.inputMs << ',' << record.endToEndMs << ','
                       << std::hex << record.boardHash << ',' << record.sourceBoardHash << std::dec << ','
                       << record.backendCode << ',' << record.candidateChecks << ',';
                for (int y = 0; y < ROWS; ++y) {
                    unsigned int mask = 0;
                    for (int col = 0; col < COLS; ++col) if (expectedBoard[y][col]) mask |= (1u << col);
                    output << std::hex << std::setw(3) << std::setfill('0') << mask;
                    if (y + 1 < ROWS) output << ':';
                }
                output << std::dec << std::setfill(' ') << '\n';
            }
        }
    }

private:
    struct MarathonMoveRecord {
        std::uint64_t sequence{};
        char piece{};
        int rotation{};
        int x{};
        int clearedLines{};
        int multiClearEvents{};
        double computeMs{};
        double inputMs{};
        double endToEndMs{};
        std::uint64_t boardHash{};
        std::uint64_t sourceBoardHash{};
        std::uint64_t candidateChecks{};
        int backendCode{};
    };

    static std::uint64_t HashBoard(const Board& board) {
        std::uint64_t hash = 1469598103934665603ull;
        for (int y = 0; y < ROWS; ++y) {
            std::uint32_t rowMask = 0;
            for (int x = 0; x < COLS; ++x) if (board[y][x]) rowMask |= (1u << x);
            hash ^= rowMask;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    void AppendSessionMarker(const char* marker) {
        if (path_.empty()) return;
        std::ofstream output{std::filesystem::path(path_), std::ios::out | std::ios::app};
        if (output) output << "# " << marker << '\n';
    }

    Board board_{};
    std::array<std::array<unsigned char, COLS>, ROWS> confidence_{};
    bool initialized_{};
    std::uint64_t sequence_{};
    int lastReportedRecovery_{-1};
    std::vector<MarathonMoveRecord> history_;
    std::wstring path_;
};
