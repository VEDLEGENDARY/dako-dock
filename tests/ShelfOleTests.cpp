#include "../src/ShelfOle.h"
#include "../src/ClipboardData.h"
#include <ole2.h>
#include <cstdio>
#include <stdexcept>
#include <string_view>

void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
// Real OLE sources can allocate a larger HGLOBAL than their terminated text requires.
class Padded final : public IDataObject {
    std::atomic<ULONG> refs_ = 1;
    Microsoft::WRL::ComPtr<IDataObject> inner_;
public:
    explicit Padded(Microsoft::WRL::ComPtr<IDataObject> inner) : inner_(std::move(inner)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (id != IID_IUnknown && id != IID_IDataObject) return E_NOINTERFACE;
        *out = static_cast<IDataObject *>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto left = --refs_; if (!left) delete this; return left; }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC *format, STGMEDIUM *medium) override {
        auto result = inner_->GetData(format, medium);
        if (SUCCEEDED(result) && format->cfFormat == CF_UNICODETEXT) {
            auto grown = GlobalReAlloc(medium->hGlobal, GlobalSize(medium->hGlobal) + 8, GMEM_MOVEABLE | GMEM_ZEROINIT);
            if (!grown) { ReleaseStgMedium(medium); *medium = {}; return E_OUTOFMEMORY; }
            medium->hGlobal = grown;
        }
        return result;
    }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *value) override { return inner_->QueryGetData(value); }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *a, STGMEDIUM *b) override { return inner_->GetDataHere(a, b); }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *a, FORMATETC *b) override { return inner_->GetCanonicalFormatEtc(a, b); }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC *a, STGMEDIUM *b, BOOL c) override { return inner_->SetData(a, b, c); }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD a, IEnumFORMATETC **b) override { return inner_->EnumFormatEtc(a, b); }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *a, DWORD b, IAdviseSink *c, DWORD *d) override { return inner_->DAdvise(a, b, c, d); }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD value) override { return inner_->DUnadvise(value); }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **value) override { return inner_->EnumDAdvise(value); }
};
clipboard::Payload Files(const std::wstring &path) {
    clipboard::Payload payload; payload.format = CF_HDROP;
    DROPFILES header{}; header.pFiles = sizeof(header); header.fWide = TRUE;
    payload.bytes.resize(sizeof(header) + (path.size() + 2) * sizeof(wchar_t));
    std::memcpy(payload.bytes.data(), &header, sizeof(header));
    std::memcpy(payload.bytes.data() + sizeof(header), path.c_str(), (path.size() + 1) * sizeof(wchar_t));
    return payload;
}
int wmain(int argc, wchar_t **argv) {
    clipboard::SetExtraHandler(shelf::HandleTransfer);
    constexpr std::wstring_view helper = L"--clipboard-transfer=";
    if (argc == 2 && std::wstring_view(argv[1]).starts_with(helper)) return clipboard::TransferHelper(argv[1] + helper.size());
    if (FAILED(OleInitialize(nullptr))) return 2;
    int code = 0;
    try {
        auto text = clipboard::TextPayload(L"Synthetic parked text 日本語");
        auto object = shelf::DataObject(text);
        Require(object != nullptr, "bounded text data object");
        FORMATETC format{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        Require(SUCCEEDED(object->GetData(&format, &medium)), "outbound native HGLOBAL text");
        auto data = static_cast<const wchar_t *>(GlobalLock(medium.hGlobal));
        Require(data && std::wstring(data) == L"Synthetic parked text 日本語", "Unicode outbound payload preserved");
        GlobalUnlock(medium.hGlobal); ReleaseStgMedium(&medium);
        format.lindex = 0; Require(object->QueryGetData(&format) == DV_E_LINDEX, "indexed media rejected");
        format.lindex = -1; format.tymed = TYMED_ISTREAM;
        Require(object->QueryGetData(&format) == DV_E_TYMED, "unimplemented medium rejected");
        format.tymed = TYMED_HGLOBAL; format.cfFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT));
        Require(SUCCEEDED(object->GetData(&format, &medium)), "copy-only preferred drop effect");
        auto effect = static_cast<const DWORD *>(GlobalLock(medium.hGlobal));
        Require(effect && *effect == DROPEFFECT_COPY, "move never advertised");
        GlobalUnlock(medium.hGlobal); ReleaseStgMedium(&medium);
        auto link = shelf::DataObject(clipboard::TextPayload(L"https://example.invalid/fixture"));
        format.cfFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"UniformResourceLocatorW"));
        Require(SUCCEEDED(link->QueryGetData(&format)), "links expose native URL format as well as text");
        format.cfFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"CanIncludeInClipboardHistory"));
        Require(SUCCEEDED(link->GetData(&format, &medium)), "clipboard-history exclusion metadata");
        auto history = static_cast<const DWORD *>(GlobalLock(medium.hGlobal));
        Require(history && !*history, "outbound metadata requests no history");
        GlobalUnlock(medium.hGlobal); ReleaseStgMedium(&medium);
        auto files = Files(L"C:\\DakoFixture\\report.txt");
        Require(shelf::Valid(files) && !shelf::Valid(Files(L"\\\\fixture\\share\\report.txt")) &&
            !shelf::Valid(Files(L"C:\\report.txt:stream")), "file metadata validated without opening any file");
        HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Require(cancel != nullptr, "cancel event");
        for (const auto &payload : {text, files}) {
            Microsoft::WRL::ComPtr<IDataObject> source;
            source.Attach(new Padded(shelf::DataObject(payload)));
            shelf::MarshaledDrop marshaled;
            auto packet = marshaled.Capture(source.Get());
            Require(packet && packet->bytes.size() <= shelf::MarshalLimit, "local COM marshal packet bounded");
            auto reply = clipboard::Transfer(clipboard::TransferOp::FixtureDrop, *packet, cancel, 3000, L"shelf-fixture");
            Require(reply.status == clipboard::TransferStatus::Ok && reply.childExited && reply.payload.format == payload.format &&
                reply.payload.bytes == payload.bytes, "owned helper reads synthetic OLE source through COM and exits");
            marshaled.Clear();
        }
        auto source = shelf::DataObject(text);
        shelf::MarshaledDrop cancelled;
        auto packet = cancelled.Capture(source.Get());
        Require(packet.has_value(), "cancellable marshal packet");
        SetEvent(cancel);
        auto reply = clipboard::Transfer(clipboard::TransferOp::FixtureDrop, *packet, cancel, 3000, L"shelf-fixture");
        Require(reply.status == clipboard::TransferStatus::Cancelled && reply.childExited, "cancelled drop helper exits before marshal release");
        cancelled.Clear(); CloseHandle(cancel);
        std::puts("PASS native OLE text/link/file data, copy-only effects, bounded helper receive and cancellation; no clipboard/file-content IO");
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s (Win32 %lu)\n", error.what(), GetLastError()); code = 1;
    }
    OleUninitialize(); return code;
}
