#include "ShelfOle.h"
#include "ClipboardData.h"
#include "FileSearchModel.h"
#include <ole2.h>
#include <shlobj.h>
#include <atomic>

namespace shelf {
namespace {
using Microsoft::WRL::ComPtr;
FORMATETC Format(UINT format) { return {static_cast<CLIPFORMAT>(format), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}; }
HRESULT Read(IDataObject *object, UINT format, clipboard::Payload &payload) {
    auto request = Format(format);
    STGMEDIUM medium{};
    HRESULT result = object->GetData(&request, &medium);
    if (FAILED(result)) return result;
    struct Release { STGMEDIUM &value; ~Release() { ReleaseStgMedium(&value); } } release{medium};
    if (medium.tymed != TYMED_HGLOBAL || !medium.hGlobal) return DV_E_TYMED;
    auto size = GlobalSize(medium.hGlobal);
    if (!size || size > clipboard::TransferLimit) return STG_E_MEDIUMFULL;
    auto *data = static_cast<const std::byte *>(GlobalLock(medium.hGlobal));
    if (!data) return E_FAIL;
    if (format != CF_HDROP) {
        if (size % sizeof(wchar_t)) { GlobalUnlock(medium.hGlobal); return DV_E_FORMATETC; }
        const auto *text = reinterpret_cast<const wchar_t *>(data);
        size_t length = 0, limit = size / sizeof(wchar_t);
        while (length < limit && text[length]) ++length;
        if (length == limit) { GlobalUnlock(medium.hGlobal); return DV_E_FORMATETC; }
        size = (length + 1) * sizeof(wchar_t);
    }
    try { payload.format = format; payload.bytes.assign(data, data + size); }
    catch (...) { GlobalUnlock(medium.hGlobal); return E_OUTOFMEMORY; }
    GlobalUnlock(medium.hGlobal);
    return S_OK;
}
class Object final : public IDataObject {
    std::atomic<ULONG> refs_ = 1;
    clipboard::Payload payload_;
    UINT url_ = RegisterClipboardFormatW(L"UniformResourceLocatorW");
    UINT preferred_ = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    UINT excluded_ = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
    UINT history_ = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
    UINT cloud_ = RegisterClipboardFormatW(L"CanUploadToCloudClipboard");
    bool link_ = false;
    bool Link() const { return link_; }
public:
    explicit Object(clipboard::Payload payload) : payload_(std::move(payload)) {
        if (payload_.format == CF_UNICODETEXT) {
            auto text = clipboard::PayloadText(payload_);
            link_ = !_wcsnicmp(text.c_str(), L"https://", 8) || !_wcsnicmp(text.c_str(), L"http://", 7);
        }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (id != IID_IUnknown && id != IID_IDataObject) return E_NOINTERFACE;
        *out = static_cast<IDataObject *>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto left = --refs_; if (!left) delete this; return left; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *format) override {
        if (!format) return E_POINTER;
        if (!(format->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
        if (format->dwAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
        if (format->lindex != -1) return DV_E_LINDEX;
        if (format->ptd) return DV_E_DVTARGETDEVICE;
        UINT id = format->cfFormat;
        return id == payload_.format || (url_ && id == url_ && Link()) || (preferred_ && id == preferred_) ||
            (excluded_ && id == excluded_) || (history_ && id == history_) || (cloud_ && id == cloud_) ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC *format, STGMEDIUM *medium) override {
        if (!medium) return E_POINTER;
        *medium = {};
        HRESULT result = QueryGetData(format); if (FAILED(result)) return result;
        DWORD value = format->cfFormat == preferred_ ? DROPEFFECT_COPY : 0;
        bool data = format->cfFormat == payload_.format || (format->cfFormat == url_ && Link());
        size_t bytes = data ? payload_.bytes.size() : sizeof(value);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!memory) return E_OUTOFMEMORY;
        void *target = GlobalLock(memory);
        if (!target) { GlobalFree(memory); return E_OUTOFMEMORY; }
        std::memcpy(target, data ? static_cast<const void *>(payload_.bytes.data()) : &value, bytes);
        GlobalUnlock(memory);
        medium->tymed = TYMED_HGLOBAL; medium->hGlobal = memory;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *, STGMEDIUM *) override { return DATA_E_FORMATETC; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *, FORMATETC *out) override {
        if (!out) return E_POINTER; out->ptd = nullptr; return DATA_S_SAMEFORMATETC;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC *, STGMEDIUM *, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC **out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (direction != DATADIR_GET) return E_NOTIMPL;
        FORMATETC formats[6]{};
        UINT count = 0; formats[count++] = Format(payload_.format);
        for (UINT id : {preferred_, excluded_, history_, cloud_}) if (id) formats[count++] = Format(id);
        if (url_ && Link()) formats[count++] = Format(url_);
        return SHCreateStdEnumFmtEtc(count, formats, out);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **out) override { if (out) *out = nullptr; return OLE_E_ADVISENOTSUPPORTED; }
};
class Source final : public IDropSource {
    std::atomic<ULONG> refs_ = 1;
    std::function<bool()> cancelled_;
public:
    explicit Source(std::function<bool()> cancelled) : cancelled_(std::move(cancelled)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (id != IID_IUnknown && id != IID_IDropSource) return E_NOINTERFACE;
        *out = static_cast<IDropSource *>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto left = --refs_; if (!left) delete this; return left; }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape || (cancelled_ && cancelled_())) return DRAGDROP_S_CANCEL;
        return keys & MK_LBUTTON ? S_OK : DRAGDROP_S_DROP;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};
}
bool Valid(const clipboard::Payload &payload) {
    if (payload.format == CF_UNICODETEXT) return !clipboard::PayloadText(payload).empty();
    auto paths = clipboard::FileList(payload);
    return paths && std::all_of(paths->begin(), paths->end(), files::LocalPath);
}
ComPtr<IDataObject> DataObject(clipboard::Payload payload) {
    ComPtr<IDataObject> result;
    if (Valid(payload)) result.Attach(new Object(std::move(payload)));
    return result;
}
HRESULT Drag(const clipboard::Payload &payload, std::function<bool()> cancelled) {
    auto object = DataObject(payload);
    if (!object) return E_INVALIDARG;
    ComPtr<IDropSource> source; source.Attach(new Source(std::move(cancelled)));
    DWORD effect = DROPEFFECT_NONE;
    return DoDragDrop(object.Get(), source.Get(), DROPEFFECT_COPY, &effect);
}
void MarshaledDrop::Clear() noexcept {
    if (!stream_) return;
    ComPtr<IStream> released;
    released.Swap(stream_); // Detach before COM pumps messages; a nested Stop must not release this packet twice.
    LARGE_INTEGER zero{};
    if (SUCCEEDED(released->Seek(zero, STREAM_SEEK_SET, nullptr))) CoReleaseMarshalData(released.Get());
}
std::optional<clipboard::Payload> MarshaledDrop::Capture(IDataObject *object) {
    Clear();
    MarshaledDrop candidate;
    if (!object || FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &candidate.stream_))) return {};
    if (FAILED(CoMarshalInterface(candidate.stream_.Get(), IID_IDataObject, object, MSHCTX_LOCAL, nullptr, MSHLFLAGS_TABLESTRONG))) {
        candidate.stream_.Reset(); return {};
    }
    STATSTG stat{};
    LARGE_INTEGER zero{};
    if (FAILED(candidate.stream_->Stat(&stat, STATFLAG_NONAME)) || !stat.cbSize.QuadPart || stat.cbSize.QuadPart > MarshalLimit ||
        FAILED(candidate.stream_->Seek(zero, STREAM_SEEK_SET, nullptr))) return {};
    clipboard::Payload payload; payload.format = CF_PRIVATEFIRST;
    payload.bytes.resize(static_cast<size_t>(stat.cbSize.QuadPart));
    ULONG read = 0;
    if (FAILED(candidate.stream_->Read(payload.bytes.data(), static_cast<ULONG>(payload.bytes.size()), &read)) || read != payload.bytes.size()) return {};
    stream_.Swap(candidate.stream_);
    return payload;
}
clipboard::TransferStatus HandleTransfer(clipboard::TransferOp operation, clipboard::Payload &payload, DWORD &error) {
    if (operation != clipboard::TransferOp::ReadDrop && operation != clipboard::TransferOp::FixtureDrop)
        return clipboard::TransferStatus::Unsupported;
    bool fixture = operation == clipboard::TransferOp::FixtureDrop;
    if (payload.format != CF_PRIVATEFIRST || payload.bytes.empty() || payload.bytes.size() > MarshalLimit)
        return clipboard::TransferStatus::Unsupported;
    HRESULT apartment = OleInitialize(nullptr);
    if (FAILED(apartment)) { error = static_cast<DWORD>(apartment); return clipboard::TransferStatus::Failed; }
    struct Apartment { ~Apartment() { OleUninitialize(); } } scope;
    ComPtr<IStream> stream;
    HRESULT result = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    ULONG written = 0;
    if (SUCCEEDED(result)) result = stream->Write(payload.bytes.data(), static_cast<ULONG>(payload.bytes.size()), &written);
    LARGE_INTEGER zero{};
    if (SUCCEEDED(result) && written != payload.bytes.size()) result = E_FAIL;
    if (SUCCEEDED(result)) result = stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ComPtr<IDataObject> object;
    if (SUCCEEDED(result)) result = CoUnmarshalInterface(stream.Get(), IID_PPV_ARGS(&object));
    if (FAILED(result)) { error = static_cast<DWORD>(result); return clipboard::TransferStatus::Failed; }
    if (!fixture && !clipboard::SessionUnlocked()) return clipboard::TransferStatus::Cancelled;
    // This is an explicit drop, not a clipboard-history capture. No clipboard API is called.
    UINT url = RegisterClipboardFormatW(L"UniformResourceLocatorW");
    for (UINT format : {static_cast<UINT>(CF_HDROP), static_cast<UINT>(CF_UNICODETEXT), url}) {
        if (!format) continue;
        clipboard::Payload returned;
        if (FAILED(Read(object.Get(), format, returned))) continue;
        if (format == url) returned.format = CF_UNICODETEXT;
        if (!Valid(returned)) continue;
        if (!fixture && !clipboard::SessionUnlocked()) return clipboard::TransferStatus::Cancelled;
        payload = std::move(returned);
        return clipboard::TransferStatus::Ok;
    }
    return clipboard::TransferStatus::Unsupported;
}
}
