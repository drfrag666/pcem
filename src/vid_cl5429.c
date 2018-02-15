/*Cirrus Logic CL-GD5429 emulation*/
#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "rom.h"
#include "video.h"
#include "vid_cl5429.h"
#include "vid_svga.h"
#include "vid_svga_render.h"
#include "vid_unk_ramdac.h"

typedef struct gd5429_t
{
        mem_mapping_t mmio_mapping;
        
        svga_t svga;
        
        rom_t bios_rom;
        
        uint32_t bank[2];
        uint32_t mask;

        struct
        {
                uint16_t bg_col, fg_col;                
                uint16_t width, height;
                uint16_t dst_pitch, src_pitch;               
                uint32_t dst_addr, src_addr;
                uint8_t mask, mode, rop;
                
                uint32_t dst_addr_backup, src_addr_backup;
                uint16_t width_backup, height_internal;
                int x_count;
        } blt;

        uint8_t hidden_dac_reg;
        int dac_3c6_count;
} gd5429_t;

void gd5429_write(uint32_t addr, uint8_t val, void *p);
uint8_t gd5429_read(uint32_t addr, void *p);

void gd5429_mmio_write(uint32_t addr, uint8_t val, void *p);
uint8_t gd5429_mmio_read(uint32_t addr, void *p);

void gd5429_blt_write_w(uint32_t addr, uint16_t val, void *p);
void gd5429_blt_write_l(uint32_t addr, uint32_t val, void *p);

void gd5429_recalc_banking(gd5429_t *gd5429);
void gd5429_recalc_mapping(gd5429_t *gd5429);

void gd5429_out(uint16_t addr, uint8_t val, void *p)
{
        gd5429_t *gd5429 = (gd5429_t *)p;
        svga_t *svga = &gd5429->svga;
        uint8_t old;
        
        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
                addr ^= 0x60;

//        pclog("gd5429 out %04X %02X\n", addr, val);
                
        switch (addr)
        {
                case 0x3c4:
                svga->seqaddr = val;
                break;
                case 0x3c5:
                if (svga->seqaddr > 5)
                {
                        svga->seqregs[svga->seqaddr & 0x1f] = val;
                        switch (svga->seqaddr & 0x1f)
                        {
                                case 0x10: case 0x30: case 0x50: case 0x70:
                                case 0x90: case 0xb0: case 0xd0: case 0xf0:
                                svga->hwcursor.x = (val << 3) | ((svga->seqaddr >> 5) & 7);
//                                pclog("svga->hwcursor.x = %i\n", svga->hwcursor.x);
                                break;
                                case 0x11: case 0x31: case 0x51: case 0x71:
                                case 0x91: case 0xb1: case 0xd1: case 0xf1:
                                svga->hwcursor.y = (val << 3) | ((svga->seqaddr >> 5) & 7);
//                                pclog("svga->hwcursor.y = %i\n", svga->hwcursor.y);
                                break;
                                case 0x12:
                                svga->hwcursor.ena = val & 1;
                                svga->hwcursor.ysize = (val & 4) ? 64 : 32;
                                svga->hwcursor.yoff = 0;
//                                pclog("svga->hwcursor.ena = %i\n", svga->hwcursor.ena);
                                break;                               
                                case 0x13:
                                svga->hwcursor.addr = 0x1fc000 + ((val & 0x3f) * 256);
//                                pclog("svga->hwcursor.addr = %x\n", svga->hwcursor.addr);
                                break;                                
                                
                                case 0x17:
                                gd5429_recalc_mapping(gd5429);
                                break;
                        }
                        return;
                }
                break;

                case 0x3c6:
//                pclog("CL write 3c6 %02x %i\n", val, gd5429->dac_3c6_count);
                if (gd5429->dac_3c6_count == 4)
                {
                        gd5429->dac_3c6_count = 0;
                        gd5429->hidden_dac_reg = val;
                        svga_recalctimings(svga);
                        return;
                }
                gd5429->dac_3c6_count = 0;
                break;
                
                case 0x3cf:
//                pclog("Write GDC %02x %02x\n", svga->gdcaddr, val);
                if (svga->gdcaddr == 0)
                        gd5429_mmio_write(0x00, val, gd5429);
                if (svga->gdcaddr == 1)
                        gd5429_mmio_write(0x04, val, gd5429);
                if (svga->gdcaddr == 5)
                {
                        svga->gdcreg[5] = val;
                        if (svga->gdcreg[0xb] & 0x04)
                                svga->writemode = svga->gdcreg[5] & 7;
                        else
                                svga->writemode = svga->gdcreg[5] & 3;
                        svga->readmode = val & 8;
                        svga->chain2_read = val & 0x10;
//                        pclog("writemode = %i\n", svga->writemode);
                        return;
                }
                if (svga->gdcaddr == 6)
                {
                        if ((svga->gdcreg[6] & 0xc) != (val & 0xc))
                        {
                                svga->gdcreg[6] = val;
                                gd5429_recalc_mapping(gd5429);
                        }
                        svga->gdcreg[6] = val;
                        return;
                }
                if (svga->gdcaddr > 8)
                {
                        svga->gdcreg[svga->gdcaddr & 0x3f] = val;
                        switch (svga->gdcaddr)
                        {
                                case 0x09: case 0x0a: case 0x0b:
                                gd5429_recalc_banking(gd5429);
                                if (svga->gdcreg[0xb] & 0x04)
                                        svga->writemode = svga->gdcreg[5] & 7;
                                else
                                        svga->writemode = svga->gdcreg[5] & 3;
                                break;

                                case 0x10:
                                gd5429_mmio_write(0x01, val, gd5429);
                                break;
                                case 0x11:
                                gd5429_mmio_write(0x05, val, gd5429);
                                break;

                                case 0x20:
                                gd5429_mmio_write(0x08, val, gd5429);
                                break;
                                case 0x21:
                                gd5429_mmio_write(0x09, val, gd5429);
                                break;
                                case 0x22:
                                gd5429_mmio_write(0x0a, val, gd5429);
                                break;
                                case 0x23:
                                gd5429_mmio_write(0x0b, val, gd5429);
                                break;
                                case 0x24:
                                gd5429_mmio_write(0x0c, val, gd5429);
                                break;
                                case 0x25:
                                gd5429_mmio_write(0x0d, val, gd5429);
                                break;
                                case 0x26:
                                gd5429_mmio_write(0x0e, val, gd5429);
                                break;
                                case 0x27:
                                gd5429_mmio_write(0x0f, val, gd5429);
                                break;
                
                                case 0x28:
                                gd5429_mmio_write(0x10, val, gd5429);
                                break;
                                case 0x29:
                                gd5429_mmio_write(0x11, val, gd5429);
                                break;
                                case 0x2a:
                                gd5429_mmio_write(0x12, val, gd5429);
                                break;

                                case 0x2c:
                                gd5429_mmio_write(0x14, val, gd5429);
                                break;
                                case 0x2d:
                                gd5429_mmio_write(0x15, val, gd5429);
                                break;
                                case 0x2e:
                                gd5429_mmio_write(0x16, val, gd5429);
                                break;

                                case 0x2f:
                                gd5429_mmio_write(0x17, val, gd5429);
                                break;
                                case 0x30:
                                gd5429_mmio_write(0x18, val, gd5429);
                                break;
                
                                case 0x32:
                                gd5429_mmio_write(0x1a, val, gd5429);
                                break;
                
                                case 0x31:
                                gd5429_mmio_write(0x40, val, gd5429);
                                break;
                        }                        
                        return;
                }
                break;
                
                case 0x3D4:
                svga->crtcreg = val & 0x3f;
                return;
                case 0x3D5:
                if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                        return;
                if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                        val = (svga->crtc[7] & ~0x10) | (val & 0x10);
                old = svga->crtc[svga->crtcreg];
                svga->crtc[svga->crtcreg] = val;

                if (old != val)
                {
                        if (svga->crtcreg < 0xe || svga->crtcreg > 0x10)
                        {
                                svga->fullchange = changeframecount;
                                svga_recalctimings(svga);
                        }
                }
                break;
        }
        svga_out(addr, val, svga);
}

