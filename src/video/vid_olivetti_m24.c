/*Olivetti M24 video emulation
  Essentially double-res CGA*/
#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "timer.h"
#include "video.h"
#include "vid_olivetti_m24.h"

typedef struct m24_t
{
        mem_mapping_t mapping;

        uint8_t crtc[32];
        int crtcreg;

        uint8_t* vram;
        uint8_t charbuffer[256];

        uint8_t ctrl;
        uint32_t base;

        uint8_t cgamode, cgacol;
        uint8_t stat;

        int linepos, displine;
        int sc, vc;
        int con, coff, cursoron, blink;
        int vsynctime, vadj;
        int lineff;
        uint16_t ma, maback;
        int dispon;

        uint64_t dispontime, dispofftime;
        pc_timer_t timer;

        int firstline, lastline;
} m24_t;

static uint8_t crtcmask[32] =
        {
                0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f, 0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
                0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };

void m24_recalctimings(m24_t* m24);

void m24_out(uint16_t addr, uint8_t val, void* p)
{
        m24_t* m24 = (m24_t*)p;
        uint8_t old;
//        pclog("m24_out %04X %02X\n", addr, val);
        switch (addr)
        {
        case 0x3d4:
                m24->crtcreg = val & 31;
                return;
        case 0x3d5:
                old = m24->crtc[m24->crtcreg];
                m24->crtc[m24->crtcreg] = val & crtcmask[m24->crtcreg];
                if (old != val)
                {
                        if (m24->crtcreg < 0xe || m24->crtcreg > 0x10)
                        {
                                fullchange = changeframecount;
                                m24_recalctimings(m24);
                        }
                }
                return;
        case 0x3d8:
                m24->cgamode = val;
                return;
        case 0x3d9:
                m24->cgacol = val;
                return;
        case 0x3de:
                m24->ctrl = val;
                m24->base = (val & 0x08) ? 0x4000 : 0;
                return;
        }
}

uint8_t m24_in(uint16_t addr, void* p)
{
        m24_t* m24 = (m24_t*)p;
        switch (addr)
        {
        case 0x3d4:
                return m24->crtcreg;
        case 0x3d5:
                return m24->crtc[m24->crtcreg];
        case 0x3da:
                return m24->stat;
        }
        return 0xff;
}

void m24_write(uint32_t addr, uint8_t val, void* p)
{
        m24_t* m24 = (m24_t*)p;
        int offset = ((timer_get_remaining_u64(&m24->timer) / CGACONST) * 4) & 0xfc;

        m24->vram[addr & 0x7FFF] = val;
        m24->charbuffer[offset] = val;
        m24->charbuffer[offset | 1] = val;
}

uint8_t m24_read(uint32_t addr, void* p)
{
        m24_t* m24 = (m24_t*)p;
        return m24->vram[addr & 0x7FFF];
}

void m24_recalctimings(m24_t* m24)
{
        double _dispontime, _dispofftime, disptime;
        if (m24->cgamode & 1)
        {
                disptime = m24->crtc[0] + 1;
                _dispontime = m24->crtc[1];
        }
        else
        {
                disptime = (m24->crtc[0] + 1) << 1;
                _dispontime = m24->crtc[1] << 1;
        }
        _dispofftime = disptime - _dispontime;
//        printf("%i %f %f %f  %i %i\n",cgamode&1,disptime,dispontime,dispofftime,crtc[0],crtc[1]);
        _dispontime *= CGACONST / 2;
        _dispofftime *= CGACONST / 2;
//        printf("Timings - on %f off %f frame %f second %f\n",dispontime,dispofftime,(dispontime+dispofftime)*262.0,(dispontime+dispofftime)*262.0*59.92);
        m24->dispontime = (uint64_t)_dispontime;
        m24->dispofftime = (uint64_t)_dispofftime;
}

