#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

unsigned int read32 ( unsigned int );

unsigned int read_register ( unsigned int );

#define RAM_START       0x40000000
#define PERIPH_START    0xE0000000

static int DBUGFETCH = 0;
static int DBUGRAM   = 0;
static int DBUGRAMW  = 0;
static int DBUGREG   = 0;
static int DBUG      = 0;
static int DBUGUART  = 0;
static int DISS      = 0;

#define MINUS_ONE   ((unsigned int)(~0))

#define ROMADDMASK 0xFFFFF
#define RAMADDMASK 0xFFFFF

#define ROMSIZE (ROMADDMASK+1)
#define RAMSIZE (RAMADDMASK+1)

unsigned short rom[ROMSIZE>>1];
unsigned short ram[RAMSIZE>>1];

#define CPSR_N (1<<31)
#define CPSR_Z (1<<30)
#define CPSR_C (1<<29)
#define CPSR_V (1<<28)
#define CPSR_Q (1<<27)

unsigned int vcdcount;
unsigned int output_vcd = 0;
FILE *fpvcd;

unsigned int systick_ctrl;
unsigned int systick_reload;
unsigned int systick_count;
unsigned int systick_calibrate;

void handle_disk(unsigned int wr);

#define MAX_DISK    4
int disk_count;
int disk_fd[MAX_DISK];
off_t disk_size[MAX_DISK];
unsigned int disk_status;
unsigned int disk_buffer[128];
unsigned int disk_offset_low, disk_offset_high;
unsigned int disk_ide_id;

unsigned int *frame_buffer;
unsigned int fb_width, fb_height;
unsigned int fb_port;

unsigned int halfadd;
unsigned int cpsr;
unsigned int handler_mode;
unsigned int reg_norm[16]; //normal execution mode, do not have a thread mode

unsigned int cpuid;
unsigned int entry = 0;
char *output_file_name;

#define MAX_INPUT   (64 * 1024)

int read_fd, write_fd;
unsigned char input_buffer[MAX_INPUT];
typedef enum {EvtKey, EvtButton} EvtType;
struct {
    EvtType typ;
    int X, Y;
} input_event[MAX_INPUT];
size_t input_read_ptr = 0, input_write_ptr = 0;
int socket_fd = -1;

unsigned long instructions;
unsigned long fetches;
unsigned long reads;
unsigned long writes;
unsigned long systick_ints;

#define MAX_SYM 4096
unsigned int symaddr[4096];
char *symbols[4096];

