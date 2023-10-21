/* Copyright (C)
* 2019 - Christoph van Wüllen, DL1YCF
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*
*/

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <math.h>
#ifdef __APPLE__
  #include "MacOS.h"  // emulate clock_gettime on old MacOS systems
#endif

#define EXTERN extern
#include "hpsdrsim.h"

#define NUMRECEIVERS 4

/*
 * These variables represent the state of the machine
 */
// data from general packet
static int ddc_port = -1;
static int duc_port = -1;
static int hp_port = -1;
static int shp_port = -1;
static int audio_port = -1;
static int duc0_port = -1;
static int ddc0_port = -1;
static int mic_port = -1;
static int wide_port = -1;
static int wide_enable = -1;
static int wide_len = -1;
static int wide_size = -1;
static int wide_rate = -1;
static int wide_ppf = -1;
static int port_mm = -1;
static int port_smm = -1;
static int pwm_min = -1;
static int pwm_max = -1;
static int bits = -1;
static int hwtim = -1;
static int pa_enable = -1;
static int alex0_enable = -1;
static int alex1_enable = -1;
static int iqform = -1;

// data from rx specific packet
static int n_adc = -1;
static int adcdither[8];
static int adcrandom[8];
static int ddcenable[NUMRECEIVERS];
static int adcmap[NUMRECEIVERS];
static int rxrate[NUMRECEIVERS];
static int syncddc[NUMRECEIVERS];

//data from tx specific packet
static int dac = -1;
static int cwmode = -1;
static int sidelevel = -1;
static int sidefreq = -1;
static int speed = -1;
static int weight = -1;
static int hang = -1;
static int delay = -1;
static int txrate = -1;
static int ducbits = -1;
static int orion = -1;
static int gain = -1;
static int txatt = -1;

//stat from high-priority packet
static int run = 0;
static int ptt = -1;
static int cwx = -1;
static int dot = -1;
static int dash = -1;
static long rxfreq[NUMRECEIVERS];
static long txfreq = -1;
static int txdrive = -1;
static int w1400 = -1; // Xvtr and Audio enable
static int ocout = -1;
static int db9 = -1;
static int mercury_atts = -1;
static int alex0[32];
static int alex1[32];
static int stepatt0 = -1;
static int stepatt1 = -1;

//
// floating point representation of TX-Drive and ADC0-Attenuator
//
static double rxatt0_dbl = 1.0;
static double rxatt1_dbl = 1.0;
static double txatt_dbl = 1.0;
static double txdrv_dbl = 0.0;

// End of state variables

static int txptr = -1;

static pthread_t ddc_specific_thread_id;
static pthread_t duc_specific_thread_id;
static pthread_t rx_thread_id[NUMRECEIVERS];
static pthread_t tx_thread_id;
static pthread_t mic_thread_id;
static pthread_t audio_thread_id;
static pthread_t highprio_thread_id = 0;
static pthread_t send_highprio_thread_id;

void   *ddc_specific_thread(void*);
void   *duc_specific_thread(void*);
void   *highprio_thread(void*);
void   *send_highprio_thread(void*);
void   *rx_thread(void *);
void   *tx_thread(void *);
void   *mic_thread(void *);
void   *audio_thread(void *);

static double txlevel;

int new_protocol_running() {
  if (run) { return 1; }
  else { return 0; }
}

