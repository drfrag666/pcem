#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "ibm.h"
#include "device.h"
#include "mem.h"
#include "video.h"
#include "vid_svga.h"
#include "io.h"
#include "cpu.h"
#include "rom.h"
#include "thread.h"
#include "timer.h"

#include "vid_ati18800.h"
#include "vid_ati28800.h"
#include "vid_ati_mach64.h"
#include "vid_cga.h"
#include "vid_cl5429.h"
#include "vid_ega.h"
#include "vid_et4000.h"
#include "vid_et4000w32.h"
#include "vid_genius.h"
#include "vid_hercules.h"
#include "vid_incolor.h"
#include "vid_colorplus.h"
#include "vid_mda.h"
#include "vid_olivetti_m24.h"
#include "vid_oti067.h"
#include "vid_paradise.h"
#include "vid_pc1512.h"
#include "vid_pc1640.h"
#include "vid_pc200.h"
#include "vid_pcjr.h"
#include "vid_ps1_svga.h"
#include "vid_s3.h"
#include "vid_s3_virge.h"
#include "vid_tandy.h"
#include "vid_tandysl.h"
#include "vid_tgui9440.h"
#include "vid_tvga.h"
#include "vid_vga.h"
#include "vid_wy700.h"
#include "vid_t3100e.h"

enum
{
        VIDEO_ISA = 0,
        VIDEO_BUS
};

#define VIDEO_FLAG_TYPE_CGA     0
#define VIDEO_FLAG_TYPE_MDA     1
#define VIDEO_FLAG_TYPE_SPECIAL 2
#define VIDEO_FLAG_TYPE_MASK    3


typedef struct
{
        int type;
        int write_b, write_w, write_l;
        int read_b, read_w, read_l;
} video_timings_t;

typedef struct
{
        char name[64];
        char internal_name[24];
        device_t *device;
        int legacy_id;
        int flags;
        video_timings_t timing;
} VIDEO_CARD;