Display *xDisplay;
Window xWin;
XImage *xImg;
int xScreenNum, imgWidth, imgHeight;
//-------------------------------------------------------------------
int input_char(int c, EvtType typ, int X, int Y)
{
    input_buffer[input_write_ptr] = c;
    input_event[input_write_ptr].typ = typ;
    input_event[input_write_ptr].X = X;
    input_event[input_write_ptr].Y = Y;
    if (++input_write_ptr > MAX_INPUT) input_write_ptr = 0;
    return c;
}
//-------------------------------------------------------------------
void fb_open(int width, int height, char *data)
{
    imgWidth = width; imgHeight = height;

	xDisplay = XOpenDisplay(NULL);
	xScreenNum = DefaultScreen(xDisplay);
	Window root = RootWindow(xDisplay,xScreenNum);
	Visual *visual = DefaultVisual(xDisplay,xScreenNum);

	xImg = XCreateImage(xDisplay,visual,DefaultDepth(xDisplay,xScreenNum),ZPixmap,0,data,imgWidth,imgHeight,32,0);
    for (int x = 0; x < imgWidth; x++)
        for (int y = 0; y < imgHeight; y++) {
            unsigned  z = x + y;
            XPutPixel(xImg, x, y, (z << 16) + (x << 8)+y);
        }

	xWin = XCreateSimpleWindow(xDisplay,root,50,50,imgWidth,imgHeight,1,0,0);
	XSelectInput(xDisplay,xWin,ExposureMask|StructureNotifyMask|KeyPressMask|ButtonPressMask);
	XMapWindow(xDisplay,xWin);
}
//-------------------------------------------------------------------
void fb_flush()
{
    XFlush(xDisplay);
}
//-------------------------------------------------------------------
int fb_qevent()
{
    XEvent event;

    fb_flush();
    while (XCheckMaskEvent(xDisplay,-1,&event)) {
	    if (event.type == Expose ||
            event.type == MappingNotify ||
            event.type == KeyPress ||
            event.type == ButtonPress)
        {
            XPutBackEvent(xDisplay,&event);
            return 1;
        }
    }
    return 0;
}
//-------------------------------------------------------------------
int fb_event()
{
	XEvent event;

    fb_flush();
    while (1) {
        XNextEvent(xDisplay,&event);
	    if(event.type == Expose)
	    {
	        XPutImage(xDisplay,xWin,DefaultGC(xDisplay,xScreenNum),xImg,0,0,0,0,imgWidth,imgHeight);
            return 0;
	    }
        else if (event.type == MappingNotify) {
            XRefreshKeyboardMapping(&event.xmapping);
            return 0;
        }
        else if (event.type == KeyPress) {
            // KeySym sym = XLookupKeysym(&event.xkey,0);
            KeySym ksym;
            char buf[8];
            if (XLookupString(&event.xkey, buf, 1, &ksym, NULL))
                return input_char(buf[0], EvtKey, event.xkey.x, event.xkey.y);
            return 0;
        } else if (event.type == ButtonPress) {
            return input_char(128 + event.xbutton.button, EvtButton, event.xkey.x, event.xkey.y);
        }
    }
    return 0;
}
//-------------------------------------------------------------------
void fb_close()
{
    XDestroyWindow(xDisplay, xWin);
    XCloseDisplay(xDisplay);
}
//-------------------------------------------------------------------
void init_syms()
{
    memset(symaddr, 0, sizeof(symaddr));
    memset(symbols, 0, sizeof(symbols));
}
//-------------------------------------------------------------------
void add_sym(unsigned int addr, char *sym)
{
    unsigned int h;
    int i;

    h = addr & (MAX_SYM-1);
    for (i = 0; i < MAX_SYM; i++) {
        if (symbols[h] == NULL) {
            symaddr[h] = addr;
            symbols[h] = strdup(sym);
            return;
        }
        if (++h == MAX_SYM)
            h = 0;
    }
}
//-------------------------------------------------------------------
char *get_sym(unsigned int addr)
{
    unsigned int h;
    int i;

    h = addr & (MAX_SYM-1);
    for (i = 0; i < MAX_SYM; i++) {
        if (symbols[h] == NULL)
            return NULL;
        else if (symaddr[h] == addr)
            return symbols[h];
        if (++h == MAX_SYM)
            h = 0;
    }
    return NULL;
}
//-------------------------------------------------------------------
void load_syms(char *name)
{
    FILE *fin;
    char buf[1024], *p, where[32], sym[128];
    unsigned int adr;
    int i;

    fin = fopen(name, "r");
    if (!fin) {
        fprintf(stderr,"Cannot open %s!\n",name);
        exit(1);
    }
    p = fgets(buf, sizeof(buf), fin);
    while (p) {
        while (*p && isspace(*p)) p++;
        i = 0;
        while (*p && !isspace(*p))
            sym[i++] = *p++;
        sym[i] = 0;
        while (*p && isspace(*p)) p++;
        i = 0;
        while (*p && !isspace(*p))
            where[i++] = *p++;
        where[i] = 0;

        if (*where && *sym) {
            adr = strtol(where, (char**)NULL, 16); 
            //fprintf(stderr,"adding %08x %s\n", adr, sym);
            add_sym(adr, sym);
        }
        p = fgets(buf, sizeof(buf), fin);
    }
    fclose(fin);
}
//-------------------------------------------------------------------
void dump_registers(void) {
    int i;

    fprintf(stderr, "Registers:\n");
    for (i = 0; i < 16; i++) {
        switch (i) {
            case 13: fprintf(stderr, "sp:  "); break;
            case 14: fprintf(stderr, "lr:  "); break;
            case 15: fprintf(stderr, "pc:  "); break;
            default: fprintf(stderr, "r%d: %s", i, i < 10 ? " " : ""); break;
        }
        fprintf(stderr, "%08x\n", reg_norm[i]);
    }   
}       
//-------------------------------------------------------------------
void dump_counters ( void )
{
    printf("\n\n");
    printf("instructions %lu\n",instructions);
    printf("fetches      %lu\n",fetches);
    printf("reads        %lu\n",reads);
    printf("writes       %lu\n",writes);
    printf("memcycles    %lu\n",fetches+reads+writes);
    printf("systick_ints %lu\n",systick_ints);
}
//-------------------------------------------------------------------
unsigned int fetch16 ( unsigned int addr )
{
    unsigned int data;

    fetches++;

if(DBUGFETCH) fprintf(stderr,"fetch16(0x%08X)=",addr);
if(DBUG) fprintf(stderr,"fetch16(0x%08X)=",addr);
    switch(addr&0xF0000000)
    {
        case 0x00000000: //ROM
            addr&=ROMADDMASK;

//if(addr<0x50)
//{
//    fprintf(stderr,"fetch16(0x%08X), abort\n",addr);
//    exit(1);
//}

            addr>>=1;
            data=rom[addr];
if(DBUGFETCH) fprintf(stderr,"0x%04X\n",data);
if(DBUG) fprintf(stderr,"0x%04X\n",data);
            return(data);
        case RAM_START: //RAM
            addr&=RAMADDMASK;
            addr>>=1;
            data=ram[addr];
if(DBUGFETCH) fprintf(stderr,"0x%04X\n",data);
if(DBUG) fprintf(stderr,"0x%04X\n",data);
            return(data);
    }
    fprintf(stderr,"fetch16(0x%08X), abort pc = 0x%04X\n",addr,read_register(15));
    exit(1);
}
//-------------------------------------------------------------------
unsigned int fetch32 ( unsigned int addr )
{
    unsigned int data;

if(DBUGFETCH) fprintf(stderr,"fetch32(0x%08X)=",addr);
if(DBUG) fprintf(stderr,"fetch32(0x%08X)=",addr);
    switch(addr&0xF0000000)
    {
        case 0x00000000: //ROM
            if(addr<0x50)
            {
                data=read32(addr);
if(DBUGFETCH) fprintf(stderr,"0x%08X\n",data);
if(DBUG) fprintf(stderr,"0x%08X\n",data);
                if(addr==0x00000000) return(data);
                if(addr==0x00000004) return(data);
                if(addr==0x0000003C) return(data);
                fprintf(stderr,"fetch32(0x%08X), abort pc = 0x%04X\n",addr,read_register(15));
                exit(1);
            }
        case RAM_START: //RAM
            //data=fetch16(addr+0);
            //data|=((unsigned int)fetch16(addr+2))<<16;
            data=read32(addr);
if(DBUGFETCH) fprintf(stderr,"0x%08X\n",data);
if(DBUG) fprintf(stderr,"0x%08X\n",data);
            return(data);
    }
    fprintf(stderr,"fetch32(0x%08X), abort pc 0x%04X\n",addr,read_register(15));
    exit(1);
}
//-------------------------------------------------------------------
void write16 ( unsigned int addr, unsigned int data )
{

    writes++;


if(DBUG) fprintf(stderr,"write16(0x%08X,0x%04X)\n",addr,data);
    switch(addr&0xF0000000)
    {
        case RAM_START: //RAM
if(DBUGRAM) fprintf(stderr,"write16(0x%08X,0x%04X)\n",addr,data);
            addr&=RAMADDMASK;
            addr>>=1;
            ram[addr]=data&0xFFFF;
            return;
    }
    fprintf(stderr,"write16(0x%08X,0x%04X), abort pc 0x%04X\n",addr,data,read_register(15));
    exit(1);
}
//-------------------------------------------------------------------
void write32 ( unsigned int addr, unsigned int data )
{
if(DBUG) fprintf(stderr,"write32(0x%08X,0x%08X)\n",addr,data);
    switch(addr&0xF0000000)
    {
        case 0xF0000000: //halt
            dump_counters();
            exit(0);
        case PERIPH_START: //periph
            if (0xE2000000 <= addr && addr < 0xE2F00000) /* frame buffer */
            {
                frame_buffer[(addr - 0xE2000000) >> 2] = data;
            }
            if (0xE3004000 <= addr && addr < 0xE3004200) /* disk buffer */
            {
                disk_buffer[(addr - 0xE3004000) >> 2] = data;
            }
            switch(addr)
            {
                case PERIPH_START + 0:
if(DISS) fprintf(stderr,"uart: [");
                    write(write_fd, &data, 1);
if(DISS) fprintf(stderr,"]\n");
                    fflush(stdout);
                    break;

                case 0xE2F00000: /* frame buffer port */
                {
                    fb_port = data;
                    break;
                }
                case 0xE3000000: /* disk offset low */
                {
                    disk_offset_low = data;
                    break;
                }
                case 0xE3000008: /* disk offset high */
                {
                    disk_offset_high = data;
                    break;
                }
                case 0xE3000010: /* select IDE ID */
                {
                    disk_ide_id = data;
                    break;
                }
                case 0xE3000020: /* start disk operation */
                {
                    handle_disk (data);
                    break;
                }
                case 0xE000E010:
                {
                    unsigned int old;

                    old=systick_ctrl;
                    systick_ctrl = data&0x00010007;
                    if(((old&1)==0)&&(systick_ctrl&1))
                    {
                        //timer started, load count
                        systick_count=systick_reload;
                    }
                    break;
                }
                case 0xE000E014:
                {
                    systick_reload=data&0x00FFFFFF;
                    break;
                }
                case 0xE000E018:
                {
                    systick_count=data&0x00FFFFFF;
                    break;
                }
                case 0xE000E01C:
                {
                    systick_calibrate=data&0x00FFFFFF;
                    break;
                }
            }
            return;
        case 0xD0000000: //debug
            switch(addr&0xFF)
            {
                case 0x00:
                {
                    fprintf(stderr,"[0x%08X][0x%08X] 0x%08X\n",read_register(14),addr,data);
                    return;
                }
                case 0x10:
                {
                    printf("0x%08X ",data);
                    return;
                }
                case 0x20:
                {
                    printf("0x%08X\n",data);
                    return;
                }
            }
        case 0x00000000:
        case RAM_START: //RAM
if(DBUGRAMW) fprintf(stderr,"write32(0x%08X,0x%08X)\n",addr,data);
            write16(addr+0,(data>> 0)&0xFFFF);
            write16(addr+2,(data>>16)&0xFFFF);
            return;
    }
    fprintf(stderr,"write32(0x%08X,0x%08X), abort pc 0x%04X\n",addr,data,read_register(15));
    exit(1);
}
//-----------------------------------------------------------------
unsigned int read16 ( unsigned int addr )
{
    unsigned int data;

    reads++;

if(DBUG) fprintf(stderr,"read16(0x%08X)=",addr);
    switch(addr&0xF0000000)
    {
        case 0x00000000: //ROM
            addr&=ROMADDMASK;
            addr>>=1;
            data=rom[addr];
if(DBUG) fprintf(stderr,"0x%04X\n",data);
            return(data);
        case RAM_START: //RAM
if(DBUGRAM) fprintf(stderr,"read16(0x%08X)=",addr);
            addr&=RAMADDMASK;
            addr>>=1;
            data=ram[addr];
if(DBUG) fprintf(stderr,"0x%04X\n",data);
if(DBUGRAM) fprintf(stderr,"0x%04X\n",data);
            return(data);
    }
    fprintf(stderr,"read16(0x%08X), abort pc 0x%04X\n",addr,read_register(15));
    exit(1);
}
//-------------------------------------------------------------------
unsigned int read32 ( unsigned int addr )
{
    unsigned int data;

if(DBUG) fprintf(stderr,"read32(0x%08X)=",addr);
    switch(addr&0xF0000000)
    {
        case 0x00000000: //ROM
        case RAM_START: //RAM
if(DBUGRAMW) fprintf(stderr,"read32(0x%08X)=",addr);
            data =read16(addr+0);
            data|=((unsigned int)read16(addr+2))<<16;
if(DBUG) fprintf(stderr,"0x%08X\n",data);
if(DBUGRAMW) fprintf(stderr,"0x%08X\n",data);
            return(data);
        case PERIPH_START:
        {
            if (0xE2000000 <= addr && addr < 0xE2F00000) /* frame buffer */
            {
                return frame_buffer[(addr - 0xE2000000) >> 2];
            }
            if (0xE3004000 <= addr && addr < 0xE3004200) /* disk buffer */
            {
                return disk_buffer[(addr - 0xE3004000) >> 2];
            }
            switch(addr)
            {
                case PERIPH_START + 4:  /* read next char */
                {
if (DBUGUART) printf("uart: [%lu %lu ", input_read_ptr, input_write_ptr);
                    if (input_read_ptr != input_write_ptr) {
                        data = input_buffer[input_read_ptr++];
                        if (input_read_ptr == MAX_INPUT) input_read_ptr = 0;
                    }
if (DBUGUART) printf("%c]\n", data);
if (DBUG) fprintf(stderr, "%08x\n", data);
                    return (data);
                }
                case PERIPH_START + 8:  /* -1 if char available */
                {
                    if (input_read_ptr != input_write_ptr) {
                        data = -1;
                    } else {
                        data = 0;
                    }
if (DBUG) fprintf(stderr, "%08x\n", data);
                    return (data);
                }
                case PERIPH_START + 12: /* read a single char */
                {
                    read(read_fd, &data, 1);
if (DBUG) fprintf(stderr, "%08x\n", data);
                    return (data);
                }
                case PERIPH_START + 16: /* event type */
                {
                    if (input_read_ptr != input_write_ptr)
                        data = input_event[input_read_ptr].typ;
                    return (data);
                }
                case PERIPH_START + 20: /* event X coord. */
                {
                    if (input_read_ptr != input_write_ptr)
                        data = input_event[input_read_ptr].X;
                    return (data);
                }
                case PERIPH_START + 24: /* event Y coord. */
                {
                    if (input_read_ptr != input_write_ptr)
                        data = input_event[input_read_ptr].Y;
                    return (data);
                }
                case 0xE2F00000: /* frame buffer port */
                {
                    return fb_port;
                }
                case 0xE2F00010: /* frame buffer data */
                {
                    switch (fb_port) {
                    case 1: data = fb_width; break;
                    case 2: data = fb_height; break;
                    }
                    return(data);
                }
                case 0xE3000030: /* disk status */
                {
                    data = disk_status;
                    return(data);
                }
                case 0xE000E010:
                {
                    data = systick_ctrl;
                    systick_ctrl&=(~0x00010000);
                    return(data);
                }
                case 0xE000E014:
                {
                    data=systick_reload;
                    return(data);
                }
                case 0xE000E018:
                {
                    data=systick_count;
                    return(data);
                }
                case 0xE000E01C:
                {
                    data=systick_calibrate;
                    return(data);
                }
                case 0xE000ED00:
                {
                    data=cpuid;
if (DBUG) fprintf(stderr, "%08x\n", data);
                    return(data);
                }
            }
        }
    }
    fprintf(stderr,"read32(0x%08X), abort pc 0x%04X\n",addr,read_register(15));
    exit(1);
}
//-------------------------------------------------------------------
unsigned int read_register ( unsigned int reg )
{
    unsigned int data;

    reg&=0xF;
if(DBUG) fprintf(stderr,"read_register(%u)=",reg);
if(DBUGREG) fprintf(stderr,"read_register(%u)=",reg);
    data=reg_norm[reg];
    if(reg==15)
    {
        if(data&1)
        {
            fprintf(stderr,"pc has lsbit set 0x%08X\n",data);
        }
        data&=~1;
    }
if(DBUG) fprintf(stderr,"0x%08X\n",data);
if(DBUGREG) fprintf(stderr,"0x%08X\n",data);
    return(data);
}
//-------------------------------------------------------------------
void write_register ( unsigned int reg, unsigned int data )
{
    reg&=0xF;
if(DBUG) fprintf(stderr,"write_register(%u,0x%08X)\n",reg,data);
if(DBUGREG) fprintf(stderr,"write_register(%u,0x%08X)\n",reg,data);
    if(reg==15) data&=~1;
    reg_norm[reg]=data;

if(output_vcd)
{
    unsigned int vv;
    fprintf(fpvcd,"b");
    for(vv=0x80000000;vv;vv>>=1)
    {
        if(vv&data) fprintf(fpvcd,"1"); else fprintf(fpvcd,"0");
    }
    fprintf(fpvcd," r%u\n",reg);
}

}
//-------------------------------------------------------------------
void do_zflag ( unsigned int x )
{
    if(x==0) cpsr|=CPSR_Z; else cpsr&=~CPSR_Z;
}
//-------------------------------------------------------------------
void do_nflag ( unsigned int x )
{
    if(x&0x80000000) cpsr|=CPSR_N; else cpsr&=~CPSR_N;
}
//-------------------------------------------------------------------
void do_cflag ( unsigned int a, unsigned int b, unsigned int c )
{
    unsigned int rc;

    cpsr&=~CPSR_C;
    rc=(a&0x7FFFFFFF)+(b&0x7FFFFFFF)+c; //carry in
    rc = (rc>>31)+(a>>31)+(b>>31);  //carry out
    if(rc&2) cpsr|=CPSR_C;
}
//-------------------------------------------------------------------
void do_vflag ( unsigned int a, unsigned int b, unsigned int c )
{
    unsigned int rc;
    unsigned int rd;

    cpsr&=~CPSR_V;
    rc=(a&0x7FFFFFFF)+(b&0x7FFFFFFF)+c; //carry in
    rc>>=31; //carry in in lsbit
    rd=(rc&1)+((a>>31)&1)+((b>>31)&1); //carry out
    rd>>=1; //carry out in lsbit
    rc=(rc^rd)&1; //if carry in != carry out then signed overflow
    if(rc) cpsr|=CPSR_V;
}
//-------------------------------------------------------------------
void do_cflag_bit ( unsigned int x )
{
   if(x) cpsr|=CPSR_C; else cpsr&=~CPSR_C;
}
//-------------------------------------------------------------------
void do_vflag_bit ( unsigned int x )
{
   if(x) cpsr|=CPSR_V; else cpsr&=~CPSR_V;
}
//-------------------------------------------------------------------
void wait_for_input(void)
{
    fd_set s_rd;

    if (input_read_ptr == input_write_ptr) {
        FD_ZERO(&s_rd);
        FD_SET(read_fd, &s_rd);
        select(read_fd + 1, &s_rd, NULL, NULL, NULL);
    }
}
//-------------------------------------------------------------------
void handle_disk(unsigned int wr)
{
    ssize_t rc;
    int fd;
    off_t off;

    disk_status = 0;

    if (disk_ide_id >= disk_count)
        return;
    fd = disk_fd[disk_ide_id];

    off = ((off_t)(disk_offset_high) << 32) + disk_offset_low;
    if (off >= disk_size[disk_ide_id])
        return;
    off = lseek(fd, off, SEEK_SET);
    if (-1 == off)
        return;

    if (wr)
        rc = write(fd, disk_buffer, sizeof(disk_buffer));
    else
        rc = read(fd, disk_buffer, sizeof(disk_buffer));
    disk_status = (rc < 0 || rc != sizeof(disk_buffer)) ? 0 : 1;
}
//-------------------------------------------------------------------
int handle_bkpt(unsigned int bp, unsigned int arg)
{
	int r = 1;
	FILE *f;
	int s, e, n;
	unsigned int sp;

	sp = read_register(13);
	switch (arg) {
		case 0x18:
			fprintf(stderr, "Exiting.\n");
			break;
		case 0x80:
			s = read32(sp + 8);
			e = read32(sp + 4);
			fprintf(stderr, "Dumping from %08x to %08x into %s...\n",
					s, e, output_file_name);
			f = fopen(output_file_name, "wb");
			while (s != e) {
				n = read32(s);
				fwrite(&n, 2, 1, f);
				s += 2;
			}
			fclose(f);
			write_register(13, read_register(13) + 8);
			r = 0;
			break;
		case 0x81:
			dump_registers();
			r = 0;
			break;
		case 0x82:
			wait_for_input();
			r = 0;
			break;
		default:
			fprintf(stderr, "bkpt 0x%02X %08x\n", bp, arg);
			break;
	}
	write_register(13, read_register(13) + 4);
	return r;
}


