#include <stdlib.h>
#include <math.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "timer.h"
#include "video.h"
#include "vid_tandy.h"
#include "dosbox/vid_cga_comp.h"

#define TANDY_RGB 0
#define TANDY_COMPOSITE 1

typedef struct tandy_t
{
        mem_mapping_t mapping;
        mem_mapping_t ram_mapping;

        uint8_t crtc[32];
        int crtcreg;

        int array_index;
        uint8_t array[32];
        int memctrl;//=-1;
        uint32_t base;
        uint8_t mode, col;
        uint8_t stat;

        uint8_t* vram, * b8000;
        uint32_t b8000_mask;

        int linepos, displine;
        int sc, vc;
        int dispon;
        int con, coff, cursoron, blink;
        int vsynctime, vadj;
        uint16_t ma, maback;

        uint64_t dispontime, dispofftime;
        pc_timer_t timer;
        int firstline, lastline;

        int composite;
} tandy_t;

static uint8_t crtcmask[32] =
        {
                0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f, 0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
                0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };

void tandy_recalcaddress(tandy_t* tandy);
void tandy_recalctimings(tandy_t* tandy);

void tandy_out(uint16_t addr, uint8_t val, void* p)
{
        tandy_t* tandy = (tandy_t*)p;
        uint8_t old;
//        pclog("Tandy OUT %04X %02X\n",addr,val);
        switch (addr)
        {
        case 0x3d0:
        case 0x3d2:
        case 0x3d4:
        case 0x3d6:
                tandy->crtcreg = val & 0x1f;
                return;
        case 0x3d1:
        case 0x3d3:
        case 0x3d5:
        case 0x3d7:
                old = tandy->crtc[tandy->crtcreg];
                tandy->crtc[tandy->crtcreg] = val & crtcmask[tandy->crtcreg];
                if (old != val)
                {
                        if (tandy->crtcreg < 0xe || tandy->crtcreg > 0x10)
                        {
                                fullchange = changeframecount;
                                tandy_recalctimings(tandy);
                        }
                }
                return;
        case 0x3d8:
                tandy->mode = val;
                update_cga16_color(tandy->mode);
                return;
        case 0x3d9:
                tandy->col = val;
                return;
        case 0x3da:
                tandy->array_index = val & 0x1f;
                break;
        case 0x3de:
                if (tandy->array_index & 16)
                        val &= 0xf;
                tandy->array[tandy->array_index & 0x1f] = val;
                break;
        case 0x3df:
                tandy->memctrl = val;
                tandy_recalcaddress(tandy);
                break;
        case 0xa0:
                mem_mapping_set_addr(&tandy->ram_mapping, ((val >> 1) & 7) * 128 * 1024, 0x20000);
                tandy_recalcaddress(tandy);
                break;
        }
}

uint8_t tandy_in(uint16_t addr, void* p)
{
        tandy_t* tandy = (tandy_t*)p;
//        if (addr!=0x3DA) pclog("Tandy IN %04X\n",addr);
        switch (addr)
        {
        case 0x3d4:
                return tandy->crtcreg;
        case 0x3d5:
                return tandy->crtc[tandy->crtcreg];
        case 0x3da:
                return tandy->stat;
        }
        return 0xFF;
}

void tandy_recalcaddress(tandy_t* tandy)
{
        if ((tandy->memctrl & 0xc0) == 0xc0)
        {
                tandy->vram = &ram[((tandy->memctrl & 0x06) << 14) + tandy->base];
                tandy->b8000 = &ram[((tandy->memctrl & 0x30) << 11) + tandy->base];
                tandy->b8000_mask = 0x7fff;
//                printf("VRAM at %05X B8000 at %05X\n",((tandy->memctrl&0x6)<<14)+tandy->base,((tandy->memctrl&0x30)<<11)+tandy->base);
        }
        else
        {
                tandy->vram = &ram[((tandy->memctrl & 0x07) << 14) + tandy->base];
                tandy->b8000 = &ram[((tandy->memctrl & 0x38) << 11) + tandy->base];
                tandy->b8000_mask = 0x3fff;
//                printf("VRAM at %05X B8000 at %05X\n",((tandy->memctrl&0x7)<<14)+tandy->base,((tandy->memctrl&0x38)<<11)+tandy->base);
        }
}