void new_protocol_general_packet(unsigned char *buffer) {
  static unsigned long seqnum = 0;
  unsigned long seqold;
  int rc;
  seqold = seqnum;
  seqnum = (buffer[0] >> 24) + (buffer[1] << 16) + (buffer[2] << 8) + buffer[3];

  if (seqnum != 0 && seqnum != seqold + 1 ) {
    printf("GP: SEQ ERROR, old=%lu new=%lu\n", seqold, seqnum);
  }

  rc = (buffer[5] << 8) + buffer[6];

  if (rc == 0) { rc = 1025; }

  if (rc != ddc_port || !run) {
    ddc_port = rc;
    printf("GP: RX specific rcv        port is  %4d\n", rc);
  }

  rc = (buffer[7] << 8) + buffer[8];

  if (rc == 0) { rc = 1026; }

  if (rc != duc_port || !run) {
    duc_port = rc;
    printf("GP: TX specific rcv        port is  %4d\n", rc);
  }

  rc = (buffer[9] << 8) + buffer[10];

  if (rc == 0) { rc = 1027; }

  if (rc != hp_port || !run) {
    hp_port = rc;
    printf("GP: HighPrio Port rcv      port is  %4d\n", rc);
  }

  rc = (buffer[11] << 8) + buffer[12];

  if (rc == 0) { rc = 1025; }

  if (rc != shp_port || !run) {
    shp_port = rc;
    printf("GP: HighPrio Port snd      port is  %4d\n", rc);
  }

  rc = (buffer[13] << 8) + buffer[14];

  if (rc == 0) { rc = 1028; }

  if (rc != audio_port || !run) {
    audio_port = rc;
    printf("GP: Audio rcv              port is  %4d\n", rc);
  }

  rc = (buffer[15] << 8) + buffer[16];

  if (rc == 0) { rc = 1029; }

  if (rc != duc0_port || !run) {
    duc0_port = rc;
    printf("GP: TX data rcv base       port is  %4d\n", rc);
  }

  rc = (buffer[17] << 8) + buffer[18];

  if (rc == 0) { rc = 1035; }

  if (rc != ddc0_port || !run) {
    ddc0_port = rc;
    printf("GP: RX data snd base       port is  %4d\n", rc);
  }

  rc = (buffer[19] << 8) + buffer[20];

  if (rc == 0) { rc = 1026; }

  if (rc != mic_port || !run) {
    mic_port = rc;
    printf("GP: Microphone data snd    port is  %4d\n", rc);
  }

  rc = (buffer[21] << 8) + buffer[22];

  if (rc == 0) { rc = 1027; }

  if (rc != wide_port || !run) {
    wide_port = rc;
    printf("GP: Wideband data snd       port is  %4d\n", rc);
  }

  rc = buffer[23];

  if (rc != wide_enable || !run) {
    wide_enable = rc;
    printf("GP: Wideband Enable Flag is %d\n", rc);
  }

  rc = (buffer[24] << 8) + buffer[25];

  if (rc == 0) { rc = 512; }

  if (rc != wide_len || !run) {
    wide_len = rc;
    printf("GP: WideBand Length is %d\n", rc);
  }

  rc = buffer[26];

  if (rc == 0) { rc = 16; }

  if (rc != wide_size || !run) {
    wide_size = rc;
    printf("GP: Wideband sample size is %d\n", rc);
  }

  rc = buffer[27];

  if (rc != wide_rate || !run) {
    wide_rate = rc;
    printf("GP: Wideband sample rate is %d\n", rc);
  }

  rc = buffer[28];

  if (rc != wide_ppf || !run) {
    wide_ppf = rc;
    printf("GP: Wideband PPF is %d\n", rc);
  }

  rc = (buffer[29] << 8) + buffer[30];

  if (rc != port_mm || !run) {
    port_mm = rc;
    printf("MemMapped Registers rcv port is %d\n", rc);
  }

  rc = (buffer[31] << 8) + buffer[32];

  if (rc != port_smm || !run) {
    port_smm = rc;
    printf("MemMapped Registers snd port is %d\n", rc);
  }

  rc = (buffer[33] << 8) + buffer[34];

  if (rc != pwm_min || !run) {
    pwm_min = rc;
    printf("GP: PWM Min value is %d\n", rc);
  }

  rc = (buffer[35] << 8) + buffer[36];

  if (rc != pwm_max || !run) {
    pwm_max = rc;
    printf("GP: PWM Max value is %d\n", rc);
  }

  rc = buffer[37];

  if (rc != bits || !run) {
    bits = rc;
    printf("GP: ModeBits=x%02x\n", rc);
  }

  rc = buffer[38];

  if (rc != hwtim || !run) {
    hwtim = rc;
    printf("GP: Hardware Watchdog enabled=%d\n", rc);
  }

  iqform = buffer[39];

  if (iqform == 0) { iqform = 3; }

  if (iqform != 3) { printf("GP: Wrong IQ Format requested: %d\n", iqform); }

  rc = (buffer[58] & 0x01);

  if (rc != pa_enable || !run) {
    pa_enable = rc;
    printf("GP: PA enabled=%d\n", rc);
  }

  rc = buffer[59] & 0x01;

  if (rc != alex0_enable || !run) {
    alex0_enable = rc;
    printf("GP: ALEX0 register enable=%d\n", rc);
  }

  rc = (buffer[59] & 0x02) >> 1;

  if (rc != alex1_enable || !run) {
    alex1_enable = rc;
    printf("GP: ALEX1 register enable=%d\n", rc);
  }

  //
  // Start HighPrio thread if we arrive here for the first time
  // The HighPrio thread keeps running all the time.
  //
  if (!highprio_thread_id) {
    if (pthread_create(&highprio_thread_id, NULL, highprio_thread, NULL) < 0) {
      perror("***** ERROR: Create HighPrio thread");
    }

    pthread_detach(highprio_thread_id);
    //
    // init state arrays to zero for the first time
    //
    memset(adcdither, -1, 8 * sizeof(int));
    memset(adcrandom, -1, 8 * sizeof(int));
    memset(ddcenable, -1, NUMRECEIVERS * sizeof(int));
    memset(adcmap, -1, NUMRECEIVERS * sizeof(int));
    memset(syncddc, -1, NUMRECEIVERS * sizeof(int));
    memset(rxfreq, -1, NUMRECEIVERS * sizeof(long));
    memset(alex0, 0, 32 * sizeof(int));
    memset(alex1, 0, 32 * sizeof(int));
  }
}

void *ddc_specific_thread(void *data) {
  int sock;
  struct sockaddr_in addr;
  socklen_t lenaddr = sizeof(addr);
  unsigned long seqnum, seqold;
  struct timeval tv;
  unsigned char buffer[2000];
  int yes = 1;
  int rc;
  int i, j;
  sock = socket(AF_INET, SOCK_DGRAM, 0);

  if (sock < 0) {
    perror("***** ERROR: RX specific: socket");
    return NULL;
  }

  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&yes, sizeof(yes));
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (void *)&yes, sizeof(yes));
  tv.tv_sec = 0;
  tv.tv_usec = 10000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (void *)&tv, sizeof(tv));
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(ddc_port);

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("***** ERROR: RX specific: bind");
    close(sock);
    return NULL;
  }

  seqnum = 0;

  while (run) {
    rc = recvfrom(sock, buffer, 1444, 0, (struct sockaddr *)&addr, &lenaddr);

    if (rc < 0 && errno != EAGAIN) {
      perror("***** ERROR: DDC specific thread: recvmsg");
      break;
    }

    if (rc < 0) { continue; }

    if (rc != 1444) {
      printf("RXspec: Received DDC specific packet with incorrect length");
      break;
    }

    seqold = seqnum;
    seqnum = (buffer[0] >> 24) + (buffer[1] << 16) + (buffer[2] << 8) + buffer[3];

    if (seqnum != 0 && seqnum != seqold + 1 ) {
      printf("RXspec: SEQ ERROR, old=%lu new=%lu\n", seqold, seqnum);
    }

    if (n_adc != buffer[4]) {
      n_adc = buffer[4];
      printf("RX: Number of ADCs: %d\n", n_adc);
    }

    for (i = 0; i < n_adc; i++) {
      rc = (buffer[5] >> i) & 0x01;

      if (rc != adcdither[i]) {
        adcdither[i] = rc;
        printf("RX: ADC%d dither=%d\n", i, rc);
      }
    }

    for (i = 0; i < n_adc; i++) {
      rc = (buffer[6] >> i) & 0x01;

      if (rc != adcrandom[i]) {
        adcrandom[i] = rc;
        printf("RX: ADC%d random=%d\n", i, rc);
      }
    }

    for (i = 0; i < NUMRECEIVERS; i++) {
      int modified = 0;
      rc = buffer[17 + 6 * i];

      if (rc != adcmap[i]) {
        modified = 1;
        adcmap[i] = rc;
      }

      rc = (buffer[18 + 6 * i] << 8) + buffer[19 + 6 * i];

      if (rc != rxrate[i]) {
        modified = 1;
        rxrate[i] = rc;
        modified = 1;
      }

      if (syncddc[i] != buffer[1363 + i]) {
        syncddc[i] = buffer[1363 + i];
        modified = 1;
      }

      rc = (buffer[7 + (i / 8)] >> (i % 8)) & 0x01;

      if (rc != ddcenable[i]) {
        modified = 1;
        ddcenable[i] = rc;
      }

      if (modified) {
        printf("RX: DDC%d Enable=%d ADC%d Rate=%d SyncMap=%02x\n",
               i, ddcenable[i], adcmap[i], rxrate[i], syncddc[i]);
        rc = 0;

        for (j = 0; j < 8; j++) {
          rc += (syncddc[i] >> i) & 0x01;
        }

        if (rc > 1) {
          printf("WARNING:\n");
          printf("WARNING:\n");
          printf("WARNING:\n");
          printf("WARNING: more than two DDC sync'ed\n");
          printf("WARNING: this simulator is not prepeared to handle this case\n");
          printf("WARNING: so are most of SDRs around!\n");
          printf("WARNING:\n");
          printf("WARNING:\n");
          printf("WARNING:\n");
        }
      }
    }
  }

  close(sock);
  return NULL;
}