//-------------------------------------------------------------------
//-------------------------------------------------------------------
//-------------------------------------------------------------------
int execute ( void )
{
    unsigned int pc;
    unsigned int sp;
    unsigned int inst;

    unsigned int ra,rb,rc;
    unsigned int rm,rd,rn,rs;
    unsigned int op;

    char *sym = NULL;

//if(fetches>400000) return(1);

    pc=read_register(15);

    if(handler_mode)
    {
        if((pc&0xF0000000)==0xF0000000)
        {
            unsigned int sp;

            handler_mode = 0;
//fprintf(stderr,"--leaving handler\n");
            sp=read_register(13);
            write_register(0,read32(sp)); sp+=4;
            write_register(1,read32(sp)); sp+=4;
            write_register(2,read32(sp)); sp+=4;
            write_register(3,read32(sp)); sp+=4;
            write_register(12,read32(sp)); sp+=4;
            write_register(14,read32(sp)); sp+=4;
            pc=read32(sp); sp+=4;
            cpsr=read32(sp); sp+=4;
            write_register(13,sp);
        }
    }
    if(systick_ctrl&1)
    {
        if(systick_count)
        {
            systick_count--;
        }
        else
        {
            systick_count=systick_reload;
            systick_ctrl|=0x00010000;
        }
    }

    if((systick_ctrl&3)==3)
    {
        if(systick_ctrl&0x00010000)
        {
            if(handler_mode==0)
            {
                unsigned int sp;

                systick_ints++;
//fprintf(stderr,"--- enter systick handler\n");
                sp=read_register(13);
                sp-=4; write32(sp,cpsr);
                sp-=4; write32(sp,pc);
                sp-=4; write32(sp,read_register(14));
                sp-=4; write32(sp,read_register(12));
                sp-=4; write32(sp,read_register(3));
                sp-=4; write32(sp,read_register(2));
                sp-=4; write32(sp,read_register(1));
                sp-=4; write32(sp,read_register(0));
                write_register(13,sp);
                pc=fetch32(0x0000003C); //systick vector
                pc+=2;
                //write_register(14,0xFFFFFF00);
                write_register(14,0xFFFFFFF9);

                handler_mode=1;
            }
        }
    }




    inst=fetch16(pc-2);
    pc+=2;
    write_register(15,pc);
if(DISS) {
        fprintf(stderr,"--- 0x%08X: 0x%04X ",(pc-4),inst);
        sym = get_sym(pc-4);
    }

if(output_vcd)
{
    unsigned int vv;
    fprintf(fpvcd,"b");
    for(vv=0x8000;vv;vv>>=1)
    {
        if(vv&inst) fprintf(fpvcd,"1"); else fprintf(fpvcd,"0");
    }
    fprintf(fpvcd," inst\n");
}



    instructions++;

    //ADC
    if((inst&0xFFC0)==0x4140)
    {
        rd=(inst>>0)&0x07;
        rm=(inst>>3)&0x07;
if(DISS) fprintf(stderr,"adc   r%u,r%u",rd,rm);
        ra=read_register(rd);
        rb=read_register(rm);
        rc=ra+rb;
        if(cpsr&CPSR_C) rc++;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        if(cpsr&CPSR_C) { do_cflag(ra,rb,1); do_vflag(ra,rb,1); }
        else            { do_cflag(ra,rb,0); do_vflag(ra,rb,0); }
        goto Return;
    }

    //ADD(1) small immediate two registers
    if((inst&0xFE00)==0x1C00)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rb=(inst>>6)&0x7;
        if(rb)
        {
if(DISS) fprintf(stderr,"adds  r%u,r%u,#0x%X",rd,rn,rb);
            ra=read_register(rn);
            rc=ra+rb;
//fprintf(stderr,"0x%08X = 0x%08X + 0x%08X\n",rc,ra,rb);
            write_register(rd,rc);
            do_nflag(rc);
            do_zflag(rc);
            do_cflag(ra,rb,0);
            do_vflag(ra,rb,0);
            goto Return;
        }
        else
        {
            //this is a mov
        }
    }

    //ADD(2) big immediate one register
    if((inst&0xF800)==0x3000)
    {
        rb=(inst>>0)&0xFF;
        rd=(inst>>8)&0x7;
if(DISS) fprintf(stderr,"adds  r%u,#0x%02X",rd,rb);
        ra=read_register(rd);
        rc=ra+rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,rb,0);
        do_vflag(ra,rb,0);
        goto Return;
    }

    //ADD(3) three registers
    if((inst&0xFE00)==0x1800)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(DISS) fprintf(stderr,"adds  r%u,r%u,r%u",rd,rn,rm);
        ra=read_register(rn);
        rb=read_register(rm);
        rc=ra+rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,rb,0);
        do_vflag(ra,rb,0);
        goto Return;
    }

    //ADD(4) two registers one or both high no flags
    if((inst&0xFF00)==0x4400)
    {
        if((inst>>6)&3)
        {
            //UNPREDICTABLE
        }
        rd=(inst>>0)&0x7;
        rd|=(inst>>4)&0x8;
        rm=(inst>>3)&0xF;
if(DISS) fprintf(stderr,"add   r%u,r%u",rd,rm);
        ra=read_register(rd);
        rb=read_register(rm);
        rc=ra+rb;
        if(rd==15)
        {
            if((rc&1)==0)
            {
                fprintf(stderr,"add pc,... produced an arm address 0x%08X 0x%08X\n",pc,rc);
                exit(1);
            }
            rc&=~1; //write_register may do this as well
            rc+=2; //The program counter is special
        }
//fprintf(stderr,"0x%08X = 0x%08X + 0x%08X\n",rc,ra,rb);
        write_register(rd,rc);
        goto Return;
    }

    //ADD(5) rd = pc plus immediate
    if((inst&0xF800)==0xA000)
    {
        rb=(inst>>0)&0xFF;
        rd=(inst>>8)&0x7;
        rb<<=2;
if(DISS) fprintf(stderr,"add   r%u,PC,#0x%02X",rd,rb);
        ra=read_register(15);
        rc=(ra&(~3))+rb;
        write_register(rd,rc);
        goto Return;
    }

    //ADD(6) rd = sp plus immediate
    if((inst&0xF800)==0xA800)
    {
        rb=(inst>>0)&0xFF;
        rd=(inst>>8)&0x7;
        rb<<=2;
if(DISS) fprintf(stderr,"add   r%u,SP,#0x%02X",rd,rb);
        ra=read_register(13);
        rc=ra+rb;
        write_register(rd,rc);
        goto Return;
    }

    //ADD(7) sp plus immediate
    if((inst&0xFF80)==0xB000)
    {
        rb=(inst>>0)&0x7F;
        rb<<=2;
if(DISS) fprintf(stderr,"add   SP,#0x%02X",rb);
        ra=read_register(13);
        rc=ra+rb;
        write_register(13,rc);
        goto Return;
    }

    //AND
    if((inst&0xFFC0)==0x4000)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"ands  r%u,r%u",rd,rm);
        ra=read_register(rd);
        rb=read_register(rm);
        rc=ra&rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        goto Return;
    }

    //ASR(1) two register immediate
    if((inst&0xF800)==0x1000)
    {
        rd=(inst>>0)&0x07;
        rm=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
if(DISS) fprintf(stderr,"asrs  r%u,r%u,#0x%X",rd,rm,rb);
        rc=read_register(rm);
        if(rb==0)
        {
            if(rc&0x80000000)
            {
                do_cflag_bit(1);
                rc=MINUS_ONE;
            }
            else
            {
                do_cflag_bit(0);
                rc=0;
            }
        }
        else
        {
            do_cflag_bit(rc&(1<<(rb-1)));
            ra=rc&0x80000000;
            rc>>=rb;
            if(ra) //asr, sign is shifted in
            {
                rc|=(MINUS_ONE)<<(32-rb);
            }
        }
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        goto Return;
    }

    //ASR(2) two register
    if((inst&0xFFC0)==0x4100)
    {
        rd=(inst>>0)&0x07;
        rs=(inst>>3)&0x07;
if(DISS) fprintf(stderr,"asrs  r%u,r%u",rd,rs);
        rc=read_register(rd);
        rb=read_register(rs);
        rb&=0xFF;
        if(rb==0)
        {
        }
        else if(rb<32)
        {
            do_cflag_bit(rc&(1<<(rb-1)));
            ra=rc&0x80000000;
            rc>>=rb;
            if(ra) //asr, sign is shifted in
            {
                rc|=(MINUS_ONE)<<(32-rb);
            }
        }
        else
        {
            if(rc&0x80000000)
            {
                do_cflag_bit(1);
                rc=(MINUS_ONE);
            }
            else
            {
                do_cflag_bit(0);
                rc=0;
            }
        }
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        goto Return;
    }

    //B(1) conditional branch
    if((inst&0xF000)==0xD000)
    {
        rb=(inst>>0)&0xFF;
        if(rb&0x80) rb|=(MINUS_ONE)<<8;
        op=(inst>>8)&0xF;
        rb<<=1;
        rb+=pc;
        rb+=2;
        switch(op)
        {
            case 0x0: //b eq  z set
if(DISS) fprintf(stderr,"beq   0x%08X",rb-3);
                if(cpsr&CPSR_Z)
                {
                    write_register(15,rb);
                }
                goto Return;

            case 0x1: //b ne  z clear
if(DISS) fprintf(stderr,"bne   0x%08X",rb-3);
                if(!(cpsr&CPSR_Z))
                {
                    write_register(15,rb);
                }
                goto Return;

            case 0x2: //b cs c set
if(DISS) fprintf(stderr,"bcs   0x%08X",rb-3);
                if(cpsr&CPSR_C)
                {
                    write_register(15,rb);
                }
                goto Return;

            case 0x3: //b cc c clear
if(DISS) fprintf(stderr,"bcc   0x%08X",rb-3);
                if(!(cpsr&CPSR_C))
                {
                    write_register(15,rb);
                }
                goto Return;

            case 0x4: //b mi n set
if(DISS) fprintf(stderr,"bmi   0x%08X",rb-3);
                if(cpsr&CPSR_N)
                {
                    write_register(15,rb);
                }
                goto Return;

            case 0x5: //b pl n clear
if(DISS) fprintf(stderr,"bpl   0x%08X",rb-3);
                if(!(cpsr&CPSR_N))
                {
                    write_register(15,rb);
                }
                goto Return;


            case 0x6: //b vs v set
if(DISS) fprintf(stderr,"bvs   0x%08X",rb-3);
                if(cpsr&CPSR_V)
                {
                    write_register(15,rb);
                }
                goto Return;

            case 0x7: //b vc v clear
if(DISS) fprintf(stderr,"bvc   0x%08X",rb-3);
                if(!(cpsr&CPSR_V))
                {
                    write_register(15,rb);
                }
                goto Return;


            case 0x8: //b hi c set z clear
if(DISS) fprintf(stderr,"bhi   0x%08X",rb-3);
                if((cpsr&CPSR_C)&&(!(cpsr&CPSR_Z)))
                {
                    write_register(15,rb);
                }
                goto Return;

            case 0x9: //b ls c clear or z set
if(DISS) fprintf(stderr,"bls   0x%08X",rb-3);
                if((cpsr&CPSR_Z)||(!(cpsr&CPSR_C)))
                {
                    write_register(15,rb);
                }
                goto Return;

            case 0xA: //b ge N == V
if(DISS) fprintf(stderr,"bge   0x%08X",rb-3);
                ra=0;
                if(  (cpsr&CPSR_N) &&  (cpsr&CPSR_V) ) ra++;
                if((!(cpsr&CPSR_N))&&(!(cpsr&CPSR_V))) ra++;
                if(ra)
                {
                    write_register(15,rb);
                }
                goto Return;

            case 0xB: //b lt N != V
if(DISS) fprintf(stderr,"blt   0x%08X",rb-3);
                ra=0;
                if((!(cpsr&CPSR_N))&&(cpsr&CPSR_V)) ra++;
                if((!(cpsr&CPSR_V))&&(cpsr&CPSR_N)) ra++;
                if(ra)
                {
                    write_register(15,rb);
                }
                goto Return;

            case 0xC: //b gt Z==0 and N == V
if(DISS) fprintf(stderr,"bgt   0x%08X",rb-3);
                ra=0;
                if(  (cpsr&CPSR_N) &&  (cpsr&CPSR_V) ) ra++;
                if((!(cpsr&CPSR_N))&&(!(cpsr&CPSR_V))) ra++;
                if(cpsr&CPSR_Z) ra=0;
                if(ra)
                {
                    write_register(15,rb);
                }
                goto Return;

            case 0xD: //b le Z==1 or N != V
if(DISS) fprintf(stderr,"ble   0x%08X",rb-3);
                ra=0;
                if((!(cpsr&CPSR_N))&&(cpsr&CPSR_V)) ra++;
                if((!(cpsr&CPSR_V))&&(cpsr&CPSR_N)) ra++;
                if(cpsr&CPSR_Z) ra++;
                if(ra)
                {
                    write_register(15,rb);
                }
                goto Return;

            case 0xE:
                //undefined instruction
                break;
            case 0xF:
                //swi
                break;
        }
    }

    //B(2) unconditional branch
    if((inst&0xF800)==0xE000)
    {
        rb=(inst>>0)&0x7FF;
        if(rb&(1<<10)) rb|=(MINUS_ONE)<<11;
        rb<<=1;
        rb+=pc;
        rb+=2;
if(DISS) fprintf(stderr,"b     0x%08X",rb-4);
        write_register(15,rb);
        goto Return;
    }

    //BIC
    if((inst&0xFFC0)==0x4380)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"bics  r%u,r%u",rd,rm);
        ra=read_register(rd);
        rb=read_register(rm);
        rc=ra&(~rb);
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        goto Return;
    }

    //BKPT
    if((inst&0xFF00)==0xBE00)
    {
        rb=(inst>>0)&0xFF;
        // fprintf(stderr,"bkpt 0x%02X\n",rb);
        // return(1);
        return(handle_bkpt(rb, read32(read_register(13))));
    }

    //BL/BLX(1)
    if((inst&0xE000)==0xE000) //BL,BLX
    {
        if((inst&0x1800)==0x1000) //H=b10
        {
if(DISS) fprintf(stderr,"\n");
            rb=inst&((1<<11)-1);
            if(rb&1<<10) rb|=(~((1<<11)-1)); //sign extend
            rb<<=12;
            rb+=pc;
            write_register(14,rb);
            return(0);
        }
        else
        if((inst&0x1800)==0x1800) //H=b11
        {
            //branch to thumb
            rb=read_register(14);
            rb+=(inst&((1<<11)-1))<<1;;
            rb+=2;

if(DISS) fprintf(stderr,"bl    0x%08X",rb-2);
            write_register(14,(pc-2)|1);
            write_register(15,rb);
            goto Return;
        }
        else
        if((inst&0x1800)==0x0800) //H=b01
        {
            //fprintf(stderr,"cannot branch to arm 0x%08X 0x%04X\n",pc,inst);
            //return(1);
            //branch to thumb
            rb=read_register(14);
            rb+=(inst&((1<<11)-1))<<1;;
            rb&=0xFFFFFFFC;
            rb+=2;

printf("hello\n");

if(DISS) fprintf(stderr,"bl    0x%08X",rb-3);
            write_register(14,(pc-2)|1);
            write_register(15,rb);
            goto Return;



        }
    }

    //BLX(2)
    if((inst&0xFF87)==0x4780)
    {
        rm=(inst>>3)&0xF;
if(DISS) fprintf(stderr,"blx   r%u",rm);
        rc=read_register(rm);
//fprintf(stderr,"blx r%u 0x%X 0x%X\n",rm,rc,pc);
        rc+=2;
        if(rc&1)
        {
            write_register(14,(pc-2)|1);
            rc&=~1;
            write_register(15,rc);
            goto Return;
        }
        else
        {
            fprintf(stderr,"\ncannot branch to arm 0x%08X 0x%04X\n",pc,inst);
            return(1);
        }
    }

    //BX
    if((inst&0xFF87)==0x4700)
    {
        rm=(inst>>3)&0xF;
if(DISS) fprintf(stderr,"bx    r%u",rm);
        rc=read_register(rm);
        rc+=2;
//fprintf(stderr,"bx r%u 0x%X 0x%X\n",rm,rc,pc);
        if(rc&1)
        {
            rc&=~1;
            write_register(15,rc);
            goto Return;
        }
        else
        {
            fprintf(stderr,"\ncannot branch to arm 0x%08X 0x%04X\n",pc,inst);
            return(1);
        }
    }

    //CMN
    if((inst&0xFFC0)==0x42C0)
    {
        rn=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"cmns  r%u,r%u",rn,rm);
        ra=read_register(rn);
        rb=read_register(rm);
        rc=ra+rb;
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,rb,0);
        do_vflag(ra,rb,0);
        goto Return;
    }

    //CMP(1) compare immediate
    if((inst&0xF800)==0x2800)
    {
        rb=(inst>>0)&0xFF;
        rn=(inst>>8)&0x07;
if(DISS) fprintf(stderr,"cmp   r%u,#0x%02X",rn,rb);
        ra=read_register(rn);
        rc=ra-rb;
//fprintf(stderr,"0x%08X 0x%08X\n",ra,rb);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,~rb,1);
        do_vflag(ra,~rb,1);
        goto Return;
    }

    //CMP(2) compare register
    if((inst&0xFFC0)==0x4280)
    {
        rn=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"cmps  r%u,r%u",rn,rm);
        ra=read_register(rn);
        rb=read_register(rm);
        rc=ra-rb;
