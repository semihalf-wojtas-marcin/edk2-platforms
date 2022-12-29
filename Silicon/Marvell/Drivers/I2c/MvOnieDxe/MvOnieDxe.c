/********************************************************************************
Copyright (C) 2016 Marvell International Ltd.

SPDX-License-Identifier: BSD-2-Clause-Patent

*******************************************************************************/

#include <Protocol/DriverBinding.h>
#include <Protocol/I2cIo.h>
#include <Protocol/Eeprom.h>
#include <Protocol/MvI2c.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/IoLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Pi/PiI2c.h>

#include "MvOnieDxe.h"
#include "MvEepromDxe.h"

STATIC CONST EFI_GUID eEPROMGuid = MARVELL_EEPROM_PROTOCOL_GUID;

EFI_DRIVER_BINDING_PROTOCOL gDriverBindingProtocol = {
  MvOnieSupported,
  MvOnieStart,
  MvOnieStop
};

EFI_STATUS
EFIAPI
MvOnieSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL            *This,
  IN EFI_HANDLE                             ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL               *RemainingDevicePath OPTIONAL
  )
{
  EFI_STATUS Status = EFI_UNSUPPORTED;
  EFI_I2C_IO_PROTOCOL *TmpI2cIo;
  UINT8 *OnieAddresses;
  UINT8 *OnieBuses;
  UINTN i;

  Status = gBS->OpenProtocol (
      ControllerHandle,
      &gEfiEepromIoProtocolGuid,
      (VOID **) &TmpI2cIo,
      gImageHandle,
      ControllerHandle,
      EFI_OPEN_PROTOCOL_BY_DRIVER
      );
  if (EFI_ERROR(Status)) {
    return EFI_UNSUPPORTED;
  }

  /* get EEPROM devices' addresses from PCD */
  OnieAddresses = PcdGetPtr (PcdEepromI2cAddresses);
  OnieBuses = PcdGetPtr (PcdEepromI2cBuses);
  if (Onieddresses == 0) {
    Status = EFI_UNSUPPORTED;
    DEBUG((DEBUG_INFO, "MvOnieSupported: I2C device found, but it's not EEPROM\n"));
    goto out;
  }

  Status = EFI_UNSUPPORTED;
  for (i = 0; OnieAddresses[i] != '\0'; i++) {
    /* I2C guid must fit and valid DeviceIndex must be provided */
    if (CompareGuid(TmpI2cIo->DeviceGuid, &I2cGuid) &&
        TmpI2cIo->DeviceIndex == I2C_DEVICE_INDEX(OnieBuses[i],
          OnieAddresses[i])) {
      DEBUG((DEBUG_INFO, "MvOnieSupported: attached to EEPROM device\n"));
      Status = EFI_SUCCESS;
      break;
    }
  }

out:
  gBS->CloseProtocol (
      ControllerHandle,
      &gEfiI2cIoProtocolGuid,
      gImageHandle,
      ControllerHandle
      );
  return Status;
}

EFI_STATUS
EFIAPI
MvOnieStart (
  IN EFI_DRIVER_BINDING_PROTOCOL            *This,
  IN EFI_HANDLE                             ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL               *RemainingDevicePath OPTIONAL
  )
{
  EFI_STATUS Status = EFI_SUCCESS;
  EEPROM_CONTEXT *EepromContext;

  OnieContext = AllocateZeroPool (sizeof(EEPROM_CONTEXT));
  if (EepromContext == NULL) {
    DEBUG((DEBUG_ERROR, "MvOnie: allocation fail\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  OnieContext->ControllerHandle = ControllerHandle;
  OnieContext->Signature = Onie_SIGNATURE;
  OnieContext->EepromProtocol.Transfer = MvEepromTransfer;

  Status = gBS->OpenProtocol (
      ControllerHandle,
      &gEfiI2cIoProtocolGuid,
      (VOID **) &OnieCotext->I2cIo,
      gImageHandle,
      ControllerHandle,
      EFI_OPEN_PROTOCOL_BY_DRIVER
      );
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "MvEeprom: failed to open I2cIo\n"));
    FreePool(EepromContext);
    return EFI_UNSUPPORTED;
  }

  OnieContext->EepromProtocol.Identifier = EepromContext->I2cIo->DeviceIndex;
  Status = gBS->InstallMultipleProtocolInterfaces (
      &ControllerHandle,
      &gMarvellEepromProtocolGuid, &OnieContext->EepromProtocol,
      NULL
      );
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "MvEeprom: failed to install EEPROM protocol\n"));
    goto fail;
  }

  return Status;

fail:
  FreePool(OnieContext);
  gBS->CloseProtocol (
      ControllerHandle,
      &gEfiI2cIoProtocolGuid,
      gImageHandle,
      ControllerHandle
      );

  return Status;
}

EFI_STATUS
EFIAPI
MvOnieStop (
  IN EFI_DRIVER_BINDING_PROTOCOL            *This,
  IN  EFI_HANDLE                            ControllerHandle,
  IN  UINTN                                 NumberOfChildren,
  IN  EFI_HANDLE                            *ChildHandleBuffer OPTIONAL
  )
{
  MARVELL_EEPROM_PROTOCOL *EepromProtocol;
  EFI_STATUS Status;
  EEPROM_CONTEXT *EepromContext;

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gMarvellEepromProtocolGuid,
                  (VOID **) &EepromProtocol,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );

  if (EFI_ERROR (Status)) {
    return EFI_DEVICE_ERROR;
  }
  EepromContext = EEPROM_SC_FROM_EEPROM(EepromProtocol);

  gBS->UninstallMultipleProtocolInterfaces (
      &ControllerHandle,
      &gMarvellEepromProtocolGuid, &EepromContext->EepromProtocol,
      &gEfiDriverBindingProtocolGuid, &gDriverBindingProtocol,
      NULL
      );
  gBS->CloseProtocol (
      ControllerHandle,
      &gEfiI2cIoProtocolGuid,
      gImageHandle,
      ControllerHandle
      );
  FreePool(EepromContext);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
MvEepromInitialise (
  IN EFI_HANDLE  ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS Status;
  Status = gBS->InstallMultipleProtocolInterfaces (
      &ImageHandle,
      &gEfiDriverBindingProtocolGuid, &gDriverBindingProtocol,
      NULL
      );
  return Status;
}