uint8_t gd5429_in(uint16_t addr, void *p)
{
        gd5429_t *gd5429 = (gd5429_t *)p;
        svga_t *svga = &gd5429->svga;

        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3d0) && !(svga->miscout & 1)) 
                addr ^= 0x60;
        
//        if (addr != 0x3da) pclog("IN gd5429 %04X\n", addr);
        
        switch (addr)
        {
                case 0x3c5:
                if (svga->seqaddr > 5)
                {
                        switch (svga->seqaddr)
                        {
                                case 6:
                                return ((svga->seqregs[6] & 0x17) == 0x12) ? 0x12 : 0x0f;
                        }
                        return svga->seqregs[svga->seqaddr & 0x3f];
                }
                break;
                
                case 0x3c6:
//                pclog("CL read 3c6 %i\n", gd5429->dac_3c6_count);
                if (gd5429->dac_3c6_count == 4)
                {
                        gd5429->dac_3c6_count = 0;
                        return gd5429->hidden_dac_reg;
                }
                gd5429->dac_3c6_count++;
                break;

                case 0x3cf:
                if (svga->gdcaddr > 8)
                {
                        return svga->gdcreg[svga->gdcaddr & 0x3f];
                }
                break;

                case 0x3D4:
                return svga->crtcreg;
                case 0x3D5:
                switch (svga->crtcreg)
                {
                        case 0x27: /*ID*/
                        return 0x9c; /*GD5429*/
                }
                return svga->crtc[svga->crtcreg];
        }
        return svga_in(addr, svga);
}

void gd5429_recalc_banking(gd5429_t *gd5429)
{
        svga_t *svga = &gd5429->svga;
        
        if (svga->gdcreg[0xb] & 0x20)
                gd5429->bank[0] = (svga->gdcreg[0x09] & 0x7f) << 14;
        else
                gd5429->bank[0] = svga->gdcreg[0x09] << 12;
                                
        if (svga->gdcreg[0xb] & 0x01)
        {
                if (svga->gdcreg[0xb] & 0x20)
                        gd5429->bank[1] = (svga->gdcreg[0x0a] & 0x7f) << 14;
                else
                        gd5429->bank[1] = svga->gdcreg[0x0a] << 12;
        }
        else
                gd5429->bank[1] = gd5429->bank[0] + 0x8000;
}

void gd5429_recalc_mapping(gd5429_t *gd5429)
{
        svga_t *svga = &gd5429->svga;
        
//        pclog("Write mapping %02X %i\n", svga->gdcreg[6], svga->seqregs[0x17] & 0x04);
        switch (svga->gdcreg[6] & 0x0C)
        {
                case 0x0: /*128k at A0000*/
                mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
                mem_mapping_disable(&gd5429->mmio_mapping);
                svga->banked_mask = 0xffff;
                break;
                case 0x4: /*64k at A0000*/
                mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
                if (svga->seqregs[0x17] & 0x04)
                        mem_mapping_set_addr(&gd5429->mmio_mapping, 0xb8000, 0x00100);
                svga->banked_mask = 0xffff;
                break;
                case 0x8: /*32k at B0000*/
                mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
                mem_mapping_disable(&gd5429->mmio_mapping);
                svga->banked_mask = 0x7fff;
                break;
                case 0xC: /*32k at B8000*/
                mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
                mem_mapping_disable(&gd5429->mmio_mapping);
                svga->banked_mask = 0x7fff;
                break;
        }
}
        
