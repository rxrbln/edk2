/** @file

  Utility functions used by virtio device drivers.

  Copyright (C) 2012, Red Hat, Inc.

  This program and the accompanying materials are licensed and made available
  under the terms and conditions of the BSD License which accompanies this
  distribution. The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS, WITHOUT
  WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <IndustryStandard/Pci22.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>

#include <Library/VirtioLib.h>


/**

  Write a word into Region 0 of the device specified by PciIo.

  Region 0 must be an iomem region. This is an internal function for the
  driver-specific VIRTIO_CFG_WRITE() macros.

  @param[in] PciIo        Target PCI device.

  @param[in] FieldOffset  Destination offset.

  @param[in] FieldSize    Destination field size, must be in { 1, 2, 4, 8 }.

  @param[in] Value        Little endian value to write, converted to UINT64.
                          The least significant FieldSize bytes will be used.


  @return  Status code returned by PciIo->Io.Write().

**/
EFIAPI
EFI_STATUS
VirtioWrite (
  IN EFI_PCI_IO_PROTOCOL *PciIo,
  IN UINTN               FieldOffset,
  IN UINTN               FieldSize,
  IN UINT64              Value
  )
{
  UINTN                     Count;
  EFI_PCI_IO_PROTOCOL_WIDTH Width;

  Count = 1;
  switch (FieldSize) {
    case 1:
      Width = EfiPciIoWidthUint8;
      break;

    case 2:
      Width = EfiPciIoWidthUint16;
      break;

    case 8:
      Count = 2;
      // fall through

    case 4:
      Width = EfiPciIoWidthUint32;
      break;

    default:
      ASSERT (FALSE);
  }

  return PciIo->Io.Write (
                     PciIo,
                     Width,
                     PCI_BAR_IDX0,
                     FieldOffset,
                     Count,
                     &Value
                     );
}


/**

  Read a word from Region 0 of the device specified by PciIo.

  Region 0 must be an iomem region. This is an internal function for the
  driver-specific VIRTIO_CFG_READ() macros.

  @param[in] PciIo        Source PCI device.

  @param[in] FieldOffset  Source offset.

  @param[in] FieldSize    Source field size, must be in { 1, 2, 4, 8 }.

  @param[in] BufferSize   Number of bytes available in the target buffer. Must
                          equal FieldSize.

  @param[out] Buffer      Target buffer.


  @return  Status code returned by PciIo->Io.Read().

**/
EFIAPI
EFI_STATUS
VirtioRead (
  IN  EFI_PCI_IO_PROTOCOL *PciIo,
  IN  UINTN               FieldOffset,
  IN  UINTN               FieldSize,
  IN  UINTN               BufferSize,
  OUT VOID                *Buffer
  )
{
  UINTN                     Count;
  EFI_PCI_IO_PROTOCOL_WIDTH Width;

  ASSERT (FieldSize == BufferSize);

  Count = 1;
  switch (FieldSize) {
    case 1:
      Width = EfiPciIoWidthUint8;
      break;

    case 2:
      Width = EfiPciIoWidthUint16;
      break;

    case 8:
      Count = 2;
      // fall through

    case 4:
      Width = EfiPciIoWidthUint32;
      break;

    default:
      ASSERT (FALSE);
  }

  return PciIo->Io.Read (
                     PciIo,
                     Width,
                     PCI_BAR_IDX0,
                     FieldOffset,
                     Count,
                     Buffer
                     );
}


