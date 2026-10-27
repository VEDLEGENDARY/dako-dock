#pragma once
#include "ClipboardTransfer.h"

namespace files {
clipboard::TransferStatus HandleTransfer(clipboard::TransferOp, clipboard::Payload &, DWORD &error);
}
