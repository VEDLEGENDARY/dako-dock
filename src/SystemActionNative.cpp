#include "SystemActionNative.h"
#include "SystemActionModel.h"
#include <shellapi.h>
#include <winioctl.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <shlobj.h>
#include <ole2.h>
#include <map>
#include <set>
#include <optional>

namespace systemactions {
namespace {
// DEVPKEY_Device_LastArrivalDate, defined by the installed Windows SDK's devpkey.h.
constexpr DEVPROPKEY ArrivalKey{{0x83da6326, 0x97a6, 0x4088, {0x94, 0x53, 0xa1, 0x92, 0x3f, 0x57, 0x3b, 0x29}}, 102};
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
struct DeviceSet {
    HDEVINFO value;
    ~DeviceSet() {
        if (value != INVALID_HANDLE_VALUE)
            SetupDiDestroyDeviceInfoList(value);
    }
};
struct Candidate {
    Drive drive;
    DEVINST node = 0;
    DWORD disk = 0;
};
std::optional<uint64_t> Arrival(DEVINST node) {
    DEVPROPTYPE type = 0;
    FILETIME time{};
    ULONG size = sizeof(time);
    if (CM_Get_DevNode_PropertyW(node, &ArrivalKey, &type, reinterpret_cast<BYTE *>(&time), &size, 0) != CR_SUCCESS ||
        type != DEVPROP_TYPE_FILETIME || size != sizeof(time))
        return {};
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart ? std::make_optional(value.QuadPart) : std::nullopt;
}
std::optional<DWORD> Number(const std::wstring &path) {
    wchar_t root[32768]{}, volume[128]{};
    if (!GetVolumePathNameW(path.c_str(), root, 32768) || !GetVolumeNameForVolumeMountPointW(root, volume, 128))
        return {};
    std::wstring device(volume);
    if (!device.empty() && device.back() == L'\\')
        device.pop_back();
    Handle handle(
        CreateFileW(device.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (handle.value == INVALID_HANDLE_VALUE)
        return {};
    STORAGE_DEVICE_NUMBER number{};
    DWORD returned = 0;
    if (!DeviceIoControl(handle.value, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &number, sizeof(number), &returned,
                         nullptr) ||
        returned < sizeof(number) || number.DeviceType != FILE_DEVICE_DISK)
        return {};
    return number.DeviceNumber;
}
std::optional<std::wstring> Property(HDEVINFO set, SP_DEVINFO_DATA &device, DWORD property) {
    wchar_t text[256]{};
    DWORD type = 0, bytes = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(set, &device, property, &type, reinterpret_cast<BYTE *>(text), sizeof(text),
                                           &bytes) ||
        type != REG_SZ || bytes < sizeof(wchar_t) || bytes > sizeof(text) || bytes % sizeof(wchar_t) ||
        text[bytes / sizeof(wchar_t) - 1])
        return {};
    std::wstring result(text);
    return Text(result, 256) ? std::make_optional(std::move(result)) : std::nullopt;
}
bool Enumerate(std::vector<Candidate> &candidates, DWORD &error) {
    wchar_t windows[32768]{}, executable[32768]{};
    UINT windowsSize = GetWindowsDirectoryW(windows, 32768);
    DWORD executableSize = GetModuleFileNameW(nullptr, executable, 32768);
    if (!windowsSize || windowsSize >= 32768 || !executableSize || executableSize >= 32768) {
        error = ERROR_INSUFFICIENT_BUFFER;
        return false;
    }
    auto systemDisk = Number(windows);
    if (!systemDisk) {
        error = ERROR_ACCESS_DENIED;
        return false;
    }
    auto ownDisk = Number(executable);
    wchar_t ownRoot[32768]{};
    if (!ownDisk && (!GetVolumePathNameW(executable, ownRoot, 32768) || GetDriveTypeW(ownRoot) != DRIVE_REMOTE)) {
        error = ERROR_ACCESS_DENIED;
        return false;
    }
    std::map<DWORD, std::wstring> mounted;
    DWORD logical = GetLogicalDrives();
    if (!logical) {
        error = GetLastError();
        return false;
    }
    for (unsigned letter = 0; letter < 26; ++letter)
        if (logical & (1UL << letter)) {
            wchar_t root[]{static_cast<wchar_t>(L'A' + letter), L':', L'\\', 0};
            UINT type = GetDriveTypeW(root);
            if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE && type != DRIVE_CDROM)
                continue;
            auto disk = Number(root);
            if (disk && *disk != *systemDisk && (!ownDisk || *disk != *ownDisk))
                mounted[*disk] += std::wstring(root) + L" ";
        }
    DeviceSet set{
        SetupDiGetClassDevsW(&GUID_DEVINTERFACE_DISK, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)};
    if (set.value == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }
    std::set<std::wstring> seen;
    for (DWORD index = 0; index < 1024; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{sizeof(interfaceData)};
        if (!SetupDiEnumDeviceInterfaces(set.value, nullptr, &GUID_DEVINTERFACE_DISK, index, &interfaceData)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS)
                return true;
            error = GetLastError();
            return false;
        }
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(set.value, &interfaceData, nullptr, 0, &needed, nullptr);
        if (needed < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || needed > 65536)
            continue;
        std::vector<std::byte> bytes(needed);
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(bytes.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA device{sizeof(device)};
        if (!SetupDiGetDeviceInterfaceDetailW(set.value, &interfaceData, detail, needed, nullptr, &device))
            continue;
        DWORD type = 0, capabilities = 0, size = 0;
        if (!SetupDiGetDeviceRegistryPropertyW(set.value, &device, SPDRP_CAPABILITIES, &type,
                                               reinterpret_cast<BYTE *>(&capabilities), sizeof(capabilities), &size) ||
            type != REG_DWORD || size != sizeof(capabilities) || !(capabilities & CM_DEVCAP_REMOVABLE) ||
            (capabilities & CM_DEVCAP_DOCKDEVICE))
            continue;
        Handle handle(
            CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        STORAGE_DEVICE_NUMBER number{};
        DWORD returned = 0;
        if (handle.value == INVALID_HANDLE_VALUE ||
            !DeviceIoControl(handle.value, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &number, sizeof(number),
                             &returned, nullptr) ||
            returned < sizeof(number) || number.DeviceType != FILE_DEVICE_DISK ||
            !mounted.contains(number.DeviceNumber))
            continue;
        wchar_t id[512]{};
        DWORD idSize = 0;
        if (!SetupDiGetDeviceInstanceIdW(set.value, &device, id, 512, &idSize) || !idSize || idSize > 512)
            continue;
        auto label = Property(set.value, device, SPDRP_FRIENDLYNAME);
        if (!label)
            label = Property(set.value, device, SPDRP_DEVICEDESC);
        if (!label)
            continue;
        auto arrived = Arrival(device.DevInst);
        if (!arrived)
            continue;
        Drive drive{id, *label, mounted[number.DeviceNumber], *arrived};
        if (!Valid(drive) || !seen.insert(drive.id).second)
            continue;
        candidates.push_back({std::move(drive), device.DevInst, number.DeviceNumber});
        if (candidates.size() >= 32)
            return true;
    }
    error = ERROR_MORE_DATA;
    return false;
}
} // namespace
clipboard::TransferStatus HandleTransfer(clipboard::TransferOp operation, clipboard::Payload &payload, DWORD &error) {
    if (operation != clipboard::TransferOp::QueryRecycleBin && operation != clipboard::TransferOp::QueryDrives &&
        operation != clipboard::TransferOp::EjectDrive)
        return clipboard::TransferStatus::Unsupported;
    HRESULT apartment = OleInitialize(nullptr);
    if (FAILED(apartment)) {
        error = static_cast<DWORD>(apartment);
        return clipboard::TransferStatus::Failed;
    }
    struct Apartment {
        ~Apartment() { OleUninitialize(); }
    } scope;
    if (operation == clipboard::TransferOp::QueryRecycleBin) {
        SHQUERYRBINFO info{sizeof(info)};
        HRESULT result = SHQueryRecycleBinW(nullptr, &info);
        if (FAILED(result) || info.i64NumItems < 0 || info.i64Size < 0) {
            error = static_cast<DWORD>(FAILED(result) ? result : E_UNEXPECTED);
            return clipboard::TransferStatus::Failed;
        }
        payload = Encode(BinTotals{static_cast<uint64_t>(info.i64NumItems), static_cast<uint64_t>(info.i64Size)});
        return clipboard::TransferStatus::Ok;
    }
    auto requested = operation == clipboard::TransferOp::EjectDrive ? DecodeDrives(payload) : std::nullopt;
    if (operation == clipboard::TransferOp::EjectDrive) {
        if (!requested || requested->size() != 1) {
            error = ERROR_INVALID_DATA;
            return clipboard::TransferStatus::Failed;
        }
        if (!clipboard::SessionUnlocked()) {
            error = ERROR_ACCESS_DENIED;
            return clipboard::TransferStatus::Cancelled;
        }
    }
    std::vector<Candidate> candidates;
    if (!Enumerate(candidates, error))
        return clipboard::TransferStatus::Failed;
    if (operation == clipboard::TransferOp::QueryDrives) {
        std::vector<Drive> drives;
        for (auto &candidate : candidates)
            drives.push_back(std::move(candidate.drive));
        auto encoded = Encode(drives);
        if (!encoded)
            return clipboard::TransferStatus::Unsupported;
        payload = std::move(*encoded);
        return clipboard::TransferStatus::Ok;
    }
    const auto &wanted = requested->front();
    for (const auto &candidate : candidates) {
        if (!SameTarget(wanted, candidate.drive))
            continue;
        if (!clipboard::SessionUnlocked())
            return clipboard::TransferStatus::Cancelled;
        ULONG status = 0, problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, candidate.node, 0) != CR_SUCCESS || !(status & DN_STARTED)) {
            error = ERROR_NOT_READY;
            return clipboard::TransferStatus::Failed;
        }
        wchar_t currentId[512]{};
        auto arrived = Arrival(candidate.node);
        if (!arrived || *arrived != wanted.arrived ||
            CM_Get_Device_IDW(candidate.node, currentId, 512, 0) != CR_SUCCESS ||
            _wcsicmp(currentId, wanted.id.c_str())) {
            error = ERROR_NOT_FOUND;
            return clipboard::TransferStatus::Changed;
        }
        PNP_VETO_TYPE veto = PNP_VetoTypeUnknown;
        wchar_t reason[MAX_PATH]{};
        // PnP performs the normal safe-removal/veto protocol across this device. Never force a raw dismount.
        CONFIGRET result = CM_Request_Device_EjectW(candidate.node, &veto, reason, MAX_PATH, 0);
        error = CM_MapCrToWin32Err(result, ERROR_GEN_FAILURE);
        payload.bytes.clear();
        return result == CR_SUCCESS ? clipboard::TransferStatus::Ok : clipboard::TransferStatus::Failed;
    }
    error = ERROR_NOT_FOUND;
    return clipboard::TransferStatus::Changed;
}
} // namespace systemactions