/**

  Configure a virtio ring.

  This function sets up internal storage (the guest-host communication area)
  and lays out several "navigation" (ie. no-ownership) pointers to parts of
  that storage.

  Relevant sections from the virtio-0.9.5 spec:
  - 1.1 Virtqueues,
  - 2.3 Virtqueue Configuration.

  @param[in]                    The number of descriptors to allocate for the
                                virtio ring, as requested by the host.

  @param[out] Ring              The virtio ring to set up.

  @retval EFI_OUT_OF_RESOURCES  AllocatePages() failed to allocate contiguous
                                pages for the requested QueueSize. Fields of
                                Ring have indeterminate value.

  @retval EFI_SUCCESS           Allocation and setup successful. Ring->Base
                                (and nothing else) is responsible for
                                deallocation.

**/
EFI_STATUS
EFIAPI
VirtioRingInit (
  IN  UINT16 QueueSize,
  OUT VRING  *Ring
  )
{
  UINTN          RingSize;
  volatile UINT8 *RingPagesPtr;

  RingSize = ALIGN_VALUE (
               sizeof *Ring->Desc            * QueueSize +
               sizeof *Ring->Avail.Flags                 +
               sizeof *Ring->Avail.Idx                   +
               sizeof *Ring->Avail.Ring      * QueueSize +
               sizeof *Ring->Avail.UsedEvent,
               EFI_PAGE_SIZE);

  RingSize += ALIGN_VALUE (
                sizeof *Ring->Used.Flags                  +
                sizeof *Ring->Used.Idx                    +
                sizeof *Ring->Used.UsedElem   * QueueSize +
                sizeof *Ring->Used.AvailEvent,
                EFI_PAGE_SIZE);

  Ring->NumPages = EFI_SIZE_TO_PAGES (RingSize);
  Ring->Base = AllocatePages (Ring->NumPages);
  if (Ring->Base == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  SetMem (Ring->Base, RingSize, 0x00);
  RingPagesPtr = Ring->Base;

  Ring->Desc = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Desc * QueueSize;

  Ring->Avail.Flags = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Avail.Flags;

  Ring->Avail.Idx = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Avail.Idx;

  Ring->Avail.Ring = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Avail.Ring * QueueSize;

  Ring->Avail.UsedEvent = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Avail.UsedEvent;

  RingPagesPtr = (volatile UINT8 *) Ring->Base +
                 ALIGN_VALUE (RingPagesPtr - (volatile UINT8 *) Ring->Base,
                   EFI_PAGE_SIZE);

  Ring->Used.Flags = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Used.Flags;

  Ring->Used.Idx = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Used.Idx;

  Ring->Used.UsedElem = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Used.UsedElem * QueueSize;

  Ring->Used.AvailEvent = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Used.AvailEvent;

  Ring->QueueSize = QueueSize;
  return EFI_SUCCESS;
}


/**

  Tear down the internal resources of a configured virtio ring.

  The caller is responsible to stop the host from using this ring before
  invoking this function: the VSTAT_DRIVER_OK bit must be clear in
  VhdrDeviceStatus.

  @param[out] Ring  The virtio ring to clean up.

**/
VOID
EFIAPI
VirtioRingUninit (
  IN OUT VRING *Ring
  )
{
  FreePages (Ring->Base, Ring->NumPages);
  SetMem (Ring, sizeof *Ring, 0x00);
}


/**

  Append a contiguous buffer for transmission / reception via the virtio ring.

  This function implements the following sections from virtio-0.9.5:
  - 2.4.1.1 Placing Buffers into the Descriptor Table
  - 2.4.1.2 Updating the Available Ring

  Free space is taken as granted, since the individual drivers support only
  synchronous requests and host side status is processed in lock-step with
  request submission. It is the calling driver's responsibility to verify the
  ring size in advance.

  @param[in out] Ring           The virtio ring to append the buffer to, as a
                                descriptor.

  @param [in] BufferPhysAddr    (Guest pseudo-physical) start address of the
                                transmit / receive buffer.

  @param [in] BufferSize        Number of bytes to transmit or receive.

  @param [in] Flags             A bitmask of VRING_DESC_F_* flags. The caller
                                computes this mask dependent on further buffers
                                to append and transfer direction.
                                VRING_DESC_F_INDIRECT is unsupported. The
                                VRING_DESC.Next field is always set, but the
                                host only interprets it dependent on
                                VRING_DESC_F_NEXT.

  @param [in] HeadIdx           The index identifying the head buffer (first
                                buffer appended) belonging to this same
                                request.

  @param [in out] NextAvailIdx  On input, the index identifying the next
                                descriptor available to carry the buffer. On
                                output, incremented by one, modulo 2^16.

**/
VOID
EFIAPI
VirtioAppendDesc (
  IN OUT VRING  *Ring,
  IN     UINTN  BufferPhysAddr,
  IN     UINT32 BufferSize,
  IN     UINT16 Flags,
  IN     UINT16 HeadIdx,
  IN OUT UINT16 *NextAvailIdx
  )
{
  volatile VRING_DESC *Desc;

  Desc        = &Ring->Desc[*NextAvailIdx % Ring->QueueSize];
  Desc->Addr  = BufferPhysAddr;
  Desc->Len   = BufferSize;
  Desc->Flags = Flags;
  Ring->Avail.Ring[(*NextAvailIdx)++ % Ring->QueueSize] =
    HeadIdx % Ring->QueueSize;
  Desc->Next  = *NextAvailIdx % Ring->QueueSize;
}