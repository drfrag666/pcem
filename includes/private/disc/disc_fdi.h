#ifndef _DISC_FDI_H_
#define _DISC_FDI_H_
void fdi_init();
void fdi_load(int drive, char *fn);
void fdi_close(int drive);
void fdi_seek(int drive, int track);
void fdi_readsector(int drive, int sector, int track, int side, int density, int sector_size);
void fdi_writesector(int drive, int sector, int track, int side, int density, int sector_size);
void fdi_readaddress(int drive, int sector, int side, int density);
void fdi_format(int drive, int sector, int side, int density, uint8_t fill);
int fdi_hole(int drive);
void fdi_stop();
void fdi_poll();


#endif /* _DISC_FDI_H_ */
