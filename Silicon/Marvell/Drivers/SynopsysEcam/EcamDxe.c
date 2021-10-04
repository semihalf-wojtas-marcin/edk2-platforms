/*
* Here be license
*/

#include <Library/AmlLib/AmlLib.h>
#include <Library/ArmadaBoardDescLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/HiiLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "EcamDxe.h"

#define MCFG_ACPI_TABLE_SIGNATURE  SIGNATURE_32 ('M', 'C', 'F', 'G')
#define FADT_ACPI_TABLE_SIGNATURE  SIGNATURE_32 ('F', 'A', 'C', 'P')
#define MASK_8  0xFF
#define NAME_BUFFER_SIZE 50

typedef struct {
  CHAR8                           BaseAddress[8];
  CHAR8                           PciSegmentGroupNumber[2];
  CHAR8                           StartBusNumber[1];
  CHAR8                           EndBusNumber[1];
  CHAR8                           Reserved[4];

} PCI_CONFIGURATION_SPACE;

typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER     Header;
  CHAR8                           Reserved[8];
  PCI_CONFIGURATION_SPACE         Pcie;

} MCFG_ACPI_TABLE;

typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER     Header;
  CHAR8                           FirmwareControl[4];
  CHAR8                           Dsdt[4];
} FADT_ACPI_TABLE;

typedef struct {
  VENDOR_DEVICE_PATH              VendorDevice_path;
  EFI_DEVICE_PATH_PROTOCOL        End;
} HII_VENDOR_DEVICE_PATH;


STATIC HII_VENDOR_DEVICE_PATH     mEcamPlatformDxeVendorDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8) (sizeof (VENDOR_DEVICE_PATH)),
        (UINT8) ((sizeof (VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    ECAM_PLATFORM_FORMSET_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      (UINT8) (END_DEVICE_PATH_LENGTH),
      (UINT8) ((END_DEVICE_PATH_LENGTH) >> 8)
    }
  }
};

extern UINT8                      EcamHiiBin[];
extern UINT8                      EcamPlatformDxeStrings[];

