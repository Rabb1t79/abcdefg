// Windows Graphics Capture backend for a selected HWND.
// The capture worker owns all WinRT/D3D objects on one MTA thread; callers only
// copy the most recent BGRA frame under a small mutex.
namespace {
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

struct WgcCaptureState {
    HWND target{};
    std::atomic<bool> stop{false};
    std::mutex mutex;
    std::condition_variable cv;
    bool failed{};
    bool haveFrame{};
    std::wstring error;
    CapturedImage latest;
    std::thread worker;
};

std::mutex g_wgcMutex;
std::shared_ptr<WgcCaptureState> g_wgcState;

void SetWgcFailure(const std::shared_ptr<WgcCaptureState>& state, const std::wstring& message) {
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->failed = true;
        state->error = message;
    }
    state->cv.notify_all();
}

void WgcCaptureThread(const std::shared_ptr<WgcCaptureState>& state) {
    bool apartmentReady = false;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        apartmentReady = true;

        if (!IsWindow(state->target) || !IsWindowVisible(state->target) || IsIconic(state->target)) {
            throw std::runtime_error("目标窗口无效、不可见或已最小化。");
        }
        if (!GraphicsCaptureSession::IsSupported()) {
            throw std::runtime_error("当前 Windows 图形设备不支持 Windows Graphics Capture。");
        }

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        const D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
        };
        winrt::com_ptr<ID3D11Device> d3dDevice;
        winrt::com_ptr<ID3D11DeviceContext> d3dContext;
        D3D_FEATURE_LEVEL selectedLevel{};
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            featureLevels, static_cast<UINT>(_countof(featureLevels)),
            D3D11_SDK_VERSION, d3dDevice.put(), &selectedLevel, d3dContext.put());
        if (FAILED(hr)) {
            hr = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                featureLevels, static_cast<UINT>(_countof(featureLevels)),
                D3D11_SDK_VERSION, d3dDevice.put(), &selectedLevel, d3dContext.put());
        }
        if (FAILED(hr)) {
            throw winrt::hresult_error(hr, L"无法创建 Direct3D 11 捕获设备。");
        }

        winrt::com_ptr<IDXGIDevice> dxgiDevice;
        winrt::check_hresult(d3dDevice->QueryInterface(
            winrt::guid_of<IDXGIDevice>(), dxgiDevice.put_void()));
        winrt::com_ptr<IInspectable> inspectableDevice;
        winrt::check_hresult(::CreateDirect3D11DeviceFromDXGIDevice(
            dxgiDevice.get(), inspectableDevice.put()));
        IDirect3DDevice winrtDevice = inspectableDevice.as<IDirect3DDevice>();

        auto activationFactory = winrt::get_activation_factory<GraphicsCaptureItem>();
        auto interopFactory = activationFactory.as<IGraphicsCaptureItemInterop>();
        GraphicsCaptureItem item{nullptr};
        winrt::check_hresult(interopFactory->CreateForWindow(
            state->target,
            winrt::guid_of<GraphicsCaptureItem>(),
            winrt::put_abi(item)));

        const auto pixelFormat = DirectXPixelFormat::B8G8R8A8UIntNormalized;
        auto poolSize = item.Size();
        if (poolSize.Width <= 0 || poolSize.Height <= 0) {
            throw std::runtime_error("WGC 返回了无效的窗口尺寸。");
        }
        auto framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
            winrtDevice, pixelFormat, 2, poolSize);
        auto session = framePool.CreateCaptureSession(item);
        session.StartCapture();

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->error.clear();
        }
        state->cv.notify_all();

        ULONGLONG lastPublish = 0;
        while (!state->stop.load()) {
            if (!IsWindow(state->target)) {
                throw std::runtime_error("目标游戏窗口已关闭。");
            }
            if (IsIconic(state->target) || !IsWindowVisible(state->target)) {
                Sleep(10);
                continue;
            }

            auto frame = framePool.TryGetNextFrame();
            if (!frame) {
                Sleep(2);
                continue;
            }

            const auto contentSize = frame.ContentSize();
            const ULONGLONG now = GetTickCount64();
            if (contentSize.Width > 0 && contentSize.Height > 0 && now - lastPublish >= 25) {
                auto surface = frame.Surface();
                auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
                winrt::com_ptr<ID3D11Texture2D> sourceTexture;
                winrt::check_hresult(access->GetInterface(
                    __uuidof(ID3D11Texture2D), sourceTexture.put_void()));

                D3D11_TEXTURE2D_DESC sourceDesc{};
                sourceTexture->GetDesc(&sourceDesc);
                if (sourceDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
                    throw std::runtime_error("WGC 帧不是 BGRA8 格式；请关闭游戏 HDR 或使用 SDR 显示模式。");
                }
                const int copyWidth = (std::min)(static_cast<int>(contentSize.Width), static_cast<int>(sourceDesc.Width));
                const int copyHeight = (std::min)(static_cast<int>(contentSize.Height), static_cast<int>(sourceDesc.Height));
                if (copyWidth <= 0 || copyHeight <= 0 || copyWidth > 10000 || copyHeight > 10000) {
                    throw std::runtime_error("WGC 帧尺寸超出允许范围。");
                }

                D3D11_TEXTURE2D_DESC stagingDesc = sourceDesc;
                stagingDesc.Usage = D3D11_USAGE_STAGING;
                stagingDesc.BindFlags = 0;
                stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                stagingDesc.MiscFlags = 0;
                stagingDesc.MipLevels = 1;
                stagingDesc.ArraySize = 1;
                stagingDesc.SampleDesc.Count = 1;
                stagingDesc.SampleDesc.Quality = 0;
                winrt::com_ptr<ID3D11Texture2D> stagingTexture;
                winrt::check_hresult(d3dDevice->CreateTexture2D(
                    &stagingDesc, nullptr, stagingTexture.put()));
                d3dContext->CopyResource(stagingTexture.get(), sourceTexture.get());

                D3D11_MAPPED_SUBRESOURCE mapped{};
                winrt::check_hresult(d3dContext->Map(
                    stagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapped));
                CapturedImage snapshot;
                snapshot.width = copyWidth;
                snapshot.height = copyHeight;
                snapshot.pixels.resize(static_cast<size_t>(copyWidth) * copyHeight);
                for (int y = 0; y < copyHeight; ++y) {
                    const auto* sourceRow = static_cast<const uint8_t*>(mapped.pData) +
                        static_cast<size_t>(y) * mapped.RowPitch;
                    auto* destinationRow = snapshot.pixels.data() +
                        static_cast<size_t>(y) * copyWidth;
                    std::memcpy(destinationRow, sourceRow,
                        static_cast<size_t>(copyWidth) * sizeof(Pixel));
                }
                d3dContext->Unmap(stagingTexture.get(), 0);

                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->latest = std::move(snapshot);
                    state->haveFrame = true;
                }
                state->cv.notify_all();
                lastPublish = now;
            }

            const bool resized = contentSize.Width != poolSize.Width ||
                                 contentSize.Height != poolSize.Height;
            frame.Close();
            if (resized) {
                poolSize = contentSize;
                framePool.Recreate(winrtDevice, pixelFormat, 2, poolSize);
            }
        }

        session.Close();
        framePool.Close();
    } catch (const winrt::hresult_error& ex) {
        SetWgcFailure(state, Format(L"WGC/Direct3D 错误 0x%08X：%s",
            static_cast<unsigned int>(ex.code().value), ex.message().c_str()));
    } catch (const std::exception& ex) {
        const int needed = MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, nullptr, 0);
        std::wstring message;
        if (needed > 1) {
            message.resize(static_cast<size_t>(needed));
            MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, message.data(), needed);
            message.resize(static_cast<size_t>(needed - 1));
        } else {
            message = L"WGC 捕获线程发生未知错误。";
        }
        SetWgcFailure(state, message);
    } catch (...) {
        SetWgcFailure(state, L"WGC 捕获线程发生未知错误。");
    }
    if (apartmentReady) winrt::uninit_apartment();
}