void tandy_ram_write(uint32_t addr, uint8_t val, void* p)
{
        tandy_t* tandy = (tandy_t*)p;
//        pclog("Tandy RAM write %05X %02X %04X:%04X\n",addr,val,CS,pc);
        ram[tandy->base + (addr & 0x1ffff)] = val;
}

uint8_t tandy_ram_read(uint32_t addr, void* p)
{
        tandy_t* tandy = (tandy_t*)p;
//        pclog("Tandy RAM read %05X %02X %04X:%04X\n",addr,ram[tandy->base + (addr & 0x1ffff)],CS,pc);
        return ram[tandy->base + (addr & 0x1ffff)];
}

void tandy_write(uint32_t addr, uint8_t val, void* p)
{
        tandy_t* tandy = (tandy_t*)p;
        if (tandy->memctrl == -1)
                return;

        egawrites++;
//        pclog("Tandy VRAM write %05X %02X %04X:%04X  %04X:%04X\n",addr,val,CS,pc,DS,SI);
        tandy->b8000[addr & tandy->b8000_mask] = val;
}

uint8_t tandy_read(uint32_t addr, void* p)
{
        tandy_t* tandy = (tandy_t*)p;
        if (tandy->memctrl == -1)
                return 0xff;

        egareads++;
//        pclog("Tandy VRAM read  %05X %02X %04X:%04X\n",addr,tandy->b8000[addr&0x7FFF],CS,pc);
        return tandy->b8000[addr & tandy->b8000_mask];
}

void tandy_recalctimings(tandy_t* tandy)
{
        double _dispontime, _dispofftime, disptime;
        if (tandy->mode & 1)
        {
                disptime = tandy->crtc[0] + 1;
                _dispontime = tandy->crtc[1];
        }
        else
        {
                disptime = (tandy->crtc[0] + 1) << 1;
                _dispontime = tandy->crtc[1] << 1;
        }
        _dispofftime = disptime - _dispontime;
        _dispontime *= CGACONST;
        _dispofftime *= CGACONST;
        tandy->dispontime = (uint64_t)_dispontime;
        tandy->dispofftime = (uint64_t)_dispofftime;
}