void *duc_specific_thread(void *data) {
  int sock;
  struct sockaddr_in addr;
  socklen_t lenaddr = sizeof(addr);
  unsigned long seqnum, seqold;
  struct timeval tv;
  unsigned char buffer[100];
  int yes = 1;
  int rc;
  sock = socket(AF_INET, SOCK_DGRAM, 0);

  if (sock < 0) {
    perror("***** ERROR: TX specific: socket");
    return NULL;
  }

  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&yes, sizeof(yes));
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (void *)&yes, sizeof(yes));
  tv.tv_sec = 0;
  tv.tv_usec = 10000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (void *)&tv, sizeof(tv));
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(duc_port);

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("***** ERROR: TXspec: bind");
    close(sock);
    return NULL;
  }

  seqnum = 0;

  while (run) {
    rc = recvfrom(sock, buffer, 60, 0, (struct sockaddr *)&addr, &lenaddr);

    if (rc < 0 && errno != EAGAIN) {
      perror("***** ERROR: TXspec: recvmsg");
      break;
    }

    if (rc < 0) { continue; }

    if (rc != 60) {
      printf("TX: wrong length\n");
      break;
    }

    seqold = seqnum;
    seqnum = (buffer[0] >> 24) + (buffer[1] << 16) + (buffer[2] << 8) + buffer[3];

    if (seqnum != 0 && seqnum != seqold + 1 ) {
      printf("TX: SEQ ERROR, old=%lu new=%lu\n", seqold, seqnum);
    }

    if (dac != buffer[4]) {
      dac = buffer[4];
      printf("TX: Number of DACs: %d\n", dac);
    }

    if (cwmode != buffer[5]) {
      cwmode = buffer[5];
      printf("TX: CW mode bits = %x\n", cwmode);
    }

    if (sidelevel != buffer[6]) {
      sidelevel = buffer[6];
      printf("TX: CW side tone level: %d\n", sidelevel);
    }

    rc = (buffer[7] << 8) + buffer[8];

    if (rc != sidefreq) {
      sidefreq = rc;
      printf("TX: CW sidetone freq: %d\n", sidefreq);
    }

    if (speed != buffer[9]) {
      speed = buffer[9];
      printf("TX: CW keyer speed: %d wpm\n", speed);
    }

    if (weight != buffer[10]) {
      weight = buffer[10];
      printf("TX: CW weight: %d\n", weight);
    }

    rc = (buffer[11] << 8) + buffer[12];

    if (hang != rc) {
      hang = rc;
      printf("TX: CW hang time: %d msec\n", hang);
    }

    if (delay != buffer[13]) {
      delay = buffer[13];
      printf("TX: RF delay: %d msec\n", delay);
    }

    rc = (buffer[14] << 8) + buffer[15];

    if (txrate != rc) {
      txrate = rc;
      printf("TX: DUC sample rate: %d\n", rc);
    }

    if (ducbits != buffer[16]) {
      ducbits = buffer[16];
      printf("TX: DUC sample width: %d bits\n", ducbits);
    }

    if (orion != buffer[50]) {
      printf("---------------------------------------------------\n");
      orion = buffer[50];

      // Bit 0:  0=LineIn, 1=No LineIn (Mic)
      if (orion & 0x01) {
        gain = buffer[51];
        printf("TX: ORION Line-In selected\n");
      } else {
        printf("TX: ORION Microphone selected\n");
      }

      // Bit 1: MicBoost 0=Off 1=On
      if (orion & 0x02) {
        printf("TX: ORION Microphone 20dB boost selected\n");
      } else {
        printf("TX: ORION Microphone 20dB boost NOT selected\n");
      }

      // Bit 2: PTT 0=Enabled 1=Disabled
      if (orion & 0x04) {
        printf("TX: ORION MicPtt disabled\n");
      } else {
        printf("TX: ORION MicPtt enabled\n");
      }

      // Bit 3: TIP connection 0=Mic  1=PTT
      if (orion & 0x08) {
        printf("TX: ORION TIP <-- Mic\n");
      } else {
        printf("TX: ORION TIP <--- PTT\n");
      }

      // Bit 4: BIAS 0=off 1=on
      if (orion & 0x10)  {
        printf("TX: MicBias enabled\n");
      } else {
        printf("TX: MicBias disabled\n");
      }

      // Bit 5: Saturn Mic: 0=Mic 1=XLR
      if (orion & 0x20)  {
        printf("TX: Saturn XLR jack\n");
      } else {
        printf("TX: Saturn Mic jack\n");
      }

      printf("---------------------------------------------------\n");
    }

    if (gain != buffer[51]) {
      gain = buffer[51];
      printf("TX: LineIn Gain (dB): %f\n", -34.0 + 1.5 * gain);
    }

    if (txatt != buffer[59]) {
      txatt = buffer[59];
      txatt_dbl = pow(10.0, -0.05 * txatt);
      printf("TX: ATT DUC0/ADC0: %d\n", txatt);
    }
  }

  close(sock);
  return NULL;
}

