// FM Transmitter for Raspberry Pi 2.
// modified from code at https://github.com/dpiponi/pifm by netbufalo.
//
// page numbers in comments refer to
// http://www.raspberrypi.org/wp-content/uploads/2012/02/BCM2835-ARM-Peripherals.pdf
//
// build:
//   $ gcc -lm -std=c99 -g pi2fm.c -o pi2fm
//

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <math.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <unistd.h>

volatile unsigned char *allof7e;

#define BCM2836_PERI_BASE        0x3F000000 // register physical address.
#define GPIO_BASE (BCM2836_PERI_BASE + 0x200000) // GPIO offset (0x200000).
#define CM_GP0CTL (0x7e101070) // p.107
#define GPFSEL0   (0x7E200000) // p.90
#define CM_GP0DIV (0x7e101074) // p.108

#define ACCESS(offset, type) (*(volatile type*)(offset+(int)allof7e-0x7e000000))

void setup_fm(int state) {
    int mem_fd = open("/dev/mem", O_RDWR|O_SYNC);
    if (mem_fd < 0) {
        printf("can't open /dev/mem\n");
        exit(-1);
    }
    allof7e = (unsigned char *)mmap(
                  NULL,
                  0x01000000,  // len
                  PROT_READ|PROT_WRITE,
                  MAP_SHARED,
                  mem_fd,
                  BCM2836_PERI_BASE // base
              );

    if (allof7e == (unsigned char *)-1) {
        exit(-1);
    }

    // set up GPIO 4 to pulse regularly at a given period.
    struct GPFSEL0_T {
        char FSEL0 : 3;
        char FSEL1 : 3;
        char FSEL2 : 3;
        char FSEL3 : 3;
        char FSEL4 : 3;
        char FSEL5 : 3;
        char FSEL6 : 3;
        char FSEL7 : 3;
        char FSEL8 : 3;
        char FSEL9 : 3;
        char RESERVED : 2;
    };

    // note sure why i can't use next line in place of following three.
    // this is a pure C issue, not a hardware issue.
    //ACCESS(GPFSEL0, struct GPFSEL0_T).FSEL4 = 4; // alternative function 0 (see p.92)
    int tmp = ACCESS(GPFSEL0, int);
    tmp = (tmp | (1<<14)) & ~ ((1<<12) | (1<<13));
    ACCESS(GPFSEL0, int) = tmp;

    struct GPCTL {
        char SRC         : 4;
        char ENAB        : 1;
        char KILL        : 1;
        char             : 1;
        char BUSY        : 1;
        char FLIP        : 1;
        char MASH        : 2;
        unsigned int     : 13;
        char PASSWD      : 8;
    };
    char clock_src_plld = 6; // p.107
    ACCESS(CM_GP0CTL, struct GPCTL) = (struct GPCTL) {clock_src_plld, state, 0, 0, 0, state, 0x5a };
}


void shutdown_fm() {
    static int shutdown = 0;
    if (!shutdown) {
        shutdown = 1;
        printf("\nShutting down\n");
        setup_fm(0);
        exit(0);
    }
}


void modulate(int period) {
    struct CM_GP0DIV_T {
        unsigned int DIV : 24;
        char PASSWD : 8;
    };

    ACCESS(CM_GP0DIV, struct CM_GP0DIV_T) = (struct CM_GP0DIV_T) { period, 0x5a };
}

// set square wave period. See p. 105 and 108
// although DIV is 24 bit the period can only be set to an accuracy of 12 bits.
// the first 12 bits control the pulse length in units of 1/500MHz.
// the next 12 bits are used to dither the period so it averages at the chosen 24 bit value.
// the resulting quare wave is then filtered using MASH.
// see p.105 and http://en.wikipedia.org/wiki/MASH_(modulator)#Decimation_structures
// the 0x5a is a "password"
void playWav(char *filename, int mod, float bandwidth) {
    int fp = STDIN_FILENO;
    if (filename[0]!='-') fp = open(filename, 'r');
    lseek(fp, 22, SEEK_SET); // Skip 44 bytes wave header.
    int len = 512;
    short *data = (short *)malloc(len);
    printf("now broadcasting: %s ...\n", filename);

    int speed = 270; // you can play faster by decreasing this value.
    unsigned int lfsr = 1;
    int readBytes;
    while (readBytes = read(fp, data, len)) {
        for (int j = 0; j<readBytes/2; j++) {
            // compute modulated carrier period.
            float dval = (float)(data[j])/65536.0 * bandwidth;
            int intval = (int)(floor(dval));
            float frac = dval - (float)intval;
            unsigned int fracval = (unsigned int)(frac*((float)(1<<16))*((float)(1<<16)));
            for (int i=0; i<speed; i++) {
                lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xD0000001u); // Galois LFSR
                modulate(intval + (fracval>lfsr?1:0) + mod);
            }
        }
    }
}


int main(int argc, char **argv) {
    if (argc>1) {
        signal(SIGTERM, &shutdown_fm);
        signal(SIGINT, &shutdown_fm);
        atexit(&shutdown_fm);
        setup_fm(1);
        float freq_out = argc>2?atof(argv[2]):77.7; // center freq
        float bandwidth = argc>3?atof(argv[3]):8; // a.k.a volume
        int freq_pi = 500; // 500 MHz (RPi core_freq?).
        int mod = (freq_pi/freq_out)*4096; // divisor * PAGE_SIZE
        modulate(mod); // initialize carrier.
        printf("starting...\n -> carrier freq: %3.1f MHz\n -> band width: %3.1f\n", freq_out, bandwidth);
        playWav(argv[1], mod, bandwidth);
    } else {
         fprintf(stderr,
                "usage: %s wavfile.wav [freq] [A.K.A volume]\n\n"
                "where wavfile is 16 bit 22.050 kHz Mono.\n"
                "set wavfile to '-' to use stdin.\n"
                "band width will default to 16 if not specified. it should only be lowered!", argv[0]);
    }

    return 0;
}