std::shared_ptr<WgcCaptureState> CurrentWgcState() {
    std::lock_guard<std::mutex> lock(g_wgcMutex);
    return g_wgcState;
}

bool IsWindowCaptureConfigured() {
    // A configured-but-failed capture must report its WGC error. Do not silently
    // fall back to desktop pixels, which could contain an occluding window.
    return static_cast<bool>(CurrentWgcState());
}

bool EnsureWindowCapture(HWND target, std::wstring& error) {
    error.clear();
    if (!target || !IsWindow(target)) {
        error = L"未选择有效的游戏窗口。";
        return false;
    }
    if (IsIconic(target) || !IsWindowVisible(target)) {
        error = L"目标游戏窗口不可见或已最小化；WGC 需要窗口保持可见。";
        return false;
    }

    auto existing = CurrentWgcState();
    if (existing && existing->target == target) {
        std::unique_lock<std::mutex> lock(existing->mutex);
        existing->cv.wait_for(lock, std::chrono::seconds(5), [&] {
            return existing->haveFrame || existing->failed;
        });
        if (existing->haveFrame && !existing->failed) return true;
        error = existing->error.empty() ? L"等待 WGC 首帧超时。" : existing->error;
        lock.unlock();
        StopWindowCapture();
        return false;
    }

    StopWindowCapture();
    auto state = std::make_shared<WgcCaptureState>();
    state->target = target;
    state->worker = std::thread(WgcCaptureThread, state);
    {
        std::lock_guard<std::mutex> lock(g_wgcMutex);
        g_wgcState = state;
    }

    std::unique_lock<std::mutex> lock(state->mutex);
    state->cv.wait_for(lock, std::chrono::seconds(6), [&] {
        return state->haveFrame || state->failed;
    });
    if (state->haveFrame && !state->failed) return true;
    error = state->error.empty() ? L"等待 WGC 首帧超时；目标窗口可能被系统或游戏限制捕获。" : state->error;
    lock.unlock();
    StopWindowCapture();
    return false;
}