void *highprio_thread(void *data) {
  int sock;
  struct sockaddr_in addr;
  socklen_t lenaddr = sizeof(addr);
  unsigned long seqnum, seqold;
  unsigned char buffer[2000];
  struct timeval tv;
  int yes = 1;
  int rc;
  unsigned long freq;
  int i;
  int count = 0;
  int alex0_mod, alex1_mod, hp_mod;
  sock = socket(AF_INET, SOCK_DGRAM, 0);

  if (sock < 0) {
    perror("***** ERROR: HP: socket");
    return NULL;
  }

  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&yes, sizeof(yes));
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (void *)&yes, sizeof(yes));
  tv.tv_sec = 0;
  tv.tv_usec = 100000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (void *)&tv, sizeof(tv));
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(hp_port);

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("***** ERROR: HP: bind");
    close(sock);
    return NULL;
  }

  while (1) {
    //
    // WATCHDOG: if no packet arrives in 10 seconds,
    //           treat this situation as if "run" is set to zero
    //
    rc = recvfrom(sock, buffer, 1444, 0, (struct sockaddr *)&addr, &lenaddr);

    if (rc < 0 && errno != EAGAIN) {
      perror("***** ERROR: HighPrio thread: recvmsg");
      break;
    }

    count++;
    if (count >= 50) {
      // WATCHDOG code
      printf("HP: Watchdog Reset\n");
      run = 0;
      pthread_join(ddc_specific_thread_id, NULL);
      pthread_join(duc_specific_thread_id, NULL);

      for (i = 0; i < NUMRECEIVERS; i++) {
        pthread_join(rx_thread_id[i], NULL);
      }

      pthread_join(send_highprio_thread_id, NULL);
      pthread_join(tx_thread_id, NULL);
      pthread_join(mic_thread_id, NULL);
      pthread_join(audio_thread_id, NULL);
      break;
    }
    if (rc < 0 ) { continue; }

    if (rc != 1444) {
      printf("Received HighPrio packet with incorrect length");
      break;
    }

    count = 0;  // reset watchdog
    hp_mod = 0;
    seqold = seqnum;
    seqnum = (buffer[0] >> 24) + (buffer[1] << 16) + (buffer[2] << 8) + buffer[3];

    if (seqnum != 0 && seqnum != seqold + 1 ) {
      printf("HP: SEQ ERROR, old=%lu new=%lu\n", seqold, seqnum);
    }

    rc = (buffer[4] >> 0) & 0x01;

    if (rc != run) {
      run = rc;
      hp_mod = 1;
      txptr = -1;
      printf("HP: Run=%d\n", rc);

      // if run=0, wait for threads to complete, otherwise spawn them off
      if (run) {
        if (pthread_create(&ddc_specific_thread_id, NULL, ddc_specific_thread, NULL) < 0) {
          perror("***** ERROR: Create DDC specific thread");
        }

        if (pthread_create(&duc_specific_thread_id, NULL, duc_specific_thread, NULL) < 0) {
          perror("***** ERROR: Create DUC specific thread");
        }

        for (i = 0; i < NUMRECEIVERS; i++) {
          if (pthread_create(&rx_thread_id[i], NULL, rx_thread, (void *) (uintptr_t) i) < 0) {
            perror("***** ERROR: Create RX thread");
          }
        }

        if (pthread_create(&tx_thread_id, NULL, tx_thread, NULL) < 0) {
          perror("***** ERROR: Create TX thread");
        }

        if (pthread_create(&send_highprio_thread_id, NULL, send_highprio_thread, NULL) < 0) {
          perror("***** ERROR: Create SendHighPrio thread");
        }

        if (pthread_create(&mic_thread_id, NULL, mic_thread, NULL) < 0) {
          perror("***** ERROR: Create Mic thread");
        }

        if (pthread_create(&audio_thread_id, NULL, audio_thread, NULL) < 0) {
          perror("***** ERROR: Create Audio thread");
        }
      } else {
        pthread_join(ddc_specific_thread_id, NULL);
        pthread_join(duc_specific_thread_id, NULL);

        for (i = 0; i < NUMRECEIVERS; i++) {
          pthread_join(rx_thread_id[i], NULL);
        }

        pthread_join(send_highprio_thread_id, NULL);
        pthread_join(tx_thread_id, NULL);
        pthread_join(mic_thread_id, NULL);
        pthread_join(audio_thread_id, NULL);
        break;
      }
    }

    rc = (buffer[4] >> 1) & 0x01;

    if (rc != ptt) {
      ptt = rc;
      hp_mod = 1;
      printf("HP: PTT=%d\n", rc);

      if (ptt == 0) {
        txptr = -1;
        memset(isample, 0, sizeof(double)*NEWRTXLEN);
        memset(qsample, 0, sizeof(double)*NEWRTXLEN);
      }
    }

    rc = (buffer[5] >> 0) & 0x01;

    if (rc != cwx) {
      hp_mod = 1;
      cwx = rc;
      printf("HP: CWX=%d\n", rc);
    }

    rc = (buffer[5] >> 1) & 0x01;

    if (rc != dot) {
      hp_mod = 1;
      dot = rc;
      printf("HP: DOT=%d\n", rc);
    }

    rc = (buffer[5] >> 2) & 0x01;

    if (rc != dash) {
      hp_mod = 1;
      dash = rc;
      printf("HP: DASH=%d\n", rc);
    }

    for (i = 0; i < NUMRECEIVERS; i++) {
      freq = (buffer[ 9 + 4 * i] << 24) + (buffer[10 + 4 * i] << 16) + (buffer[11 + 4 * i] << 8) + buffer[12 + 4 * i];

      if (bits & 0x08) {
        freq = round(122880000.0 * (double) freq / 4294967296.0);
      }

      if (freq != rxfreq[i]) {
        rxfreq[i] = freq;
        printf("HP: DDC%d freq: %lu\n", i, freq);
      }
    }

    freq = (buffer[329] << 24) + (buffer[330] << 16) + (buffer[331] << 8) + buffer[332];

    if (bits & 0x08) {
      freq = round(122880000.0 * (double) freq / 4294967296.0);
    }

    if (freq != txfreq) {
      txfreq = freq;
      printf("HP: DUC freq: %lu\n", freq);
    }

    rc = buffer[345];

    if (rc != txdrive) {
      txdrive = rc;
      hp_mod = 1;
      txdrv_dbl = (double) txdrive * 0.003921568627;
      printf("HP: TX drive= %d (%f)\n", txdrive, txdrv_dbl);
    }

    rc = buffer[1400];

    if (rc != w1400) {
      w1400 = rc;
      hp_mod = 1;
      printf("HP: Xvtr/Audio enable=%x\n", rc);
    }

    rc = buffer[1401];

    if (rc != ocout) {
      hp_mod = 1;
      ocout = rc;
      printf("HP: OC outputs=%x\n", rc);
    }

    rc = buffer[1402];

    if (rc != db9) {
      hp_mod = 1;
      db9 = rc;
      printf("HP: Outputs DB9=%x\n", rc);
    }

    rc = buffer[1403];

    if (rc != mercury_atts) {
      hp_mod = 1;
      mercury_atts = rc;
      printf("HP: MercuryAtts=%x\n", rc);
    }

    // Store Alex0 and Alex1 bits in separate ints
    alex0_mod = alex1_mod = 0;
    freq = (buffer[1428] << 24) + (buffer[1429] << 16) + (buffer[1430] << 8) + buffer[1431];

    for (i = 0; i < 32; i++) {
      rc = (freq >> i) & 0x01;

      if (rc != alex1[i]) {
        alex1[i] = rc;
        alex1_mod = 1;
        hp_mod = 1;
        printf("HP: ALEX1 bit%d set to %d\n", i, rc);
      }
    }

    if (alex1_mod) {
      printf("HP: ALEX1 bits=0x%08lx\n", freq);
    }

    freq = (buffer[1432] << 24) + (buffer[1433] << 16) + (buffer[1434] << 8) + buffer[1435];

    for (i = 0; i < 32; i++) {
      rc = (freq >> i) & 0x01;

      if (rc != alex0[i]) {
        alex0[i] = rc;
        alex0_mod = 1;
        hp_mod = 1;
        printf("HP: ALEX0 bit%d set to %d\n", i, rc);
      }
    }

    if (alex0_mod) {
      printf("HP: ALEX0 bits=0x%08lx\n", freq);
    }

    rc = buffer[1442];

    if (rc != stepatt1) {
      hp_mod = 1;
      stepatt1 = rc;
      rxatt1_dbl = pow(10.0, -0.05 * stepatt1);
      printf("HP: StepAtt1 = %d\n", rc);
    }

    rc = buffer[1443];

    if (rc != stepatt0) {
      hp_mod = 1;
      stepatt0 = rc;
      printf("HP: StepAtt0 = %d\n", stepatt0);
    }

    // rxatt0 depends both on ALEX att and Step Att, so re-calc. it each time
    if (NEWDEVICE == NDEV_ORION2 || NEWDEVICE == NDEV_SATURN) {
      // There is no step attenuator on ANAN7000
      rxatt0_dbl = pow(10.0, -0.05 * stepatt0);
    } else {
      rxatt0_dbl = pow(10.0, -0.05 * (stepatt0 + 10 * alex0[14] + 20 * alex0[13]));
    }

    if (hp_mod) {
      printf("HP-----------------------------------HP\n");
    }
  }

  printf("HP thread terminating.\n");
  highprio_thread_id = 0;
  close(sock);
  return NULL;
}

