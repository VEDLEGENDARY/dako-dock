#include "WindowsFileSearch.h"
#include "FileSearchModel.h"
#include <searchapi.h>
#include <oleauto.h>
#include <wrl/client.h>
#include <initializer_list>
#include <iterator>

namespace files {
namespace {
using Microsoft::WRL::ComPtr;
struct Apartment {
    HRESULT status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~Apartment() { if (SUCCEEDED(status)) CoUninitialize(); }
};
class Value {
public:
    VARIANT raw{};
    Value() { VariantInit(&raw); }
    ~Value() { VariantClear(&raw); }
    Value(const Value &) = delete;
    Value &operator=(const Value &) = delete;
    explicit Value(const std::wstring &text) : Value() { raw.vt = VT_BSTR; raw.bstrVal = SysAllocStringLen(text.data(), static_cast<UINT>(text.size())); }
    explicit Value(long number) : Value() { raw.vt = VT_I4; raw.lVal = number; }
    explicit Value(IDispatch *object) : Value() { raw.vt = VT_DISPATCH; raw.pdispVal = object; if (object) object->AddRef(); }
    VARIANT *Out() { VariantClear(&raw); VariantInit(&raw); return &raw; }
    IDispatch *Object() const { return raw.vt == VT_DISPATCH ? raw.pdispVal : nullptr; }
    std::optional<std::wstring> Text(size_t limit) const {
        if (raw.vt != VT_BSTR || !raw.bstrVal || SysStringLen(raw.bstrVal) > limit) return {};
        std::wstring text(raw.bstrVal, SysStringLen(raw.bstrVal));
        if (text.find(L'\0') != std::wstring::npos) return {};
        return text;
    }
};
HRESULT Call(IDispatch *object, const wchar_t *name, WORD flags, std::initializer_list<const Value *> inputs, Value &output) {
    if (!object) return E_POINTER;
    LPOLESTR names[]{const_cast<LPOLESTR>(name)};
    DISPID id = DISPID_UNKNOWN;
    HRESULT result = object->GetIDsOfNames(IID_NULL, names, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(result)) return result;
    std::vector<VARIANT> arguments;
    for (auto input = std::rbegin(inputs); input != std::rend(inputs); ++input) arguments.push_back((*input)->raw);
    DISPID put = DISPID_PROPERTYPUT;
    DISPPARAMS params{arguments.data(), flags == DISPATCH_PROPERTYPUT ? &put : nullptr,
                     static_cast<UINT>(arguments.size()), flags == DISPATCH_PROPERTYPUT ? 1U : 0U};
    EXCEPINFO exception{};
    result = object->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &params, output.Out(), &exception, nullptr);
    if (exception.pfnDeferredFillIn) exception.pfnDeferredFillIn(&exception);
    if (result == DISP_E_EXCEPTION && FAILED(exception.scode)) result = exception.scode;
    SysFreeString(exception.bstrSource); SysFreeString(exception.bstrDescription); SysFreeString(exception.bstrHelpFile);
    return result;
}
HRESULT Create(const wchar_t *name, ComPtr<IDispatch> &object) {
    CLSID id{};
    // Read registration without CLSIDFromProgID's legacy create-on-missing behavior.
    wchar_t value[64]{};
    DWORD bytes = sizeof(value);
    auto key = std::wstring(name) + L"\\CLSID";
    LSTATUS read = RegGetValueW(HKEY_CLASSES_ROOT, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, value, &bytes);
    if (read != ERROR_SUCCESS) return HRESULT_FROM_WIN32(read);
    if (wcslen(value) != 38 || value[0] != L'{') return CO_E_CLASSSTRING;
    HRESULT result = CLSIDFromString(value, &id);
    return FAILED(result) ? result : CoCreateInstance(id, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&object));
}
HRESULT Query(const std::wstring &input, std::vector<Result> &results) {
    auto sql = Sql(input);
    if (!sql) return E_INVALIDARG;
    Apartment apartment;
    if (FAILED(apartment.status)) return apartment.status;
    ComPtr<ISearchManager> manager;
    HRESULT result = CoCreateInstance(__uuidof(CSearchManager), nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                                      IID_PPV_ARGS(&manager));
    if (FAILED(result)) return result;
    ComPtr<ISearchCatalogManager> catalog;
    if (FAILED(result = manager->GetCatalog(L"SystemIndex", &catalog))) return result;
    ComPtr<ISearchQueryHelper> helper;
    if (FAILED(result = catalog->GetQueryHelper(&helper))) return result;
    PWSTR connectionText = nullptr;
    if (FAILED(result = helper->get_ConnectionString(&connectionText))) return result;
    struct TextScope { PWSTR text; ~TextScope() { CoTaskMemFree(text); } } textScope{connectionText};
    if (!connectionText || wcslen(connectionText) > 32768) return E_INVALIDARG;
    ComPtr<IDispatch> connection, records;
    if (FAILED(result = Create(L"ADODB.Connection", connection)) || FAILED(result = Create(L"ADODB.Recordset", records))) return result;
    Value out, one(1L), four(4L), client(3L), unspecified(-1L), empty(std::wstring{}), zero(0L);
    if (FAILED(result = Call(connection.Get(), L"ConnectionTimeout", DISPATCH_PROPERTYPUT, {&one}, out)) ||
        FAILED(result = Call(connection.Get(), L"CommandTimeout", DISPATCH_PROPERTYPUT, {&four}, out)) ||
        FAILED(result = Call(connection.Get(), L"CursorLocation", DISPATCH_PROPERTYPUT, {&client}, out))) return result;
    Value connectionString{std::wstring(connectionText)};
    if (FAILED(result = Call(connection.Get(), L"Open", DISPATCH_METHOD, {&connectionString, &empty, &empty, &unspecified}, out))) return result;
    Value statement(*sql), source(connection.Get());
    if (FAILED(result = Call(records.Get(), L"Open", DISPATCH_METHOD, {&statement, &source, &zero, &one, &one}, out))) return result;
    Value fields, urlField, nameField, urlName(std::wstring(L"System.ItemUrl")), displayName(std::wstring(L"System.ItemNameDisplay"));
    if (FAILED(result = Call(records.Get(), L"Fields", DISPATCH_PROPERTYGET, {}, fields)) ||
        FAILED(result = Call(fields.Object(), L"Item", DISPATCH_PROPERTYGET, {&urlName}, urlField)) ||
        FAILED(result = Call(fields.Object(), L"Item", DISPATCH_PROPERTYGET, {&displayName}, nameField))) return result;
    for (size_t row = 0; row < ResultLimit; ++row) {
        Value eof;
        if (FAILED(result = Call(records.Get(), L"EOF", DISPATCH_PROPERTYGET, {}, eof))) return result;
        if (eof.raw.vt != VT_BOOL) return E_UNEXPECTED;
        if (eof.raw.boolVal != VARIANT_FALSE) break;
        Value url, name;
        if (FAILED(result = Call(urlField.Object(), L"Value", DISPATCH_PROPERTYGET, {}, url)) ||
            FAILED(result = Call(nameField.Object(), L"Value", DISPATCH_PROPERTYGET, {}, name))) return result;
        auto uri = url.Text(32768), title = name.Text(512);
        auto path = uri ? LocalUrl(*uri) : std::nullopt;
        if (path) {
            auto label = title && !title->empty() ? *title : std::filesystem::path(*path).filename().wstring();
            if (!label.empty() && label.size() <= 512) results.push_back({std::move(label), std::move(*path)});
        }
        if (FAILED(result = Call(records.Get(), L"MoveNext", DISPATCH_METHOD, {}, out))) return result;
    }
    Call(records.Get(), L"Close", DISPATCH_METHOD, {}, out);
    Call(connection.Get(), L"Close", DISPATCH_METHOD, {}, out);
    return S_OK;
}
}
clipboard::TransferStatus HandleTransfer(clipboard::TransferOp operation, clipboard::Payload &payload, DWORD &error) {
    if (operation != clipboard::TransferOp::SearchFiles && operation != clipboard::TransferOp::FixtureFiles)
        return clipboard::TransferStatus::Unsupported;
    std::vector<Result> results;
    HRESULT result = S_OK;
    if (operation == clipboard::TransferOp::FixtureFiles) results = {{L"Synthetic report.txt", L"C:\\DakoFixture\\report.txt"}};
    else result = Query(clipboard::PayloadText(payload), results);
    error = static_cast<DWORD>(result);
    if (FAILED(result)) return clipboard::TransferStatus::Failed;
    auto bytes = Encode(results);
    if (!bytes) return clipboard::TransferStatus::Unsupported;
    payload.format = CF_PRIVATEFIRST;
    payload.bytes = std::move(*bytes);
    return clipboard::TransferStatus::Ok;
}
}