static VIDEO_CARD video_cards[] =
{
        {"ATI Graphics Pro Turbo (Mach64 GX)",     "mach64gx",       &mach64gx_device,                  GFX_MACH64GX,        VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_BUS, 2,  2,  1,  20, 20, 21}},
        {"ATI Video Xpression (Mach64 VT2)",       "mach64vt2",      &mach64vt2_device,                 GFX_MACH64VT2,       VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_BUS, 2,  2,  1,  20, 20, 21}},
        {"ATI VGA Charger (ATI-28800)",            "ati28800",       &ati28800_device,                  GFX_VGACHARGER,      VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_ISA, 3,  3,  6,   5,  5, 10}},
        {"ATI VGA Edge-16 (ATI-18800)",            "ati18800",       &ati18800_device,                  GFX_VGAEDGE16,       VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_ISA, 8, 16, 32,   8, 16, 32}},
        {"CGA",                                    "cga",            &cga_device,                       GFX_CGA,             VIDEO_FLAG_TYPE_CGA,     {VIDEO_ISA, 8, 16, 32,   8, 16, 32}},
        {"Cirrus Logic CL-GD5429",                 "cl_gd5429",      &gd5429_device,                    GFX_CL_GD5429,       VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_BUS, 4,  4,  8,  10, 10, 20}},
        {"Cirrus Logic CL-GD5430",                 "cl_gd5430",      &gd5430_device,                    GFX_CL_GD5430,       VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_BUS, 4,  4,  8,  10, 10, 20}},
        {"Cirrus Logic CL-GD5434",                 "cl_gd5434",      &gd5434_device,                    GFX_CL_GD5434,       VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_BUS, 4,  4,  8,  10, 10, 20}},
        {"Diamond Stealth 32 (Tseng ET4000/w32p)", "stealth32",      &et4000w32p_device,                GFX_ET4000W32,       VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_BUS, 4,  4,  4,  10, 10, 10}},
        {"Diamond Stealth 3D 2000 (S3 ViRGE)",     "stealth3d_2000", &s3_virge_device,                  GFX_VIRGE,           VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_BUS, 2,  2,  3,  28, 28, 45}},
        {"EGA",                                    "ega",            &ega_device,                       GFX_EGA,             VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_ISA, 8, 16, 32,   8, 16, 32}},
        {"Hercules",                               "hercules",       &hercules_device,                  GFX_HERCULES,        VIDEO_FLAG_TYPE_MDA,     {VIDEO_ISA, 8, 16, 32,   8, 16, 32}},
        {"Hercules InColor",                       "incolor",        &incolor_device,                   GFX_INCOLOR,         VIDEO_FLAG_TYPE_MDA,     {VIDEO_ISA, 8, 16, 32,   8, 16, 32}},
        {"MDA",                                    "mda",            &mda_device,                       GFX_MDA,             VIDEO_FLAG_TYPE_MDA,     {VIDEO_ISA, 8, 16, 32,   8, 16, 32}},
        {"MDSI Genius",                            "genius",         &genius_device,                    GFX_GENIUS,          VIDEO_FLAG_TYPE_CGA,     {VIDEO_ISA, 8, 16, 32,   8, 16, 32}},
        {"Number Nine 9FX (S3 Trio64)",            "n9_9fx",         &s3_9fx_device,                    GFX_N9_9FX,          VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_BUS, 3,  2,  4,  25, 25, 40}},
        {"OAK OTI-067",                            "oti067",         &oti067_device,                    GFX_OTI067,          VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_ISA, 6,  8, 16,   6,  8, 16}},
        {"Olivetti GO481 (Paradise PVGA1A)",       "olivetti_go481", &paradise_pvga1a_oli_go481_device, GFX_OLIVETTI_GO481,  VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_ISA, 6,  8, 16,   6,  8, 16}},
        {"Paradise Bahamas 64 (S3 Vision864)",     "bahamas64",      &s3_bahamas64_device,              GFX_BAHAMAS64,       VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_BUS, 4,  4,  5,  20, 20, 35}},
        {"Phoenix S3 Trio32",                      "px_trio32",      &s3_phoenix_trio32_device,         GFX_PHOENIX_TRIO32,  VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_BUS, 3,  2,  4,  25, 25, 40}},
        {"Phoenix S3 Trio64",                      "px_trio64",      &s3_phoenix_trio64_device,         GFX_PHOENIX_TRIO64,  VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_BUS, 3,  2,  4,  25, 25, 40}},
        {"Plantronics ColorPlus",                  "plantronics",    &colorplus_device,                 GFX_COLORPLUS,       VIDEO_FLAG_TYPE_CGA,     {VIDEO_ISA, 8, 16, 32,   8, 16, 32}},
        {"S3 ViRGE/DX",                            "virge375",       &s3_virge_375_device,              GFX_VIRGEDX,         VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_BUS, 2,  2,  3,  28, 28, 45}},
        {"Trident TVGA8900D",                      "tvga8900d",      &tvga8900d_device,                 GFX_TVGA,            VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_ISA, 3,  3,  6,   8,  8, 12}},
        {"Tseng ET4000AX",                         "et4000ax",       &et4000_device,                    GFX_ET4000,          VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_ISA, 3,  3,  6,   5,  5, 10}},
        {"Trident TGUI9400CXi",                    "tgui9400cxi",    &tgui9400cxi_device,               GFX_TGUI9400CXI,     VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_BUS, 4,  8, 16,   4,  8, 16}},
        {"Trident TGUI9440",                       "tgui9440",       &tgui9440_device,                  GFX_TGUI9440,        VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_BUS, 4,  8, 16,   4,  8, 16}},
        {"VGA",                                    "vga",            &vga_device,                       GFX_VGA,             VIDEO_FLAG_TYPE_SPECIAL, {VIDEO_ISA, 8, 16, 32,   8, 16, 32}},
        {"Wyse 700",                               "wy700",          &wy700_device,                     GFX_WY700,           VIDEO_FLAG_TYPE_CGA,     {VIDEO_ISA, 8, 16, 32,   8, 16, 32}},
        {"",                                       "",               NULL,                              0}
};

static video_timings_t timing_dram     = {VIDEO_BUS, 0,0,0, 0,0,0}; /*No additional waitstates*/
static video_timings_t timing_pc1512   = {VIDEO_BUS, 0,0,0, 0,0,0}; /*PC1512 video code handles waitstates itself*/
static video_timings_t timing_pc1640   = {VIDEO_ISA, 8,16,32, 8,16,32};
static video_timings_t timing_pc200    = {VIDEO_ISA, 8,16,32, 8,16,32};
static video_timings_t timing_m24      = {VIDEO_ISA, 8,16,32, 8,16,32};
static video_timings_t timing_pvga1a   = {VIDEO_ISA, 6, 8,16, 6, 8,16};
static video_timings_t timing_wd90c11  = {VIDEO_ISA, 3, 3, 6, 5, 5,10};
static video_timings_t timing_oti067   = {VIDEO_ISA, 6, 8,16, 6, 8,16};
static video_timings_t timing_vga      = {VIDEO_ISA, 8,16,32, 8,16,32};
static video_timings_t timing_ps1_svga = {VIDEO_ISA, 6, 8,16, 6, 8,16};
static video_timings_t timing_t3100e   = {VIDEO_ISA, 8,16,32, 8,16,32};