void *rx_thread(void *data) {
  int sock;
  struct sockaddr_in addr;
  // One instance of this thread is started for each DDC
  unsigned long seqnum;
  unsigned char buffer[1444];
  int yes = 1;
  int i;
  long wait;
  double i0sample, q0sample;
  double i1sample, q1sample;
  double irsample, qrsample;
  double fac;
  int sample;
  unsigned char *p;
  int noisept;
  int myddc;
  int sync, size;
  int myadc, syncadc;
  int rxptr;
  int divptr;
  int decimation;
  unsigned int seed;
  double off, tonearg, tonedelta;
  double off2, tonearg2, tonedelta2;
  int do_tone, t3p, t3l;
  struct timespec delay;
  tonearg = 0.0;
  tonearg2 = 0.0;
  t3p = 0.0;
  myddc = (int) (uintptr_t) data;

  if (myddc < 0 || myddc >= NUMRECEIVERS) { return NULL; }

  seqnum = 0;
  // unique seed value for random number generator
  seed = ((uintptr_t) &seed) & 0xffffff;
  sock = socket(AF_INET, SOCK_DGRAM, 0);

  if (sock < 0) {
    perror("***** ERROR: RXthread: socket");
    return NULL;
  }

  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&yes, sizeof(yes));
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (void *)&yes, sizeof(yes));
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(ddc0_port + myddc);

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("***** ERROR: RXthread: bind");
    close(sock);
    return NULL;
  }

  noisept = 0;
  clock_gettime(CLOCK_MONOTONIC, &delay);
  printf("RX thread %d, enabled=%d\n", myddc, ddcenable[myddc]);
  rxptr = NEWRTXLEN / 2 - 8192;
  divptr = 0;

  while (run) {
    if (ddcenable[myddc] <= 0 || rxrate[myddc] == 0 || rxfreq[myddc] == 0) {
      usleep(5000);
      clock_gettime(CLOCK_MONOTONIC, &delay);
      continue;
    }

    decimation = 1536 / rxrate[myddc];
    myadc = adcmap[myddc];

    //
    // IQ frequency of 14.1 MHz signal
    //
    if (myadc == 0 && labs(7100000L - rxfreq[myddc]) < 500 * rxrate[myddc]) {
      off = (double)(7100000 - rxfreq[myddc]);
      tonedelta = -6.283185307179586476925286766559 * off / ((double) (1000 * rxrate[myddc]));
      do_tone = 3;
      t3l = 200 * rxrate[myddc];
    } else if (myadc == 0 && labs(14100000L - rxfreq[myddc]) < 500 * rxrate[myddc]) {
      off = (double)(14100000 - rxfreq[myddc]);
      tonedelta = -6.283185307179586476925286766559 * off / ((double) (1000 * rxrate[myddc]));
      do_tone = 1;
    } else if (myadc == 0 && labs(21100000L - rxfreq[myddc]) < 500 * rxrate[myddc]) {
      off = (double)(21100000 - rxfreq[myddc]);
      tonedelta = -6.283185307179586476925286766559 * off / ((double) (1000 * rxrate[myddc]));
      off2 = (double)(21100900 - rxfreq[myddc]);
      tonedelta2 = -6.283185307179586476925286766559 * off2 / ((double) (1000 * rxrate[myddc]));
      do_tone = 2;
    } else {
      do_tone = 0;
    }

    // for simplicity, we only allow for a single "synchronized" DDC,
    // this well covers the PureSignal and DIVERSITY cases
    sync = 0;
    i = syncddc[myddc];

    while (i) {
      sync++;
      i = i >> 1;
    }

    // sync == 0 means no synchronizatsion
    // sync == 1,2,3  means synchronization with DDC0,1,2
    // Usually we send 238 samples per buffer, but with synchronization
    // we send 119 sample *pairs*.
    if (sync) {
      size = 119;
      wait = 119000000L / rxrate[myddc]; // time for these samples in nano-secs
      syncadc = adcmap[sync - 1];
    } else {
      size = 238;
      wait = 238000000L / rxrate[myddc]; // time for these samples in nano-secs
    }

    //
    // ADC0 RX: noise + 14.1 MHz signal at -73 dBm
    // ADC0 TX: noise + distorted TX signal
    // ADC1 RX: noise
    // ADC1 TX: HERMES only: original TX signal
    // ADC2   : original TX signal
    //
    p = buffer;
    *p++ = (seqnum >> 24) & 0xFF;
    *p++ = (seqnum >> 16) & 0xFF;
    *p++ = (seqnum >>  8) & 0xFF;
    *p++ = (seqnum >>  0) & 0xFF;
    seqnum += 1;
    // do not use time stamps
    *p++ = 0;
    *p++ = 0;
    *p++ = 0;
    *p++ = 0;
    *p++ = 0;
    *p++ = 0;
    *p++ = 0;
    *p++ = 0;
    // 24 bits per sample *ALWAYS*
    *p++ = 0;
    *p++ = 24;
    *p++ = 0;
    *p++ = sync ? 2 * size : size; // should be 238 in either case

    if (txptr < 0) {
      rxptr = NEWRTXLEN / 2 - 8192;
    }

    for (i = 0; i < size; i++) {
      //
      // produce noise depending on the ADC
      //
      i1sample = i0sample = noiseItab[noisept];
      q1sample = q0sample = noiseQtab[noisept++];

      if (noisept == LENNOISE) { noisept = rand_r(&seed) / NOISEDIV; }

      //
      // PS: produce sample PAIRS,
      // a) distorted TX data (with Drive and Attenuation and ADC noise)
      // b) original TX data (normalized)
      //
      // DIV: produce sample PAIRS,
      // a) add man-made-noise on I-sample of RX channel
      // b) add man-made-noise on Q-sample of "synced" channel
      //
      if (sync && (rxrate[myadc] == 192) && ptt && (syncadc == n_adc)) {
        irsample = isample[rxptr];
        qrsample = qsample[rxptr++];

        if (rxptr >= NEWRTXLEN) { rxptr = 0; }

        fac = txatt_dbl * txdrv_dbl * (IM3a + IM3b * (irsample * irsample + qrsample * qrsample) * txdrv_dbl * txdrv_dbl);

        if (myadc == 0) {
          i0sample += irsample * fac;
          q0sample += qrsample * fac;
        }

        if (NEWDEVICE == NDEV_SATURN) {
          i1sample = irsample * 0.6121;
          q1sample = qrsample * 0.6121;
        } else {
          i1sample = irsample * 0.2899;
          q1sample = qrsample * 0.2899;
        }
      } else if (do_tone == 1) {
        i0sample += cos(tonearg) * 0.0002239 * rxatt0_dbl;
        q0sample += sin(tonearg) * 0.0002239 * rxatt0_dbl;
        tonearg += tonedelta;

        if (tonearg > 6.3) { tonearg -= 6.283185307179586476925286766559; }

        if (tonearg < -6.3) { tonearg += 6.283185307179586476925286766559; }
      } else if (do_tone == 2) {
        i0sample += (cos(tonearg) + cos(tonearg2)) * 0.0002239 * rxatt0_dbl;
        q0sample += (sin(tonearg) + sin(tonearg2)) * 0.0002239 * rxatt0_dbl;
        tonearg += tonedelta;
        tonearg2 += tonedelta2;

        if (tonearg > 6.3) { tonearg -= 6.283185307179586476925286766559; }

        if (tonearg2 > 6.3) { tonearg2 -= 6.283185307179586476925286766559; }

        if (tonearg < -6.3) { tonearg += 6.283185307179586476925286766559; }

        if (tonearg2 < -6.3) { tonearg2 += 6.283185307179586476925286766559; }
      } else if (do_tone == 3 && t3p >= 0) {
        i0sample += cos(tonearg) * 0.000003162278 * rxatt0_dbl;
        q0sample += sin(tonearg) * 0.000003162278 * rxatt0_dbl;
        tonearg += tonedelta;

        if (tonearg > 6.3) { tonearg -= 6.283185307179586476925286766559; }

        if (tonearg < -6.3) { tonearg += 6.283185307179586476925286766559; }
      }

      t3p++;

      if (t3p >= t3l) { t3p = -t3l; }

      if (diversity && !sync && myadc == 0) {
        i0sample += 0.0001 * rxatt0_dbl * divtab[divptr];
        divptr += decimation;

        if (divptr >= LENDIV) { divptr = 0; }
      }

      if (diversity && !sync && myadc == 1) {
        q0sample += 0.0002 * rxatt1_dbl * divtab[divptr];
        divptr += decimation;

        if (divptr >= LENDIV) { divptr = 0; }
      }

      if (diversity && sync && !ptt) {
        if (myadc == 0) { i0sample += 0.0001 * rxatt0_dbl * divtab[divptr]; }

        if (syncadc == 1) { q1sample += 0.0002 * rxatt1_dbl * divtab[divptr]; }

        divptr += decimation;

        if (divptr >= LENDIV) { divptr = 0; }
      }

      if (sync) {
        sample = i0sample * 8388607.0;
        *p++ = (sample >> 16) & 0xFF;
        *p++ = (sample >>  8) & 0xFF;
        *p++ = (sample >>  0) & 0xFF;
        sample = q0sample * 8388607.0;
        *p++ = (sample >> 16) & 0xFF;
        *p++ = (sample >>  8) & 0xFF;
        *p++ = (sample >>  0) & 0xFF;
        sample = i1sample * 8388607.0;
        *p++ = (sample >> 16) & 0xFF;
        *p++ = (sample >>  8) & 0xFF;
        *p++ = (sample >>  0) & 0xFF;
        sample = q1sample * 8388607.0;
        *p++ = (sample >> 16) & 0xFF;
        *p++ = (sample >>  8) & 0xFF;
        *p++ = (sample >>  0) & 0xFF;
      } else {
        sample = i0sample * 8388607.0;
        *p++ = (sample >> 16) & 0xFF;
        *p++ = (sample >>  8) & 0xFF;
        *p++ = (sample >>  0) & 0xFF;
        sample = q0sample * 8388607.0;
        *p++ = (sample >> 16) & 0xFF;
        *p++ = (sample >>  8) & 0xFF;
        *p++ = (sample >>  0) & 0xFF;
      }
    }

    delay.tv_nsec += wait;

    while (delay.tv_nsec >= 1000000000) {
      delay.tv_nsec -= 1000000000;
      delay.tv_sec++;
    }

    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &delay, NULL);

    if (sendto(sock, buffer, 1444, 0, (struct sockaddr * )&addr_new, sizeof(addr_new)) < 0) {
      perror("***** ERROR: RX thread sendto");
      break;
    }
  }

  close(sock);
  return NULL;
}