//fprintf(stderr,"0x%08X 0x%08X\n",ra,rb);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,~rb,1);
        do_vflag(ra,~rb,1);
        goto Return;
    }

    //CMP(3) compare high register
    if((inst&0xFF00)==0x4500)
    {
        if(((inst>>6)&3)==0x0)
        {
            //UNPREDICTABLE
        }
        rn=(inst>>0)&0x7;
        rn|=(inst>>4)&0x8;
        if(rn==0xF)
        {
            //UNPREDICTABLE
        }
        rm=(inst>>3)&0xF;
if(DISS) fprintf(stderr,"cmps  r%u,r%u",rn,rm);
        ra=read_register(rn);
        rb=read_register(rm);
        rc=ra-rb;
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,~rb,1);
        do_vflag(ra,~rb,1);
        goto Return;
    }

    //CPS
    if((inst&0xFFE8)==0xB660)
    {
if(DISS) fprintf(stderr,"cps   TODO\n");
        return(1);
    }

    //CPY copy high register
    if((inst&0xFFC0)==0x4600)
    {
        //same as mov except you can use both low registers
        //going to let mov handle high registers
        rd=(inst>>0)&0x7; //mov handles the high registers
        rm=(inst>>3)&0x7; //mov handles the high registers
if(DISS) fprintf(stderr,"cpy   r%u,r%u",rd,rm);
        rc=read_register(rm);
        //if(rd==15) //mov handles the high registers like r15
        //{
            //rc&=~1;
            //rc+=2; //The program counter is special
        //}
        write_register(rd,rc);
        goto Return;
    }

    //EOR
    if((inst&0xFFC0)==0x4040)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"eors  r%u,r%u",rd,rm);
        ra=read_register(rd);
        rb=read_register(rm);
        rc=ra^rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        goto Return;
    }

    //LDMIA
    if((inst&0xF800)==0xC800)
    {
        rn=(inst>>8)&0x7;
if(DISS)
{
    fprintf(stderr,"ldmia  r%u!,{",rn);
    for(ra=0,rb=0x01,rc=0;rb;rb=(rb<<1)&0xFF,ra++)
    {
        if(inst&rb)
        {
            if(rc) fprintf(stderr,",");
            fprintf(stderr,"r%u",ra);
            rc++;
        }
    }
    fprintf(stderr,"}");
}
        sp=read_register(rn);
        for(ra=0,rb=0x01;rb;rb=(rb<<1)&0xFF,ra++)
        {
            if(inst&rb)
            {
                write_register(ra,read32(sp));
                sp+=4;
            }
        }
        //there is a write back exception.
        if((inst&(1<<rn))==0) write_register(rn,sp);
        goto Return;
    }

    //LDR(1) two register immediate
    if((inst&0xF800)==0x6800)
    {
        rd=(inst>>0)&0x07;
        rn=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
        rb<<=2;
if(DISS) fprintf(stderr,"ldr   r%u,[r%u,#0x%X]",rd,rn,rb);
        rb=read_register(rn)+rb;
        rc=read32(rb);
        write_register(rd,rc);
        goto Return;
    }

    //LDR(2) three register
    if((inst&0xFE00)==0x5800)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(DISS) fprintf(stderr,"ldr   r%u,[r%u,r%u]",rd,rn,rm);
        rb=read_register(rn)+read_register(rm);
        rc=read32(rb);
        write_register(rd,rc);
        goto Return;
    }

    //LDR(3)
    if((inst&0xF800)==0x4800)
    {
        rb=(inst>>0)&0xFF;
        rd=(inst>>8)&0x07;
        rb<<=2;
if(DISS) fprintf(stderr,"ldr   r%u,[PC+#0x%X] ",rd,rb);
        ra=read_register(15);
        ra&=~3;
        rb+=ra;
if(DISS) fprintf(stderr,";@ 0x%X",rb);
        rc=read32(rb);
        write_register(rd,rc);
        goto Return;
    }

    //LDR(4)
    if((inst&0xF800)==0x9800)
    {
        rb=(inst>>0)&0xFF;
        rd=(inst>>8)&0x07;
        rb<<=2;
if(DISS) fprintf(stderr,"ldr   r%u,[SP+#0x%X]",rd,rb);
        ra=read_register(13);
        //ra&=~3;
        rb+=ra;
        rc=read32(rb);
        write_register(rd,rc);
        goto Return;
    }

    //LDRB(1)
    if((inst&0xF800)==0x7800)
    {
        rd=(inst>>0)&0x07;
        rn=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
if(DISS) fprintf(stderr,"ldrb  r%u,[r%u,#0x%X]",rd,rn,rb);
        rb=read_register(rn)+rb;
        rc=read16(rb&(~1));
        if(rb&1)
        {
            rc>>=8;
        }
        else
        {
        }
        write_register(rd,rc&0xFF);
        goto Return;
    }

    //LDRB(2)
    if((inst&0xFE00)==0x5C00)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(DISS) fprintf(stderr,"ldrb  r%u,[r%u,r%u]",rd,rn,rm);
        rb=read_register(rn)+read_register(rm);
        rc=read16(rb&(~1));
        if(rb&1)
        {
            rc>>=8;
        }
        else
        {
        }
        write_register(rd,rc&0xFF);
        goto Return;
    }

    //LDRH(1)
    if((inst&0xF800)==0x8800)
    {
        rd=(inst>>0)&0x07;
        rn=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
        rb<<=1;
if(DISS) fprintf(stderr,"ldrh  r%u,[r%u,#0x%X]",rd,rn,rb);
        rb=read_register(rn)+rb;
        rc=read16(rb);
        write_register(rd,rc&0xFFFF);
        goto Return;
    }

    //LDRH(2)
    if((inst&0xFE00)==0x5A00)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(DISS) fprintf(stderr,"ldrh  r%u,[r%u,r%u]",rd,rn,rm);
        rb=read_register(rn)+read_register(rm);
        rc=read16(rb);
        write_register(rd,rc&0xFFFF);
        goto Return;
    }

    //LDRSB
    if((inst&0xFE00)==0x5600)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(DISS) fprintf(stderr,"ldrsb r%u,[r%u,r%u]",rd,rn,rm);
        rb=read_register(rn)+read_register(rm);
        rc=read16(rb&(~1));
        if(rb&1)
        {
            rc>>=8;
        }
        else
        {
        }
        rc&=0xFF;
        if(rc&0x80) rc|=((MINUS_ONE)<<8);
        write_register(rd,rc);
        goto Return;
    }

    //LDRSH
    if((inst&0xFE00)==0x5E00)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(DISS) fprintf(stderr,"ldrsh r%u,[r%u,r%u]",rd,rn,rm);
        rb=read_register(rn)+read_register(rm);
        rc=read16(rb);
        rc&=0xFFFF;
        if(rc&0x8000) rc|=((MINUS_ONE)<<16);
        write_register(rd,rc);
        goto Return;
    }

    //LSL(1)
    if((inst&0xF800)==0x0000)
    {
        rd=(inst>>0)&0x07;
        rm=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
if(DISS) fprintf(stderr,"lsls  r%u,r%u,#0x%X",rd,rm,rb);
        rc=read_register(rm);
        if(rb==0)
        {
            //if immed_5 == 0
            //C unnaffected
            //result not shifted
        }
        else
        {
            //else immed_5 > 0
            do_cflag_bit(rc&(1<<(32-rb)));
            rc<<=rb;
        }
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        goto Return;
    }

    //LSL(2) two register
    if((inst&0xFFC0)==0x4080)
    {
        rd=(inst>>0)&0x07;
        rs=(inst>>3)&0x07;
if(DISS) fprintf(stderr,"lsls  r%u,r%u",rd,rs);
        rc=read_register(rd);
        rb=read_register(rs);
        rb&=0xFF;
        if(rb==0)
        {
        }
        else if(rb<32)
        {
            do_cflag_bit(rc&(1<<(32-rb)));
            rc<<=rb;
        }
        else if(rb==32)
        {
            do_cflag_bit(rc&1);
            rc=0;
        }
        else
        {
            do_cflag_bit(0);
            rc=0;
        }
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        goto Return;
    }

    //LSR(1) two register immediate
    if((inst&0xF800)==0x0800)
    {
        rd=(inst>>0)&0x07;
        rm=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
if(DISS) fprintf(stderr,"lsrs  r%u,r%u,#0x%X",rd,rm,rb);
        rc=read_register(rm);
        if(rb==0)
        {
            do_cflag_bit(rc&0x80000000);
            rc=0;
        }
        else
        {
            do_cflag_bit(rc&(1<<(rb-1)));
            rc>>=rb;
        }
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        goto Return;
    }

    //LSR(2) two register
    if((inst&0xFFC0)==0x40C0)
    {
        rd=(inst>>0)&0x07;
        rs=(inst>>3)&0x07;
if(DISS) fprintf(stderr,"lsrs  r%u,r%u",rd,rs);
        rc=read_register(rd);
        rb=read_register(rs);
        rb&=0xFF;
        if(rb==0)
        {
        }
        else if(rb<32)
        {
            do_cflag_bit(rc&(1<<(rb-1)));
            rc>>=rb;
        }
        else if(rb==32)
        {
            do_cflag_bit(rc&0x80000000);
            rc=0;
        }
        else
        {
            do_cflag_bit(0);
            rc=0;
        }
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        goto Return;
    }

    //MOV(1) immediate
    if((inst&0xF800)==0x2000)
    {
        rb=(inst>>0)&0xFF;
        rd=(inst>>8)&0x07;
if(DISS) fprintf(stderr,"movs  r%u,#0x%02X",rd,rb);
        write_register(rd,rb);
        do_nflag(rb);
        do_zflag(rb);
        goto Return;
    }

    //MOV(2) two low registers
    if((inst&0xFFC0)==0x1C00)
    {
        rd=(inst>>0)&7;
        rn=(inst>>3)&7;
if(DISS) fprintf(stderr,"movs  r%u,r%u",rd,rn);
        rc=read_register(rn);
//fprintf(stderr,"0x%08X\n",rc);
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag_bit(0);
        do_vflag_bit(0);
        goto Return;
    }

    //MOV(3)
    if((inst&0xFF00)==0x4600)
    {
        rd=(inst>>0)&0x7;
        rd|=(inst>>4)&0x8;
        rm=(inst>>3)&0xF;
if(DISS) fprintf(stderr,"mov   r%u,r%u",rd,rm);
        rc=read_register(rm);
        if((rd==14)&&(rm==15))
        {
            //printf("mov lr,pc warning 0x%08X\n",pc-2);
            //rc|=1;
        }
        if(rd==15)
        {
            //if((rc&1)==0)
            //{
                //fprintf(stderr,"cpy or mov pc,... produced an ARM address 0x%08X 0x%08X\n",pc,rc);
                //exit(1);
            //}
            rc&=~1; //write_register may do this as well
            rc+=2; //The program counter is special
        }
        write_register(rd,rc);
        goto Return;
    }

    //MUL
    if((inst&0xFFC0)==0x4340)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"muls  r%u,r%u",rd,rm);
        ra=read_register(rd);
        rb=read_register(rm);
        rc=ra*rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        goto Return;
    }

    //MVN
    if((inst&0xFFC0)==0x43C0)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"mvns  r%u,r%u",rd,rm);
        ra=read_register(rm);
        rc=(~ra);
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        goto Return;
    }

    //NEG
    if((inst&0xFFC0)==0x4240)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"negs  r%u,r%u",rd,rm);
        ra=read_register(rm);
        rc=0-ra;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(0,~ra,1);
        do_vflag(0,~ra,1);
        goto Return;
    }

    //ORR
    if((inst&0xFFC0)==0x4300)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"orrs  r%u,r%u",rd,rm);
        ra=read_register(rd);
        rb=read_register(rm);
        rc=ra|rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        goto Return;
    }


    //POP
    if((inst&0xFE00)==0xBC00)
    {
if(DISS)
{
    fprintf(stderr,"pop   {");
    for(ra=0,rb=0x01,rc=0;rb;rb=(rb<<1)&0xFF,ra++)
    {
        if(inst&rb)
        {
            if(rc) fprintf(stderr,",");
            fprintf(stderr,"r%u",ra);
            rc++;
        }
    }
    if(inst&0x100)
    {
        if(rc) fprintf(stderr,",");
        fprintf(stderr,"pc");
    }
    fprintf(stderr,"}");
}

        sp=read_register(13);
        for(ra=0,rb=0x01;rb;rb=(rb<<1)&0xFF,ra++)
        {
            if(inst&rb)
            {
                write_register(ra,read32(sp));
                sp+=4;
            }
        }
        if(inst&0x100)
        {
            rc=read32(sp);
            if((rc&1)==0)
            {
                fprintf(stderr,"\npop {rc} with an ARM address pc 0x%08X popped 0x%08X",pc,rc);
                //exit(1);
                rc&=~1;
            }
            rc+=2;
            write_register(15,rc);
            sp+=4;
        }
        write_register(13,sp);
        goto Return;
    }

    //PUSH
    if((inst&0xFE00)==0xB400)
    {

if(DISS)
{
    fprintf(stderr,"push  {");
    for(ra=0,rb=0x01,rc=0;rb;rb=(rb<<1)&0xFF,ra++)
    {
        if(inst&rb)
        {
            if(rc) fprintf(stderr,",");
            fprintf(stderr,"r%u",ra);
            rc++;
        }
    }
    if(inst&0x100)
    {
        if(rc) fprintf(stderr,",");
        fprintf(stderr,"lr");
    }
    fprintf(stderr,"}");
}

        sp=read_register(13);
//fprintf(stderr,"sp 0x%08X\n",sp);
        for(ra=0,rb=0x01,rc=0;rb;rb=(rb<<1)&0xFF,ra++)
        {
            if(inst&rb)
            {
                rc++;
            }
        }
        if(inst&0x100) rc++;
        rc<<=2;
        sp-=rc;
        rd=sp;
        for(ra=0,rb=0x01;rb;rb=(rb<<1)&0xFF,ra++)
        {
            if(inst&rb)
            {
                write32(rd,read_register(ra));
                rd+=4;
            }
        }
        if(inst&0x100)
        {
            rc=read_register(14);
            write32(rd,rc); //read_register(14));

            if((rc&1)==0)
            {
                fprintf(stderr,"\npush {lr} with an ARM address pc 0x%08X popped 0x%08X\n",pc,rc);
//                exit(1);
            }


        }
        write_register(13,sp);
        goto Return;
    }

    //REV
    if((inst&0xFFC0)==0xBA00)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"rev   r%u,r%u",rd,rn);
        ra=read_register(rn);
        rc =((ra>> 0)&0xFF)<<24;
        rc|=((ra>> 8)&0xFF)<<16;
        rc|=((ra>>16)&0xFF)<< 8;
        rc|=((ra>>24)&0xFF)<< 0;
        write_register(rd,rc);
        goto Return;
    }

    //REV16
    if((inst&0xFFC0)==0xBA40)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"rev16 r%u,r%u",rd,rn);
        ra=read_register(rn);
        rc =((ra>> 0)&0xFF)<< 8;
        rc|=((ra>> 8)&0xFF)<< 0;
        rc|=((ra>>16)&0xFF)<<24;
        rc|=((ra>>24)&0xFF)<<16;
        write_register(rd,rc);
        goto Return;
    }

    //REVSH
    if((inst&0xFFC0)==0xBAC0)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"revsh r%u,r%u",rd,rn);
        ra=read_register(rn);
        rc =((ra>> 0)&0xFF)<< 8;
        rc|=((ra>> 8)&0xFF)<< 0;
        if(rc&0x8000) rc|=0xFFFF0000;
        else          rc&=0x0000FFFF;
        write_register(rd,rc);
        goto Return;
    }

    //ROR
    if((inst&0xFFC0)==0x41C0)
    {
        rd=(inst>>0)&0x7;
        rs=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"rors  r%u,r%u",rd,rs);
        rc=read_register(rd);
        ra=read_register(rs);
        ra&=0xFF;
        if(ra==0)
        {
        }
        else
        {
            ra&=0x1F;
            if(ra==0)
            {
                do_cflag_bit(rc&0x80000000);
            }
            else
            {
                do_cflag_bit(rc&(1<<(ra-1)));
                rb=rc<<(32-ra);
                rc>>=ra;
                rc|=rb;
            }
        }
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        goto Return;
    }

    //SBC
    if((inst&0xFFC0)==0x4180)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"sbc   r%u,r%u",rd,rm);
        ra=read_register(rd);
        rb=read_register(rm);
        rc=ra-rb;
        if(!(cpsr&CPSR_C)) rc--;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        if(cpsr&CPSR_C)
        {
            do_cflag(ra,~rb,1);
            do_vflag(ra,~rb,1);
        }
        else
        {
            do_cflag(ra,~rb,0);
            do_vflag(ra,~rb,0);
        }
        goto Return;
    }

    //SETEND
    if((inst&0xFFF7)==0xB650)
    {
        fprintf(stderr,"setend not implemented\n");
        return(1);
    }

    //STMIA
    if((inst&0xF800)==0xC000)
    {
        rn=(inst>>8)&0x7;

if(DISS)
{
    fprintf(stderr,"stmia r%u!,{",rn);
    for(ra=0,rb=0x01,rc=0;rb;rb=(rb<<1)&0xFF,ra++)
    {
        if(inst&rb)
        {
            if(rc) fprintf(stderr,",");
            fprintf(stderr,"r%u",ra);
            rc++;
        }
    }
    fprintf(stderr,"}");
}
        sp=read_register(rn);
        for(ra=0,rb=0x01;rb;rb=(rb<<1)&0xFF,ra++)
        {
            if(inst&rb)
            {
                write32(sp,read_register(ra));
                sp+=4;
            }
        }
        write_register(rn,sp);
        goto Return;
    }

    //STR(1)
    if((inst&0xF800)==0x6000)
    {
        rd=(inst>>0)&0x07;
        rn=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
        rb<<=2;
if(DISS) fprintf(stderr,"str   r%u,[r%u,#0x%X]",rd,rn,rb);
        rb=read_register(rn)+rb;
        rc=read_register(rd);
        write32(rb,rc);
        goto Return;
    }

    //STR(2)
    if((inst&0xFE00)==0x5000)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(DISS) fprintf(stderr,"str   r%u,[r%u,r%u]",rd,rn,rm);
        rb=read_register(rn)+read_register(rm);
        rc=read_register(rd);
        write32(rb,rc);
        goto Return;
    }

    //STR(3)
    if((inst&0xF800)==0x9000)
    {
        rb=(inst>>0)&0xFF;
        rd=(inst>>8)&0x07;
        rb<<=2;
if(DISS) fprintf(stderr,"str   r%u,[SP,#0x%X]",rd,rb);
        rb=read_register(13)+rb;
//fprintf(stderr,"0x%08X\n",rb);
        rc=read_register(rd);
        write32(rb,rc);
        goto Return;
    }

    //STRB(1)
    if((inst&0xF800)==0x7000)
    {
        rd=(inst>>0)&0x07;
        rn=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
if(DISS) fprintf(stderr,"strb  r%u,[r%u,#0x%X]",rd,rn,rb);
        rb=read_register(rn)+rb;
        rc=read_register(rd);
        ra=read16(rb&(~1));
        if(rb&1)
        {
            ra&=0x00FF;
            ra|=rc<<8;
        }
        else
        {
            ra&=0xFF00;
            ra|=rc&0x00FF;
        }
        write16(rb&(~1),ra&0xFFFF);
        goto Return;
    }

    //STRB(2)
    if((inst&0xFE00)==0x5400)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(DISS) fprintf(stderr,"strb  r%u,[r%u,r%u]",rd,rn,rm);
        rb=read_register(rn)+read_register(rm);
        rc=read_register(rd);
        ra=read16(rb&(~1));
        if(rb&1)
        {
            ra&=0x00FF;
            ra|=rc<<8;
        }
        else
        {
            ra&=0xFF00;
            ra|=rc&0x00FF;
        }
        write16(rb&(~1),ra&0xFFFF);
        goto Return;
    }

    //STRH(1)
    if((inst&0xF800)==0x8000)
    {
        rd=(inst>>0)&0x07;
        rn=(inst>>3)&0x07;
        rb=(inst>>6)&0x1F;
        rb<<=1;
if(DISS) fprintf(stderr,"strh  r%u,[r%u,#0x%X]",rd,rn,rb);
        rb=read_register(rn)+rb;
        rc=read_register(rd);
        write16(rb,rc&0xFFFF);
        goto Return;
    }

    //STRH(2)
    if((inst&0xFE00)==0x5200)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(DISS) fprintf(stderr,"strh  r%u,[r%u,r%u]",rd,rn,rm);
        rb=read_register(rn)+read_register(rm);
        rc=read_register(rd);
        write16(rb,rc&0xFFFF);
        goto Return;
    }

    //SUB(1)
    if((inst&0xFE00)==0x1E00)
    {
        rd=(inst>>0)&7;
        rn=(inst>>3)&7;
        rb=(inst>>6)&7;
if(DISS) fprintf(stderr,"subs  r%u,r%u,#0x%X",rd,rn,rb);
        ra=read_register(rn);
        rc=ra-rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,~rb,1);
        do_vflag(ra,~rb,1);
        goto Return;
    }

    //SUB(2)
    if((inst&0xF800)==0x3800)
    {
        rb=(inst>>0)&0xFF;
        rd=(inst>>8)&0x07;
if(DISS) fprintf(stderr,"subs  r%u,#0x%02X",rd,rb);
        ra=read_register(rd);
        rc=ra-rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,~rb,1);
        do_vflag(ra,~rb,1);
        goto Return;
    }

    //SUB(3)
    if((inst&0xFE00)==0x1A00)
    {
        rd=(inst>>0)&0x7;
        rn=(inst>>3)&0x7;
        rm=(inst>>6)&0x7;
if(DISS) fprintf(stderr,"subs  r%u,r%u,r%u",rd,rn,rm);
        ra=read_register(rn);
        rb=read_register(rm);
        rc=ra-rb;
        write_register(rd,rc);
        do_nflag(rc);
        do_zflag(rc);
        do_cflag(ra,~rb,1);
        do_vflag(ra,~rb,1);
        goto Return;
    }

    //SUB(4)
    if((inst&0xFF80)==0xB080)
    {
        rb=inst&0x7F;
        rb<<=2;
if(DISS) fprintf(stderr,"sub   SP,#0x%02X",rb);
        ra=read_register(13);
        ra-=rb;
        write_register(13,ra);
        goto Return;
    }

    //SWI
    if((inst&0xFF00)==0xDF00)
    {
        rb=inst&0xFF;
if(DISS) fprintf(stderr,"swi   0x%02X",rb);

        if((inst&0xFF)==0xCC)
        {
            write_register(0,cpsr);
            goto Return;
        }
        else
        {
            fprintf(stderr,"\n\nswi 0x%02X\n",rb);
            return(1);
        }
    }

    //SXTB
    if((inst&0xFFC0)==0xB240)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"sxtb  r%u,r%u",rd,rm);
        ra=read_register(rm);
        rc=ra&0xFF;
        if(rc&0x80) rc|=(MINUS_ONE)<<8;
        write_register(rd,rc);
        goto Return;
    }

    //SXTH
    if((inst&0xFFC0)==0xB200)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"sxth  r%u,r%u",rd,rm);
        ra=read_register(rm);
        rc=ra&0xFFFF;
        if(rc&0x8000) rc|=(MINUS_ONE)<<16;
        write_register(rd,rc);
        goto Return;
    }

    //TST
    if((inst&0xFFC0)==0x4200)
    {
        rn=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"tst   r%u,r%u",rn,rm);
        ra=read_register(rn);
        rb=read_register(rm);
        rc=ra&rb;
        do_nflag(rc);
        do_zflag(rc);
        goto Return;
    }

    //UXTB
    if((inst&0xFFC0)==0xB2C0)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"uxtb  r%u,r%u",rd,rm);
        ra=read_register(rm);
        rc=ra&0xFF;
        write_register(rd,rc);
        goto Return;
    }

    //UXTH
    if((inst&0xFFC0)==0xB280)
    {
        rd=(inst>>0)&0x7;
        rm=(inst>>3)&0x7;
if(DISS) fprintf(stderr,"uxth  r%u,r%u",rd,rm);
        ra=read_register(rm);
        rc=ra&0xFFFF;
        write_register(rd,rc);
        goto Return;
    }

    fprintf(stderr,"invalid instruction 0x%08X 0x%04X\n",pc-4,inst);
    return(1);

