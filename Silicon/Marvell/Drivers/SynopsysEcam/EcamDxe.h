#ifndef _ECAM_DXE_H_
#define _ECAM_DXE_H_

#define ECAM_DISABLED   0x0
#define ECAM_ENABLED    0x1

#define PCIE_ECAM_VAR_NAME L"EcamPreference"

#include <Guid/HiiPlatformSetupFormset.h>
#include <Guid/EcamPlatformFormSet.h>

typedef struct {
  UINT8   Preference;
} ECAM_VARSTORE_DATA;

#endif