int video_card_available(int card)
{
        if (video_cards[card].device)
                return device_available(video_cards[card].device);

        return 1;
}

char *video_card_getname(int card)
{
        return video_cards[card].name;
}

device_t *video_card_getdevice(int card)
{
        return video_cards[card].device;
}

int video_card_has_config(int card)
{
        return video_cards[card].device->config ? 1 : 0;
}

int video_card_getid(char *s)
{
        int c = 0;

        while (video_cards[c].device)
        {
                if (!strcmp(video_cards[c].name, s))
                        return c;
                c++;
        }
        
        return 0;
}

int video_old_to_new(int card)
{
        int c = 0;
        
        while (video_cards[c].device)
        {
                if (video_cards[c].legacy_id == card)
                        return c;
                c++;
        }
        
        return 0;
}

int video_new_to_old(int card)
{
        return video_cards[card].legacy_id;
}

char *video_get_internal_name(int card)
{
        return video_cards[card].internal_name;
}

int video_get_video_from_internal_name(char *s)
{
	int c = 0;
	
	while (video_cards[c].legacy_id != -1)
	{
		if (!strcmp(video_cards[c].internal_name, s))
			return video_cards[c].legacy_id;
		c++;
	}
	
	return 0;
}

int video_is_mda()
{
        switch (romset)
        {
                case ROM_IBMPCJR:
                case ROM_TANDY:
                case ROM_TANDY1000HX:
                case ROM_TANDY1000SL2:
                case ROM_PC1512:
                case ROM_PC1640:
                case ROM_PC200:
                case ROM_OLIM24:
                case ROM_PC2086:
                case ROM_PC3086:
                case ROM_MEGAPC:
                case ROM_ACER386:
                case ROM_IBMPS1_2011:
                case ROM_IBMPS2_M30_286:
                case ROM_IBMPS2_M50:
                case ROM_IBMPS2_M55SX:
                case ROM_IBMPS2_M80:
                case ROM_IBMPS1_2121:
        	case ROM_T3100E:
                return 0;
        }
        return (video_cards[video_old_to_new(gfxcard)].flags & VIDEO_FLAG_TYPE_MASK) == VIDEO_FLAG_TYPE_MDA;
}
int video_is_cga()
{
        switch (romset)
        {
                case ROM_IBMPCJR:
                case ROM_TANDY:
                case ROM_TANDY1000HX:
                case ROM_TANDY1000SL2:
                case ROM_PC1512:
                case ROM_PC200:
                case ROM_OLIM24:
        	case ROM_T3100E:
                return 1;
                
                case ROM_PC1640:
                case ROM_PC2086:
                case ROM_PC3086:
                case ROM_MEGAPC:
                case ROM_ACER386:
                case ROM_IBMPS1_2011:
                case ROM_IBMPS2_M30_286:
                case ROM_IBMPS2_M50:
                case ROM_IBMPS2_M55SX:
                case ROM_IBMPS2_M80:
                case ROM_IBMPS1_2121:
                return 0;
        }
        return (video_cards[video_old_to_new(gfxcard)].flags & VIDEO_FLAG_TYPE_MASK) == VIDEO_FLAG_TYPE_CGA;
}
int video_is_ega_vga()
{
        switch (romset)
        {
                case ROM_IBMPCJR:
                case ROM_TANDY:
                case ROM_TANDY1000HX:
                case ROM_TANDY1000SL2:
                case ROM_PC1512:
                case ROM_PC200:
                case ROM_OLIM24:
        	case ROM_T3100E:
                return 0;
                
                case ROM_PC1640:
                case ROM_PC2086:
                case ROM_PC3086:
                case ROM_MEGAPC:
                case ROM_ACER386:
                case ROM_IBMPS1_2011:
                case ROM_IBMPS2_M30_286:
                case ROM_IBMPS2_M50:
                case ROM_IBMPS2_M55SX:
                case ROM_IBMPS2_M80:
                case ROM_IBMPS1_2121:
                return 1;
        }
        return (video_cards[video_old_to_new(gfxcard)].flags & VIDEO_FLAG_TYPE_MASK) == VIDEO_FLAG_TYPE_SPECIAL;
}

int video_fullscreen = 0, video_fullscreen_scale, video_fullscreen_first;
int video_force_aspect_ration = 0;
int vid_disc_indicator = 0;

uint32_t *video_15to32, *video_16to32;

int egareads=0,egawrites=0;
int changeframecount=2;

uint8_t rotatevga[8][256];

int frames = 0;
int video_frames = 0;
int video_refresh_rate = 0;

int fullchange;

uint8_t edatlookup[4][4];