void gd5429_recalctimings(svga_t *svga)
{
        gd5429_t *gd5429 = (gd5429_t *)svga->p;
        int clock = (svga->miscout >> 2) & 3;
        int n, d, p;
        double vclk;
        
        if (!svga->rowoffset)
                svga->rowoffset = 0x100;
        
        svga->interlace = svga->crtc[0x1a] & 1;
        
        if (svga->seqregs[7] & 0x01)
        {
                svga->render = svga_render_8bpp_highres;
                svga->bpp = 8;
        }
        
        svga->ma_latch |= ((svga->crtc[0x1b] & 0x01) << 16) | ((svga->crtc[0x1b] & 0xc) << 15);
//        pclog("MA now %05X %02X\n", svga->ma_latch, svga->crtc[0x1b]);
        
        if (gd5429->hidden_dac_reg & 0x80)
        {
                if (gd5429->hidden_dac_reg & 0x40)
                {
                        switch (gd5429->hidden_dac_reg & 0xf)
                        {
                                case 0x0:
                                svga->render = svga_render_15bpp_highres;
                                svga->bpp = 15;
                                break;
                                case 0x1:
                                svga->render = svga_render_16bpp_highres;
                                svga->bpp = 16;
                                break;
                                case 0x5:
                                svga->render = svga_render_24bpp_highres;
                                svga->bpp = 24;
                                break;
                        }
                }
                else
                        svga->render = svga_render_15bpp_highres; 
        }
        
        n = svga->seqregs[0xb + clock] & 0x7f;
        d = (svga->seqregs[0x1b + clock] >> 1) & 0x1f;
        p = svga->seqregs[0x1b + clock] & 1;
        
        vclk = (14318184.0 * ((float)n / (float)d)) / (float)(1 + p);
        switch (svga->seqregs[7] & 6)
        {
                case 2:
                vclk /= 2.0;
                break;
                case 4:
                vclk /= 3.0;
                break;
        }
        svga->clock = cpuclock / vclk;
}

void gd5429_hwcursor_draw(svga_t *svga, int displine)
{
        int x;
        uint8_t dat[2];
        int xx;
        int offset = svga->hwcursor_latch.x - svga->hwcursor_latch.xoff;

        if (svga->interlace && svga->hwcursor_oddeven)
                svga->hwcursor_latch.addr += 4;

        for (x = 0; x < 32; x += 8)
        {
                dat[0] = svga->vram[svga->hwcursor_latch.addr];
                dat[1] = svga->vram[svga->hwcursor_latch.addr + 0x80];
                for (xx = 0; xx < 8; xx++)
                {
                        if (offset >= svga->hwcursor_latch.x)
                        {
                                if (dat[1] & 0x80)
                                        ((uint32_t *)buffer32->line[displine])[offset + 32] = 0;
                                if (dat[0] & 0x80)
                                        ((uint32_t *)buffer32->line[displine])[offset + 32] ^= 0xffffff;
                        }
                           
                        offset++;
                        dat[0] <<= 1;
                        dat[1] <<= 1;
                }
                svga->hwcursor_latch.addr++;
        }

        if (svga->interlace && !svga->hwcursor_oddeven)
                svga->hwcursor_latch.addr += 4;
}


void gd5429_write_linear(uint32_t addr, uint8_t val, void *p);

void gd5429_write(uint32_t addr, uint8_t val, void *p)
{
        gd5429_t *gd5429 = (gd5429_t *)p;
        svga_t *svga = &gd5429->svga;
//        pclog("gd5429_write : %05X %02X  ", addr, val);
        addr &= svga->banked_mask;
        addr = (addr & 0x7fff) + gd5429->bank[(addr >> 15) & 1];
//        pclog("%08X\n", addr);
        gd5429_write_linear(addr, val, p);
}

uint8_t gd5429_read(uint32_t addr, void *p)
{
        gd5429_t *gd5429 = (gd5429_t *)p;
        svga_t *svga = &gd5429->svga;
        uint8_t ret;
//        pclog("gd5429_read : %05X ", addr);
        addr &= svga->banked_mask;
        addr = (addr & 0x7fff) + gd5429->bank[(addr >> 15) & 1];
        ret = svga_read_linear(addr, &gd5429->svga);
//        pclog("%08X %02X\n", addr, ret);  
        return ret;      
}

