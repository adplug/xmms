/* 
   AdPlug/XMMS - AdPlug XMMS Plugin
   Copyright (C) 2002 Simon Peter <dn.tlp@gmx.net>

   AdPlug/XMMS is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This plugin is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this plugin; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <sys/types.h>
#include "system.h"
#include "config.h"
#include <xmms/plugin.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <adplug/adplug.h>

static InputPlugin adplug_ip;
static pthread_t play_thread;
static bool playing = false, pause = false;

static void adplug_about(void)
{
  static GtkWidget *box;
  box = xmms_show_message(_("About AdPlug-XMMS"),
			  _("Blah."),
			  _("Ok"), FALSE, NULL, NULL);
  gtk_signal_connect(GTK_OBJECT(box), "destroy",gtk_widget_destroyed, &box);
}

static int adplug_is_our_file(char *filename)
{
  return FALSE;
}

static void* play_loop(void *arg)
{
  while(playing) {
  }
  pthread_exit(NULL);
}

static char* adplug_title(char *filename)
{
  return title;
}

static void adplug_play(char *filename)
{
  if (tone_ip.output->open_audio(FMT_S16_NE, OUTPUT_FREQ, 1) == 0) {
    audio_error = TRUE;
    going = FALSE;
    return;
  }

  name = tone_title(filename);
  tone_ip.set_info(name, -1, 16 * OUTPUT_FREQ, OUTPUT_FREQ, 1);
  g_free(name);
  pthread_create(&play_thread, NULL, play_loop, filename);
}

static void adplug_stop(void)
{
  if(!playing) return;
  playing = false;
  pthread_join(play_thread,NULL);
  adplug_ip.output->close_audio();
}

static void adplug_pause(short paused)
{
  pause = paused ? true : false;
  tone_ip.output->pause(paused);
}

static int adplug_get_time(void)
{
  return tone_ip.output->output_time();
}

static void adplug_song_info(char *filename, char **title, int *length)
{
  *length = -1;
  *title = tone_title(filename);
}

static void adplug_init()
{
}

static void adplug_config()
{
}

static void adplug_seek(int time)
{
}

static int adplug_get_time()
{
}

static void adplug_info_box(char *filename)
{
}

static InputPlugin adplug_ip = 
  {
    NULL,
    NULL,
    "AdPlug-XMMS " VERSION,
    adplug_init,
    adplug_about,
    adplug_config,
    adplug_is_our_file,
    NULL,
    adplug_play,
    adplug_stop,
    adplug_pause,
    adplug_seek,
    NULL,
    adplug_get_time,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    adplug_song_info,
    adplug_info_box,
    NULL
  };

InputPlugin *get_iplugin_info(void)
{
  return &adplug_ip;
}
