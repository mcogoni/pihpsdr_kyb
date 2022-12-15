/* Copyright (C)
* 2017 - John Melton, G0ORX/N6LYT
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*
*/

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <gtk/gtk.h>
#include "main.h"
#include "discovery.h"
#include "receiver.h"
#include "sliders.h"
#include "toolbar.h"
#include "band_menu.h"
#include "diversity_menu.h"
#include "vfo.h"
#include "radio.h"
#include "radio_menu.h"
#include "new_menu.h"
#include "new_protocol.h"
#ifdef PURESIGNAL
#include "ps_menu.h"
#endif
#include "agc.h"
#include "filter.h"
#include "mode.h"
#include "band.h"
#include "bandstack.h"
#include "noise_menu.h"
#include "wdsp.h"
#ifdef CLIENT_SERVER
#include "client_server.h"
#endif
#include "ext.h"
#include "zoompan.h"
#include "equalizer_menu.h"
#include "store.h"


// The following calls functions can be called usig g_idle_add

int ext_discovery(void *data) {
  discovery();
  return 0;
}

//
// ALL calls to vfo_update should go through g_idle_add(ext_vfo_update)
// such that they can be filtered out if they come at high rate
//
static guint vfo_timeout=0;

static int vfo_timeout_cb(void * data) {
  vfo_timeout=0;
  vfo_update();
  return 0;
}

int ext_vfo_update(void *data) {
  if (vfo_timeout==0) {
    vfo_timeout=g_timeout_add(100, vfo_timeout_cb, NULL);
  }
  return 0;
}

int ext_mox_update(void *data) {
  mox_update(GPOINTER_TO_INT(data));
  return 0;
}

int ext_vox_changed(void *data) {
  vox_changed(GPOINTER_TO_INT(data));
  return 0;
}

int ext_sliders_update(void *data) {
  sliders_update();
  return 0;
}

int ext_start_rx(void *data) {
  start_rx();
  return 0;
}

int ext_start_tx(void *data) {
  start_tx();
  return 0;
}

int ext_update_noise(void *data) {
  update_noise();
  return 0;
}

int ext_update_eq(void *data) {
  update_eq();
  return 0;
}

int ext_set_duplex(void *data) {
  setDuplex();
  return 0;
}