void gd5429_write_linear(uint32_t addr, uint8_t val, void *p)
{
        gd5429_t *gd5429 = (gd5429_t *)p;
        svga_t *svga = &gd5429->svga;
        uint8_t vala, valb, valc, vald, wm = svga->writemask;
        int writemask2 = svga->writemask;

        cycles -= video_timing_write_b;
        cycles_lost += video_timing_write_b;

        egawrites++;
        
//        if (svga_output) pclog("Write LFB %08X %02X ", addr, val);
        if (!(svga->gdcreg[6] & 1)) 
                svga->fullchange = 2;
        if ((svga->chain4 || svga->fb_only) && (svga->writemode < 4))
        {
                writemask2 = 1 << (addr & 3);
                addr &= ~3;
        }
        else if (svga->chain2_write)
        {
                writemask2 &= ~0xa;
                if (addr & 1)
                        writemask2 <<= 1;
                addr &= ~1;
                addr <<= 2;
        }
        else
        {
                addr <<= 2;
        }
        addr &= svga->decode_mask;
        if (addr >= svga->vram_max)
                return;
        addr &= svga->vram_mask;
//        if (svga_output) pclog("%08X\n", addr);
        svga->changedvram[addr >> 12] = changeframecount;
        
        switch (svga->writemode)
        {
                case 4:
                if (svga->gdcreg[0xb] & 0x10)
                {
//                        pclog("Writemode 4 : %X ", addr);
                        addr <<= 2;
                        svga->changedvram[addr >> 12] = changeframecount;
//                        pclog("%X %X  %02x\n", addr, val, svga->gdcreg[0xb]);
                        if (val & 0x80)
                        {
                                svga->vram[addr + 0] = svga->gdcreg[1];
                                svga->vram[addr + 1] = svga->gdcreg[0x11];
                        }
                        if (val & 0x40)
                        {
                                svga->vram[addr + 2] = svga->gdcreg[1];
                                svga->vram[addr + 3] = svga->gdcreg[0x11];
                        }
                        if (val & 0x20)
                        {
                                svga->vram[addr + 4] = svga->gdcreg[1];
                                svga->vram[addr + 5] = svga->gdcreg[0x11];
                        }
                        if (val & 0x10)
                        {
                                svga->vram[addr + 6] = svga->gdcreg[1];
                                svga->vram[addr + 7] = svga->gdcreg[0x11];
                        }
                        if (val & 0x08)
                        {
                                svga->vram[addr + 8] = svga->gdcreg[1];
                                svga->vram[addr + 9] = svga->gdcreg[0x11];
                        }
                        if (val & 0x04)
                        {
                                svga->vram[addr + 10] = svga->gdcreg[1];
                                svga->vram[addr + 11] = svga->gdcreg[0x11];
                        }
                        if (val & 0x02)
                        {
                                svga->vram[addr + 12] = svga->gdcreg[1];
                                svga->vram[addr + 13] = svga->gdcreg[0x11];
                        }
                        if (val & 0x01)
                        {
                                svga->vram[addr + 14] = svga->gdcreg[1];
                                svga->vram[addr + 15] = svga->gdcreg[0x11];
                        }
                }
                else
                {
//                        pclog("Writemode 4 : %X ", addr);
                        addr <<= 1;
                        svga->changedvram[addr >> 12] = changeframecount;
//                        pclog("%X %X  %02x\n", addr, val, svga->gdcreg[0xb]);
                        if (val & 0x80)
                                svga->vram[addr + 0] = svga->gdcreg[1];
                        if (val & 0x40)
                                svga->vram[addr + 1] = svga->gdcreg[1];
                        if (val & 0x20)
                                svga->vram[addr + 2] = svga->gdcreg[1];
                        if (val & 0x10)
                                svga->vram[addr + 3] = svga->gdcreg[1];
                        if (val & 0x08)
                                svga->vram[addr + 4] = svga->gdcreg[1];
                        if (val & 0x04)
                                svga->vram[addr + 5] = svga->gdcreg[1];
                        if (val & 0x02)
                                svga->vram[addr + 6] = svga->gdcreg[1];
                        if (val & 0x01)
                                svga->vram[addr + 7] = svga->gdcreg[1];
                }
                break;
                        
                case 5:
                if (svga->gdcreg[0xb] & 0x10)
                {
//                        pclog("Writemode 5 : %X ", addr);
                        addr <<= 2;
                        svga->changedvram[addr >> 12] = changeframecount;
//                        pclog("%X %X  %02x\n", addr, val, svga->gdcreg[0xb]);
                        svga->vram[addr +  0] = (val & 0x80) ? svga->gdcreg[1] : svga->gdcreg[0];
                        svga->vram[addr +  1] = (val & 0x80) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
                        svga->vram[addr +  2] = (val & 0x40) ? svga->gdcreg[1] : svga->gdcreg[0];
                        svga->vram[addr +  3] = (val & 0x40) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
                        svga->vram[addr +  4] = (val & 0x20) ? svga->gdcreg[1] : svga->gdcreg[0];
                        svga->vram[addr +  5] = (val & 0x20) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
                        svga->vram[addr +  6] = (val & 0x10) ? svga->gdcreg[1] : svga->gdcreg[0];
                        svga->vram[addr +  7] = (val & 0x10) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
                        svga->vram[addr +  8] = (val & 0x08) ? svga->gdcreg[1] : svga->gdcreg[0];
                        svga->vram[addr +  9] = (val & 0x08) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
                        svga->vram[addr + 10] = (val & 0x04) ? svga->gdcreg[1] : svga->gdcreg[0];
                        svga->vram[addr + 11] = (val & 0x04) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
                        svga->vram[addr + 12] = (val & 0x02) ? svga->gdcreg[1] : svga->gdcreg[0];
                        svga->vram[addr + 13] = (val & 0x02) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
                        svga->vram[addr + 14] = (val & 0x01) ? svga->gdcreg[1] : svga->gdcreg[0];
                        svga->vram[addr + 15] = (val & 0x01) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
                }
                else
                {
//                        pclog("Writemode 5 : %X ", addr);
                        addr <<= 1;
                        svga->changedvram[addr >> 12] = changeframecount;
//                        pclog("%X %X  %02x\n", addr, val, svga->gdcreg[0xb]);
                        svga->vram[addr + 0] = (val & 0x80) ? svga->gdcreg[1] : svga->gdcreg[0];
                        svga->vram[addr + 1] = (val & 0x40) ? svga->gdcreg[1] : svga->gdcreg[0];
                        svga->vram[addr + 2] = (val & 0x20) ? svga->gdcreg[1] : svga->gdcreg[0];
                        svga->vram[addr + 3] = (val & 0x10) ? svga->gdcreg[1] : svga->gdcreg[0];
                        svga->vram[addr + 4] = (val & 0x08) ? svga->gdcreg[1] : svga->gdcreg[0];
                        svga->vram[addr + 5] = (val & 0x04) ? svga->gdcreg[1] : svga->gdcreg[0];
                        svga->vram[addr + 6] = (val & 0x02) ? svga->gdcreg[1] : svga->gdcreg[0];
                        svga->vram[addr + 7] = (val & 0x01) ? svga->gdcreg[1] : svga->gdcreg[0];
                }
                break;
                
                case 1:
                if (writemask2 & 1) svga->vram[addr]       = svga->la;
                if (writemask2 & 2) svga->vram[addr | 0x1] = svga->lb;
                if (writemask2 & 4) svga->vram[addr | 0x2] = svga->lc;
                if (writemask2 & 8) svga->vram[addr | 0x3] = svga->ld;
                break;
                case 0:
                if (svga->gdcreg[3] & 7) 
                        val = svga_rotate[svga->gdcreg[3] & 7][val];
                if (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) && (!svga->gdcreg[1] || (svga->seqregs[7] & 1)))
                {
                        if (writemask2 & 1) svga->vram[addr]       = val;
                        if (writemask2 & 2) svga->vram[addr | 0x1] = val;
                        if (writemask2 & 4) svga->vram[addr | 0x2] = val;
                        if (writemask2 & 8) svga->vram[addr | 0x3] = val;
                }
                else
                {
                        if (svga->gdcreg[1] & 1) vala = (svga->gdcreg[0] & 1) ? 0xff : 0;
                        else                     vala = val;
                        if (svga->gdcreg[1] & 2) valb = (svga->gdcreg[0] & 2) ? 0xff : 0;
                        else                     valb = val;
                        if (svga->gdcreg[1] & 4) valc = (svga->gdcreg[0] & 4) ? 0xff : 0;
                        else                     valc = val;
                        if (svga->gdcreg[1] & 8) vald = (svga->gdcreg[0] & 8) ? 0xff : 0;
                        else                     vald = val;

                        switch (svga->gdcreg[3] & 0x18)
                        {
                                case 0: /*Set*/
                                if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | (svga->la & ~svga->gdcreg[8]);
                                if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | (svga->lb & ~svga->gdcreg[8]);
                                if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | (svga->lc & ~svga->gdcreg[8]);
                                if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | (svga->ld & ~svga->gdcreg[8]);
                                break;
                                case 8: /*AND*/
                                if (writemask2 & 1) svga->vram[addr]       = (vala | ~svga->gdcreg[8]) & svga->la;
                                if (writemask2 & 2) svga->vram[addr | 0x1] = (valb | ~svga->gdcreg[8]) & svga->lb;
                                if (writemask2 & 4) svga->vram[addr | 0x2] = (valc | ~svga->gdcreg[8]) & svga->lc;
                                if (writemask2 & 8) svga->vram[addr | 0x3] = (vald | ~svga->gdcreg[8]) & svga->ld;
                                break;
                                case 0x10: /*OR*/
                                if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | svga->la;
                                if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | svga->lb;
                                if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | svga->lc;
                                if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | svga->ld;
                                break;
                                case 0x18: /*XOR*/
                                if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) ^ svga->la;
                                if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) ^ svga->lb;
                                if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) ^ svga->lc;
                                if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) ^ svga->ld;
                                break;
                        }