//
// This thread receives data (TX samples) from the PC
//
void *tx_thread(void * data) {
  int sock;
  struct sockaddr_in addr;
  socklen_t lenaddr = sizeof(addr);
  unsigned long seqnum, seqold;
  unsigned char buffer[1444];
  int yes = 1;
  int rc;
  int i;
  unsigned char *p;
  int sample;
  double di, dq;
  double sum;
  struct timeval tv;
  sock = socket(AF_INET, SOCK_DGRAM, 0);

  if (sock < 0) {
    perror("***** ERROR: TX: socket");
    return NULL;
  }

  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&yes, sizeof(yes));
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (void *)&yes, sizeof(yes));
  tv.tv_sec = 0;
  tv.tv_usec = 10000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (void *)&tv, sizeof(tv));
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(duc0_port);

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("***** ERROR: TX: bind");
    close(sock);
    return NULL;
  }

  seqnum = 0;

  while (run) {
    rc = recvfrom(sock, buffer, 1444, 0, (struct sockaddr *)&addr, &lenaddr);

    if (rc < 0 && errno != EAGAIN) {
      perror("***** ERROR: TX thread: recvmsg");
      break;
    }

    if (rc < 0) { continue; }

    if (rc != 1444) {
      printf("Received TX packet with incorrect length");
      break;
    }

    seqold = seqnum;
    seqnum = (buffer[0] << 24) + (buffer[1] << 16) + (buffer[2] << 8) + buffer[3];

    if (seqnum != 0 && seqnum != seqold + 1 ) {
      printf("TXthread: SEQ ERROR, old=%lu new=%lu\n", seqold, seqnum);
    }

    p = buffer + 4;
    sum = 0.0;

    if (txptr < 0) {
      txptr = NEWRTXLEN / 2;
    }

    for (i = 0; i < 240; i++) {
      // process 240 TX iq samples
      sample  = (int)((signed char) (*p++)) << 16;
      sample |= (int)((((unsigned char)(*p++)) << 8) & 0xFF00);
      sample |= (int)((unsigned char)(*p++) & 0xFF);
      di = (double) sample / 8388608.0;
      sample  = (int)((signed char) (*p++)) << 16;
      sample |= (int)((((unsigned char)(*p++)) << 8) & 0xFF00);
      sample |= (int)((unsigned char)(*p++) & 0xFF);
      dq = (double) sample / 8388608.0;
      //
      //      I don't know why (perhaps the CFFIR in the SDR program)
      //      but somehow I must multiply the samples to get the correct
      //      strength
      //
      di *= 1.117;
      dq *= 1.117;
      //
      //      put TX samples into ring buffer
      //
      isample[txptr] = di;
      qsample[txptr++] = dq;

      if (txptr >= NEWRTXLEN) { txptr = 0; }

      //
      //      accumulate TX power
      //
      sum += (di * di + dq * dq);
    }

    //
    // For a full-amplitude signal (e.g. TUNE), sum is 240
    // and thus txlevel = txdrv_dbl^2
    //
    txlevel = sum * txdrv_dbl * txdrv_dbl * 0.0041667;
  }

  close(sock);
  return NULL;
}