#ifdef CLIENT_SERVER
//
// Execute a remote command and send a response.
// Because of the response required, we cannot just
// delegate to actions.c
//
int ext_remote_command(void *data) {
  HEADER *header=(HEADER *)data;
  REMOTE_CLIENT *client=header->context.client;
  int temp;
  switch(ntohs(header->data_type)) {
    case CMD_RESP_RX_FREQ:
      {
      FREQ_COMMAND *freq_command=(FREQ_COMMAND *)data;
      temp=active_receiver->pan;
      int vfo=freq_command->id;
      long long f=ntohll(freq_command->hz);
      vfo_set_frequency(vfo,f);
      vfo_update();
      send_vfo_data(client,VFO_A);
      send_vfo_data(client,VFO_B);
      if(temp!=active_receiver->pan) {
        send_pan(client->socket,active_receiver->id,active_receiver->pan);
      }
      }
      break;
    case CMD_RESP_RX_STEP:
      {
      STEP_COMMAND *step_command=(STEP_COMMAND *)data;
      temp=active_receiver->pan;
      short steps=ntohs(step_command->steps);
      vfo_step(steps);
      //send_vfo_data(client,VFO_A);
      //send_vfo_data(client,VFO_B);
      if(temp!=active_receiver->pan) {
        send_pan(client->socket,active_receiver->id,active_receiver->pan);
      }
      }
      break;
    case CMD_RESP_RX_MOVE:
      {
      MOVE_COMMAND *move_command=(MOVE_COMMAND *)data;
      temp=active_receiver->pan;
      long long hz=ntohll(move_command->hz);
      vfo_move(hz,move_command->round);
      //send_vfo_data(client,VFO_A);
      //send_vfo_data(client,VFO_B);
      if(temp!=active_receiver->pan) {
        send_pan(client->socket,active_receiver->id,active_receiver->pan);
      }
      }
      break;
    case CMD_RESP_RX_MOVETO:
      {
      MOVE_TO_COMMAND *move_to_command=(MOVE_TO_COMMAND *)data;
      temp=active_receiver->pan;
      long long hz=ntohll(move_to_command->hz);
      vfo_move_to(hz);
      send_vfo_data(client,VFO_A);
      send_vfo_data(client,VFO_B);
      if(temp!=active_receiver->pan) {
        send_pan(client->socket,active_receiver->id,active_receiver->pan);
      }
      }
      break;
    case CMD_RESP_RX_ZOOM:
      {
      ZOOM_COMMAND *zoom_command=(ZOOM_COMMAND *)data;
      temp=ntohs(zoom_command->zoom);
      set_zoom(zoom_command->id,(double)temp);
      send_zoom(client->socket,active_receiver->id,active_receiver->zoom);
      send_pan(client->socket,active_receiver->id,active_receiver->pan);
      }
      break;
    case CMD_RESP_RX_PAN:
      {
      PAN_COMMAND *pan_command=(PAN_COMMAND *)data;
      temp=ntohs(pan_command->pan);
      set_pan(pan_command->id,(double)temp);
      send_pan(client->socket,active_receiver->id,active_receiver->pan);
      }
      break;
    case CMD_RESP_RX_VOLUME:
      {
      VOLUME_COMMAND *volume_command=(VOLUME_COMMAND *)data;
      temp=ntohs(volume_command->volume);
      set_af_gain(volume_command->id,(double)temp/100.0);
      }
      break;
    case CMD_RESP_RX_AGC:
      {
      AGC_COMMAND *agc_command=(AGC_COMMAND *)data;
      RECEIVER *rx=receiver[agc_command->id];
      rx->agc=ntohs(agc_command->agc);
      set_agc(rx,rx->agc);
      send_agc(client->socket,rx->id,rx->agc);
      g_idle_add(ext_vfo_update, NULL);
      }
      break;
    case CMD_RESP_RX_AGC_GAIN:
      {
      AGC_GAIN_COMMAND *agc_gain_command=(AGC_GAIN_COMMAND *)data;
      temp=ntohs(agc_gain_command->gain);
      set_agc_gain(agc_gain_command->id,(double)temp);
      RECEIVER *rx=receiver[agc_gain_command->id];
      send_agc_gain(client->socket,rx->id,(int)rx->agc_gain,(int)rx->agc_hang,(int)rx->agc_thresh);
      }
      break;
    case CMD_RESP_RX_GAIN:
      {
      RFGAIN_COMMAND *command=(RFGAIN_COMMAND *) data;
      double td=ntohd(command->gain);
      set_rf_gain(command->id, td);
      }
      break;
    case CMD_RESP_RX_ATTENUATION:
      {
      ATTENUATION_COMMAND *attenuation_command=(ATTENUATION_COMMAND *)data;
      temp=ntohs(attenuation_command->attenuation);
      set_attenuation(temp);
      }
      break;
    case CMD_RESP_RX_SQUELCH:
      {
      SQUELCH_COMMAND *squelch_command=(SQUELCH_COMMAND *)data;
      receiver[squelch_command->id]->squelch_enable=squelch_command->enable;
      temp=ntohs(squelch_command->squelch);
      receiver[squelch_command->id]->squelch=(double)temp;
      set_squelch(receiver[squelch_command->id]);
      }
      break;
    case CMD_RESP_RX_NOISE:
      {
      NOISE_COMMAND *noise_command=(NOISE_COMMAND *)data;
      RECEIVER *rx=receiver[noise_command->id];
      rx->nb=noise_command->nb;
      rx->nb2=noise_command->nb2;
      mode_settings[vfo[rx->id].mode].nb=rx->nb;
      mode_settings[vfo[rx->id].mode].nb2=rx->nb2;
      rx->nr=noise_command->nr;
      rx->nr2=noise_command->nr2;
      mode_settings[vfo[rx->id].mode].nr=rx->nr;
      mode_settings[vfo[rx->id].mode].nr2=rx->nr2;
      rx->anf=noise_command->anf;
      mode_settings[vfo[rx->id].mode].anf=rx->anf;
      rx->snb=noise_command->snb;
      mode_settings[vfo[rx->id].mode].snb=rx->snb;
      set_noise();
      send_noise(client->socket,rx->id,rx->nb,rx->nb2,rx->nr,rx->nr2,rx->anf,rx->snb);
      }
      break;
    case CMD_RESP_RX_BAND:
      {
      BAND_COMMAND *band_command=(BAND_COMMAND *)data;
      RECEIVER *rx=receiver[band_command->id];
      short b=htons(band_command->band);
      vfo_band_changed(rx->id,b);
      send_vfo_data(client,VFO_A);
      send_vfo_data(client,VFO_B);
      }
      break;
    case CMD_RESP_RX_MODE:
      {
      MODE_COMMAND *mode_command=(MODE_COMMAND *)data;
      RECEIVER *rx=receiver[mode_command->id];
      short m=htons(mode_command->mode);
      vfo_mode_changed(m);
      send_vfo_data(client,VFO_A);
      send_vfo_data(client,VFO_B);
      send_filter(client->socket,rx->id,m);
      }
      break;
    case CMD_RESP_RX_FILTER:
      {
      FILTER_COMMAND *filter_command=(FILTER_COMMAND *)data;
      RECEIVER *rx=receiver[filter_command->id];
      short f=htons(filter_command->filter);
      vfo_filter_changed(f);
      send_vfo_data(client,VFO_A);
      send_vfo_data(client,VFO_B);
      send_filter(client->socket,rx->id,f);
      }
      break;
    case CMD_RESP_SPLIT:
      {
      SPLIT_COMMAND *split_command=(SPLIT_COMMAND *)data;
      if(can_transmit) {
        split=split_command->split;
        tx_set_mode(transmitter,get_tx_mode());
        g_idle_add(ext_vfo_update, NULL);
      }
      send_split(client->socket,split);
      }
      break;
    case CMD_RESP_SAT:
      {
      SAT_COMMAND *sat_command=(SAT_COMMAND *)data;
      sat_mode=sat_command->sat;
      g_idle_add(ext_vfo_update, NULL);
      send_sat(client->socket,sat_mode);
      }
      break;
    case CMD_RESP_DUP:
      {
      DUP_COMMAND *dup_command=(DUP_COMMAND *)data;
      duplex=dup_command->dup;
      g_idle_add(ext_vfo_update, NULL);
      send_dup(client->socket,duplex);
      }
      break;
    case CMD_RESP_LOCK:
      {
      LOCK_COMMAND *lock_command=(LOCK_COMMAND *)data;
      locked=lock_command->lock;
      g_idle_add(ext_vfo_update, NULL);
      send_lock(client->socket,locked);
      }
      break;
    case CMD_RESP_CTUN:
      {
      CTUN_COMMAND *ctun_command=(CTUN_COMMAND *)data;
      int v=ctun_command->id;
      vfo[v].ctun=ctun_command->ctun;
      if(!vfo[v].ctun) {
        vfo[v].offset=0;
      }
      vfo[v].ctun_frequency=vfo[v].frequency;
      set_offset(active_receiver,vfo[v].offset);
      g_idle_add(ext_vfo_update, NULL);
      send_ctun(client->socket,v,vfo[v].ctun);
      send_vfo_data(client,v);
      }
      break;
    case CMD_RESP_RX_FPS:
      {
      FPS_COMMAND *fps_command=(FPS_COMMAND *)data;
      int rx=fps_command->id;
      receiver[rx]->fps=fps_command->fps;
      calculate_display_average(receiver[rx]);
      set_displaying(receiver[rx],1);
      send_fps(client->socket,rx,receiver[rx]->fps);
      }
      break;
    case CMD_RESP_RX_SELECT:
      {
      RX_SELECT_COMMAND *rx_select_command=(RX_SELECT_COMMAND *)data;
      int rx=rx_select_command->id;
      receiver_set_active(receiver[rx]);
      send_rx_select(client->socket,rx);
      }
      break;
    case CMD_RESP_VFO:
      {
      VFO_COMMAND *vfo_command=(VFO_COMMAND *)data;
      int action=vfo_command->id;
      switch(action) {
        case VFO_A_TO_B:
          vfo_a_to_b();
          break;
        case VFO_B_TO_A:
          vfo_b_to_a();
          break;
        case VFO_A_SWAP_B:
          vfo_a_swap_b();
          break;
      }
      send_vfo_data(client,VFO_A);
      send_vfo_data(client,VFO_B);
      }
      break;
    case CMD_RESP_RIT_UPDATE:
      {
      RIT_UPDATE_COMMAND *rit_update_command=(RIT_UPDATE_COMMAND *)data;
      int rx=rit_update_command->id;
      vfo_rit_update(rx);
      send_vfo_data(client,rx);
      }
      break;
    case CMD_RESP_RIT_CLEAR:
      {
      RIT_CLEAR_COMMAND *rit_clear_command=(RIT_CLEAR_COMMAND *)data;
      int rx=rit_clear_command->id;
      vfo_rit_clear(rx);
      send_vfo_data(client,rx);
      }
      break;
    case CMD_RESP_RIT:
      {
      RIT_COMMAND *rit_command=(RIT_COMMAND *)data;
      int rx=rit_command->id;
      short rit=ntohs(rit_command->rit);
      vfo_rit(rx,(int)rit);
      send_vfo_data(client,rx);
      }
      break;
    case CMD_RESP_XIT_UPDATE:
      {
      XIT_UPDATE_COMMAND *xit_update_command=(XIT_UPDATE_COMMAND *)data;
      send_vfo_data(client,VFO_A);
      send_vfo_data(client,VFO_B);
      }
      break;
    case CMD_RESP_XIT_CLEAR:
      {
      XIT_CLEAR_COMMAND *xit_clear_command=(XIT_CLEAR_COMMAND *)data;
      send_vfo_data(client,VFO_A);
      send_vfo_data(client,VFO_B);
      }
      break;
    case CMD_RESP_XIT:
      {
      XIT_COMMAND *xit_command=(XIT_COMMAND *)data;
      short xit=ntohs(xit_command->xit);
      send_vfo_data(client,VFO_A);
      send_vfo_data(client,VFO_B);
      }
      break;
    case CMD_RESP_SAMPLE_RATE:
      {
      SAMPLE_RATE_COMMAND *sample_rate_command=(SAMPLE_RATE_COMMAND *)data;
      int rx=(int)sample_rate_command->id;
      long long rate=ntohll(sample_rate_command->sample_rate);
      if(rx==-1) {
        radio_change_sample_rate((int)rate);
        send_sample_rate(client->socket,-1,radio_sample_rate);
      } else {
        receiver_change_sample_rate(receiver[rx],(int)rate);
        send_sample_rate(client->socket,rx,receiver[rx]->sample_rate);
      }
      }
      break;
    case CMD_RESP_RECEIVERS:
      {
      RECEIVERS_COMMAND *receivers_command=(RECEIVERS_COMMAND *)data;
      int r=receivers_command->receivers;
      radio_change_receivers(r);
      send_receivers(client->socket,receivers);
      }
      break;
    case CMD_RESP_RIT_INCREMENT:
      {
      RIT_INCREMENT_COMMAND *rit_increment_command=(RIT_INCREMENT_COMMAND *)data;
      short increment=ntohs(rit_increment_command->increment);
      rit_increment=(int)increment;
      send_rit_increment(client->socket,rit_increment);
      }
      break;
    case CMD_RESP_FILTER_BOARD:
      {
      FILTER_BOARD_COMMAND *filter_board_command=(FILTER_BOARD_COMMAND *)data;
      filter_board=(int)filter_board_command->filter_board;
      load_filters();
      send_filter_board(client->socket,filter_board);
      }
      break;
    case CMD_RESP_SWAP_IQ:
      {
      SWAP_IQ_COMMAND *swap_iq_command=(SWAP_IQ_COMMAND *)data;
      iqswap=(int)swap_iq_command->iqswap;
      send_swap_iq(client->socket,iqswap);
      }
      break;
    case CMD_RESP_REGION:
      {
      REGION_COMMAND *region_command=(REGION_COMMAND *)data;
      iqswap=(int)region_command->region;
      send_region(client->socket,region);
      }
      break;
  }
  g_free(data);
  return 0;
}

int ext_receiver_remote_update_display(void *data) {
  RECEIVER *rx=(RECEIVER *)data;
  receiver_remote_update_display(rx);
  return 0;
}
int ext_remote_set_zoom(void *data) {
  int zoom=GPOINTER_TO_INT(data);
  remote_set_zoom(active_receiver->id,(double)zoom);
  return 0;
}

int ext_remote_set_pan(void *data) {
  int pan=GPOINTER_TO_INT(data);
  remote_set_pan(active_receiver->id,(double)pan);
  return 0;
}

int ext_set_title(void *data) {
  gtk_window_set_title(GTK_WINDOW(top_window),(char *)data);
  return 0;
}
#endif
