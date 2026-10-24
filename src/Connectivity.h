#pragma once
#include <winioctl.h>
#include <bthdef.h>
#include <bthioctl.h>
#include <map>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Rfcomm.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.Collections.h>

constexpr UINT ConnectivityChanged=WM_APP+6;
struct WifiNetwork {
    GUID adapter{};
    DOT11_SSID ssid{};
    std::wstring name,profile;
    ULONG quality=0;
    bool secure=false,connected=false,connectable=false;
    DOT11_AUTH_ALGORITHM authentication{};
    DOT11_CIPHER_ALGORITHM cipher{};
};
struct BluetoothEntry {
    BLUETOOTH_DEVICE_INFO native{sizeof(BLUETOOTH_DEVICE_INFO)};
    std::wstring name;
    std::wstring identifier;
    bool paired=false,connected=false,lowEnergy=false;
};
struct ConnectionState {
    std::vector<WifiNetwork> networks;
    std::vector<BluetoothEntry> devices;
    bool wifiAvailable=false,wifiOn=false,bluetoothAvailable=false,bluetoothOn=false;
    bool wifiBusy=false,bluetoothBusy=false;
    bool wifiConnecting=false;
    std::wstring wifiMessage,bluetoothMessage;
};

// Separate queues prevent a slow Bluetooth inquiry from delaying Wi-Fi operations.
class Connectivity {
    using DeviceInformation=winrt::Windows::Devices::Enumeration::DeviceInformation;
    struct LeState {
        std::mutex mutex;
        std::map<std::wstring,DeviceInformation> devices;
        std::map<ULONGLONG,bool> connections;
        std::atomic<bool> alive{true};
        HWND window=nullptr;
        void Changed(){if(alive)PostMessageW(window,ConnectivityChanged,0,0);}
    };
    struct LeLease {
        winrt::Windows::Devices::Bluetooth::BluetoothLEDevice device{nullptr};
        winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattSession session{nullptr};
        winrt::event_token token{};
        void Close(){try{if(device)device.ConnectionStatusChanged(token);if(session){session.MaintainConnection(false);session.Close();}if(device)device.Close();}catch(const winrt::hresult_error&){}session=nullptr;device=nullptr;}
    };
    class Worker {
        std::mutex mutex_;
        std::condition_variable wake_;
        std::deque<std::function<void()>> jobs_;
        bool stop_=false;
        std::thread thread_;
    public:
        Worker():thread_([this] {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            for (;;) {
                std::function<void()> job;
                { std::unique_lock lock(mutex_); wake_.wait(lock,[this] {return stop_||!jobs_.empty();});
                  if(stop_)break; job=std::move(jobs_.front());jobs_.pop_front(); }
                try {job();}catch(const winrt::hresult_error&){}catch(const std::exception&){}
            }
            winrt::uninit_apartment();
        }){}
        void Stop(){{std::scoped_lock lock(mutex_);stop_=true;jobs_.clear();}wake_.notify_one();if(thread_.joinable())thread_.join();}
        ~Worker(){Stop();}
        void Post(std::function<void()> job) {{std::scoped_lock lock(mutex_);if(stop_)return;jobs_.push_back(std::move(job));}wake_.notify_one();}
    };
    HWND window_;
    std::mutex stateMutex_;
    ConnectionState state_;
    HANDLE wlan_=nullptr;
    std::vector<GUID> adapters_;
    std::wstring wifiTarget_;
    std::map<ULONGLONG,winrt::Windows::Devices::Bluetooth::BluetoothDevice> deviceLeases_;
    std::atomic<bool> shuttingDown_{false};
    std::shared_ptr<LeState> leState_=std::make_shared<LeState>();
    winrt::Windows::Devices::Enumeration::DeviceWatcher leWatcher_{nullptr};
    std::array<winrt::event_token,3> leTokens_{};
    bool leDiscovering_=false;
    ULONGLONG leDeadline_=0;
    std::map<ULONGLONG,LeLease> leLeases_;
    Worker wifiWorker_,bluetoothWorker_;
    void Changed(){if(!shuttingDown_)PostMessageW(window_,ConnectivityChanged,0,0);}
    void Message(bool bluetooth,std::wstring text) {
        {std::scoped_lock lock(stateMutex_);(bluetooth?state_.bluetoothMessage:state_.wifiMessage)=std::move(text);(bluetooth?state_.bluetoothBusy:state_.wifiBusy)=false;}
        Changed();
    }
    static std::wstring Error(DWORD code) {
        if(code==ERROR_ACCESS_DENIED)return L"Windows denied access. Choose More settings to review permissions.";
        if(code==ERROR_NDIS_DOT11_POWER_STATE_INVALID)return L"Wi-Fi is switched off.";
        wchar_t* message=nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,reinterpret_cast<PWSTR>(&message),0,nullptr);
        std::wstring result=message?message:L"Windows could not complete this request.";
        if(message)LocalFree(message);
        while(!result.empty()&&(result.back()==L'\r'||result.back()==L'\n'))result.pop_back();
        return result;
    }
    static std::wstring Xml(const std::wstring& text) {
        std::wstring result;
        for(wchar_t c:text)switch(c){case L'&':result+=L"&amp;";break;case L'<':result+=L"&lt;";break;case L'>':result+=L"&gt;";break;case L'\"':result+=L"&quot;";break;case L'\'':result+=L"&apos;";break;default:result+=c;}
        return result;
    }
    void OpenWifi() {
        if(wlan_)return;
        DWORD version=0,result=WlanOpenHandle(2,nullptr,&version,&wlan_);
        if(result!=ERROR_SUCCESS){wlan_=nullptr;Message(false,Error(result));return;}
        WlanRegisterNotification(wlan_,WLAN_NOTIFICATION_SOURCE_ACM,TRUE,[](PWLAN_NOTIFICATION_DATA data,PVOID context) {
            auto* self=static_cast<Connectivity*>(context);
            if(self->shuttingDown_||data->NotificationSource!=WLAN_NOTIFICATION_SOURCE_ACM)return;
            if(data->NotificationCode==wlan_notification_acm_connection_complete||data->NotificationCode==wlan_notification_acm_connection_attempt_fail||data->NotificationCode==wlan_notification_acm_disconnected){
                {std::scoped_lock lock(self->stateMutex_);self->state_.wifiConnecting=false;}
                if(data->NotificationCode==wlan_notification_acm_connection_complete&&data->dwDataSize>=sizeof(WLAN_CONNECTION_NOTIFICATION_DATA)){
                    auto* connection=static_cast<WLAN_CONNECTION_NOTIFICATION_DATA*>(data->pData);
                    if(connection->wlanReasonCode==WLAN_REASON_CODE_SUCCESS)self->Message(false,L"Connected.");
                    else{wchar_t reason[512]{};WlanReasonCodeToString(connection->wlanReasonCode,512,reason,nullptr);self->Message(false,reason);}
                }else if(data->NotificationCode==wlan_notification_acm_disconnected)self->Message(false,L"Not connected.");
            }
            if(data->NotificationCode==wlan_notification_acm_connection_attempt_fail && data->dwDataSize>=sizeof(WLAN_CONNECTION_NOTIFICATION_DATA)) {
                auto* connection=static_cast<WLAN_CONNECTION_NOTIFICATION_DATA*>(data->pData);
                wchar_t reason[512]{};WlanReasonCodeToString(connection->wlanReasonCode,512,reason,nullptr);self->Message(false,reason);
            }
            if(data->NotificationCode==wlan_notification_acm_scan_complete||data->NotificationCode==wlan_notification_acm_connection_complete||
               data->NotificationCode==wlan_notification_acm_disconnected||data->NotificationCode==wlan_notification_acm_connection_attempt_fail)
                self->wifiWorker_.Post([self]{self->ReadWifi();});
        },this,nullptr,nullptr);
    }
    static ULONGLONG LeAddress(const DeviceInformation& device){
        auto properties=device.Properties();if(!properties.HasKey(L"System.Devices.Aep.DeviceAddress"))return 0;
        auto text=winrt::unbox_value_or<winrt::hstring>(properties.Lookup(L"System.Devices.Aep.DeviceAddress"),L"");
        ULONGLONG address=0;
        for(wchar_t c:text){int digit=c>=L'0'&&c<=L'9'?c-L'0':c>=L'a'&&c<=L'f'?c-L'a'+10:c>=L'A'&&c<=L'F'?c-L'A'+10:-1;if(digit>=0)address=(address<<4)|digit;}
        return address;
    }
    void StopLeWatcher(){
        if(!leWatcher_)return;
        try{leWatcher_.Added(leTokens_[0]);leWatcher_.Updated(leTokens_[1]);leWatcher_.Removed(leTokens_[2]);
            auto status=leWatcher_.Status();if(status==winrt::Windows::Devices::Enumeration::DeviceWatcherStatus::Started||status==winrt::Windows::Devices::Enumeration::DeviceWatcherStatus::EnumerationCompleted)leWatcher_.Stop();
        }catch(const winrt::hresult_error&){}leWatcher_=nullptr;
    }
    void StartLeWatcher(bool discover){
        if(leWatcher_&&leDiscovering_==discover)return;
        StopLeWatcher();leDiscovering_=discover;leDeadline_=discover?GetTickCount64()+25000:0;
        try{
            std::wstring selector=L"System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\"";
            if(!discover)selector+=L" AND System.Devices.Aep.IsPaired:=System.StructuredQueryType.Boolean#True";
            leWatcher_=DeviceInformation::CreateWatcher(selector,{L"System.Devices.Aep.DeviceAddress",L"System.Devices.Aep.IsConnected"},winrt::Windows::Devices::Enumeration::DeviceInformationKind::AssociationEndpoint);
            auto shared=leState_;
            leTokens_[0]=leWatcher_.Added([shared](auto&&,const DeviceInformation& device){if(!shared->alive)return;
                {std::scoped_lock lock(shared->mutex);if(shared->devices.size()<128)shared->devices.insert_or_assign(std::wstring(device.Id()),device);}shared->Changed();});
            leTokens_[1]=leWatcher_.Updated([shared](auto&&,const winrt::Windows::Devices::Enumeration::DeviceInformationUpdate& update){if(!shared->alive)return;
                {std::scoped_lock lock(shared->mutex);auto found=shared->devices.find(std::wstring(update.Id()));if(found!=shared->devices.end())found->second.Update(update);}shared->Changed();});
            leTokens_[2]=leWatcher_.Removed([shared](auto&&,const winrt::Windows::Devices::Enumeration::DeviceInformationUpdate& update){if(!shared->alive)return;
                {std::scoped_lock lock(shared->mutex);shared->devices.erase(std::wstring(update.Id()));}shared->Changed();});
            leWatcher_.Start();
        }catch(const winrt::hresult_error&){StopLeWatcher();}
    }
    void ReadWifi() {
        OpenWifi();if(!wlan_)return;
        PWLAN_INTERFACE_INFO_LIST list=nullptr;
        DWORD result=WlanEnumInterfaces(wlan_,nullptr,&list);
        if(result!=ERROR_SUCCESS){Message(false,Error(result));return;}
        std::vector<WifiNetwork> networks;
        bool available=list->dwNumberOfItems>0,on=false;
        adapters_.clear();std::wstring message;
        for(DWORD i=0;i<list->dwNumberOfItems;++i) {
            const auto& item=list->InterfaceInfo[i];adapters_.push_back(item.InterfaceGuid);
            DWORD bytes=0;WLAN_OPCODE_VALUE_TYPE opcode{};PWLAN_RADIO_STATE radio=nullptr;
            if(WlanQueryInterface(wlan_,&item.InterfaceGuid,wlan_intf_opcode_radio_state,nullptr,&bytes,reinterpret_cast<PVOID*>(&radio),&opcode)==ERROR_SUCCESS){
                for(DWORD p=0;p<radio->dwNumberOfPhys;++p)on|=radio->PhyRadioState[p].dot11SoftwareRadioState==dot11_radio_state_on&&radio->PhyRadioState[p].dot11HardwareRadioState==dot11_radio_state_on;
                WlanFreeMemory(radio);
            }
            PWLAN_AVAILABLE_NETWORK_LIST found=nullptr;
            result=WlanGetAvailableNetworkList(wlan_,&item.InterfaceGuid,0,nullptr,&found);
            if(result!=ERROR_SUCCESS){message=Error(result);continue;}
            for(DWORD j=0;j<found->dwNumberOfItems && networks.size()<128;++j){
                const auto& entry=found->Network[j];if(!entry.dot11Ssid.uSSIDLength)continue;
                WifiNetwork network;network.adapter=item.InterfaceGuid;network.ssid=entry.dot11Ssid;
                network.name=Wide(reinterpret_cast<const char*>(entry.dot11Ssid.ucSSID),static_cast<int>(entry.dot11Ssid.uSSIDLength));
                if(network.name.empty())network.name=L"Wireless network";
                network.profile=entry.strProfileName;network.quality=entry.wlanSignalQuality;network.secure=entry.bSecurityEnabled!=FALSE;
                network.connected=(entry.dwFlags&WLAN_AVAILABLE_NETWORK_CONNECTED)!=0;network.connectable=entry.bNetworkConnectable!=FALSE;
                network.authentication=entry.dot11DefaultAuthAlgorithm;network.cipher=entry.dot11DefaultCipherAlgorithm;
                auto duplicate=std::find_if(networks.begin(),networks.end(),[&](const auto& n){return n.ssid.uSSIDLength==network.ssid.uSSIDLength&&memcmp(n.ssid.ucSSID,network.ssid.ucSSID,n.ssid.uSSIDLength)==0;});
                if(duplicate==networks.end())networks.push_back(std::move(network));else if(network.connected||(!network.profile.empty()&&duplicate->profile.empty()))*duplicate=std::move(network);
            }
            WlanFreeMemory(found);
        }
        WlanFreeMemory(list);
        std::stable_sort(networks.begin(),networks.end(),[](const auto& a,const auto& b){return a.connected!=b.connected?a.connected:a.quality>b.quality;});
        {std::scoped_lock lock(stateMutex_);state_.networks=std::move(networks);state_.wifiAvailable=available;state_.wifiOn=on;state_.wifiBusy=false;
         if(state_.wifiConnecting&&std::any_of(state_.networks.begin(),state_.networks.end(),[&](const auto& entry){return entry.connected&&entry.name==wifiTarget_;})){state_.wifiConnecting=false;state_.wifiMessage=L"Connected.";}
         if(!message.empty())state_.wifiMessage=std::move(message);else if(!available)state_.wifiMessage=L"No Wi-Fi adapter is available.";}
        Changed();
    }
    void ReadBluetooth(bool inquiry) {
        if(!leWatcher_||inquiry||(leDiscovering_&&GetTickCount64()>leDeadline_))StartLeWatcher(inquiry);
        BLUETOOTH_FIND_RADIO_PARAMS params{sizeof(params)};HANDLE radio=nullptr;
        HBLUETOOTH_RADIO_FIND finder=BluetoothFindFirstRadio(&params,&radio);
        std::vector<BluetoothEntry> devices;std::wstring message;
        bool available=finder!=nullptr,on=false;
        if(finder){
            BluetoothFindRadioClose(finder);
            on=true;
            BLUETOOTH_DEVICE_SEARCH_PARAMS search{sizeof(search)};
            search.fReturnAuthenticated=TRUE;search.fReturnRemembered=TRUE;search.fReturnConnected=TRUE;
            search.fReturnUnknown=TRUE;search.fIssueInquiry=inquiry;search.cTimeoutMultiplier=2;search.hRadio=radio;
            BLUETOOTH_DEVICE_INFO info{sizeof(info)};HBLUETOOTH_DEVICE_FIND found=BluetoothFindFirstDevice(&search,&info);
            if(found){do{
                if(devices.size()>=128)break;
                BluetoothEntry device;device.native=info;device.name=info.szName[0]?info.szName:L"Bluetooth device";
                device.paired=info.fAuthenticated!=FALSE;device.connected=info.fConnected!=FALSE;devices.push_back(std::move(device));
                info.dwSize=sizeof(info);
            }while(BluetoothFindNextDevice(found,&info));BluetoothFindDeviceClose(found);}
            CloseHandle(radio);
        }
        try {
            for(const auto& adapter:winrt::Windows::Devices::Radios::Radio::GetRadiosAsync().get())
                if(adapter.Kind()==winrt::Windows::Devices::Radios::RadioKind::Bluetooth){available=true;on=adapter.State()==winrt::Windows::Devices::Radios::RadioState::On;break;}
        }catch(const winrt::hresult_error&){}
        std::stable_sort(devices.begin(),devices.end(),[](const auto& a,const auto& b){return a.connected!=b.connected?a.connected:a.paired!=b.paired?a.paired:a.name<b.name;});
        {std::scoped_lock lock(stateMutex_);state_.devices=std::move(devices);state_.bluetoothAvailable=available;state_.bluetoothOn=on;state_.bluetoothBusy=false;
         if(!available)state_.bluetoothMessage=L"No Bluetooth adapter is available.";else if(!on)state_.bluetoothMessage=L"Bluetooth is switched off.";}
        Changed();
    }