//                        pclog("- %02X %02X %02X %02X   %08X\n",vram[addr],vram[addr|0x1],vram[addr|0x2],vram[addr|0x3],addr);
                }
                break;
                case 2:
                if (!(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1])
                {
                        if (writemask2 & 1) svga->vram[addr]       = (((val & 1) ? 0xff : 0) & svga->gdcreg[8]) | (svga->la & ~svga->gdcreg[8]);
                        if (writemask2 & 2) svga->vram[addr | 0x1] = (((val & 2) ? 0xff : 0) & svga->gdcreg[8]) | (svga->lb & ~svga->gdcreg[8]);
                        if (writemask2 & 4) svga->vram[addr | 0x2] = (((val & 4) ? 0xff : 0) & svga->gdcreg[8]) | (svga->lc & ~svga->gdcreg[8]);
                        if (writemask2 & 8) svga->vram[addr | 0x3] = (((val & 8) ? 0xff : 0) & svga->gdcreg[8]) | (svga->ld & ~svga->gdcreg[8]);
                }
                else
                {
                        vala = ((val & 1) ? 0xff : 0);
                        valb = ((val & 2) ? 0xff : 0);
                        valc = ((val & 4) ? 0xff : 0);
                        vald = ((val & 8) ? 0xff : 0);
                        switch (svga->gdcreg[3] & 0x18)
                        {
                                case 0: /*Set*/
                                if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | (svga->la & ~svga->gdcreg[8]);
                                if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | (svga->lb & ~svga->gdcreg[8]);
                                if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | (svga->lc & ~svga->gdcreg[8]);
                                if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | (svga->ld & ~svga->gdcreg[8]);
                                break;
                                case 8: /*AND*/
                                if (writemask2 & 1) svga->vram[addr]       = (vala | ~svga->gdcreg[8]) & svga->la;
                                if (writemask2 & 2) svga->vram[addr | 0x1] = (valb | ~svga->gdcreg[8]) & svga->lb;
                                if (writemask2 & 4) svga->vram[addr | 0x2] = (valc | ~svga->gdcreg[8]) & svga->lc;
                                if (writemask2 & 8) svga->vram[addr | 0x3] = (vald | ~svga->gdcreg[8]) & svga->ld;
                                break;
                                case 0x10: /*OR*/
                                if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | svga->la;
                                if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | svga->lb;
                                if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | svga->lc;
                                if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | svga->ld;
                                break;
                                case 0x18: /*XOR*/
                                if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) ^ svga->la;
                                if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) ^ svga->lb;
                                if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) ^ svga->lc;
                                if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) ^ svga->ld;
                                break;
                        }
                }
                break;
                case 3:
                if (svga->gdcreg[3] & 7) 
                        val = svga_rotate[svga->gdcreg[3] & 7][val];
                wm = svga->gdcreg[8];
                svga->gdcreg[8] &= val;

                vala = (svga->gdcreg[0] & 1) ? 0xff : 0;
                valb = (svga->gdcreg[0] & 2) ? 0xff : 0;
                valc = (svga->gdcreg[0] & 4) ? 0xff : 0;
                vald = (svga->gdcreg[0] & 8) ? 0xff : 0;
                switch (svga->gdcreg[3] & 0x18)
                {
                        case 0: /*Set*/
                        if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | (svga->la & ~svga->gdcreg[8]);
                        if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | (svga->lb & ~svga->gdcreg[8]);
                        if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | (svga->lc & ~svga->gdcreg[8]);
                        if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | (svga->ld & ~svga->gdcreg[8]);
                        break;
                        case 8: /*AND*/
                        if (writemask2 & 1) svga->vram[addr]       = (vala | ~svga->gdcreg[8]) & svga->la;
                        if (writemask2 & 2) svga->vram[addr | 0x1] = (valb | ~svga->gdcreg[8]) & svga->lb;
                        if (writemask2 & 4) svga->vram[addr | 0x2] = (valc | ~svga->gdcreg[8]) & svga->lc;
                        if (writemask2 & 8) svga->vram[addr | 0x3] = (vald | ~svga->gdcreg[8]) & svga->ld;
                        break;
                        case 0x10: /*OR*/
                        if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | svga->la;
                        if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | svga->lb;
                        if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | svga->lc;
                        if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | svga->ld;
                        break;
                        case 0x18: /*XOR*/
                        if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) ^ svga->la;
                        if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) ^ svga->lb;
                        if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) ^ svga->lc;
                        if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) ^ svga->ld;
                        break;
                }
                svga->gdcreg[8] = wm;
                break;
        }
}