/*Video timing settings -

8-bit - 1mb/sec
        B = 8 ISA clocks
        W = 16 ISA clocks
        L = 32 ISA clocks
        
Slow 16-bit - 2mb/sec
        B = 6 ISA clocks
        W = 8 ISA clocks
        L = 16 ISA clocks

Fast 16-bit - 4mb/sec
        B = 3 ISA clocks
        W = 3 ISA clocks
        L = 6 ISA clocks
        
Slow VLB/PCI - 8mb/sec (ish)
        B = 4 bus clocks
        W = 8 bus clocks
        L = 16 bus clocks
        
Mid VLB/PCI -
        B = 4 bus clocks
        W = 5 bus clocks
        L = 10 bus clocks
        
Fast VLB/PCI -
        B = 3 bus clocks
        W = 3 bus clocks
        L = 4 bus clocks
*/

int video_speed = 0;
int video_timing[7][4] =
{
        {VIDEO_ISA, 8, 16, 32},
        {VIDEO_ISA, 6,  8, 16},
        {VIDEO_ISA, 3,  3,  6},
        {VIDEO_BUS, 4,  8, 16},
        {VIDEO_BUS, 4,  5, 10},
        {VIDEO_BUS, 3,  3,  4}
};

void video_updatetiming()
{
	if (video_speed == -1)
	{
                video_timings_t *timing;
                int new_gfxcard = 0;
                
                switch (romset)
                {
                        case ROM_IBMPCJR:
                        case ROM_TANDY:
                        case ROM_TANDY1000HX:
                        case ROM_TANDY1000SL2:
                        timing = &timing_dram;
                        break;

                        case ROM_PC1512:
                        timing = &timing_pc1512;
                        break;
                
                        case ROM_PC1640:
                        timing = &timing_pc1640;
                        break;
                
                        case ROM_PC200:
                        timing = &timing_pc200;
                        break;
                
                        case ROM_OLIM24:
                        timing = &timing_m24;
                        break;

                        case ROM_PC2086:
                        case ROM_PC3086:
                        timing = &timing_pvga1a;
                        break;

                        case ROM_MEGAPC:
                        timing = &timing_wd90c11;
                        break;
                        
                        case ROM_ACER386:
                        timing = &timing_oti067;
                        break;
                
                        case ROM_IBMPS1_2011:
                        case ROM_IBMPS2_M30_286:
                        case ROM_IBMPS2_M50:
                        case ROM_IBMPS2_M55SX:
                        case ROM_IBMPS2_M80:
                        timing = &timing_vga;
                        break;

                        case ROM_IBMPS1_2121:
                        timing = &timing_ps1_svga;
                        break;

        		case ROM_T3100E:
                        timing = &timing_t3100e;
                        break;
                        
                        default:
                        new_gfxcard = video_old_to_new(gfxcard);
                        timing = &video_cards[new_gfxcard].timing;
                        break;
                }
                
                
		if (timing->type == VIDEO_ISA)
	        {
	                video_timing_read_b = ISA_CYCLES(timing->read_b);
	                video_timing_read_w = ISA_CYCLES(timing->read_w);
	                video_timing_read_l = ISA_CYCLES(timing->read_l);
	                video_timing_write_b = ISA_CYCLES(timing->write_b);
	                video_timing_write_w = ISA_CYCLES(timing->write_w);
	                video_timing_write_l = ISA_CYCLES(timing->write_l);
	        }
	        else
	        {
	                video_timing_read_b = (int)(bus_timing * timing->read_b);
	                video_timing_read_w = (int)(bus_timing * timing->read_w);
	                video_timing_read_l = (int)(bus_timing * timing->read_l);
	                video_timing_write_b = (int)(bus_timing * timing->write_b);
	                video_timing_write_w = (int)(bus_timing * timing->write_w);
	                video_timing_write_l = (int)(bus_timing * timing->write_l);
	        }
	}
        else 
	{
                if (video_timing[video_speed][0] == VIDEO_ISA)
                {
                        video_timing_read_b = ISA_CYCLES(video_timing[video_speed][1]);
                        video_timing_read_w = ISA_CYCLES(video_timing[video_speed][2]);
                        video_timing_read_l = ISA_CYCLES(video_timing[video_speed][3]);
                        video_timing_write_b = ISA_CYCLES(video_timing[video_speed][1]);
                        video_timing_write_w = ISA_CYCLES(video_timing[video_speed][2]);
                        video_timing_write_l = ISA_CYCLES(video_timing[video_speed][3]);
                }
                else
                {
                        video_timing_read_b = (int)(bus_timing * video_timing[video_speed][1]);
                        video_timing_read_w = (int)(bus_timing * video_timing[video_speed][2]);
                        video_timing_read_l = (int)(bus_timing * video_timing[video_speed][3]);
                        video_timing_write_b = (int)(bus_timing * video_timing[video_speed][1]);
                        video_timing_write_w = (int)(bus_timing * video_timing[video_speed][2]);
                        video_timing_write_l = (int)(bus_timing * video_timing[video_speed][3]);
                }
        }
        if (cpu_16bitbus)
        {
                video_timing_read_l = video_timing_read_w * 2;
                video_timing_write_l = video_timing_write_w * 2;
        }
}