void *send_highprio_thread(void *data) {
  int sock;
  struct sockaddr_in addr;
  unsigned long seqnum;
  unsigned char buffer[60];
  int yes = 1;
  int rc;
  unsigned char *p;
  seqnum = 0;
  sock = socket(AF_INET, SOCK_DGRAM, 0);

  if (sock < 0) {
    perror("***** ERROR: SendHighPrio thread: socket");
    return NULL;
  }

  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&yes, sizeof(yes));
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (void *)&yes, sizeof(yes));
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(shp_port);

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("***** ERROR: SendHighPrio thread: bind");
    close(sock);
    return NULL;
  }

  seqnum = 0;

  while (1) {
    if (!run) {
      close(sock);
      break;
    }

    // prepare buffer
    memset(buffer, 0, 60);
    p = buffer;
    *p++ = (seqnum >> 24) & 0xFF;
    *p++ = (seqnum >> 16) & 0xFF;
    *p++ = (seqnum >>  8) & 0xFF;
    *p++ = (seqnum >>  0) & 0xFF;
    *p++ = 0;    // no PTT and CW attached
    *p++ = 0;    // no ADC overload
    *p++ = 0;
    *p++ = txdrive;
    p += 6;
    rc = (int) ((4095.0 / c1) * sqrt(maxpwr * txlevel * c2));
    *p++ = (rc >> 8) & 0xFF;
    *p++ = (rc     ) & 0xFF;
    buffer[49] = 63; // about 13 volts supply

    if (sendto(sock, buffer, 60, 0, (struct sockaddr * )&addr_new, sizeof(addr_new)) < 0) {
      perror("***** ERROR: HP send thread sendto");
      break;
    }

    seqnum++;
    usleep(50000); // wait 50 msec then send again
  }

  close(sock);
  return NULL;
}