void tandy_poll(void* p)
{
//        int *cgapal=cga4pal[((tandy->col&0x10)>>2)|((cgamode&4)>>1)|((cgacol&0x20)>>5)];
        tandy_t* tandy = (tandy_t*)p;
        uint16_t ca = (tandy->crtc[15] | (tandy->crtc[14] << 8)) & 0x3fff;
        int drawcursor;
        int x, c;
        int oldvc;
        uint8_t chr, attr;
        uint16_t dat;
        int cols[4];
        int col;
        int oldsc;
        if (!tandy->linepos)
        {
//                cgapal[0]=tandy->col&15;
//                printf("Firstline %i Lastline %i tandy->displine %i\n",firstline,lastline,tandy->displine);
                timer_advance_u64(&tandy->timer, tandy->dispofftime);
                tandy->stat |= 1;
                tandy->linepos = 1;
                oldsc = tandy->sc;
                if ((tandy->crtc[8] & 3) == 3)
                        tandy->sc = (tandy->sc << 1) & 7;
                if (tandy->dispon)
                {
                        if (tandy->displine < tandy->firstline)
                        {
                                tandy->firstline = tandy->displine;
                                video_wait_for_buffer();
//                                printf("Firstline %i\n",firstline);
                        }
                        tandy->lastline = tandy->displine;
                        cols[0] = tandy->array[2] & 0xf;
                        for (c = 0; c < 8; c++)
                        {
                                if (tandy->array[3] & 4)
                                {
                                        ((uint32_t*)buffer32->line[tandy->displine])[c] = cols[0];
                                        if (tandy->mode & 1) ((uint32_t*)buffer32->line[tandy->displine])[c + (tandy->crtc[1] << 3) + 8] = cols[0];
                                        else ((uint32_t*)buffer32->line[tandy->displine])[c + (tandy->crtc[1] << 4) + 8] = cols[0];
                                }
                                else if ((tandy->mode & 0x12) == 0x12)
                                {
                                        ((uint32_t*)buffer32->line[tandy->displine])[c] = 0;
                                        if (tandy->mode & 1) ((uint32_t*)buffer32->line[tandy->displine])[c + (tandy->crtc[1] << 3) + 8] = 0;
                                        else ((uint32_t*)buffer32->line[tandy->displine])[c + (tandy->crtc[1] << 4) + 8] = 0;
                                }
                                else
                                {
                                        ((uint32_t*)buffer32->line[tandy->displine])[c] = tandy->col & 15;
                                        if (tandy->mode & 1) ((uint32_t*)buffer32->line[tandy->displine])[c + (tandy->crtc[1] << 3) + 8] = tandy->col & 15;
                                        else ((uint32_t*)buffer32->line[tandy->displine])[c + (tandy->crtc[1] << 4) + 8] = tandy->col & 15;
                                }
                        }
//                        printf("X %i %i\n",c+(crtc[1]<<4)+8,c+(crtc[1]<<3)+8);
//                        printf("Drawing %i %i %i\n",tandy->displine,vc,sc);
                        if ((tandy->array[3] & 0x10) && (tandy->mode & 1)) /*320x200x16*/
                        {
                                for (x = 0; x < tandy->crtc[1]; x++)
                                {
                                        dat = (tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 3) * 0x2000)] << 8) |
                                              tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 3) * 0x2000) + 1];
                                        tandy->ma++;
                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 3) + 8] =
                                                ((uint32_t*)buffer32->line[tandy->displine])[(x << 3) + 9] = tandy->array[((dat >> 12) & tandy->array[1]) + 16];
                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 3) + 10] =
                                                ((uint32_t*)buffer32->line[tandy->displine])[(x << 3) + 11] = tandy->array[((dat >> 8) & tandy->array[1]) + 16];
                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 3) + 12] =
                                                ((uint32_t*)buffer32->line[tandy->displine])[(x << 3) + 13] = tandy->array[((dat >> 4) & tandy->array[1]) + 16];
                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 3) + 14] =
                                                ((uint32_t*)buffer32->line[tandy->displine])[(x << 3) + 15] = tandy->array[(dat & tandy->array[1]) + 16];
                                }
                        }
                        else if (tandy->array[3] & 0x10) /*160x200x16*/
                        {
                                for (x = 0; x < tandy->crtc[1]; x++)
                                {
                                        dat = (tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 3) * 0x2000)] << 8) |
                                              tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 3) * 0x2000) + 1];
                                        tandy->ma++;
                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + 8] =
                                                ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + 9] =
                                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + 10] =
                                                                ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + 11] = tandy->array[((dat >> 12) & tandy->array[1]) + 16];
                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + 12] =
                                                ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + 13] =
                                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + 14] =
                                                                ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + 15] = tandy->array[((dat >> 8) & tandy->array[1]) + 16];
                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + 16] =
                                                ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + 17] =
                                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + 18] =
                                                                ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + 19] = tandy->array[((dat >> 4) & tandy->array[1]) + 16];
                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + 20] =
                                                ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + 21] =
                                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + 22] =
                                                                ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + 23] = tandy->array[(dat & tandy->array[1]) + 16];
                                }
                        }
                        else if (tandy->array[3] & 0x08) /*640x200x4 - this implementation is a complete guess!*/
                        {
                                for (x = 0; x < tandy->crtc[1]; x++)
                                {
                                        dat = (tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 3) * 0x2000)] << 8) |
                                              tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 3) * 0x2000) + 1];
                                        tandy->ma++;
                                        for (c = 0; c < 8; c++)
                                        {
                                                chr = (dat >> 7) & 1;
                                                chr |= ((dat >> 14) & 2);
                                                ((uint32_t*)buffer32->line[tandy->displine])[(x << 3) + 8 + c] = tandy->array[(chr & tandy->array[1]) + 16];
                                                dat <<= 1;
                                        }
                                }
                        }
                        else if (tandy->mode & 1)
                        {
                                for (x = 0; x < tandy->crtc[1]; x++)
                                {
                                        chr = tandy->vram[(tandy->ma << 1) & 0x3fff];
                                        attr = tandy->vram[((tandy->ma << 1) + 1) & 0x3fff];
                                        drawcursor = ((tandy->ma == ca) && tandy->con && tandy->cursoron);
                                        if (tandy->mode & 0x20)
                                        {
                                                cols[1] = tandy->array[((attr & 15) & tandy->array[1]) + 16];
                                                cols[0] = tandy->array[(((attr >> 4) & 7) & tandy->array[1]) + 16];
                                                if ((tandy->blink & 16) && (attr & 0x80) && !drawcursor)
                                                        cols[1] = cols[0];
                                        }
                                        else
                                        {
                                                cols[1] = tandy->array[((attr & 15) & tandy->array[1]) + 16];
                                                cols[0] = tandy->array[((attr >> 4) & tandy->array[1]) + 16];
                                        }
                                        if (tandy->sc & 8)
                                        {
                                                for (c = 0; c < 8; c++)
                                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 3) + c + 8] = cols[0];
                                        }
                                        else
                                        {
                                                for (c = 0; c < 8; c++)
                                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 3) + c + 8] = cols[(fontdat[chr][tandy->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                                        }
//                                        if (!((ma^(crtc[15]|(crtc[14]<<8)))&0x3FFF)) printf("Cursor match! %04X\n",ma);
                                        if (drawcursor)
                                        {
                                                for (c = 0; c < 8; c++)
                                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 3) + c + 8] ^= 0xffffff;
                                        }
                                        tandy->ma++;
                                }
                        }
                        else if (!(tandy->mode & 2))
                        {
                                for (x = 0; x < tandy->crtc[1]; x++)
                                {
                                        chr = tandy->vram[(tandy->ma << 1) & 0x3fff];
                                        attr = tandy->vram[((tandy->ma << 1) + 1) & 0x3fff];
                                        drawcursor = ((tandy->ma == ca) && tandy->con && tandy->cursoron);
                                        if (tandy->mode & 0x20)
                                        {
                                                cols[1] = tandy->array[((attr & 15) & tandy->array[1]) + 16];
                                                cols[0] = tandy->array[(((attr >> 4) & 7) & tandy->array[1]) + 16];
                                                if ((tandy->blink & 16) && (attr & 0x80) && !drawcursor)
                                                        cols[1] = cols[0];
                                        }
                                        else
                                        {
                                                cols[1] = tandy->array[((attr & 15) & tandy->array[1]) + 16];
                                                cols[0] = tandy->array[((attr >> 4) & tandy->array[1]) + 16];
                                        }
                                        tandy->ma++;
                                        if (tandy->sc & 8)
                                        {
                                                for (c = 0; c < 8; c++)
                                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + (c << 1) + 8] =
                                                                ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + (c << 1) + 1 + 8] = cols[0];
                                        }
                                        else
                                        {
                                                for (c = 0; c < 8; c++)
                                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + (c << 1) + 8] =
                                                                ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[chr][tandy->sc & 7]
                                                                                                                                                  & (1 << (c ^ 7))) ? 1 : 0];
                                        }
                                        if (drawcursor)
                                        {
                                                for (c = 0; c < 16; c++)
                                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + c + 8] ^= 0xffffff;
                                        }
                                }
                        }
                        else if (!(tandy->mode & 16))
                        {
                                cols[0] = tandy->col & 15;
                                col = (tandy->col & 16) ? 8 : 0;
                                if (tandy->mode & 4)
                                {
                                        cols[1] = col | 3;
                                        cols[2] = col | 4;
                                        cols[3] = col | 7;
                                }
                                else if (tandy->col & 32)
                                {
                                        cols[1] = col | 3;
                                        cols[2] = col | 5;
                                        cols[3] = col | 7;
                                }
                                else
                                {
                                        cols[1] = col | 2;
                                        cols[2] = col | 4;
                                        cols[3] = col | 6;
                                }
                                for (x = 0; x < tandy->crtc[1]; x++)
                                {
                                        dat = (tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 1) * 0x2000)] << 8) |
                                              tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 1) * 0x2000) + 1];
                                        tandy->ma++;
                                        for (c = 0; c < 8; c++)
                                        {
                                                ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + (c << 1) + 8] =
                                                        ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + (c << 1) + 1 + 8] = cols[dat >> 14];
                                                dat <<= 2;
                                        }
                                }
                        }
                        else
                        {
                                cols[0] = 0;
                                cols[1] = tandy->array[(tandy->col & tandy->array[1]) + 16];
                                for (x = 0; x < tandy->crtc[1]; x++)
                                {
                                        dat = (tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 1) * 0x2000)] << 8) |
                                              tandy->vram[((tandy->ma << 1) & 0x1fff) + ((tandy->sc & 1) * 0x2000) + 1];
                                        tandy->ma++;
                                        for (c = 0; c < 16; c++)
                                        {
                                                ((uint32_t*)buffer32->line[tandy->displine])[(x << 4) + c + 8] = cols[dat >> 15];
                                                dat <<= 1;
                                        }
                                }
                        }
                }
                else
                {
                        if (tandy->array[3] & 4)
                        {
                                if (tandy->mode & 1) hline(buffer32, 0, tandy->displine, (tandy->crtc[1] << 3) + 16, tandy->array[2] & 0xf);
                                else hline(buffer32, 0, tandy->displine, (tandy->crtc[1] << 4) + 16, tandy->array[2] & 0xf);
                        }
                        else
                        {
                                cols[0] = ((tandy->mode & 0x12) == 0x12) ? 0 : (tandy->col & 0xf);
                                if (tandy->mode & 1) hline(buffer32, 0, tandy->displine, (tandy->crtc[1] << 3) + 16, cols[0]);
                                else hline(buffer32, 0, tandy->displine, (tandy->crtc[1] << 4) + 16, cols[0]);
                        }
                }
                if (tandy->mode & 1) x = (tandy->crtc[1] << 3) + 16;
                else x = (tandy->crtc[1] << 4) + 16;

                if (tandy->composite)
                {
                        for (c = 0; c < x; c++)
                                buffer32->line[tandy->displine][c] = ((uint32_t*)buffer32->line[tandy->displine])[c] & 0xf;

                        Composite_Process(tandy->mode, 0, x >> 2, buffer32->line[tandy->displine]);
                }
                else
                {
                        for (c = 0; c < x; c++)
                                ((uint32_t*)buffer32->line[tandy->displine])[c] = cgapal[((uint32_t*)buffer32->line[tandy->displine])[c] & 0xf];
                }

                tandy->sc = oldsc;
                if (tandy->vc == tandy->crtc[7] && !tandy->sc)
                {
                        tandy->stat |= 8;
//                        printf("VSYNC on %i %i\n",vc,sc);
                }
                tandy->displine++;
                if (tandy->displine >= 360)
                        tandy->displine = 0;
        }
        else
        {
                timer_advance_u64(&tandy->timer, tandy->dispontime);
                if (tandy->dispon)
                        tandy->stat &= ~1;
                tandy->linepos = 0;
                if (tandy->vsynctime)
                {
                        tandy->vsynctime--;
                        if (!tandy->vsynctime)
                        {
                                tandy->stat &= ~8;
//                                printf("VSYNC off %i %i\n",vc,sc);
                        }
                }
                if (tandy->sc == (tandy->crtc[11] & 31) || ((tandy->crtc[8] & 3) == 3 && tandy->sc == ((tandy->crtc[11] & 31) >> 1)))
                {
                        tandy->con = 0;
                        tandy->coff = 1;
                }
                if (tandy->vadj)
                {
                        tandy->sc++;
                        tandy->sc &= 31;
                        tandy->ma = tandy->maback;
                        tandy->vadj--;
                        if (!tandy->vadj)
                        {
                                tandy->dispon = 1;
                                tandy->ma = tandy->maback = (tandy->crtc[13] | (tandy->crtc[12] << 8)) & 0x3fff;
                                tandy->sc = 0;
//                                printf("Display on!\n");
                        }
                }
                else if (tandy->sc == tandy->crtc[9] || ((tandy->crtc[8] & 3) == 3 && tandy->sc == (tandy->crtc[9] >> 1)))
                {
                        tandy->maback = tandy->ma;
//                        con=0;
//                        coff=0;
                        tandy->sc = 0;
                        oldvc = tandy->vc;
                        tandy->vc++;
                        tandy->vc &= 127;
//                        printf("VC %i %i %i %i  %i\n",vc,crtc[4],crtc[6],crtc[7],tandy->dispon);
                        if (tandy->vc == tandy->crtc[6])
                                tandy->dispon = 0;
                        if (oldvc == tandy->crtc[4])
                        {
//                                printf("Display over at %i\n",tandy->displine);
                                tandy->vc = 0;
                                tandy->vadj = tandy->crtc[5];
                                if (!tandy->vadj)
                                        tandy->dispon = 1;
                                if (!tandy->vadj)
                                        tandy->ma = tandy->maback = (tandy->crtc[13] | (tandy->crtc[12] << 8)) & 0x3fff;
                                if ((tandy->crtc[10] & 0x60) == 0x20) tandy->cursoron = 0;
                                else tandy->cursoron = tandy->blink & 16;
//                                printf("CRTC10 %02X %i\n",crtc[10],cursoron);
                        }
                        if (tandy->vc == tandy->crtc[7])
                        {
                                tandy->dispon = 0;
                                tandy->displine = 0;
                                tandy->vsynctime = 16;//(crtc[3]>>4)+1;
//                                printf("tandy->vsynctime %i %02X\n",tandy->vsynctime,crtc[3]);
//                                tandy->stat|=8;
                                if (tandy->crtc[7])
                                {
//                                        printf("Lastline %i Firstline %i  %i   %i %i\n",lastline,firstline,lastline-firstline,crtc[1],xsize);
                                        if (tandy->mode & 1) x = (tandy->crtc[1] << 3) + 16;
                                        else x = (tandy->crtc[1] << 4) + 16;
                                        tandy->lastline++;
                                        if (x != xsize || (tandy->lastline - tandy->firstline) != ysize)
                                        {
                                                xsize = x;
                                                ysize = tandy->lastline - tandy->firstline;
//                                                printf("Resize to %i,%i - R1 %i\n",xsize,ysize,crtc[1]);
                                                if (xsize < 64) xsize = 656;
                                                if (ysize < 32) ysize = 200;
                                                updatewindowsize(xsize, (ysize << 1) + 16);
                                        }
//                                        printf("Blit %i %i\n",firstline,lastline);
//printf("Xsize is %i\n",xsize);

                                        video_blit_memtoscreen(0, tandy->firstline - 4, 0, (tandy->lastline - tandy->firstline) + 8, xsize, (tandy->lastline - tandy->firstline) + 8);

                                        frames++;
                                        video_res_x = xsize - 16;
                                        video_res_y = ysize;
                                        if ((tandy->array[3] & 0x10) && (tandy->mode & 1)) /*320x200x16*/
                                        {
                                                video_res_x /= 2;
                                                video_bpp = 4;
                                        }
                                        else if (tandy->array[3] & 0x10) /*160x200x16*/
                                        {
                                                video_res_x /= 4;
                                                video_bpp = 4;
                                        }
                                        else if (tandy->array[3] & 0x08) /*640x200x4 - this implementation is a complete guess!*/
                                                video_bpp = 2;
                                        else if (tandy->mode & 1)
                                        {
                                                video_res_x /= 8;
                                                video_res_y /= tandy->crtc[9] + 1;
                                                video_bpp = 0;
                                        }
                                        else if (!(tandy->mode & 2))
                                        {
                                                video_res_x /= 16;
                                                video_res_y /= tandy->crtc[9] + 1;
                                                video_bpp = 0;
                                        }
                                        else if (!(tandy->mode & 16))
                                        {
                                                video_res_x /= 2;
                                                video_bpp = 2;
                                        }
                                        else
                                                video_bpp = 1;
                                }
                                tandy->firstline = 1000;
                                tandy->lastline = 0;
                                tandy->blink++;
                        }
                }
                else
                {
                        tandy->sc++;
                        tandy->sc &= 31;
                        tandy->ma = tandy->maback;
                }
                if ((tandy->sc == (tandy->crtc[10] & 31) || ((tandy->crtc[8] & 3) == 3 && tandy->sc == ((tandy->crtc[10] & 31) >> 1))))
                        tandy->con = 1;
        }
}