void gd5429_start_blit(uint32_t cpu_dat, int count, void *p)
{
        gd5429_t *gd5429 = (gd5429_t *)p;
        svga_t *svga = &gd5429->svga;
        int blt_mask = gd5429->blt.mask & 7;

        if (gd5429->blt.mode & 0x10)
                blt_mask *= 2;
                
//        pclog("gd5429_start_blit %i\n", count);
        if (count == -1)
        {
                gd5429->blt.dst_addr_backup = gd5429->blt.dst_addr;
                gd5429->blt.src_addr_backup = gd5429->blt.src_addr;
                gd5429->blt.width_backup    = gd5429->blt.width;
                gd5429->blt.height_internal = gd5429->blt.height;
                gd5429->blt.x_count         = gd5429->blt.mask & 7;
                if (gd5429->blt.mode & 0x10)
                        gd5429->blt.x_count *= 2;
//                pclog("gd5429_start_blit : size %i, %i %i\n", gd5429->blt.width, gd5429->blt.height, gd5429->blt.x_count);
                
                if (gd5429->blt.mode & 0x04)
                {
//                        pclog("blt.mode & 0x04\n");
                        mem_mapping_set_handler(&svga->mapping, NULL, NULL, NULL, NULL, gd5429_blt_write_w, gd5429_blt_write_l);
                        mem_mapping_set_p(&svga->mapping, gd5429);
                        return;
                }
                else
                {
                        mem_mapping_set_handler(&gd5429->svga.mapping, gd5429_read, NULL, NULL, gd5429_write, NULL, NULL);
                        mem_mapping_set_p(&gd5429->svga.mapping, gd5429);
                        gd5429_recalc_mapping(gd5429);
                }                
        }
        else if (gd5429->blt.height_internal == 0xffff)
                return;
        
        while (count)
        {
                uint8_t src = 0, dst;
                int mask = 0;
                
                if (gd5429->blt.mode & 0x04)
                {
                        if (gd5429->blt.mode & 0x80)
                        {
                                if (gd5429->blt.mode & 0x10)
                                        fatal("Here blt.mode & 0x10\n");
                                src = (cpu_dat & 0x80) ? gd5429->blt.fg_col : gd5429->blt.bg_col;
                                mask = cpu_dat & 0x80;
                                cpu_dat <<= 1;
                                count--;
                        }
                        else
                        {
                                src = cpu_dat & 0xff;
                                cpu_dat >>= 8;
                                count -= 8;
                                mask = 1;
                        }
                }
                else
                {
                        switch (gd5429->blt.mode & 0xc0)
                        {
                                case 0x00:
                                src = svga->vram[gd5429->blt.src_addr & svga->vram_mask];
                                gd5429->blt.src_addr += ((gd5429->blt.mode & 0x01) ? -1 : 1);
                                mask = 1;
                                break;
                                case 0x40:
                                if (gd5429->blt.mode & 0x10)
                                        src = svga->vram[(gd5429->blt.src_addr & (svga->vram_mask & ~15)) | (gd5429->blt.dst_addr & 15)];
                                else
                                        src = svga->vram[(gd5429->blt.src_addr & (svga->vram_mask & ~7)) | (gd5429->blt.dst_addr & 7)];
                                mask = 1;
                                break;
                                case 0x80:
                                if (gd5429->blt.mode & 0x10)
                                {
                                        mask = svga->vram[gd5429->blt.src_addr & svga->vram_mask] & (0x80 >> (gd5429->blt.x_count >> 1));
                                        if (gd5429->blt.dst_addr & 1)
                                                src = mask ? (gd5429->blt.fg_col >> 8) : (gd5429->blt.bg_col >> 8);
                                        else
                                                src = mask ? gd5429->blt.fg_col : gd5429->blt.bg_col;
//                                        pclog("  src=%02x mask=%i fg_col=%04x bg_col=%04x %i %08x\n", src, mask, gd5429->blt.fg_col, gd5429->blt.bg_col, gd5429->blt.x_count, gd5429->blt.src_addr);
                                        gd5429->blt.x_count++;
                                        if (gd5429->blt.x_count == 16)
                                        {
                                                gd5429->blt.x_count = 0;
                                                gd5429->blt.src_addr++;
                                        }
                                }
                                else
                                {
                                        mask = svga->vram[gd5429->blt.src_addr & svga->vram_mask] & (0x80 >> gd5429->blt.x_count);
                                        src = mask ? gd5429->blt.fg_col : gd5429->blt.bg_col;
                                        gd5429->blt.x_count++;
                                        if (gd5429->blt.x_count == 8)
                                        {
                                                gd5429->blt.x_count = 0;
                                                gd5429->blt.src_addr++;
                                        }
                                }
                                break;
                                case 0xc0:                                
                                if (gd5429->blt.mode & 0x10)
                                {
                                        mask = svga->vram[gd5429->blt.src_addr & svga->vram_mask] & (0x80 >> ((gd5429->blt.dst_addr >> 1) & 7));
                                        if (gd5429->blt.dst_addr & 1)
                                                src = mask ? (gd5429->blt.fg_col >> 8) : (gd5429->blt.bg_col >> 8);
                                        else
                                                src = mask ? gd5429->blt.fg_col : gd5429->blt.bg_col;
                                }
                                else
                                {
                                        mask = svga->vram[gd5429->blt.src_addr & svga->vram_mask] & (0x80 >> (gd5429->blt.dst_addr & 7));
                                        src = mask ? gd5429->blt.fg_col : gd5429->blt.bg_col;
                                }
                                break;
                        }
                        count--;                        
                }
                dst = svga->vram[gd5429->blt.dst_addr & svga->vram_mask];
                svga->changedvram[(gd5429->blt.dst_addr & svga->vram_mask) >> 12] = changeframecount;
               
//                pclog("Blit %i,%i %06X %06X  %06X %02X %02X  %02X %02X ", gd5429->blt.width, gd5429->blt.height_internal, gd5429->blt.src_addr, gd5429->blt.dst_addr, gd5429->blt.src_addr & svga->vram_mask, svga->vram[gd5429->blt.src_addr & svga->vram_mask], 0x80 >> (gd5429->blt.dst_addr & 7), src, dst);
                switch (gd5429->blt.rop)
                {
                        case 0x00: dst = 0;             break;
                        case 0x05: dst =   src &  dst;  break;
                        case 0x06: dst =   dst;         break;
                        case 0x09: dst =   src & ~dst;  break;
                        case 0x0b: dst = ~ dst;         break;
                        case 0x0d: dst =   src;         break;
                        case 0x0e: dst = 0xff;          break;
                        case 0x50: dst = ~ src &  dst;  break;
                        case 0x59: dst =   src ^  dst;  break;
                        case 0x6d: dst =   src |  dst;  break;
                        case 0x90: dst = ~(src |  dst); break;
                        case 0x95: dst = ~(src ^  dst); break;
                        case 0xad: dst =   src | ~dst;  break;
                        case 0xd0: dst =  ~src;         break;
                        case 0xd6: dst =  ~src |  dst;  break;
                        case 0xda: dst = ~(src &  dst); break;                       
                }
                //pclog("%02X  %02X\n", dst, mask);
                
                if ((gd5429->blt.width_backup - gd5429->blt.width) >= blt_mask &&
                    !((gd5429->blt.mode & 0x08) && !mask))
                        svga->vram[gd5429->blt.dst_addr & svga->vram_mask] = dst;
                
                gd5429->blt.dst_addr += ((gd5429->blt.mode & 0x01) ? -1 : 1);
                
                gd5429->blt.width--;
                
                if (gd5429->blt.width == 0xffff)
                {
                        gd5429->blt.width = gd5429->blt.width_backup;

                        gd5429->blt.dst_addr = gd5429->blt.dst_addr_backup = gd5429->blt.dst_addr_backup + ((gd5429->blt.mode & 0x01) ? -gd5429->blt.dst_pitch : gd5429->blt.dst_pitch);
                        
                        switch (gd5429->blt.mode & 0xc0)
                        {
                                case 0x00:
                                gd5429->blt.src_addr = gd5429->blt.src_addr_backup = gd5429->blt.src_addr_backup + ((gd5429->blt.mode & 0x01) ? -gd5429->blt.src_pitch : gd5429->blt.src_pitch);
                                break;
                                case 0x40:
                                if (gd5429->blt.mode & 0x10)
                                        gd5429->blt.src_addr = ((gd5429->blt.src_addr + ((gd5429->blt.mode & 0x01) ? -16 : 16)) & 0x70) | (gd5429->blt.src_addr & ~0x70);
                                else
                                        gd5429->blt.src_addr = ((gd5429->blt.src_addr + ((gd5429->blt.mode & 0x01) ? -8 : 8)) & 0x38) | (gd5429->blt.src_addr & ~0x38);
                                break;
                                case 0x80:
                                if (gd5429->blt.x_count != 0)
                                {
                                        gd5429->blt.x_count = 0;
                                        gd5429->blt.src_addr++;
                                }
                                break;
                                case 0xc0:
                                gd5429->blt.src_addr = ((gd5429->blt.src_addr + ((gd5429->blt.mode & 0x01) ? -1 : 1)) & 7) | (gd5429->blt.src_addr & ~7);
                                break;
                        }
                        
                        gd5429->blt.height_internal--;
                        if (gd5429->blt.height_internal == 0xffff)
                        {
                                if (gd5429->blt.mode & 0x04)
                                {
                                        mem_mapping_set_handler(&gd5429->svga.mapping, gd5429_read, NULL, NULL, gd5429_write, NULL, NULL);
                                        mem_mapping_set_p(&gd5429->svga.mapping, gd5429);
                                        gd5429_recalc_mapping(gd5429);
                                }
                                return;
                        }
                                
                        if (gd5429->blt.mode & 0x04)
                                return;
                }                        
        }
}