//
// This thread receives the audio samples and plays them
//
void *audio_thread(void *data) {
  int sock;
  struct sockaddr_in addr;
  socklen_t lenaddr = sizeof(addr);
  unsigned long seqnum, seqold;
  unsigned char buffer[260];
  int yes = 1;
  int rc;
  struct timeval tv;
  sock = socket(AF_INET, SOCK_DGRAM, 0);

  if (sock < 0) {
    perror("***** ERROR: Audio: socket");
    return NULL;
  }

  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&yes, sizeof(yes));
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (void *)&yes, sizeof(yes));
  tv.tv_sec = 0;
  tv.tv_usec = 10000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (void *)&tv, sizeof(tv));
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(audio_port);

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("***** ERROR: Audio: bind");
    close(sock);
    return NULL;
  }

  seqnum = 0;

  while (run) {
    rc = recvfrom(sock, buffer, 260, 0, (struct sockaddr *)&addr, &lenaddr);

    if (rc < 0 && errno != EAGAIN) {
      perror("***** ERROR: Audio thread: recvmsg");
      break;
    }

    if (rc < 0) { continue; }

    if (rc != 260) {
      printf("Received Audio packet with incorrect length");
      break;
    }

    seqold = seqnum;
    seqnum = (buffer[0] << 24) + (buffer[1] << 16) + (buffer[2] << 8) + buffer[3];

    if (seqnum != 0 && seqnum != seqold + 1 ) {
      printf("Audio thread: SEQ ERROR, old=%lu new=%lu\n", seqold, seqnum);
    }

    // just skip the audio samples
  }

  close (sock);
  return NULL;
}

//
// The microphone thread just sends silence, that is
// a "zeroed" mic frame every 1.333 msec
//
void *mic_thread(void *data) {
  int sock;
  struct sockaddr_in addr;
  unsigned long seqnum;
  unsigned char buffer[132];
  unsigned char *p;
  int yes = 1;
  struct timespec delay;
  sock = socket(AF_INET, SOCK_DGRAM, 0);

  if (sock < 0) {
    perror("***** ERROR: Mic thread: socket");
    return NULL;
  }

  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&yes, sizeof(yes));
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (void *)&yes, sizeof(yes));
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(mic_port);

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("***** ERROR: Mic thread: bind");
    close(sock);
    return NULL;
  }

  seqnum = 0;
  memset(buffer, 0, 132);
  clock_gettime(CLOCK_MONOTONIC, &delay);

  while (run) {
    // update seq number
    p = buffer;
    *p++ = (seqnum >> 24) & 0xFF;
    *p++ = (seqnum >> 16) & 0xFF;
    *p++ = (seqnum >>  8) & 0xFF;
    *p++ = (seqnum >>  0) & 0xFF;
    seqnum++;
    // 64 samples with 48000 kHz, makes 1333333 nsec
    delay.tv_nsec += 1333333;

    while (delay.tv_nsec >= 1000000000) {
      delay.tv_nsec -= 1000000000;
      delay.tv_sec++;
    }

    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &delay, NULL);

    if (sendto(sock, buffer, 132, 0, (struct sockaddr * )&addr_new, sizeof(addr_new)) < 0) {
      perror("***** ERROR: Mic thread sendto");
      break;
    }
  }

  close(sock);
  return NULL;
}
