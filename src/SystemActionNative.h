#pragma once
#include "ClipboardTransfer.h"

namespace systemactions {
// Only metadata queries and a separately confirmed PnP safe-removal request; no raw volume dismount/eject.
clipboard::TransferStatus HandleTransfer(clipboard::TransferOp, clipboard::Payload &, DWORD &);
}