/**
* Install Hii Pages
*
* @retval EFI_SUCCESS                Successfully installed Hii pages
* @retval  EFI_OUT_OF_RESOURCES      Out of resources
*/
STATIC
EFI_STATUS
InstallHiiPages (
  VOID
  )
{
  EFI_STATUS      Status;
  EFI_HII_HANDLE  HiiHandle;
  EFI_HANDLE      DriverHandle;

  DriverHandle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (&DriverHandle,
                                                   &gEfiDevicePathProtocolGuid,
                                                   &mEcamPlatformDxeVendorDevicePath,
                                                   NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  HiiHandle = HiiAddPackages (&gEcamPlatformFormSetGuid,
                              DriverHandle,
                              EcamPlatformDxeStrings,
                              EcamHiiBin,
                              NULL);

  if (HiiHandle == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  return EFI_SUCCESS;
}

/**
*
* Find address of MCFG and FADT Acpi tables
*
* Iterate through XSDT table until Header Signature matches searched table.
* Stop iteration when both tables are found.
*
* @param [in, out] Mcfg      Pointer to Mcfg table
* @param [in, out] Fadt      Pointer to Dsdt table
**/
VOID
LocateTables (
  MCFG_ACPI_TABLE                               **Mcfg,
  FADT_ACPI_TABLE                               **Fadt
  )
{
  EFI_STATUS                                    Status;
  EFI_ACPI_6_0_ROOT_SYSTEM_DESCRIPTION_POINTER  *Rsdp;
  EFI_ACPI_DESCRIPTION_HEADER                   *Xsdt;
  UINTN                                         Index;
  UINT64                                        Data64;
  EFI_ACPI_DESCRIPTION_HEADER                   *Header;

  *Mcfg = NULL;
  *Fadt = NULL;

  Status = EfiGetSystemConfigurationTable (&gEfiAcpiTableGuid, (VOID *) &Rsdp);
    if (EFI_ERROR (Status) || (Rsdp == NULL)) {
      DEBUG ((DEBUG_ERROR, "EFI_ERROR or Rsdp == NULL\n"));
      return;
    }

  Xsdt = (EFI_ACPI_DESCRIPTION_HEADER *) (UINTN) Rsdp->XsdtAddress;
  if (Xsdt == NULL || Xsdt->Signature != EFI_ACPI_6_0_EXTENDED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE) {
    Xsdt = (EFI_ACPI_DESCRIPTION_HEADER *) (UINTN) Rsdp->RsdtAddress;
    if (Xsdt == NULL || Xsdt->Signature != EFI_ACPI_6_0_ROOT_SYSTEM_DESCRIPTION_TABLE_SIGNATURE) {
      DEBUG ((DEBUG_ERROR, "XSDT/RSDT == NULL or wrong signature\n"));
      return;
    }
  }

  for (Index = sizeof (EFI_ACPI_DESCRIPTION_HEADER); Index < Xsdt->Length; Index = Index + sizeof (UINT64)) {
    Data64  = *(UINT64 *) ((UINT8 *) Xsdt + Index);
    Header  = (EFI_ACPI_DESCRIPTION_HEADER *) (UINTN) Data64;

    if (Header->Signature == MCFG_ACPI_TABLE_SIGNATURE) {
     *Mcfg = (MCFG_ACPI_TABLE *) (UINTN) Data64;
    } else if (Header->Signature == FADT_ACPI_TABLE_SIGNATURE) {
      *Fadt = (FADT_ACPI_TABLE *) (UINTN) Data64;
    }

    if (*Mcfg != NULL && *Fadt != NULL) {
      return;
    }
  }
}

/**
 * Driver's entry point
 *
 * Fill MCFG and DSDT table depending on chosen configuration option.
 *
 * @retval EFI_DEVICE_ERROR         Error while setting MCFG table
 * @retval EFI_SUCCESS              Successfully set MCFG table

**/

EFI_STATUS
EFIAPI
EcamEntryPoint (
  IN EFI_HANDLE       ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS                                      Status;
  ECAM_VARSTORE_DATA                              EcamPreference;
  UINTN                                           BufferSize;
  MCFG_ACPI_TABLE                                 *Mcfg;
  FADT_ACPI_TABLE                                 *Fadt;
  MV_PCIE_CONTROLLER CONST                        **PcieController;
  UINTN                                           PcieControllerCount;
  UINTN                                           Index;
  UINT64                                          Data64;
  PCI_CONFIGURATION_SPACE                         *McfgPciConfigurationSpace;
  UINTN                                           DsdtAddress;
  UINT32                                          *Dsdt;
  EFI_ACPI_DESCRIPTION_HEADER                     *DsdtHeader;
  AML_ROOT_NODE_HANDLE                            RootNodeHandle;
  AML_OBJECT_NODE_HANDLE                          PciCrsNodeHandle;
  AML_OBJECT_NODE_HANDLE                          PciResNodeHandle;
  AML_DATA_NODE_HANDLE                            *PciCrsBufferOp;
  AML_DATA_NODE_HANDLE                            *PciResBufferOp;
  AML_DATA_NODE_HANDLE                            *PciCrsVariableArgument;
  AML_DATA_NODE_HANDLE                            *PciResVariableArgument;
  EFI_ACPI_WORD_ADDRESS_SPACE_DESCRIPTOR          *Word;
  UINT8                                           *PciCrsVariableArgumentBuffer;
  UINT8                                           *PciResVariableArgumentBuffer;
  CHAR8                                           PciCrsNodeNameBuffer[NAME_BUFFER_SIZE];
  CHAR8                                           PciResNodeNameBuffer[NAME_BUFFER_SIZE];
  EFI_ACPI_32_BIT_FIXED_MEMORY_RANGE_DESCRIPTOR   *PciResFixedMemory;

  BufferSize = sizeof(EcamPreference);
  Dsdt = 0x0;
  DsdtHeader = 0x0;

  Status = gRT->GetVariable(PCIE_ECAM_VAR_NAME, &gEcamPlatformFormSetGuid,
    NULL, &BufferSize, &EcamPreference);

  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "%a: no Ecam Preference found, defaulting to %s\n",
      __func__, PcdGet8(PcdDefaultEcamPref)));
    EcamPreference.Preference = PcdGet8(PcdDefaultEcamPref);
  }

  Status = gRT->SetVariable(PCIE_ECAM_VAR_NAME, &gEcamPlatformFormSetGuid,
    EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS, sizeof(EcamPreference),
    &EcamPreference);

  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "%a: Setting variable failed\n", __func__));
    return Status;
  }

  LocateTables(&Mcfg, &Fadt);

  if (Mcfg == NULL || Fadt == NULL) {
    DEBUG((DEBUG_ERROR, "%a: MCFG or Fadt table is NULL\n", __func__));
    return EFI_DEVICE_ERROR;
  }

  DsdtAddress = ((UINT32)Fadt->Dsdt[3]) << 24;
  DsdtAddress |= ((UINT32)Fadt->Dsdt[2]) << 16;
  DsdtAddress |= ((UINT32)Fadt->Dsdt[1]) << 8;
  DsdtAddress |= ((UINT32)Fadt->Dsdt[0]);

  Dsdt = (UINT32*) DsdtAddress;
  DsdtHeader = (EFI_ACPI_DESCRIPTION_HEADER *) Dsdt;

  Status = AmlParseDefinitionBlock((EFI_ACPI_DESCRIPTION_HEADER *) Dsdt, &RootNodeHandle);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "%a: AmlParseDefinitionBlock Failed\n", __func__));
    return EFI_DEVICE_ERROR;
  }

  ArmadaBoardPcieControllerGet(PcieController, &PcieControllerCount);
  if (PcieControllerCount < 0) {
    DEBUG((DEBUG_ERROR, "%a: No Pcie Controller found\n", __func__));
    return EFI_SUCCESS;
  }

  McfgPciConfigurationSpace = &Mcfg->Pcie;

  for (Index = 0; Index < PcieControllerCount; Index++) {
    AsciiSPrint(PciCrsNodeNameBuffer, NAME_BUFFER_SIZE, "\\_SB_.PCI%d._CRS.RBUF", Index);
    Status = AmlFindNode(RootNodeHandle, PciCrsNodeNameBuffer, &PciCrsNodeHandle);
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_ERROR, "%a: Could not find node: %a\n", __func__, PciCrsNodeNameBuffer));
      continue;
    }
    PciCrsBufferOp = (AML_DATA_NODE_HANDLE *) AmlObtainFixedArgument((AML_OBJECT_NODE_HANDLE *) PciCrsNodeHandle, 1);

    // Get Word Address Space
    PciCrsVariableArgument = (AML_DATA_NODE_HANDLE *) AmlObtainVariableArgument(PciCrsBufferOp, NULL);
    if (PciCrsVariableArgument != NULL) {
      PciCrsVariableArgumentBuffer = AmlGetAmlNodeHandleBuffer(PciCrsVariableArgument);
      Word = (EFI_ACPI_WORD_ADDRESS_SPACE_DESCRIPTOR *) ((VOID*)PciCrsVariableArgumentBuffer);
    } else {
        DEBUG((DEBUG_ERROR, "%a: Could not get PCI variable argument\n", __func__));
        continue;
    }

    // Get handle to Ecam BaseAddress
    AsciiSPrint(PciResNodeNameBuffer, NAME_BUFFER_SIZE, "\\_SB_.PCI%d.RES0._CRS", Index);
    Status = AmlFindNode(RootNodeHandle, PciResNodeNameBuffer, &PciResNodeHandle);
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_ERROR, "%a: Could not find node: %a\n", __func__, PciResNodeNameBuffer));
      continue;
    }

    PciResBufferOp = (AML_DATA_NODE_HANDLE *) AmlObtainFixedArgument((AML_OBJECT_NODE_HANDLE *) PciResNodeHandle, 1);
    PciResVariableArgument = (AML_DATA_NODE_HANDLE *) AmlObtainVariableArgument(PciResBufferOp, NULL);
    if (PciResVariableArgumentBuffer != NULL) {
      PciResVariableArgumentBuffer = AmlGetAmlNodeHandleBuffer(PciResVariableArgument);
      PciResFixedMemory = (EFI_ACPI_32_BIT_FIXED_MEMORY_RANGE_DESCRIPTOR *) ((VOID*) PciResVariableArgumentBuffer);
    } else {
        DEBUG((DEBUG_ERROR, "%a: Could not get PCI RES0 variable argument\n", __func__));
        continue;
    }

      Data64 = PcieController[Index]->ConfigSpaceAddress;
      // set BaseAddress
      McfgPciConfigurationSpace->BaseAddress[0] = (CHAR8) (Data64 & MASK_8);
      McfgPciConfigurationSpace->BaseAddress[1] = (CHAR8) ((Data64 >> 8) & MASK_8);
      McfgPciConfigurationSpace->BaseAddress[2] = (CHAR8) ((Data64 >> 16) & MASK_8);
      McfgPciConfigurationSpace->BaseAddress[3] = (CHAR8) ((Data64 >> 24) & MASK_8);
      McfgPciConfigurationSpace->BaseAddress[4] = (CHAR8) ((Data64 >> 32) & MASK_8);
      McfgPciConfigurationSpace->BaseAddress[5] = (CHAR8) ((Data64 >> 40) & MASK_8);
      McfgPciConfigurationSpace->BaseAddress[6] = (CHAR8) ((Data64 >> 48) & MASK_8);
      McfgPciConfigurationSpace->BaseAddress[7] = (CHAR8) ((Data64 >> 56) & MASK_8);

    if (EcamPreference.Preference == ECAM_DISABLED) {

      // set PCI_BUS_MIN
      McfgPciConfigurationSpace->StartBusNumber[0] = (CHAR8) PcieController[Index]->PcieBusMin;

      // set PCI_BUS_MAX
      McfgPciConfigurationSpace->EndBusNumber[0] = (CHAR8) PcieController[Index]->PcieBusMax;

      // configure DSDT table
      Word->AddrRangeMin = PcieController[Index]->PcieBusMin;
      Word->AddrRangeMax = PcieController[Index]->PcieBusMax;
      Word->AddrLen = 0xFF;

      // set BaseAddress
      PciResFixedMemory->BaseAddress = PcieController[Index]->ConfigSpaceAddress;

    } else {
      // set BaseAddress to 0xD0008000
      McfgPciConfigurationSpace->BaseAddress[1] = (CHAR8) 128;

      // set BUS_MIN to 0x0
      McfgPciConfigurationSpace->StartBusNumber[0] = (CHAR8) 0x0;

      // set BUS_MAX to 0x0
      McfgPciConfigurationSpace->EndBusNumber[0] = (CHAR8) 0x0;

      // configure DSDT table
      Word->AddrRangeMax = 0x0;
      Word->AddrRangeMin = 0x0;
      Word->AddrLen = 0x1;

      // set Ecam BaseAddress
      PciResFixedMemory->BaseAddress = PcieController[Index]->ConfigSpaceAddress + 0x8000;

    }
      // Update _CRS.RBUF node
      Status = AmlUpdateNode(PciCrsVariableArgument, 9, PciCrsVariableArgumentBuffer,
                             sizeof(EFI_ACPI_WORD_ADDRESS_SPACE_DESCRIPTOR));
      if (EFI_ERROR(Status)) {
        DEBUG((DEBUG_ERROR, "%a: Could not update Word node\n", __func__));
      }

      // Update _RES0._CRS node
      Status = AmlUpdateNode(PciResVariableArgument, 9, PciResVariableArgumentBuffer,
                             sizeof(EFI_ACPI_32_BIT_FIXED_MEMORY_RANGE_DESCRIPTOR));
      if (EFI_ERROR(Status)) {
        DEBUG((DEBUG_ERROR, "%a: Could not set Fixed memory BaseAddress\n", __func__));
      }

    McfgPciConfigurationSpace++;
  }

  UINT32 Size =  ((EFI_ACPI_DESCRIPTION_HEADER *)Dsdt)->Length;
  AML_ROOT_NODE_HANDLE *Root = AmlGetRoot(PciCrsVariableArgument);
  Status = AmlWriteTree(Root, (UINT8 *) Dsdt, &Size);

  UpdateChecksum((EFI_ACPI_DESCRIPTION_HEADER *)(&(Mcfg->Header)));

  return InstallHiiPages();
}
