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

#include <strstream>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <pthread.h>
#include <gtk/gtk.h>
#include <adplug/adplug.h>
#include <adplug/emuopl.h>
#include <adplug/silentopl.h>
#include <xmms/util.h>
#include <xmms/plugin.h>

/***** Defines *****/

// Version string
#define ADPLUG_XMMS_VERSION "AdPlug/XMMS " VERSION

// Sound buffer size in samples
#define SNDBUFSIZE 512

// Default subsong number
#define DFL_SUBSONG 1

// 8 and 16 bit xmms compatible audio formats
#define FORMAT_8  FMT_U8
#define FORMAT_16 FMT_S16_NE

// Define a min macro
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

// Define sampsize macro
#define sampsize (1 * (cfg.bit16 ? 2 : 1) * (cfg.stereo ? 2 : 1))

/***** Global variables *****/

extern "C" InputPlugin adplug_ip;
static gboolean audio_error = FALSE;

// Configuration (and defaults)
static struct {
  unsigned long freq;
  bool bit16, stereo, endless;
} cfg = { 44100l, true, false, false };

// Player variables
static struct {
  CPlayer *p;
  unsigned int subsong, songlength;
  int seek;
  char filename[PATH_MAX];
  char *songtitle;
  float time_ms;
  bool playing;
  pthread_t play_thread;
} plr = { NULL, 0, 0, -1, "", NULL, 0.0f, false, 0 };

/***** Debugging *****/

#ifdef DEBUG

#include <stdarg.h>

static void dbg_printf(const char *fmt, ...)
{
  va_list argptr;

  va_start(argptr, fmt);
  vfprintf(stderr, fmt, argptr);
  va_end(argptr);
}

#else

static void dbg_printf(const char *fmt, ...)
{ }

#endif

/***** Dialog boxes *****/

static void MessageBox(const char *title, const char *text, const char *button)
{
  char *tmptitle = (char *)malloc(strlen(title) + 1),
    *tmptxt = (char *)malloc(strlen(text) + 1),
    *tmpbutton = (char *)malloc(strlen(button) + 1);

  strcpy(tmptitle, title); strcpy(tmptxt, text); strcpy(tmpbutton, button);

  GtkWidget *msgbox = xmms_show_message(tmptitle, tmptxt, tmpbutton, FALSE,
					GTK_SIGNAL_FUNC(gtk_widget_destroyed), &msgbox);

  free(tmptitle); free(tmptxt); free(tmpbutton);
}

static void adplug_about(void)
{
  ostrstream text;

  text << ADPLUG_XMMS_VERSION "\n" \
    "Copyright (C) 2002 Simon Peter <dn.tlp@gmx.net>" \
    "\n\nThis plugin uses the AdPlug library, which is copyright (C) Simon Peter, et al.\n" \
    "Linked AdPlug library version: " << CAdPlug::get_version();

  MessageBox("About " ADPLUG_XMMS_VERSION, text.str(), "Ugh!");
  text.freeze(0);
}

static void adplug_info_box(char *filename)
{
  ostrstream infotext;
  CSilentopl tmpopl;
  CPlayer *p = CAdPlug::factory(filename, &tmpopl);

  if(!p) return;

  infotext << "Filename: " << filename << "\n\n" << \
    "--- Song Info -------------------------\n" << \
    "Title: " << p->gettitle() << endl << \
    "Author: " << p->getauthor() << endl << \
    "File Type: " << p->gettype() << endl << \
    "Subsongs: " << p->getsubsongs() << endl << \
    "Instruments: " << p->getinstruments() << endl << \
    "\nInstrument names:\n";

  MessageBox("AdPlug :: Song Info", infotext.str(), "Close");
  infotext.freeze(0);
}

/***** Main player (thread) *****/

static void *play_loop(void *filename)
{
  CEmuopl opl(cfg.freq, cfg.bit16, cfg.stereo);
  long toadd = 0, i, towrite;
  char *sndbuf, *sndbufpos;
  bool playing = true;

  // Try to load module
  if(!(plr.p = CAdPlug::factory((char *)filename, &opl))) {
    dbg_printf("play_loop(\"%s\"): File could not be opened! Bailing out...\n",
	       (char *)filename);
    MessageBox("AdPlug :: Error", "File could not be opened!", "Ok");
    pthread_exit(NULL);
  }

  // Cache song length
  plr.songlength = CAdPlug::songlength(plr.p, plr.subsong);

  // cache song title
  if(!plr.p->gettitle().empty()) {
    plr.songtitle = (char *)malloc(plr.p->gettitle().length() + 1);
    strcpy(plr.songtitle, plr.p->gettitle().c_str());
  }

  // reset to first subsong on new file
  if(strcmp((char *)filename, plr.filename)) {
    strcpy(plr.filename, (char *)filename);
    plr.subsong = DFL_SUBSONG;
  }

  // Allocate audio buffer
  sndbuf = (char *)malloc(SNDBUFSIZE * sampsize);

  // Set XMMS main window information
  adplug_ip.set_info(plr.songtitle, plr.songlength, cfg.freq * sampsize * 8,
		     cfg.freq, cfg.stereo ? 2 : 1);

  // Rewind player to right subsong
  plr.p->rewind(plr.subsong);

  // main playback loop
  while((playing || cfg.endless) && plr.playing) {
    // seek requested ?
    if (plr.seek != -1) {
      // backward seek ?
      if (plr.seek < plr.time_ms) {
        plr.p->rewind(plr.subsong);
        plr.time_ms = 0.0f;
      }

      // seek to needed position
      while((plr.time_ms < plr.seek) && plr.p->update())
        plr.time_ms += 1000 / plr.p->getrefresh();

      // Reset output plugin and some values
      adplug_ip.output->flush((int)plr.time_ms);
      plr.seek = -1;
    }

    // fill sound buffer
    towrite = SNDBUFSIZE; sndbufpos = sndbuf;
    while (towrite > 0) {
      while (toadd < 0) {
        toadd += cfg.freq;
	playing = plr.p->update();
        plr.time_ms += 1000 / plr.p->getrefresh();
      }
      i = min(towrite,(long)(toadd / plr.p->getrefresh() + 4) & ~3);
      opl.update((short *)sndbufpos, i);
      sndbufpos += i * sampsize; towrite -= i;
      toadd -= (long)(plr.p->getrefresh() * i);
    }

    // write sound buffer and update vis
    adplug_ip.add_vis_pcm(adplug_ip.output->written_time(),
			  cfg.bit16 ? FORMAT_16 : FORMAT_8,
			  cfg.stereo ? 2 : 1, SNDBUFSIZE * sampsize, sndbuf);
    while(adplug_ip.output->buffer_free() < SNDBUFSIZE * sampsize) xmms_usleep(10000);
    adplug_ip.output->write_audio(sndbuf, SNDBUFSIZE * sampsize);
  }

  if(!playing) // wait for output plugin to finish if song has self-ended
    while(adplug_ip.output->buffer_playing()) xmms_usleep(10000);
  else { // or else, flush its output buffers
    adplug_ip.output->buffer_free(); adplug_ip.output->buffer_free();
  }

  // free everything and exit
  delete plr.p; plr.p = 0;
  if(plr.songtitle) { free(plr.songtitle); plr.songtitle = 0; }
  free(sndbuf);
  plr.playing = false; // important! XMMS won't get a self-ended song without it.
  pthread_exit(NULL);
}

