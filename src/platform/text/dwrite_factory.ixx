// folia.platform.dwrite_factory — DirectWrite factory bootstrap.
module;
#include <dwrite.h>
#include <wrl/client.h>

export module folia.platform.dwrite_factory;
import std;

using Microsoft::WRL::ComPtr;

export namespace folia::platform {

inline ComPtr<IDWriteFactory> create_dwrite_factory() {
    ComPtr<IDWriteFactory> factory;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
    return factory;
}

} // namespace folia::platform