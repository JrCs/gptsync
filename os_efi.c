/*
 * gptsync/os_efi.c
 * EFI glue for gptsync
 *
 * Copyright (c) 2006 Christoph Pfisterer
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *  * Neither the name of Christoph Pfisterer nor the names of the
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "gptsync.h"

// variables

EFI_BLOCK_IO    *BlockIO = NULL;

//
// sector I/O functions
//

UINTN read_sector(UINT64 lba, UINT8 *buffer)
{
    EFI_STATUS          Status;
    
    Status = BlockIO->ReadBlocks(BlockIO, BlockIO->Media->MediaId, lba, 512, buffer);
    if (EFI_ERROR(Status)) {
        // TODO: report error
        return 1;
    }
    return 0;
}

UINTN write_sector(UINT64 lba, UINT8 *buffer)
{
    EFI_STATUS          Status;
    
    Status = BlockIO->WriteBlocks(BlockIO, BlockIO->Media->MediaId, lba, 512, buffer);
    if (EFI_ERROR(Status)) {
        // TODO: report error
        return 1;
    }
    return 0;
}

//
// Keyboard input
//

static BOOLEAN ReadAllKeyStrokes(VOID)
{
    EFI_STATUS          Status;
    BOOLEAN             GotKeyStrokes;
    EFI_INPUT_KEY       Key;
    
    GotKeyStrokes = FALSE;
    for (;;) {
        Status = ST->ConIn->ReadKeyStroke(ST->ConIn, &Key);
        if (Status == EFI_SUCCESS) {
            GotKeyStrokes = TRUE;
            continue;
        }
        break;
    }
    return GotKeyStrokes;
}

static VOID PauseForKey(VOID)
{
    UINTN               Index;
    
    Print(L"\n* Hit any key to continue *");
    
    if (ReadAllKeyStrokes()) {  // remove buffered key strokes
        BS->Stall(5000000);     // 5 seconds delay
        ReadAllKeyStrokes();    // empty the buffer again
    }
    
    BS->WaitForEvent(1, &ST->ConIn->WaitForKey, &Index);
    ReadAllKeyStrokes();        // empty the buffer to protect the menu
    
    Print(L"\n");
}

UINTN input_boolean(CHARN *prompt, BOOLEAN *bool_out)
{
    EFI_STATUS          Status;
    UINTN               Index;
    EFI_INPUT_KEY       Key;
    
    Print(prompt);
    
    if (ReadAllKeyStrokes()) {  // remove buffered key strokes
        BS->Stall(500000);      // 0.5 seconds delay
        ReadAllKeyStrokes();    // empty the buffer again
    }
    
    BS->WaitForEvent(1, &ST->ConIn->WaitForKey, &Index);
    Status = ST->ConIn->ReadKeyStroke(ST->ConIn, &Key);
    if (EFI_ERROR(Status))
        return 1;
    
    if (Key.UnicodeChar == 'y' || Key.UnicodeChar == 'Y') {
        Print(L"Yes\n");
        *bool_out = TRUE;
    } else {
        Print(L"No\n");
        *bool_out = FALSE;
    }
    
    ReadAllKeyStrokes();
    return 0;
}

//
// main entry point
//

EFI_STATUS
EFIAPI
efi_main    (IN EFI_HANDLE           ImageHandle,
             IN EFI_SYSTEM_TABLE     *SystemTable)
{
    EFI_STATUS          Status;
    UINTN               SyncStatus;
    UINTN               Index;
    UINTN               HandleCount;
    EFI_HANDLE          *HandleBuffer;
    EFI_HANDLE          DeviceHandle;
    EFI_DEVICE_PATH     *DevicePath, *NextDevicePath;
    BOOLEAN             Usable;

    InitializeLib(ImageHandle, SystemTable);
    
    Status = LibLocateHandle(ByProtocol, &BlockIoProtocol, NULL,
                             &HandleCount, &HandleBuffer);
    if (EFI_ERROR (Status)) {
        Status = EFI_NOT_FOUND;
        return Status;
    }
    
    for (Index = 0; Index < HandleCount; Index++) {
        
        DeviceHandle = HandleBuffer[Index];
        
        // check device path
        DevicePath = DevicePathFromHandle(DeviceHandle);
        Usable = TRUE;
        while (DevicePath != NULL && !IsDevicePathEndType(DevicePath)) {
            NextDevicePath = NextDevicePathNode(DevicePath);
            
            if (DevicePathType(DevicePath) == MESSAGING_DEVICE_PATH &&
                (DevicePathSubType(DevicePath) == MSG_USB_DP ||
                 DevicePathSubType(DevicePath) == MSG_USB_CLASS_DP ||
                 DevicePathSubType(DevicePath) == MSG_1394_DP ||
                 DevicePathSubType(DevicePath) == MSG_FIBRECHANNEL_DP))
                Usable = FALSE;         // USB/FireWire/FC device
            if (DevicePathType(DevicePath) == MEDIA_DEVICE_PATH)
                Usable = FALSE;         // partition, El Torito entry, legacy BIOS device
            
            DevicePath = NextDevicePath;
        }
        if (!Usable)
            continue;
        
        Status = BS->HandleProtocol(DeviceHandle, &BlockIoProtocol, (VOID **) &BlockIO);
        if (EFI_ERROR(Status)) {
            // TODO: report error
            BlockIO = NULL;
        } else {
            if (BlockIO->Media->BlockSize != 512)
                BlockIO = NULL;    // optical media
            else
                break;
        }
        
    }
    
    FreePool (HandleBuffer);
    
    if (BlockIO == NULL) {
        Print(L"Internal hard disk device not found!\n");
        return EFI_NOT_FOUND;
    }
    
    
    SyncStatus = gptsync();
    
    if (SyncStatus == 0)
        PauseForKey();
    
    
    if (SyncStatus)
        return EFI_NOT_FOUND;
    return EFI_SUCCESS;
}