void* tandy_init()
{
        int display_type;
        tandy_t* tandy = malloc(sizeof(tandy_t));
        memset(tandy, 0, sizeof(tandy_t));

        display_type = model_get_config_int("display_type");
        tandy->composite = (display_type != TANDY_RGB);

        cga_comp_init(1);
        tandy->memctrl = -1;
        tandy->base = (mem_size - 128) * 1024;

        timer_add(&tandy->timer, tandy_poll, tandy, 1);
        mem_mapping_add(&tandy->mapping, 0xb8000, 0x08000, tandy_read, NULL, NULL, tandy_write, NULL, NULL, NULL, 0, tandy);
        mem_mapping_add(&tandy->ram_mapping, 0x80000, 0x20000, tandy_ram_read, NULL, NULL, tandy_ram_write, NULL, NULL, NULL, 0, tandy);
        /*Base 128k mapping is controlled via port 0xA0, so we remove it from the main mapping*/
        mem_mapping_set_addr(&ram_low_mapping, 0, (mem_size - 128) * 1024);
        io_sethandler(0x03d0, 0x0010, tandy_in, NULL, NULL, tandy_out, NULL, NULL, tandy);
        io_sethandler(0x00a0, 0x0001, tandy_in, NULL, NULL, tandy_out, NULL, NULL, tandy);
        tandy->b8000_mask = 0x3fff;

        return tandy;
}