int video_timing_read_b, video_timing_read_w, video_timing_read_l;
int video_timing_write_b, video_timing_write_w, video_timing_write_l;

int video_res_x, video_res_y, video_bpp;

void (*video_blit_memtoscreen_func)(int x, int y, int y1, int y2, int w, int h);

void video_init()
{
        pclog("Video_init %i %i\n",romset,gfxcard);

        switch (romset)
        {
                case ROM_IBMPCJR:
                device_add(&pcjr_video_device);
                return;
                
                case ROM_TANDY:
                case ROM_TANDY1000HX:
                device_add(&tandy_device);
                return;

                case ROM_TANDY1000SL2:
                device_add(&tandysl_device);
                return;

                case ROM_PC1512:
                device_add(&pc1512_device);
                return;
                
                case ROM_PC1640:
                device_add(&pc1640_device);
                return;
                
                case ROM_PC200:
                device_add(&pc200_device);
                return;
                
                case ROM_OLIM24:
                device_add(&m24_device);
                return;

                case ROM_PC2086:
                device_add(&paradise_pvga1a_pc2086_device);
                return;

                case ROM_PC3086:
                device_add(&paradise_pvga1a_pc3086_device);
                return;

                case ROM_MEGAPC:
                device_add(&paradise_wd90c11_megapc_device);
                return;
                        
                case ROM_ACER386:
                device_add(&oti067_acer386_device);
                return;
                
                case ROM_IBMPS1_2011:
                case ROM_IBMPS2_M30_286:
                case ROM_IBMPS2_M50:
                case ROM_IBMPS2_M55SX:
                case ROM_IBMPS2_M80:
                device_add(&ps1vga_device);
                return;

                case ROM_IBMPS1_2121:
                device_add(&ps1_m2121_svga_device);
                return;

		case ROM_T3100E:
                device_add(&t3100e_device);
                return;
        }
        device_add(video_cards[video_old_to_new(gfxcard)].device);
}


BITMAP *buffer32;

uint8_t fontdat[2048][8];
uint8_t fontdatm[2048][16];
uint8_t fontdatw[512][32];	/* Wyse700 font */
uint8_t fontdat8x12[256][16];	/* MDSI Genius font */

int xsize=1,ysize=1;

void loadfont(char *s, int format)
{
        FILE *f=romfopen(s,"rb");
        int c,d;
        
        pclog("loadfont %i %s %p\n", format, s, f);
        if (!f)
	{
		return;
	}
	switch (format)
        {
		case 0:	/* MDA */
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdatm[c][d]=getc(f);
                        }
                }
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdatm[c][d+8]=getc(f);
                        }
                }
                fseek(f,4096+2048,SEEK_SET);
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdat[c][d]=getc(f);
                        }
                }
		break;
		case 1:	/* PC200 */
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdatm[c][d]=getc(f);
                        }
                }
                for (c=0;c<256;c++)
                {
                       	for (d=0;d<8;d++)
                        {
                                fontdatm[c][d+8]=getc(f);
                        }
                }
                fseek(f, 4096, SEEK_SET);
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdat[c][d]=getc(f);
                        }
                        for (d=0;d<8;d++) getc(f);                
                }
		break;
		default:
		case 2:	/* CGA */
                for (c=0;c<256;c++)
                {
                       	for (d=0;d<8;d++)
                        {
                                fontdat[c][d]=getc(f);
                        }
                }
		break;
		case 3: /* Wyse 700 */
                for (c=0;c<512;c++)
                {
                        for (d=0;d<32;d++)
                        {
                                fontdatw[c][d]=getc(f);
                        }
                }
		break;
		case 4: /* MDSI Genius */
                for (c=0;c<256;c++)
                {
                        for (d=0;d<16;d++)
                        {
                                fontdat8x12[c][d]=getc(f);
                        }
                }
		break;
		case 5: /* Toshiba 3100e */
		for (d = 0; d < 2048; d += 512)	/* Four languages... */
		{
	                for (c = d; c < d+256; c++)
                	{
                       		fread(&fontdatm[c][8], 1, 8, f);
                	}
                	for (c = d+256; c < d+512; c++)
                	{
                        	fread(&fontdatm[c][8], 1, 8, f);
                	}
	                for (c = d; c < d+256; c++)
                	{
                        	fread(&fontdatm[c][0], 1, 8, f);
                	}
                	for (c = d+256; c < d+512; c++)
                	{
                        	fread(&fontdatm[c][0], 1, 8, f);
                	}
			fseek(f, 4096, SEEK_CUR);	/* Skip blank section */
	                for (c = d; c < d+256; c++)
                	{
                       		fread(&fontdat[c][0], 1, 8, f);
                	}
                	for (c = d+256; c < d+512; c++)
                	{
                        	fread(&fontdat[c][0], 1, 8, f);
                	}
		}
                break;

        }
        fclose(f);
}