void m24_poll(void* p)
{
        m24_t* m24 = (m24_t*)p;
        uint16_t ca = (m24->crtc[15] | (m24->crtc[14] << 8)) & 0x3fff;
        int drawcursor;
        int x, c;
        int oldvc;
        uint8_t chr, attr;
        uint16_t dat, dat2;
        uint32_t cols[4];
        int col;
        int oldsc;
        if (!m24->linepos)
        {
//                pclog("Line poll  %i %i %i %i - %04X %i %i %i\n", m24_lineff, vc, sc, vadj, ma, firstline, lastline, displine);
                timer_advance_u64(&m24->timer, m24->dispofftime);
                m24->stat |= 1;
                m24->linepos = 1;
                oldsc = m24->sc;
                if ((m24->crtc[8] & 3) == 3)
                        m24->sc = (m24->sc << 1) & 7;
                if (m24->dispon)
                {
                        pclog("dispon %i\n", m24->linepos);
                        if (m24->displine < m24->firstline)
                        {
                                m24->firstline = m24->displine;
//                                printf("Firstline %i\n",firstline);
                        }
                        m24->lastline = m24->displine;
                        for (c = 0; c < 8; c++)
                        {
                                if ((m24->cgamode & 0x12) == 0x12)
                                {
                                        ((uint32_t*)buffer32->line[m24->displine])[c] = cgapal[0];
                                        if (m24->cgamode & 1) ((uint32_t*)buffer32->line[m24->displine])[c + (m24->crtc[1] << 3) + 8] = cgapal[0];
                                        else ((uint32_t*)buffer32->line[m24->displine])[c + (m24->crtc[1] << 4) + 8] = cgapal[0];
                                }
                                else
                                {
                                        ((uint32_t*)buffer32->line[m24->displine])[c] = cgapal[m24->cgacol & 15];
                                        if (m24->cgamode & 1) ((uint32_t*)buffer32->line[m24->displine])[c + (m24->crtc[1] << 3) + 8] = cgapal[m24->cgacol & 15];
                                        else ((uint32_t*)buffer32->line[m24->displine])[c + (m24->crtc[1] << 4) + 8] = cgapal[m24->cgacol & 15];
                                }
                        }
                        if (m24->cgamode & 1)
                        {
                                for (x = 0; x < m24->crtc[1]; x++)
                                {
                                        chr = m24->charbuffer[x << 1];
                                        attr = m24->charbuffer[(x << 1) + 1];
                                        drawcursor = ((m24->ma == ca) && m24->con && m24->cursoron);
                                        if (m24->cgamode & 0x20)
                                        {
                                                cols[1] = cgapal[attr & 15];
                                                cols[0] = cgapal[(attr >> 4) & 7];
                                                if ((m24->blink & 16) && (attr & 0x80) && !drawcursor)
                                                        cols[1] = cols[0];
                                        }
                                        else
                                        {
                                                cols[1] = cgapal[attr & 15];
                                                cols[0] = cgapal[attr >> 4];
                                        }
                                        if (drawcursor)
                                        {
                                                for (c = 0; c < 8; c++)
                                                        ((uint32_t*)buffer32->line[m24->displine])[(x << 3) + c + 8] =
                                                                cols[(fontdatm[chr][((m24->sc & 7) << 1) | m24->lineff] & (1 << (c ^ 7))) ? 1 : 0] ^ 0xffffff;
                                        }
                                        else
                                        {
                                                for (c = 0; c < 8; c++)
                                                        ((uint32_t*)buffer32->line[m24->displine])[(x << 3) + c + 8] = cols[(fontdatm[chr][((m24->sc & 7) << 1) | m24->lineff]
                                                                                                                             & (1 << (c ^ 7))) ? 1 : 0];
                                        }
                                        m24->ma++;
                                }
                        }
                        else if (!(m24->cgamode & 2))
                        {
                                for (x = 0; x < m24->crtc[1]; x++)
                                {
                                        chr = m24->vram[((m24->ma << 1) & 0x3fff) + m24->base];
                                        attr = m24->vram[(((m24->ma << 1) + 1) & 0x3fff) + m24->base];
                                        drawcursor = ((m24->ma == ca) && m24->con && m24->cursoron);
                                        if (m24->cgamode & 0x20)
                                        {
                                                cols[1] = cgapal[attr & 15];
                                                cols[0] = cgapal[(attr >> 4) & 7];
                                                if ((m24->blink & 16) && (attr & 0x80))
                                                        cols[1] = cols[0];
                                        }
                                        else
                                        {
                                                cols[1] = cgapal[attr & 15];
                                                cols[0] = cgapal[attr >> 4];
                                        }
                                        m24->ma++;
                                        if (drawcursor)
                                        {
                                                for (c = 0; c < 8; c++)
                                                        ((uint32_t*)buffer32->line[m24->displine])[(x << 4) + (c << 1) + 8] =
                                                                ((uint32_t*)buffer32->line[m24->displine])[(x << 4) + (c << 1) + 1 + 8] =
                                                                        cols[(fontdatm[chr][((m24->sc & 7) << 1) | m24->lineff] & (1 << (c ^ 7))) ? 1 : 0] ^ 0xffffff;
                                        }
                                        else
                                        {
                                                for (c = 0; c < 8; c++)
                                                        ((uint32_t*)buffer32->line[m24->displine])[(x << 4) + (c << 1) + 8] =
                                                                ((uint32_t*)buffer32->line[m24->displine])[(x << 4) + (c << 1) + 1 + 8] = cols[(fontdatm[chr][((m24->sc & 7) << 1) | m24->lineff]
                                                                                                                                                & (1 << (c ^ 7))) ? 1 : 0];
                                        }
                                }
                        }
                        else if (!(m24->cgamode & 16))
                        {
                                cols[0] = cgapal[m24->cgacol & 15];
                                col = (m24->cgacol & 16) ? 8 : 0;
                                if (m24->cgamode & 4)
                                {
                                        cols[1] = cgapal[col | 3];
                                        cols[2] = cgapal[col | 4];
                                        cols[3] = cgapal[col | 7];
                                }
                                else if (m24->cgacol & 32)
                                {
                                        cols[1] = cgapal[col | 3];
                                        cols[2] = cgapal[col | 5];
                                        cols[3] = cgapal[col | 7];
                                }
                                else
                                {
                                        cols[1] = cgapal[col | 2];
                                        cols[2] = cgapal[col | 4];
                                        cols[3] = cgapal[col | 6];
                                }
                                for (x = 0; x < m24->crtc[1]; x++)
                                {
                                        dat = (m24->vram[((m24->ma << 1) & 0x1fff) + ((m24->sc & 1) * 0x2000) + m24->base] << 8) |
                                              m24->vram[((m24->ma << 1) & 0x1fff) + ((m24->sc & 1) * 0x2000) + 1 + m24->base];
                                        m24->ma++;
                                        for (c = 0; c < 8; c++)
                                        {
                                                ((uint32_t*)buffer32->line[m24->displine])[(x << 4) + (c << 1) + 8] =
                                                        ((uint32_t*)buffer32->line[m24->displine])[(x << 4) + (c << 1) + 1 + 8] = cols[dat >> 14];
                                                dat <<= 2;
                                        }
                                }
                        }
                        else
                        {
                                if (m24->ctrl & 1)
                                {
                                        dat2 = ((m24->sc & 1) * 0x4000) | (m24->lineff * 0x2000);
                                        cols[0] = cgapal[0];
                                        cols[1] = cgapal[15];
                                }
                                else
                                {
                                        dat2 = (m24->sc & 1) * 0x2000;
                                        cols[0] = cgapal[0];
                                        cols[1] = cgapal[m24->cgacol & 15];
                                }
                                for (x = 0; x < m24->crtc[1]; x++)
                                {
                                        dat = (m24->vram[((m24->ma << 1) & 0x1fff) + dat2] << 8) | m24->vram[((m24->ma << 1) & 0x1fff) + dat2 + 1];
                                        m24->ma++;
                                        for (c = 0; c < 16; c++)
                                        {
                                                ((uint32_t*)buffer32->line[m24->displine])[(x << 4) + c + 8] = cols[dat >> 15];
                                                dat <<= 1;
                                        }
                                }
                        }
                }
                else
                {
                        cols[0] = cgapal[((m24->cgamode & 0x12) == 0x12) ? 0 : (m24->cgacol & 15)];
                        if (m24->cgamode & 1) hline(buffer32, 0, m24->displine, (m24->crtc[1] << 3) + 16, cols[0]);
                        else hline(buffer32, 0, m24->displine, (m24->crtc[1] << 4) + 16, cols[0]);
                }

                if (m24->cgamode & 1) x = (m24->crtc[1] << 3) + 16;
                else x = (m24->crtc[1] << 4) + 16;

                m24->sc = oldsc;
                if (m24->vc == m24->crtc[7] && !m24->sc)
                        m24->stat |= 8;
                m24->displine++;
                if (m24->displine >= 720) m24->displine = 0;
        }
        else
        {
//                pclog("Line poll  %i %i %i %i\n", m24_lineff, vc, sc, vadj);
                timer_advance_u64(&m24->timer, m24->dispontime);
                if (m24->dispon) m24->stat &= ~1;
                m24->linepos = 0;
                m24->lineff ^= 1;
                if (m24->lineff)
                {
                        m24->ma = m24->maback;
                }
                else
                {
                        if (m24->vsynctime)
                        {
                                m24->vsynctime--;
                                if (!m24->vsynctime)
                                        m24->stat &= ~8;
                        }
                        if (m24->sc == (m24->crtc[11] & 31) || ((m24->crtc[8] & 3) == 3 && m24->sc == ((m24->crtc[11] & 31) >> 1)))
                        {
                                m24->con = 0;
                                m24->coff = 1;
                        }
                        if (m24->vadj)
                        {
                                m24->sc++;
                                m24->sc &= 31;
                                m24->ma = m24->maback;
                                m24->vadj--;
                                if (!m24->vadj)
                                {
                                        m24->dispon = 1;
                                        m24->ma = m24->maback = (m24->crtc[13] | (m24->crtc[12] << 8)) & 0x3fff;
                                        m24->sc = 0;
                                }
                        }
                        else if (m24->sc == m24->crtc[9] || ((m24->crtc[8] & 3) == 3 && m24->sc == (m24->crtc[9] >> 1)))
                        {
                                m24->maback = m24->ma;
                                m24->sc = 0;
                                oldvc = m24->vc;
                                m24->vc++;
                                m24->vc &= 127;

                                if (m24->vc == m24->crtc[6])
                                        m24->dispon = 0;

                                if (oldvc == m24->crtc[4])
                                {
                                        m24->vc = 0;
                                        m24->vadj = m24->crtc[5];
                                        if (!m24->vadj) m24->dispon = 1;
                                        if (!m24->vadj) m24->ma = m24->maback = (m24->crtc[13] | (m24->crtc[12] << 8)) & 0x3fff;
                                        if ((m24->crtc[10] & 0x60) == 0x20) m24->cursoron = 0;
                                        else m24->cursoron = m24->blink & 16;
                                }

                                if (m24->vc == m24->crtc[7])
                                {
                                        m24->dispon = 0;
                                        m24->displine = 0;
                                        m24->vsynctime = (m24->crtc[3] >> 4) + 1;
                                        if (m24->crtc[7])
                                        {
                                                if (m24->cgamode & 1) x = (m24->crtc[1] << 3) + 16;
                                                else x = (m24->crtc[1] << 4) + 16;
                                                m24->lastline++;
                                                if (x != xsize || (m24->lastline - m24->firstline) != ysize)
                                                {
                                                        xsize = x;
                                                        ysize = m24->lastline - m24->firstline;
                                                        if (xsize < 64) xsize = 656;
                                                        if (ysize < 32) ysize = 200;
                                                        updatewindowsize(xsize, ysize + 16);
                                                }

                                                video_blit_memtoscreen(0, m24->firstline - 8, 0, (m24->lastline - m24->firstline) + 16, xsize, (m24->lastline - m24->firstline) + 16);
                                                frames++;

                                                video_res_x = xsize - 16;
                                                video_res_y = ysize;
                                                if (m24->cgamode & 1)
                                                {
                                                        video_res_x /= 8;
                                                        video_res_y /= (m24->crtc[9] + 1) * 2;
                                                        video_bpp = 0;
                                                }
                                                else if (!(m24->cgamode & 2))
                                                {
                                                        video_res_x /= 16;
                                                        video_res_y /= (m24->crtc[9] + 1) * 2;
                                                        video_bpp = 0;
                                                }
                                                else if (!(m24->cgamode & 16))
                                                {
                                                        video_res_x /= 2;
                                                        video_res_y /= 2;
                                                        video_bpp = 2;
                                                }
                                                else if (!(m24->ctrl & 1))
                                                {
                                                        video_res_y /= 2;
                                                        video_bpp = 1;
                                                }
                                        }
                                        m24->firstline = 1000;
                                        m24->lastline = 0;
                                        m24->blink++;
                                }
                        }
                        else
                        {
                                m24->sc++;
                                m24->sc &= 31;
                                m24->ma = m24->maback;
                        }
                        if ((m24->sc == (m24->crtc[10] & 31) || ((m24->crtc[8] & 3) == 3 && m24->sc == ((m24->crtc[10] & 31) >> 1))))
                                m24->con = 1;
                }
                if (m24->dispon && (m24->cgamode & 1))
                {
                        for (x = 0; x < (m24->crtc[1] << 1); x++)
                                m24->charbuffer[x] = m24->vram[(((m24->ma << 1) + x) & 0x3fff) + m24->base];
                }
        }
}

void* m24_init()
{
        m24_t* m24 = malloc(sizeof(m24_t));
        memset(m24, 0, sizeof(m24_t));

        m24->vram = malloc(0x8000);

        timer_add(&m24->timer, m24_poll, m24, 1);
        mem_mapping_add(&m24->mapping, 0xb8000, 0x08000, m24_read, NULL, NULL, m24_write, NULL, NULL, NULL, 0, m24);
        io_sethandler(0x03d0, 0x0010, m24_in, NULL, NULL, m24_out, NULL, NULL, m24);
        return m24;
}

void m24_close(void* p)
{
        m24_t* m24 = (m24_t*)p;

        free(m24->vram);
        free(m24);
}

void m24_speed_changed(void* p)
{
        m24_t* m24 = (m24_t*)p;

        m24_recalctimings(m24);
}

device_t m24_device =
        {
                "Olivetti M24 (video)",
                0,
                m24_init,
                m24_close,
                NULL,
                m24_speed_changed,
                NULL,
                NULL
        };