public:
    explicit Connectivity(HWND window):window_(window){leState_->window=window;}
    ~Connectivity(){
        shuttingDown_=true;
        leState_->alive=false;
        wifiWorker_.Stop();bluetoothWorker_.Stop();
        StopLeWatcher();for(auto& [address,lease]:leLeases_){(void)address;lease.Close();}
        if(wlan_){WlanRegisterNotification(wlan_,WLAN_NOTIFICATION_SOURCE_NONE,TRUE,nullptr,nullptr,nullptr,nullptr);WlanCloseHandle(wlan_,nullptr);}
    }
    ConnectionState Snapshot(){
        ConnectionState result;{std::scoped_lock lock(stateMutex_);result=state_;}
        std::scoped_lock lock(leState_->mutex);
        for(const auto& [identifier,device]:leState_->devices){
            try{if(device.Name().empty())continue;BluetoothEntry entry;entry.lowEnergy=true;entry.identifier=identifier;entry.name=device.Name();entry.paired=device.Pairing().IsPaired();entry.native.Address.ullLong=LeAddress(device);
                auto properties=device.Properties();if(properties.HasKey(L"System.Devices.Aep.IsConnected"))entry.connected=winrt::unbox_value_or<bool>(properties.Lookup(L"System.Devices.Aep.IsConnected"),false);
                auto own=leState_->connections.find(entry.native.Address.ullLong);if(own!=leState_->connections.end())entry.connected=own->second;
                if(std::none_of(result.devices.begin(),result.devices.end(),[&](const auto& classic){return entry.native.Address.ullLong&&classic.native.Address.ullLong==entry.native.Address.ullLong;}))result.devices.push_back(std::move(entry));
            }catch(const winrt::hresult_error&){}
        }
        std::stable_sort(result.devices.begin(),result.devices.end(),[](const auto& a,const auto& b){return a.connected!=b.connected?a.connected:a.paired!=b.paired?a.paired:a.name<b.name;});return result;
    }
    void CancelDiscovery(){bluetoothWorker_.Post([this]{if(leDiscovering_)StartLeWatcher(false);});}
    void RefreshWifi(bool scan=false){
        {std::scoped_lock lock(stateMutex_);if(state_.wifiBusy)return;state_.wifiBusy=true;if(!state_.wifiConnecting)state_.wifiMessage.clear();}Changed();
        wifiWorker_.Post([this,scan]{ReadWifi();if(scan&&wlan_){for(const auto& adapter:adapters_)WlanScan(wlan_,&adapter,nullptr,nullptr,nullptr);}});
    }
    void RefreshBluetooth(bool discover=false){
        {std::scoped_lock lock(stateMutex_);if(state_.bluetoothBusy)return;state_.bluetoothBusy=true;state_.bluetoothMessage.clear();}Changed();
        bluetoothWorker_.Post([this,discover]{ReadBluetooth(discover);});
    }
    void SetWifi(bool enabled){wifiWorker_.Post([this,enabled]{
        OpenWifi();if(!wlan_)return;if(adapters_.empty())ReadWifi();
        DWORD lastResult=ERROR_SUCCESS;
        for(const auto& adapter:adapters_){DWORD size=0;WLAN_OPCODE_VALUE_TYPE opcode{};PWLAN_RADIO_STATE state=nullptr;
            if(WlanQueryInterface(wlan_,&adapter,wlan_intf_opcode_radio_state,nullptr,&size,reinterpret_cast<void**>(&state),&opcode)!=ERROR_SUCCESS)continue;
            for(DWORD i=0;i<state->dwNumberOfPhys;++i){auto phy=state->PhyRadioState[i];phy.dot11SoftwareRadioState=enabled?dot11_radio_state_on:dot11_radio_state_off;
                lastResult=WlanSetInterface(wlan_,&adapter,wlan_intf_opcode_radio_state,sizeof(phy),&phy,nullptr);}
            WlanFreeMemory(state);
        }ReadWifi();if(lastResult!=ERROR_SUCCESS)Message(false,Error(lastResult));
    });}
    void ConnectWifi(WifiNetwork network,std::wstring password={}){wifiWorker_.Post([this,network=std::move(network),password=std::move(password)]()mutable{
        OpenWifi();if(!wlan_)return;
        WLAN_CONNECTION_PARAMETERS parameters{};parameters.dot11BssType=dot11_BSS_type_infrastructure;
        std::wstring profile;
        if(!network.profile.empty()){parameters.wlanConnectionMode=wlan_connection_mode_profile;parameters.strProfile=network.profile.c_str();}
        else if(!network.secure){parameters.wlanConnectionMode=wlan_connection_mode_discovery_unsecure;parameters.pDot11Ssid=&network.ssid;}
        else{
            std::wstring auth=network.authentication==DOT11_AUTH_ALGO_WPA3_SAE?L"WPA3SAE":network.authentication==DOT11_AUTH_ALGO_RSNA_PSK?L"WPA2PSK":network.authentication==DOT11_AUTH_ALGO_WPA_PSK?L"WPAPSK":L"";
            if(auth.empty()){Message(false,L"This managed network requires Windows. Choose More settings.");if(!password.empty())SecureZeroMemory(password.data(),password.size()*sizeof(wchar_t));return;}
            std::wstring hex;const wchar_t* digits=L"0123456789ABCDEF";
            for(ULONG i=0;i<network.ssid.uSSIDLength;++i){hex+=digits[network.ssid.ucSSID[i]>>4];hex+=digits[network.ssid.ucSSID[i]&15];}
            profile=L"<?xml version=\"1.0\"?><WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\"><name>"+Xml(network.name)+
                L"</name><SSIDConfig><SSID><hex>"+hex+L"</hex></SSID></SSIDConfig><connectionType>ESS</connectionType><connectionMode>manual</connectionMode><MSM><security><authEncryption><authentication>"+auth+
                L"</authentication><encryption>"+(network.cipher==DOT11_CIPHER_ALGO_TKIP?L"TKIP":L"AES")+
                L"</encryption><useOneX>false</useOneX></authEncryption><sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>"+Xml(password)+
                L"</keyMaterial></sharedKey></security></MSM></WLANProfile>";
            parameters.wlanConnectionMode=wlan_connection_mode_temporary_profile;parameters.strProfile=profile.c_str();
        }
        wifiTarget_=network.name;{std::scoped_lock lock(stateMutex_);state_.wifiConnecting=true;}
        DWORD result=WlanConnect(wlan_,&network.adapter,&parameters,nullptr);
        if(!password.empty())SecureZeroMemory(password.data(),password.size()*sizeof(wchar_t));if(!profile.empty())SecureZeroMemory(profile.data(),profile.size()*sizeof(wchar_t));
        if(result!=ERROR_SUCCESS){{std::scoped_lock lock(stateMutex_);state_.wifiConnecting=false;}Message(false,Error(result));}
        else{bool pending=false;{std::scoped_lock lock(stateMutex_);pending=state_.wifiConnecting;}if(pending)Message(false,L"Connecting…");}
    });}
    void DisconnectWifi(WifiNetwork network){wifiWorker_.Post([this,network]{OpenWifi();if(!wlan_)return;DWORD result=WlanDisconnect(wlan_,&network.adapter,nullptr);Message(false,result==ERROR_SUCCESS?L"Disconnecting…":Error(result));});}
    void SetBluetooth(bool enabled){bluetoothWorker_.Post([this,enabled]{
        try{bool found=false;for(const auto& radio:winrt::Windows::Devices::Radios::Radio::GetRadiosAsync().get())if(radio.Kind()==winrt::Windows::Devices::Radios::RadioKind::Bluetooth){
            found=true;auto result=radio.SetStateAsync(enabled?winrt::Windows::Devices::Radios::RadioState::On:winrt::Windows::Devices::Radios::RadioState::Off).get();
            if(result!=winrt::Windows::Devices::Radios::RadioAccessStatus::Allowed)Message(true,L"Windows did not allow this radio change.");}
            if(!found)Message(true,L"No Bluetooth radio is available.");
        }catch(const winrt::hresult_error&){Message(true,L"Windows could not change Bluetooth. Choose More settings.");}ReadBluetooth(false);
    });}
    void BluetoothAction(BluetoothEntry device,int action){bluetoothWorker_.Post([this,device=std::move(device),action]()mutable{
        if(device.lowEnergy){
            using namespace winrt::Windows::Devices::Bluetooth;using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
            try{
                if(action==2||action==3){auto info=DeviceInformation::CreateFromIdAsync(device.identifier,{},winrt::Windows::Devices::Enumeration::DeviceInformationKind::AssociationEndpoint).get();
                    bool success=false;
                    if(action==2){auto status=info.Pairing().PairAsync().get().Status();success=status==winrt::Windows::Devices::Enumeration::DevicePairingResultStatus::Paired||status==winrt::Windows::Devices::Enumeration::DevicePairingResultStatus::AlreadyPaired;}
                    else{auto status=info.Pairing().UnpairAsync().get().Status();success=status==winrt::Windows::Devices::Enumeration::DeviceUnpairingResultStatus::Unpaired||status==winrt::Windows::Devices::Enumeration::DeviceUnpairingResultStatus::AlreadyUnpaired;}
                    Message(true,success?L"":L"Windows could not complete pairing. Choose More settings.");
                }else if(action==0){
                    Message(true,L"Requesting connection…");auto remote=BluetoothLEDevice::FromBluetoothAddressAsync(device.native.Address.ullLong).get();
                    if(!remote){Message(true,L"Wake the device and try again.");return;}
                    auto session=GattSession::FromDeviceIdAsync(remote.BluetoothDeviceId()).get();
                    if(!session||!session.CanMaintainConnection()){Message(true,L"This device's connection is managed by Windows. Choose More settings.");return;}
                    auto existing=leLeases_.find(device.native.Address.ullLong);if(existing!=leLeases_.end())existing->second.Close();
                    LeLease lease;lease.device=remote;lease.session=session;auto shared=leState_;auto address=device.native.Address.ullLong;
                    lease.token=remote.ConnectionStatusChanged([shared,address](const BluetoothLEDevice& sender,auto&&){if(!shared->alive)return;
                        {std::scoped_lock lock(shared->mutex);shared->connections[address]=sender.ConnectionStatus()==BluetoothConnectionStatus::Connected;}shared->Changed();});
                    session.MaintainConnection(true);leLeases_.insert_or_assign(address,std::move(lease));
                    {std::scoped_lock lock(leState_->mutex);leState_->connections[address]=remote.ConnectionStatus()==BluetoothConnectionStatus::Connected;}
                    Message(true,remote.ConnectionStatus()==BluetoothConnectionStatus::Connected?L"Connected.":L"Connecting… Keep the device awake.");
                }else{
                    auto found=leLeases_.find(device.native.Address.ullLong);
                    if(found!=leLeases_.end()){found->second.Close();leLeases_.erase(found);{std::scoped_lock lock(leState_->mutex);leState_->connections.erase(device.native.Address.ullLong);}Message(true,L"Dako's connection was released. Other apps may still use this device.");}
                    else Message(true,L"Windows or another app owns this connection. Choose More settings to manage it.");
                }
            }catch(const winrt::hresult_error&){Message(true,L"Windows could not complete the Bluetooth request. Choose More settings.");}
            return;
        }
        BLUETOOTH_FIND_RADIO_PARAMS params{sizeof(params)};HANDLE radio=nullptr;auto finder=BluetoothFindFirstRadio(&params,&radio);
        if(!finder){Message(true,L"Bluetooth is unavailable.");return;}BluetoothFindRadioClose(finder);
        DWORD result=ERROR_SUCCESS;
        try{
            if(action==0){
                Message(true,L"Requesting connection…");
                auto remote=winrt::Windows::Devices::Bluetooth::BluetoothDevice::FromBluetoothAddressAsync(device.native.Address.ullLong).get();
                if(remote){auto services=remote.GetRfcommServicesAsync(winrt::Windows::Devices::Bluetooth::BluetoothCacheMode::Uncached).get();(void)services;deviceLeases_.insert_or_assign(device.native.Address.ullLong,std::move(remote));}
                else result=ERROR_DEVICE_NOT_CONNECTED;
            }else if(action==1){
                DWORD returned=0;BTH_ADDR address=device.native.Address.ullLong;
                if(!DeviceIoControl(radio,IOCTL_BTH_DISCONNECT_DEVICE,&address,sizeof(address),nullptr,0,&returned,nullptr))result=GetLastError();
                deviceLeases_.erase(device.native.Address.ullLong);
            }else if(action==2){result=BluetoothAuthenticateDeviceEx(window_,radio,&device.native,nullptr,MITMProtectionRequiredGeneralBonding);}
            else if(action==3){result=BluetoothRemoveDevice(&device.native.Address);}
        }catch(const winrt::hresult_error&){result=ERROR_DEVICE_NOT_CONNECTED;}
        CloseHandle(radio);ReadBluetooth(false);
        if(result!=ERROR_SUCCESS&&result!=ERROR_CANCELLED)Message(true,Error(result));
        else if(action==0){auto state=Snapshot();auto found=std::find_if(state.devices.begin(),state.devices.end(),[&](const auto& entry){return entry.native.Address.ullLong==device.native.Address.ullLong;});
            Message(true,found!=state.devices.end()&&found->connected?L"Connected.":L"The device has not connected. Wake it or use More settings.");}
        else Message(true,L"");
    });}
};
