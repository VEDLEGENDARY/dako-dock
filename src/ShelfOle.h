#pragma once
#include "ClipboardTransfer.h"
#include <objidl.h>
#include <wrl/client.h>
#include <optional>
#include <functional>

namespace shelf {
inline constexpr size_t MarshalLimit = 64 * 1024;
// Only bytes generated here are passed to the private owned helper. Never load a marshal packet from disk.
class MarshaledDrop {
    Microsoft::WRL::ComPtr<IStream> stream_;
public:
    ~MarshaledDrop() { Clear(); }
    MarshaledDrop() = default;
    MarshaledDrop(const MarshaledDrop &) = delete;
    void Clear() noexcept;
    std::optional<clipboard::Payload> Capture(IDataObject *);
    size_t Bytes() const { return stream_ ? MarshalLimit : 0; }
};
bool Valid(const clipboard::Payload &);
Microsoft::WRL::ComPtr<IDataObject> DataObject(clipboard::Payload);
HRESULT Drag(const clipboard::Payload &, std::function<bool()> cancelled = {});
clipboard::TransferStatus HandleTransfer(clipboard::TransferOp, clipboard::Payload &, DWORD &);
}