void StopWindowCapture() {
    std::shared_ptr<WgcCaptureState> state;
    {
        std::lock_guard<std::mutex> lock(g_wgcMutex);
        state = std::move(g_wgcState);
    }
    if (!state) return;
    state->stop.store(true);
    if (state->worker.joinable()) state->worker.join();
}

bool TryCaptureWindowRect(const Rect& rect, CapturedImage& out, std::wstring& error) {
    auto state = CurrentWgcState();
    if (!state) {
        error = L"WGC 窗口捕获尚未初始化。";
        return false;
    }
    if (IsIconic(state->target) || !IsWindowVisible(state->target)) {
        error = L"所选游戏窗口已最小化或隐藏；WGC 无法提供有效帧。";
        return false;
    }

    CapturedImage full;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->failed) {
            error = state->error.empty() ? L"WGC 捕获线程已停止。" : state->error;
            return false;
        }
        if (!state->haveFrame) {
            error = L"WGC 尚未收到窗口帧。";
            return false;
        }
        full = state->latest;
    }

    RECT windowBounds{};
    if (!GetWindowRect(state->target, &windowBounds)) {
        error = L"无法读取游戏窗口边界。";
        return false;
    }
    RECT clientBounds{};
    POINT clientOrigin{0, 0};
    RECT candidates[3]{};
    int candidateCount = 0;
    candidates[candidateCount++] = windowBounds;

    if (GetClientRect(state->target, &clientBounds) &&
        ClientToScreen(state->target, &clientOrigin)) {
        RECT screenClient{
            clientOrigin.x, clientOrigin.y,
            clientOrigin.x + clientBounds.right - clientBounds.left,
            clientOrigin.y + clientBounds.bottom - clientBounds.top
        };
        if (screenClient.right > screenClient.left && screenClient.bottom > screenClient.top) {
            candidates[candidateCount++] = screenClient;
        }
    }
    RECT extendedBounds{};
    if (SUCCEEDED(DwmGetWindowAttribute(
        state->target, DWMWA_EXTENDED_FRAME_BOUNDS,
        &extendedBounds, sizeof(extendedBounds))) &&
        extendedBounds.right > extendedBounds.left &&
        extendedBounds.bottom > extendedBounds.top) {
        candidates[candidateCount++] = extendedBounds;
    }

    int best = 0;
    double bestScore = (std::numeric_limits<double>::max)();
    for (int i = 0; i < candidateCount; ++i) {
        const int width = candidates[i].right - candidates[i].left;
        const int height = candidates[i].bottom - candidates[i].top;
        if (width <= 0 || height <= 0) continue;
        const double score =
            std::abs(static_cast<double>(full.width - width) / width) +
            std::abs(static_cast<double>(full.height - height) / height);
        if (score < bestScore) { bestScore = score; best = i; }
    }

    const RECT bounds = candidates[best];
    const int referenceWidth = bounds.right - bounds.left;
    const int referenceHeight = bounds.bottom - bounds.top;
    if (referenceWidth <= 0 || referenceHeight <= 0) {
        error = L"无法匹配 WGC 帧与游戏窗口的坐标范围。";
        return false;
    }

    const double scaleX = static_cast<double>(full.width) / referenceWidth;
    const double scaleY = static_cast<double>(full.height) / referenceHeight;
    const int left = static_cast<int>(std::floor((rect.left - bounds.left) * scaleX));
    const int top = static_cast<int>(std::floor((rect.top - bounds.top) * scaleY));
    const int right = static_cast<int>(std::ceil((rect.left + rect.width - bounds.left) * scaleX));
    const int bottom = static_cast<int>(std::ceil((rect.top + rect.height - bounds.top) * scaleY));
    if (left < 0 || top < 0 || right > full.width || bottom > full.height ||
        right - left < 8 || bottom - top < 8) {
        error = L"框选区域与当前 WGC 窗口帧不匹配；可能移动或缩放了游戏窗口，请重新框选。";
        return false;
    }

    out.width = right - left;
    out.height = bottom - top;
    out.pixels.resize(static_cast<size_t>(out.width) * out.height);
    for (int y = 0; y < out.height; ++y) {
        const auto* sourceRow = full.pixels.data() +
            static_cast<size_t>(top + y) * full.width + left;
        auto* destinationRow = out.pixels.data() + static_cast<size_t>(y) * out.width;
        std::memcpy(destinationRow, sourceRow, static_cast<size_t>(out.width) * sizeof(Pixel));
    }
    return true;
}
} // namespace