/***** Informational *****/

static int adplug_is_our_file(char *filename)
{
  CSilentopl tmpopl;
  CPlayer *p = CAdPlug::factory(filename,&tmpopl);

  dbg_printf("adplug_is_our_file(\"%s\"): returned ",filename);

  if(p) {
    delete p;
    dbg_printf("TRUE\n");
    return TRUE;
  }

  dbg_printf("FALSE\n");
  return FALSE;
}

static int adplug_get_time(void)
{
  if(audio_error) return -2;
  if(!plr.playing) return -1;
  return adplug_ip.output->output_time();
}

static void adplug_song_info(char *filename, char **title, int *length)
{
  CSilentopl tmpopl;
  CPlayer *p = CAdPlug::factory(filename, &tmpopl);

  dbg_printf("adplug_song_info(\"%s\", \"%s\", %d): ", filename, *title, *length);

  if(p) {
    // allocate and set title string
    if(p->gettitle().empty())
      *title = 0;
    else {
      *title = (char *)malloc(p->gettitle().length() + 1);
      strcpy(*title, p->gettitle().c_str());
    }

    // get song length
    *length = CAdPlug::songlength(p, plr.subsong);

    // delete temporary player object
    delete p;
  }

  dbg_printf("title = \"%s\", length = %d\n", *title, *length);
}

/***** Player control *****/

static void adplug_play(char *filename)
{
  dbg_printf("adplug_play(\"%s\")\n", filename);
  audio_error = FALSE;

  // open output plugin
  if (!adplug_ip.output->open_audio(cfg.bit16 ? FORMAT_16 : FORMAT_8, cfg.freq, cfg.stereo ? 2 : 1)) {
    audio_error = TRUE;
    return;
  }

  // Initialize global player data (important! XMMS segfaults if it's not in here!)
  plr.playing = true; plr.time_ms = 0.0f; plr.seek = -1;

  // start player thread
  pthread_create(&plr.play_thread, NULL, play_loop, filename);
}

static void adplug_stop(void)
{
  plr.playing = false; pthread_join(plr.play_thread,NULL); // stop player thread
  adplug_ip.output->close_audio(); // close output plugin
  dbg_printf("adplug_stop(): stopped player!\n");
}

static void adplug_pause(short paused)
{
  adplug_ip.output->pause(paused);
}

static void adplug_seek(int time)
{
  plr.seek = time * 1000; // time is in seconds, but we count in ms
}

/***** Miscellaneous handling *****/

static void adplug_init(void)
{
  dbg_printf("adplug_init()\n");
}

static void adplug_config(void)
{
  dbg_printf("adplug_config()\n");
}

static void adplug_quit(void)
{
  dbg_printf("adplug_quit()\n");
}

/***** Plugin (exported) *****/

InputPlugin adplug_ip =
  {
    NULL,                       // handle (filled by XMMS)
    NULL,                       // filename (filled by XMMS)
    ADPLUG_XMMS_VERSION,        // plugin description
    adplug_init,                // plugin functions...
    adplug_about,
    adplug_config,
    adplug_is_our_file,
    NULL, // scan_dir (look in Input/cdaudio/cdaudio.c)
    adplug_play,
    adplug_stop,
    adplug_pause,
    adplug_seek,
    NULL, // set_eq
    adplug_get_time,
    NULL,                       // get_volume (handled by output plugin)
    NULL,                       // set_volume (...)
    adplug_quit,
    NULL,                       // OBSOLETE - DO NOT USE!
    NULL,                       // add_vis_pcm (filled by XMMS)
    NULL,                       // set_info (filled by XMMS)
    NULL,                       // set_info_text (filled by XMMS)
    adplug_song_info,
    adplug_info_box,            // ...end functions
    NULL                        // output plugin (filled by XMMS)
  };

extern "C" InputPlugin *get_iplugin_info(void)
{
  return &adplug_ip;
}