static struct
{
        int x, y, y1, y2, w, h;
        int busy;
        int buffer_in_use;

        thread_t *blit_thread;
        event_t *wake_blit_thread;
        event_t *blit_complete;
        event_t *buffer_not_in_use;
} blit_data;

static void blit_thread(void *param);

uint32_t cgapal[16];

void initvideo()
{
        int c, d, e;

        buffer32 = create_bitmap(2048, 2048);

        for (c = 0; c < 256; c++)
        {
                e = c;
                for (d = 0; d < 8; d++)
                {
                        rotatevga[d][c] = e;
                        e = (e >> 1) | ((e & 1) ? 0x80 : 0);
                }
        }
        for (c = 0; c < 4; c++)
        {
                for (d = 0; d < 4; d++)
                {
                        edatlookup[c][d] = 0;
                        if (c & 1) edatlookup[c][d] |= 1;
                        if (d & 1) edatlookup[c][d] |= 2;
                        if (c & 2) edatlookup[c][d] |= 0x10;
                        if (d & 2) edatlookup[c][d] |= 0x20;
//                        printf("Edat %i,%i now %02X\n",c,d,edatlookup[c][d]);
                }
        }

        video_15to32 = malloc(4 * 65536);
        for (c = 0; c < 65536; c++)
                video_15to32[c] = ((c & 31) << 3) | (((c >> 5) & 31) << 11) | (((c >> 10) & 31) << 19);

        video_16to32 = malloc(4 * 65536);
        for (c = 0; c < 65536; c++)
                video_16to32[c] = ((c & 31) << 3) | (((c >> 5) & 63) << 10) | (((c >> 11) & 31) << 19);

        cgapal_rebuild(DISPLAY_RGB, 0);

        blit_data.wake_blit_thread = thread_create_event();
        blit_data.blit_complete = thread_create_event();
        blit_data.buffer_not_in_use = thread_create_event();
        blit_data.blit_thread = thread_create(blit_thread, NULL);
}

void closevideo()
{
        thread_kill(blit_data.blit_thread);
        thread_destroy_event(blit_data.buffer_not_in_use);
        thread_destroy_event(blit_data.blit_complete);
        thread_destroy_event(blit_data.wake_blit_thread);

        free(video_15to32);
        free(video_16to32);
        destroy_bitmap(buffer32);
}


static void blit_thread(void *param)
{
        while (1)
        {
                thread_wait_event(blit_data.wake_blit_thread, -1);
                thread_reset_event(blit_data.wake_blit_thread);
                
                video_blit_memtoscreen_func(blit_data.x, blit_data.y, blit_data.y1, blit_data.y2, blit_data.w, blit_data.h);
                
                blit_data.busy = 0;
                thread_set_event(blit_data.blit_complete);
        }
}

void video_blit_complete()
{
        blit_data.buffer_in_use = 0;
        thread_set_event(blit_data.buffer_not_in_use);
}

void video_wait_for_blit()
{
        while (blit_data.busy)
                thread_wait_event(blit_data.blit_complete, 1);
        thread_reset_event(blit_data.blit_complete);
}
void video_wait_for_buffer()
{
        while (blit_data.buffer_in_use)
                thread_wait_event(blit_data.buffer_not_in_use, 1);
        thread_reset_event(blit_data.buffer_not_in_use);
}

void video_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h)
{
        video_frames++;
        if (h <= 0)
                return;
        video_wait_for_blit();
        blit_data.busy = 1;
        blit_data.buffer_in_use = 1;
        blit_data.x = x;
        blit_data.y = y;
        blit_data.y1 = y1;
        blit_data.y2 = y2;
        blit_data.w = w;
        blit_data.h = h;
        thread_set_event(blit_data.wake_blit_thread);
}

