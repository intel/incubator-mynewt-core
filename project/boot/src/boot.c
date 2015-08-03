#include <assert.h>
#include <stddef.h>
#include <inttypes.h>
#include "ffs/ffs.h"
#include "bootutil/image.h"
#include "bootutil/loader.h"

/* XXX: Where? */
#include "stm32f4xx/stm32f4xx.h"

/** Image slots. */
static uint32_t boot_img_addrs[2] = {
    0x08020000,
    0x08080000,
};

/** Internal flash layout. */
static struct ffs_area_desc boot_area_descs[] = {
    [0] =  { 0x08000000, 16 * 1024 },
    [1] =  { 0x08004000, 16 * 1024 },
    [2] =  { 0x08008000, 16 * 1024 },
    [3] =  { 0x0800c000, 16 * 1024 },
    [4] =  { 0x08010000, 64 * 1024 },
    [5] =  { 0x08020000, 128 * 1024 },
    [6] =  { 0x08040000, 128 * 1024 },
    [7] =  { 0x08060000, 128 * 1024 },
    [8] =  { 0x08080000, 128 * 1024 },
    [9] =  { 0x080a0000, 128 * 1024 },
    [10] = { 0x080c0000, 128 * 1024 },
    [11] = { 0x080e0000, 128 * 1024 },
    { 0, 0 },
};

/** Contains indices of the areas which can contain image data. */
static uint16_t boot_img_areas[] = {
    5, 6, 7, 8, 9, 10, 11
};

#define BOOT_NUM_IMG_AREAS \
    ((int)(sizeof boot_img_areas / sizeof boot_img_areas[0]))

#define BOOT_AREA_IDX_SCRATCH 11


/**
 * Boots the image described by the supplied image header.
 *
 * @param hdr                   The header for the image to boot.
 */
static void
boot_jump(const struct image_header *hdr, uint32_t image_addr)
{
    typedef void jump_fn(void);

    uint32_t base0entry;
    uint32_t img_start;
    uint32_t jump_addr;
    jump_fn *fn;

    /* PIC code not currently supported. */
    assert(!(hdr->ih_flags & IMAGE_F_PIC));

    img_start = image_addr + hdr->ih_hdr_size;

    /* First word contains initial MSP value. */
    __set_MSP(*(uint32_t *)img_start);

    /* Second word contains address of entry point (Reset_Handler). */
    base0entry = *(uint32_t *)(img_start + 4);
    jump_addr = base0entry;
    fn = (jump_fn *)jump_addr;

    /* Remap memory such that flash gets mapped to the code region. */
    SYSCFG->MEMRMP = 0;
    __DSB();

    /* Jump to image. */
    fn();
}

int
main(void)
{
    struct boot_rsp rsp;
    int rc;

    const struct boot_req req = {
        .br_area_descs = boot_area_descs,
        .br_image_addrs = boot_img_addrs,
        .br_image_areas = boot_img_areas,
        .br_scratch_area_idx = BOOT_AREA_IDX_SCRATCH,
        .br_num_image_areas = BOOT_NUM_IMG_AREAS,
        .br_num_slots = 2,
    };

    rc = boot_go(&req, &rsp);
    assert(rc == 0);

    boot_jump(rsp.br_hdr, rsp.br_image_addr);

    return 0;
}
