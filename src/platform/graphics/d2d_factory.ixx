// folia.platform.d2d_factory — Direct2D factory bootstrap.
module;
#include <d2d1.h>
#include <wrl/client.h>

export module folia.platform.d2d_factory;
import std;

using Microsoft::WRL::ComPtr;

export namespace folia::platform {

inline ComPtr<ID2D1Factory> create_d2d_factory() {
    ComPtr<ID2D1Factory> factory;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf());
    return factory;
}

} // namespace folia::platform