void cgapal_rebuild(int display_type, int contrast)
{
        switch (display_type)
	{
                case DISPLAY_GREEN:
                if (contrast)
                {
                        cgapal[0x0] = makecol(0x00, 0x00, 0x00);
                        cgapal[0x1] = makecol(0x00, 0x34, 0x0c);
                        cgapal[0x2] = makecol(0x04, 0x5d, 0x14);
                        cgapal[0x3] = makecol(0x04, 0x69, 0x18);
                        cgapal[0x4] = makecol(0x08, 0xa2, 0x24);
                        cgapal[0x5] = makecol(0x08, 0xb2, 0x28);
                        cgapal[0x6] = makecol(0x0c, 0xe7, 0x34);
                        cgapal[0x7] = makecol(0x0c, 0xf3, 0x38);
                        cgapal[0x8] = makecol(0x00, 0x1c, 0x04);
                        cgapal[0x9] = makecol(0x04, 0x4d, 0x10);
                        cgapal[0xa] = makecol(0x04, 0x7d, 0x1c);
                        cgapal[0xb] = makecol(0x04, 0x8e, 0x20);
                        cgapal[0xc] = makecol(0x08, 0xc7, 0x2c);
                        cgapal[0xd] = makecol(0x08, 0xd7, 0x30);
                        cgapal[0xe] = makecol(0x14, 0xff, 0x45);
                        cgapal[0xf] = makecol(0x34, 0xff, 0x5d);
                }
                else
                {
                        cgapal[0x0] = makecol(0x00, 0x00, 0x00);
                        cgapal[0x1] = makecol(0x00, 0x34, 0x0c);
                        cgapal[0x2] = makecol(0x04, 0x55, 0x14);
                        cgapal[0x3] = makecol(0x04, 0x5d, 0x14);
                        cgapal[0x4] = makecol(0x04, 0x86, 0x20);
                        cgapal[0x5] = makecol(0x04, 0x92, 0x20);
                        cgapal[0x6] = makecol(0x08, 0xba, 0x2c);
                        cgapal[0x7] = makecol(0x08, 0xc7, 0x2c);
                        cgapal[0x8] = makecol(0x04, 0x8a, 0x20);
                        cgapal[0x9] = makecol(0x08, 0xa2, 0x24);
                        cgapal[0xa] = makecol(0x08, 0xc3, 0x2c);
                        cgapal[0xb] = makecol(0x08, 0xcb, 0x30);
                        cgapal[0xc] = makecol(0x0c, 0xe7, 0x34);
                        cgapal[0xd] = makecol(0x0c, 0xef, 0x38);
                        cgapal[0xe] = makecol(0x24, 0xff, 0x51);
                        cgapal[0xf] = makecol(0x34, 0xff, 0x5d);
                }
                break;
                case DISPLAY_AMBER:
                if (contrast)
                {
                        cgapal[0x0] = makecol(0x00, 0x00, 0x00);
                        cgapal[0x1] = makecol(0x55, 0x14, 0x00);
                        cgapal[0x2] = makecol(0x82, 0x2c, 0x00);
                        cgapal[0x3] = makecol(0x92, 0x34, 0x00);
                        cgapal[0x4] = makecol(0xcf, 0x61, 0x00);
                        cgapal[0x5] = makecol(0xdf, 0x6d, 0x00);
                        cgapal[0x6] = makecol(0xff, 0x9a, 0x04);
                        cgapal[0x7] = makecol(0xff, 0xae, 0x18);
                        cgapal[0x8] = makecol(0x2c, 0x08, 0x00);
                        cgapal[0x9] = makecol(0x6d, 0x20, 0x00);
                        cgapal[0xa] = makecol(0xa6, 0x45, 0x00);
                        cgapal[0xb] = makecol(0xba, 0x51, 0x00);
                        cgapal[0xc] = makecol(0xef, 0x79, 0x00);
                        cgapal[0xd] = makecol(0xfb, 0x86, 0x00);
                        cgapal[0xe] = makecol(0xff, 0xcb, 0x28);
                        cgapal[0xf] = makecol(0xff, 0xe3, 0x34);
                }
                else
                {
                        cgapal[0x0] = makecol(0x00, 0x00, 0x00);
                        cgapal[0x1] = makecol(0x55, 0x14, 0x00);
                        cgapal[0x2] = makecol(0x79, 0x24, 0x00);
                        cgapal[0x3] = makecol(0x86, 0x2c, 0x00);
                        cgapal[0x4] = makecol(0xae, 0x49, 0x00);
                        cgapal[0x5] = makecol(0xbe, 0x55, 0x00);
                        cgapal[0x6] = makecol(0xe3, 0x71, 0x00);
                        cgapal[0x7] = makecol(0xef, 0x79, 0x00);
                        cgapal[0x8] = makecol(0xb2, 0x4d, 0x00);
                        cgapal[0x9] = makecol(0xcb, 0x5d, 0x00);
                        cgapal[0xa] = makecol(0xeb, 0x79, 0x00);
                        cgapal[0xb] = makecol(0xf3, 0x7d, 0x00);
                        cgapal[0xc] = makecol(0xff, 0x9e, 0x04);
                        cgapal[0xd] = makecol(0xff, 0xaa, 0x10);
                        cgapal[0xe] = makecol(0xff, 0xdb, 0x30);
                        cgapal[0xf] = makecol(0xff, 0xe3, 0x34);
                }
                break;
                case DISPLAY_WHITE:
                if (contrast)
                {
                        cgapal[0x0] = makecol(0x00, 0x00, 0x00);
                        cgapal[0x1] = makecol(0x37, 0x3d, 0x40);
                        cgapal[0x2] = makecol(0x55, 0x5c, 0x5f);
                        cgapal[0x3] = makecol(0x61, 0x67, 0x6b);
                        cgapal[0x4] = makecol(0x8f, 0x95, 0x95);
                        cgapal[0x5] = makecol(0x9b, 0xa0, 0x9f);
                        cgapal[0x6] = makecol(0xcc, 0xcf, 0xc8);
                        cgapal[0x7] = makecol(0xdf, 0xde, 0xd4);
                        cgapal[0x8] = makecol(0x24, 0x27, 0x29);
                        cgapal[0x9] = makecol(0x42, 0x48, 0x4c);
                        cgapal[0xa] = makecol(0x70, 0x76, 0x78);
                        cgapal[0xb] = makecol(0x81, 0x87, 0x87);
                        cgapal[0xc] = makecol(0xaf, 0xb3, 0xb0);
                        cgapal[0xd] = makecol(0xbb, 0xbf, 0xba);
                        cgapal[0xe] = makecol(0xef, 0xed, 0xdf);
                        cgapal[0xf] = makecol(0xff, 0xfd, 0xed);
                }
                else
                {
                        cgapal[0x0] = makecol(0x00, 0x00, 0x00);
                        cgapal[0x1] = makecol(0x37, 0x3d, 0x40);
                        cgapal[0x2] = makecol(0x4a, 0x50, 0x54);
                        cgapal[0x3] = makecol(0x55, 0x5c, 0x5f);
                        cgapal[0x4] = makecol(0x78, 0x7e, 0x80);
                        cgapal[0x5] = makecol(0x81, 0x87, 0x87);
                        cgapal[0x6] = makecol(0xa3, 0xa7, 0xa6);
                        cgapal[0x7] = makecol(0xaf, 0xb3, 0xb0);
                        cgapal[0x8] = makecol(0x7a, 0x81, 0x83);
                        cgapal[0x9] = makecol(0x8c, 0x92, 0x92);
                        cgapal[0xa] = makecol(0xac, 0xb0, 0xad);
                        cgapal[0xb] = makecol(0xb3, 0xb7, 0xb4);
                        cgapal[0xc] = makecol(0xd1, 0xd3, 0xcb);
                        cgapal[0xd] = makecol(0xd9, 0xdb, 0xd2);
                        cgapal[0xe] = makecol(0xf7, 0xf5, 0xe7);
                        cgapal[0xf] = makecol(0xff, 0xfd, 0xed);
                }
                break;
                
                default:
                cgapal[0x0] = makecol(0x00, 0x00, 0x00);
                cgapal[0x1] = makecol(0x00, 0x00, 0xaa);
                cgapal[0x2] = makecol(0x00, 0xaa, 0x00);
                cgapal[0x3] = makecol(0x00, 0xaa, 0xaa);
                cgapal[0x4] = makecol(0xaa, 0x00, 0x00);
                cgapal[0x5] = makecol(0xaa, 0x00, 0xaa);
                if (display_type == DISPLAY_RGB_NO_BROWN)
                {
                        cgapal[0x6] = makecol(0xaa, 0xaa, 0x00);
                }
                else
                {
                        cgapal[0x6] = makecol(0xaa, 0x55, 0x00);
                }
                cgapal[0x7] = makecol(0xaa, 0xaa, 0xaa);
                cgapal[0x8] = makecol(0x55, 0x55, 0x55);
                cgapal[0x9] = makecol(0x55, 0x55, 0xff);
                cgapal[0xa] = makecol(0x55, 0xff, 0x55);
                cgapal[0xb] = makecol(0x55, 0xff, 0xff);
                cgapal[0xc] = makecol(0xff, 0x55, 0x55);
                cgapal[0xd] = makecol(0xff, 0x55, 0xff);
                cgapal[0xe] = makecol(0xff, 0xff, 0x55);
                cgapal[0xf] = makecol(0xff, 0xff, 0xff);
                break;
        }
}