Return:
    if (DISS) fprintf(stderr,sym ? " ; %s\n" : "\n", sym);
    return 0;
}
//-------------------------------------------------------------------
int reset ( void )
{
    memset(ram,0x00,sizeof(ram));

    systick_ctrl=0x00000004;
    systick_reload=0x00000000;
    systick_count=0x00000000;
    systick_calibrate=0x00ABCDEF;
    handler_mode=0;
    cpsr=0;

    reg_norm[13]=fetch32(entry); //cortex-m
    reg_norm[14]=0xFFFFFFFF;
    reg_norm[15]=fetch32(entry+4); //cortex-m
    if((reg_norm[15]&1)==0)
    {
        fprintf(stderr,"reset vector with an ARM address 0x%08X\n",reg_norm[15]);
        exit(1);
    }
    reg_norm[15]&=~1;
    reg_norm[15]+=2;

    instructions=0;
    fetches=0;
    reads=0;
    writes=0;

    return(0);
}
//-------------------------------------------------------------------
int run ( void )
{
    char c;
    reset();
    while(1)
    {
        if(output_vcd)
        {
            fprintf(fpvcd,"#%u\n",vcdcount++);
        }
        if (0 == (instructions & 0x1f)) {
            if (frame_buffer && fb_qevent()) {
                fb_event();
            } else {
                while (read(read_fd, &c, 1) == 1)
                    input_char(c, EvtKey, -1, -1);
            }
        }
        if(execute()) break;
    }
    dump_counters();
    return(0);
}
//-------------------------------------------------------------------
unsigned int load_binary(unsigned int addr, char *name)
{
	int f;
	int r;
	struct stat st;

	f = open(name, O_RDONLY);
	if (f < 0) {
		perror("Can't open file:");
		exit(1);
	}
	fstat(f, &st);
	memset(&rom[addr >> 1], 0x0a0a, st.st_size + 1);
	r = read(f, &rom[addr >> 1], st.st_size);
	close(f);
	return (r + 1) & ~0x1;
}
//-------------------------------------------------------------------
unsigned int htoi(char *h)
{
	unsigned int r = 0;

	while (*h) {
		r <<= 4;
		*h = toupper(*h);
		r += *h - (*h > '9' ? 55 : 48);
		h++;
	}
	return r;
}
//-------------------------------------------------------------------
int start_server(int port)
{
	struct sockaddr_in server, client;
	socklen_t c;
	int t = 1;

	socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(port);
	if (bind(socket_fd, (struct sockaddr *) &server, sizeof(server)) < 0) {
		perror("Failed to bind");
		exit(1);
	}
	if (listen(socket_fd, 1) < 0) {
		perror("Failed to listen");
		exit(1);
	}
	setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &t,sizeof(int));
	c = sizeof(struct sockaddr_in);
	fprintf(stderr, "Waiting for connection\n");
	read_fd = accept(socket_fd, (struct sockaddr *) &client, (socklen_t *) &c);
	write_fd = read_fd;
	fprintf(stderr, "Connected\n");
    return 0;
}
//-------------------------------------------------------------------
void stop_server(void)
{
	shutdown(read_fd, SHUT_RDWR);
	close(read_fd);
	close(socket_fd);
}
//-------------------------------------------------------------------
const char options[] = "c:d:e:g:h:m:o:p:sv?";
//-------------------------------------------------------------------
static void usage()
{
    fprintf(stderr, "usage: thumbulator [options] <rom.bin>...\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "    -c <cpuid>         set CPUID\n");
    fprintf(stderr, "    -d <dump.out>      set dump output file name\n");
    fprintf(stderr, "    -e <entry>         set entry address\n");
    fprintf(stderr, "    -g WxH             init gfx framebuffer\n");
    fprintf(stderr, "    -h <disk.img>      set next disk image\n");
    fprintf(stderr, "    -m <symbol.map>    load symbols (fmt: sym addr)\n");
    fprintf(stderr, "    -o <org>           set load address\n");
    fprintf(stderr, "    -p <port>          start TCP server on <port>\n");
    fprintf(stderr, "    -s                 enable disassembly.\n");
    fprintf(stderr, "    -v                 enable VCD output.\n");
    fprintf(stderr, "    -?                 help\n");
    exit(1);
}
//-------------------------------------------------------------------
void handle_cmd_line(int argc, char *argv[])
{
	int i, c, fd;
    char *p, *opt, *optarg, tmp[128];
	unsigned int org = 0;
    off_t off;
    int mem_size;

    if (1 == argc)
        usage();

    for (int i = 1; i < argc; i++) {
        opt    = argv[i];
        optarg = i+1 < argc ? argv[i+1] : NULL;
        if (opt[0] != '-') {
            org += load_binary(org, opt);
            continue;
        }
        c = opt[1];
        p = strchr(options, opt[1]);
        if (p && p[1] == ':') {
            if (NULL == optarg) {
                fprintf(stderr, "No argument for %s\n", opt);
                exit(1);
            }
            i++;
        }
		switch (c) {
		case 'c': cpuid = htoi(optarg); break;
		case 'd': output_file_name = optarg; break;
		case 'e': entry = htoi(optarg); break;
        case 'g':
            strcpy(tmp, optarg);
            p = strchr(tmp, 'x');
            if (NULL == p) {
                fprintf(stderr,"Define the resolution as WxH!");
                exit(1);
            }
            *p = 0;
            fb_width  = atoi(tmp); fb_height = atoi(p + 1);
            mem_size = fb_width * fb_height * 4;
            if (mem_size > 15*1024*1024) {
                fprintf(stderr, "Max. 15MB frame buffer supported!");
                exit(1);
            }
            frame_buffer = malloc(mem_size);
            if (NULL == frame_buffer) {
                fprintf(stderr, "Cannot allocate frame buffer!");
                exit(1);
            }
            break;
        case 'h':
            if (disk_count == MAX_DISK) {
                fprintf(stderr, "Not enough disk slots (max. %d)!", MAX_DISK);
                exit(1);
            }
            fd = open(optarg, O_RDWR);
            if (fd < 0) {
                fprintf(stderr, "Cannot open %s!", optarg);
                exit(1);
            }
            disk_fd[disk_count] = fd;
            off = lseek(fd, 0, SEEK_END);
            disk_size[disk_count++] = off - sizeof(disk_buffer);
            break;
		case 'm': load_syms(optarg); break;
		case 'o': org = htoi(optarg); break;
		case 'p': start_server(atoi(optarg)); break;
        case 's': DISS = 1; break;
        case 'v': output_vcd = 1; break;
        case '?':
        default:  usage(); break;
		}
	}
}
//-------------------------------------------------------------------
int main ( int argc, char *argv[] )
{
    int ra, flags;

    init_syms();
    output_vcd=0;
    cpuid = 0;
    read_fd = STDIN_FILENO;
    write_fd = STDOUT_FILENO;
    memset(rom,0xFF,sizeof(rom));
    memset(ram,0x00,sizeof(ram));
    frame_buffer = NULL;
    fb_width = fb_height = 0;
    disk_count = 0;
    for (ra = 0; ra < MAX_DISK; ra++)
        disk_fd[ra] = -1;
    handle_cmd_line(argc, argv);
    flags = fcntl(read_fd, F_GETFL, 0);
    fcntl(read_fd, F_SETFL, flags | O_NONBLOCK);

    if(output_vcd)
    {
        fprintf(stderr,"output vcd enabled\n");
        fpvcd=fopen("output.vcd","wt");
        if(fpvcd==NULL)
        {
            fprintf(stderr,"Error creating file output.vcd\n");
            output_vcd=0;
            return(1);
        }
        fprintf(fpvcd,"$version Generated by thumbulator $end\n");
        fprintf(fpvcd,"$timescale 1ns $end\n");
        fprintf(fpvcd,"$scope module thumb $end\n");
        for(ra=0;ra<16;ra++)
        {
            fprintf(fpvcd,"$var wire 32 r%u r%u $end\n",ra,ra);
        }
        fprintf(fpvcd,"$var wire 16 inst inst $end\n");
        fprintf(fpvcd,"$upscope $end\n");
        fprintf(fpvcd,"$enddefinitions $end\n");
        vcdcount=0;
        fprintf(fpvcd,"#%u\n",vcdcount++);
        for(ra=0;ra<16;ra++)
        {
            fprintf(fpvcd,"b0 r%u\n",ra);
        }
        fprintf(fpvcd,"b0 inst\n");
    }

    if (frame_buffer)
        fb_open(fb_width, fb_height, (char*) frame_buffer);

    run();
    if (socket_fd != -1)
        stop_server();

    if(output_vcd)
    {
        fclose(fpvcd);
    }
    if (frame_buffer)
        fb_close();
    for (ra = 0; ra < MAX_DISK; ra++)
        if (disk_fd[ra] >= 0)
            close(disk_fd[ra]);
    return(0);
}
//-------------------------------------------------------------------
//-------------------------------------------------------------------
//-------------------------------------------------------------------



//-------------------------------------------------------------------------
//
// Copyright (c) 2010 David Welch dwelch@dwelch.com
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//-------------------------------------------------------------------------


// vim:ts=4 sw=4 et:
