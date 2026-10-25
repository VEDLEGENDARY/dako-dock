# Native wireless controls

Wi-Fi and Bluetooth open Dako flyouts. Their footer links open Windows Settings as a secondary action. Device/network state comes from Windows; successful request submission alone is not shown as a successful connection.

## Wi-Fi

The WLAN client API supplies adapters, radio state, available networks, signal quality, saved profile names, and connection notifications. The flyout supports refresh, radio on/off, saved-network connection, open networks, password-based temporary profiles, and disconnection. Passwords are not written to Dako's settings or logs; input, queued credentials, escaped key and temporary profile buffers are wiped on completion, early failure and queue cancellation. Password length and 64-digit hexadecimal keys are validated before submission. Connection feedback has a 20-second timeout; duplicate conflicting commands are serialized. Existing Windows profiles remain Windows-managed.

Enterprise authentication is delegated to Windows Settings. Windows permission or adapter errors are surfaced in the flyout; Dako does not change privacy settings to bypass them. Recent Windows versions can restrict Wi-Fi enumeration through location access rules. [Microsoft WLAN documentation](https://learn.microsoft.com/en-us/windows/win32/api/wlanapi/nf-wlanapi-wlangetavailablenetworklist).

## Bluetooth

Headphones/earphones are the primary target, with Classic and low-energy device enumeration supported. Classic devices use the Windows device cache and a short inquiry only when requested. Low-energy devices use an association-endpoint watcher; active discovery is bounded and returns to paired-device monitoring when the flyout closes. [Microsoft device enumeration sample](https://github.com/microsoft/Windows-universal-samples/blob/main/Samples/BluetoothLE/cs/Scenario1_Discovery.xaml.cs).

The UI provides radio state, refresh/discovery, connection requests, disconnect, pairing and removal. Pairing uses cancellable Windows pairing operations; unsupported or consent-dependent cases have a More settings fallback. Classic connection requests trigger native service discovery and check the reported connection state. This does not guarantee that every device's audio/input profile will activate automatically; actual headphone audio routing still needs hardware testing.

Classic disconnection uses the Bluetooth stack's disconnect operation. It disconnects the selected remote device's active links; it does not remove or reinstall drivers. [Microsoft Bluetooth disconnect documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/bthioctl/ni-bthioctl-ioctl_bth_disconnect_device).

For low-energy devices, Dako can maintain a GATT session and release a session it owns. Windows or another application may still own a connection, which is stated in the flyout rather than reported as disconnected. Device-specific profile support and OS permissions still apply. [Microsoft BLE connection documentation](https://learn.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.bluetoothledevice.frombluetoothaddressasync).

Automated UI/render checks use clearly separate sample data and do not change real radio state, connect/disconnect, pair/remove a device, or submit network credentials.

WinRT media/wireless waits have a 15-second deadline and poll shutdown cancellation at 25 ms. Late completions own their event state without capturing the destroyed service. Classic native inquiry and synchronous OS/shell APIs retain their platform blocking behavior; local read-only shutdown stress is evidence for this host, not a universal shutdown bound.