void gd5429_mmio_write(uint32_t addr, uint8_t val, void *p)
{
        gd5429_t *gd5429 = (gd5429_t *)p;

//        pclog("MMIO write %08X %02X\n", addr, val);
        switch (addr & 0xff)
        {
                case 0x00:
                gd5429->blt.bg_col = (gd5429->blt.bg_col & 0xff00) | val;
                break;
                case 0x01:
                gd5429->blt.bg_col = (gd5429->blt.bg_col & 0x00ff) | (val << 8);
                break;

                case 0x04:
                gd5429->blt.fg_col = (gd5429->blt.fg_col & 0xff00) | val;
                break;
                case 0x05:
                gd5429->blt.fg_col = (gd5429->blt.fg_col & 0x00ff) | (val << 8);
                break;

                case 0x08:
                gd5429->blt.width = (gd5429->blt.width & 0xff00) | val;
                break;
                case 0x09:
                gd5429->blt.width = (gd5429->blt.width & 0x00ff) | (val << 8);
                break;
                case 0x0a:
                gd5429->blt.height = (gd5429->blt.height & 0xff00) | val;
                break;
                case 0x0b:
                gd5429->blt.height = (gd5429->blt.height & 0x00ff) | (val << 8);
                break;
                case 0x0c:
                gd5429->blt.dst_pitch = (gd5429->blt.dst_pitch & 0xff00) | val;
                break;
                case 0x0d:
                gd5429->blt.dst_pitch = (gd5429->blt.dst_pitch & 0x00ff) | (val << 8);
                break;
                case 0x0e:
                gd5429->blt.src_pitch = (gd5429->blt.src_pitch & 0xff00) | val;
                break;
                case 0x0f:
                gd5429->blt.src_pitch = (gd5429->blt.src_pitch & 0x00ff) | (val << 8);
                break;
                
                case 0x10:
                gd5429->blt.dst_addr = (gd5429->blt.dst_addr & 0xffff00) | val;
                break;
                case 0x11:
                gd5429->blt.dst_addr = (gd5429->blt.dst_addr & 0xff00ff) | (val << 8);
                break;
                case 0x12:
                gd5429->blt.dst_addr = (gd5429->blt.dst_addr & 0x00ffff) | (val << 16);
                break;

                case 0x14:
                gd5429->blt.src_addr = (gd5429->blt.src_addr & 0xffff00) | val;
                break;
                case 0x15:
                gd5429->blt.src_addr = (gd5429->blt.src_addr & 0xff00ff) | (val << 8);
                break;
                case 0x16:
                gd5429->blt.src_addr = (gd5429->blt.src_addr & 0x00ffff) | (val << 16);
                break;

                case 0x17:
                gd5429->blt.mask = val;
                break;
                case 0x18:
                gd5429->blt.mode = val;
                break;
                
                case 0x1a:
                gd5429->blt.rop = val;
                break;
                
                case 0x40:
                if (val & 0x02)
                        gd5429_start_blit(0, -1, gd5429);
                break;
        }
}

