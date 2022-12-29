/********************************************************************************
Copyright (C) 2016 Marvell International Ltd.

SPDX-License-Identifier: BSD-2-Clause-Patent

*******************************************************************************/

#ifndef __MV_ONIE_H__
#define __MV_ONIE_H__

#include <Uefi.h>

#define EEPROM_SIGNATURE          SIGNATURE_32 ('O', 'N', 'I', 'E')

#define MAX_BUFFER_LENGTH 64

#define I2C_GUID \
  { \
  0xadc1901b, 0xb83c, 0x4831, { 0x8f, 0x59, 0x70, 0x89, 0x8f, 0x26, 0x57, 0x1e } \
  }

typedef struct {
  UINT32  Signature;
  EFI_HANDLE ControllerHandle;
  EFI_I2C_IO_PROTOCOL *I2cIo;
  MARVELL_EEPROM_PROTOCOL EepromProtocol;
} ONIE_CONTEXT;

#define ONIE_SC_FROM_IO(a) CR (a, ONIE_CONTEXT, I2cIo, ONIE_SIGNATURE)
#define ONIE_SC_FROM_EEPROM(a) CR (a, ONIE_CONTEXT, EepromProtocol, ONIE_SIGNATURE)

EFI_STATUS
EFIAPI
MvOnieSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL            *This,
  IN EFI_HANDLE                             ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL               *RemainingDevicePath OPTIONAL
  );

EFI_STATUS
EFIAPI
MvOnieStart (
  IN EFI_DRIVER_BINDING_PROTOCOL            *This,
  IN EFI_HANDLE                             ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL               *RemainingDevicePath OPTIONAL
  );

EFI_STATUS
EFIAPI
MvOnieStop (
  IN EFI_DRIVER_BINDING_PROTOCOL            *This,
  IN  EFI_HANDLE                            ControllerHandle,
  IN  UINTN                                 NumberOfChildren,
  IN  EFI_HANDLE                            *ChildHandleBuffer OPTIONAL
  );
#endif // __MV_ONIE_H__