void tandy_close(void* p)
{
        tandy_t* tandy = (tandy_t*)p;

        free(tandy);
}

void tandy_speed_changed(void* p)
{
        tandy_t* tandy = (tandy_t*)p;

        tandy_recalctimings(tandy);
}

device_t tandy_device =
        {
                "Tandy 1000 (video)",
                0,
                tandy_init,
                tandy_close,
                NULL,
                tandy_speed_changed,
                NULL,
                NULL
        };

static device_config_t tandy_config[] =
        {
                {
                        .name = "display_type",
                        .description = "Display type",
                        .type = CONFIG_SELECTION,
                        .selection =
                                {
                                        {
                                                .description = "RGB",
                                                .value = TANDY_RGB
                                        },
                                        {
                                                .description = "Composite",
                                                .value = TANDY_COMPOSITE
                                        },
                                        {
                                                .description = ""
                                        }
                                },
                        .default_int = TANDY_RGB
                },
                {
                        .type = -1
                }
        };

/*These aren't really devices as such - more of a convenient way to hook in the
  config information*/
device_t tandy1000_device =
        {
                "Tandy 1000",
                0,
                NULL,
                NULL,
                NULL,
                NULL,
                NULL,
                NULL,
                tandy_config
        };
device_t tandy1000hx_device =
        {
                "Tandy 1000HX",
                0,
                NULL,
                NULL,
                NULL,
                NULL,
                NULL,
                NULL,
                tandy_config
        };