uint8_t gd5429_mmio_read(uint32_t addr, void *p)
{
//        gd5429_t *gd5429 = (gd5429_t *)p;

//        pclog("MMIO read %08X\n", addr);
        switch (addr & 0xff)
        {
                case 0x40: /*BLT status*/
                return 0;
        }
        return 0xff; /*All other registers read-only*/
}

void gd5429_blt_write_w(uint32_t addr, uint16_t val, void *p)
{
//        pclog("gd5429_blt_write_w %08X %08X\n", addr, val);
        gd5429_start_blit(val, 16, p);
}

void gd5429_blt_write_l(uint32_t addr, uint32_t val, void *p)
{
        gd5429_t *gd5429 = (gd5429_t *)p;
        
//        pclog("gd5429_blt_write_l %08X %08X  %04X %04X\n", addr, val,  ((val >> 8) & 0x00ff) | ((val << 8) & 0xff00), ((val >> 24) & 0x00ff) | ((val >> 8) & 0xff00));
        if ((gd5429->blt.mode & 0x84) == 0x84)
        {
                gd5429_start_blit( val        & 0xff, 8, p);
                gd5429_start_blit((val >> 8)  & 0xff, 8, p);
                gd5429_start_blit((val >> 16) & 0xff, 8, p);
                gd5429_start_blit((val >> 24) & 0xff, 8, p);
        }
        else
                gd5429_start_blit(val, 32, p);
}

void *gd5429_init()
{
        gd5429_t *gd5429 = malloc(sizeof(gd5429_t));
        svga_t *svga = &gd5429->svga;
        memset(gd5429, 0, sizeof(gd5429_t));

        rom_init(&gd5429->bios_rom, "5429.vbi", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
        
        svga_init(&gd5429->svga, gd5429, 1 << 21, /*2mb*/
                   gd5429_recalctimings,
                   gd5429_in, gd5429_out,
                   gd5429_hwcursor_draw,
                   NULL);

        mem_mapping_set_handler(&gd5429->svga.mapping, gd5429_read, NULL, NULL, gd5429_write, NULL, NULL);
        mem_mapping_set_p(&gd5429->svga.mapping, gd5429);

        mem_mapping_add(&gd5429->mmio_mapping, 0, 0, gd5429_mmio_read, NULL, NULL, gd5429_mmio_write, NULL, NULL,  NULL, 0, gd5429);

        io_sethandler(0x03c0, 0x0020, gd5429_in, NULL, NULL, gd5429_out, NULL, NULL, gd5429);

        svga->hwcursor.yoff = 32;
        svga->hwcursor.xoff = 0;

        gd5429->bank[1] = 0x8000;

        /*Default VCLK values*/
        svga->seqregs[0xb] = 0x66;
        svga->seqregs[0xc] = 0x5b;
        svga->seqregs[0xd] = 0x45;
        svga->seqregs[0xe] = 0x7e;
        svga->seqregs[0x1b] = 0x3b;
        svga->seqregs[0x1c] = 0x2f;
        svga->seqregs[0x1d] = 0x30;
        svga->seqregs[0x1e] = 0x33;

        
        return gd5429;
}

static int gd5429_available()
{
        return rom_present("5429.vbi");
}

void gd5429_close(void *p)
{
        gd5429_t *gd5429 = (gd5429_t *)p;

        svga_close(&gd5429->svga);
        
        free(gd5429);
}

void gd5429_speed_changed(void *p)
{
        gd5429_t *gd5429 = (gd5429_t *)p;
        
        svga_recalctimings(&gd5429->svga);
}

void gd5429_force_redraw(void *p)
{
        gd5429_t *gd5429 = (gd5429_t *)p;

        gd5429->svga.fullchange = changeframecount;
}

void gd5429_add_status_info(char *s, int max_len, void *p)
{
        gd5429_t *gd5429 = (gd5429_t *)p;
        
        svga_add_status_info(s, max_len, &gd5429->svga);
}

device_t gd5429_device =
{
        "Cirrus Logic GD5429",
        DEVICE_NOT_WORKING,
        gd5429_init,
        gd5429_close,
        gd5429_available,
        gd5429_speed_changed,
        gd5429_force_redraw,
        gd5429_add_status_info
};
