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

#include <strstream.h>
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
#include <xmms/configfile.h>

/***** Defines *****/

// Version string
#define ADPLUG_XMMS_VERSION	"AdPlug/XMMS " VERSION

// Sound buffer size in samples
#define SNDBUFSIZE	512

// AdPlug's 8 and 16 bit audio formats
#define FORMAT_8	FMT_U8
#define FORMAT_16	FMT_S16_NE

/***** Global variables *****/

extern "C" InputPlugin	adplug_ip;
static gboolean		audio_error = FALSE;

// Configuration (and defaults)
static struct {
  unsigned long freq;
  bool bit16, stereo, endless, quickdetect;
} cfg = { 44100l, true, false, false, false };

// Player variables
static struct {
  CPlayer	*p;
  unsigned int	subsong, songlength;
  int		seek;
  char		filename[PATH_MAX];
  char		*songtitle;
  float		time_ms;
  bool		playing;
  pthread_t	play_thread;
  GtkLabel	*infobox;
  GtkDialog	*infodlg;
} plr = { NULL, 0, 0, -1, "", NULL, 0.0f, false, 0, NULL, NULL };

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

/***** [Dialog]: Utility functions *****/

static GtkWidget *make_framed(GtkWidget *what, const gchar *label)
{
  GtkWidget *framebox = gtk_frame_new(label);

  gtk_container_add(GTK_CONTAINER(framebox), what);
  return framebox;
}

static GtkWidget *print_left(const gchar *text)
{
  GtkLabel *label = GTK_LABEL(gtk_label_new(text));

  gtk_label_set_justify(label, GTK_JUSTIFY_LEFT);
  gtk_misc_set_padding(GTK_MISC(label), 2, 2);
  return GTK_WIDGET(label);
}

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

/***** Dialog boxes *****/

static void adplug_about(void)
{
  ostrstream text;

  text << ADPLUG_XMMS_VERSION "\n"
    "Copyright (C) 2002 Simon Peter <dn.tlp@gmx.net>\n\n"
    "This plugin is released under the terms and conditions of the GNU LGPL.\n"
    "See http://www.gnu.org/licenses/lgpl.html for details."
    "\n\nThis plugin uses the AdPlug library, which is copyright (C) Simon Peter, et al.\n"
    "Linked AdPlug library version: " << CAdPlug::get_version() << ends;

  MessageBox("About " ADPLUG_XMMS_VERSION, text.str(), "Ugh!");
  text.freeze(0);
}

static void close_config_box_ok(GtkButton *button, GPtrArray *rblist)
{
  // Apply configuration settings
  cfg.bit16 = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_ptr_array_index(rblist, 0)));
  cfg.stereo = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_ptr_array_index(rblist, 1)));

  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_ptr_array_index(rblist, 2)))) cfg.freq = 11025;
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_ptr_array_index(rblist, 3)))) cfg.freq = 22050;
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_ptr_array_index(rblist, 4)))) cfg.freq = 44100;
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_ptr_array_index(rblist, 5)))) cfg.freq = 48000;

  cfg.endless = !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_ptr_array_index(rblist, 6)));
  cfg.quickdetect = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_ptr_array_index(rblist, 7)));

  g_ptr_array_free(rblist, FALSE);
}

static void close_config_box_cancel(GtkButton *button, GPtrArray *rblist)
{
  g_ptr_array_free(rblist, FALSE);
}

static void adplug_config(void)
{
  GtkDialog *config_dlg = GTK_DIALOG(gtk_dialog_new());
  GtkNotebook *notebook = GTK_NOTEBOOK(gtk_notebook_new());
  GtkTable *table;
  GPtrArray *rblist = g_ptr_array_new();

  gtk_window_set_title(GTK_WINDOW(config_dlg), "AdPlug :: Configuration");
  gtk_window_set_policy(GTK_WINDOW(config_dlg), FALSE, FALSE, TRUE); // Window is auto sized
  gtk_window_set_modal(GTK_WINDOW(config_dlg), TRUE);
  gtk_container_add(GTK_CONTAINER(config_dlg->vbox), GTK_WIDGET(notebook));

  // Add Ok & Cancel buttons
  {
    GtkWidget *button;

    button = gtk_button_new_with_label("Ok");
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(close_config_box_ok),
		       (gpointer)rblist);
    gtk_signal_connect_object_after(GTK_OBJECT(button), "clicked",
				    GTK_SIGNAL_FUNC(gtk_widget_destroy),
				    GTK_OBJECT(config_dlg));
    gtk_container_add(GTK_CONTAINER(config_dlg->action_area), button);

    button = gtk_button_new_with_label("Cancel");
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(close_config_box_cancel),
		       (gpointer)rblist);
    gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			      GTK_SIGNAL_FUNC(gtk_widget_destroy),
			      GTK_OBJECT(config_dlg));
    gtk_container_add(GTK_CONTAINER(config_dlg->action_area), button);
  }

  /***** Page 1: General *****/

  table = GTK_TABLE(gtk_table_new(1, 2, TRUE));
  gtk_table_set_row_spacings(table, 5); gtk_table_set_col_spacings(table, 5);
  gtk_notebook_append_page(notebook, GTK_WIDGET(table), print_left("General"));

  // Add "Sound quality" section
  {
    GtkTable *sqt = GTK_TABLE(gtk_table_new(2, 2, FALSE));
    GtkVBox *fvb;
    GtkRadioButton *rb;

    gtk_table_set_row_spacings(sqt, 5);
    gtk_table_set_col_spacings(sqt, 5);
    gtk_table_attach_defaults(table, make_framed(GTK_WIDGET(sqt), "Sound quality"),
			      0, 1, 0, 1);

    // Add "Resolution" section
    fvb = GTK_VBOX(gtk_vbox_new(TRUE, 0));
    gtk_table_attach_defaults(sqt, make_framed(GTK_WIDGET(fvb), "Resolution"),
			      0, 1, 0, 1);
    rb = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(NULL, "8bit"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rb), !cfg.bit16);
    gtk_container_add(GTK_CONTAINER(fvb), GTK_WIDGET(rb));
    rb = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label_from_widget(rb, "16bit"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rb), cfg.bit16);
    gtk_container_add(GTK_CONTAINER(fvb), GTK_WIDGET(rb));
    g_ptr_array_add(rblist, (gpointer)rb);

    // Add "Channels" section
    fvb = GTK_VBOX(gtk_vbox_new(TRUE, 0));
    gtk_table_attach_defaults(sqt, make_framed(GTK_WIDGET(fvb), "Channels"),
			      0, 1, 1, 2);
    rb = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(NULL, "Mono"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rb), !cfg.stereo);
    gtk_container_add(GTK_CONTAINER(fvb), GTK_WIDGET(rb));
    rb = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label_from_widget(rb, "Stereo"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rb), cfg.stereo);
    gtk_container_add(GTK_CONTAINER(fvb), GTK_WIDGET(rb));
    g_ptr_array_add(rblist, (gpointer)rb);

    // Add "Frequency" section
    fvb = GTK_VBOX(gtk_vbox_new(TRUE, 0));
    gtk_table_attach_defaults(sqt, make_framed(GTK_WIDGET(fvb), "Frequency"),
			      1, 2, 0, 2);
    rb = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(NULL, "11025"));
    if(cfg.freq == 11025) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rb), TRUE);
    gtk_container_add(GTK_CONTAINER(fvb), GTK_WIDGET(rb));
    g_ptr_array_add(rblist, (gpointer)rb);
    rb = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label_from_widget(rb, "22050"));
    if(cfg.freq == 22050) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rb), TRUE);
    gtk_container_add(GTK_CONTAINER(fvb), GTK_WIDGET(rb));
    g_ptr_array_add(rblist, (gpointer)rb);
    rb = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label_from_widget(rb, "44100"));
    if(cfg.freq == 44100) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rb), TRUE);
    gtk_container_add(GTK_CONTAINER(fvb), GTK_WIDGET(rb));
    g_ptr_array_add(rblist, (gpointer)rb);
    rb = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label_from_widget(rb, "48000"));
    if(cfg.freq == 48000) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rb), TRUE);
    gtk_container_add(GTK_CONTAINER(fvb), GTK_WIDGET(rb));
    g_ptr_array_add(rblist, (gpointer)rb);
  }

  // Add "Playback" section
  {
    GtkVBox *vb = GTK_VBOX(gtk_vbox_new(FALSE, 0));
    GtkCheckButton *cb;

    gtk_table_attach_defaults(table, make_framed(GTK_WIDGET(vb), "Playback"),
			      1, 2, 0, 1);

    cb = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Detect songend"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb), !cfg.endless);
    gtk_container_add(GTK_CONTAINER(vb), GTK_WIDGET(cb));
    g_ptr_array_add(rblist, (gpointer)cb);

    cb = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Quick file detection"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb), cfg.quickdetect);
    gtk_container_add(GTK_CONTAINER(vb), GTK_WIDGET(cb));
    g_ptr_array_add(rblist, (gpointer)cb);
    gtk_widget_set_state(GTK_WIDGET(cb), GTK_STATE_INSENSITIVE);
  }

  /***** Page 2: Formats *****/

  table = GTK_TABLE(gtk_table_new(1, 1, TRUE));
  gtk_notebook_append_page(notebook, GTK_WIDGET(table), print_left("Formats"));

  // Add "Format selection" section
  {
    GtkHBox *hb = GTK_HBOX(gtk_hbox_new(FALSE, 0));

    gtk_table_attach_defaults(table, make_framed(GTK_WIDGET(hb), "Format selection"),
			      0, 1, 0, 1);

    gtk_container_add(GTK_CONTAINER(hb), print_left("Not yet..."));
  }

  // Show window
  gtk_widget_show_all(GTK_WIDGET(config_dlg));
}

static void add_instlist(GtkCList *instlist, const char *t1, const char *t2)
{
  gchar *rowstr[2];

  rowstr[0] = g_strdup(t1); rowstr[1] = g_strdup(t2);
  gtk_clist_append(instlist, rowstr);
  g_free(rowstr[0]); g_free(rowstr[1]);
}

static void adplug_stop(void);
static void adplug_play(char *filename);

static void subsong_slider(GtkAdjustment *adj)
{
  adplug_stop();
  plr.subsong = (unsigned int)adj->value - 1;
  adplug_play(plr.filename);
}

static void close_infobox(GtkDialog *infodlg)
{
  // Forget our references to the instance of the "currently playing song" info
  // box. But only if we're really destroying that one... ;)
  if(infodlg == plr.infodlg) {
    plr.infobox = NULL;
    plr.infodlg = NULL;
  }
}

static void adplug_info_box(char *filename)
{
  CSilentopl tmpopl;
  CPlayer *p = (strcmp(filename, plr.filename) || !plr.p) ?
    CAdPlug::factory(filename, &tmpopl) : plr.p;

  if(!p) return; // bail out if no player could be created
  if(p == plr.p && plr.infodlg) return; // only one info box for active song

  ostrstream infotext;
  unsigned int i;
  GtkDialog *infobox = GTK_DIALOG(gtk_dialog_new());
  GtkButton *okay_button = GTK_BUTTON(gtk_button_new_with_label("Ok"));
  GtkPacker *packer = GTK_PACKER(gtk_packer_new());
  GtkHBox *hbox = GTK_HBOX(gtk_hbox_new(TRUE, 2));

  // Build file info box
  gtk_window_set_title(GTK_WINDOW(infobox), "AdPlug :: File Info");
  gtk_window_set_policy(GTK_WINDOW(infobox), FALSE, FALSE, TRUE); // Window is auto sized
  gtk_container_add(GTK_CONTAINER(infobox->vbox), GTK_WIDGET(packer));
  gtk_packer_set_default_border_width(packer, 2);
  gtk_box_set_homogeneous(GTK_BOX(hbox), FALSE);
  gtk_signal_connect_object(GTK_OBJECT(okay_button), "clicked",
			    GTK_SIGNAL_FUNC(gtk_widget_destroy),
			    GTK_OBJECT(infobox));
  gtk_signal_connect(GTK_OBJECT(infobox), "destroy",
		     GTK_SIGNAL_FUNC(close_infobox), 0);
  gtk_container_add(GTK_CONTAINER(infobox->action_area), GTK_WIDGET(okay_button));

  // Add filename section
  gtk_packer_add_defaults(packer, make_framed(print_left(filename), "Filename"),
			  GTK_SIDE_TOP, GTK_ANCHOR_CENTER, GTK_FILL_X);

  // Add "Song info" section
  infotext << "Title: " << p->gettitle() << endl <<
    "Author: " << p->getauthor() << endl <<
    "File Type: " << p->gettype() << endl <<
    "Subsongs: " << p->getsubsongs() << endl <<
    "Instruments: " << p->getinstruments();
  if(plr.p == p)
    infotext << ends;
  else {
    infotext << endl << "Orders: " << p->getorders() << endl <<
      "Patterns: " << p->getpatterns() << ends;
  }
  gtk_container_add(GTK_CONTAINER(hbox),
		    make_framed(print_left(infotext.str()), "Song"));
  infotext.freeze(0); // unfreeze info text

  // Add "Playback info" section if currently playing
  if(plr.p == p) {
    plr.infobox = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_justify(plr.infobox, GTK_JUSTIFY_LEFT);
    gtk_misc_set_padding(GTK_MISC(plr.infobox), 2, 2);
    gtk_container_add(GTK_CONTAINER(hbox),
		      make_framed(GTK_WIDGET(plr.infobox), "Playback"));
  }
  gtk_packer_add_defaults(packer, GTK_WIDGET(hbox), GTK_SIDE_TOP,
			  GTK_ANCHOR_CENTER, GTK_FILL_X);

  // Add instrument names section
  if(p->getinstruments()) {
    GtkScrolledWindow *instwnd = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
    GtkCList *instnames;
    gchar tmpstr[10];

    {
      gchar *rowstr[] = {"#","Instrument name"};
      instnames = GTK_CLIST(gtk_clist_new_with_titles(2, rowstr));
    }
    gtk_clist_set_column_justification(instnames, 0, GTK_JUSTIFY_RIGHT);

    for(i=0;i<p->getinstruments();i++) {
      sprintf(tmpstr, "%d", i + 1);
      add_instlist(instnames, tmpstr, p->getinstrument(i).c_str());
    }

    gtk_clist_columns_autosize(instnames);
    gtk_scrolled_window_set_policy(instwnd, GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(instwnd), GTK_WIDGET(instnames));
    gtk_packer_add(packer, GTK_WIDGET(instwnd), GTK_SIDE_TOP,
		   GTK_ANCHOR_CENTER, GTK_FILL_X, 0, 0, 0, 0, 50);
  }

  // Add "Song message" section
  if(!p->getdesc().empty()) {
    GtkScrolledWindow *msgwnd = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
    GtkText *msg = GTK_TEXT(gtk_text_new(NULL, NULL));
    gint pos = 0;

    gtk_scrolled_window_set_policy(msgwnd, GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    gtk_text_set_editable(msg, FALSE);
    gtk_text_set_word_wrap(msg, TRUE);
    //    gtk_text_set_line_wrap(msg, TRUE);
    gtk_editable_insert_text(GTK_EDITABLE(msg), p->getdesc().c_str(),
			     p->getdesc().length(), &pos);

    gtk_container_add(GTK_CONTAINER(msgwnd), GTK_WIDGET(msg));
    gtk_packer_add(packer, make_framed(GTK_WIDGET(msgwnd), "Song message"),
		   GTK_SIDE_TOP, GTK_ANCHOR_CENTER, GTK_FILL_X, 2, 0, 0, 200, 50);
  }

  // Add subsong slider section
  if(p == plr.p && p->getsubsongs() > 1) {
    GtkAdjustment *adj = GTK_ADJUSTMENT(gtk_adjustment_new(plr.subsong + 1, 1,
							   p->getsubsongs() + 1,
							   1, 5, 1));
    GtkHScale *slider = GTK_HSCALE(gtk_hscale_new(adj));

    gtk_signal_connect(GTK_OBJECT(adj), "value_changed",
		       GTK_SIGNAL_FUNC(subsong_slider), NULL);
    gtk_range_set_update_policy(GTK_RANGE(slider), GTK_UPDATE_DISCONTINUOUS);
    gtk_scale_set_digits(GTK_SCALE(slider), 0);
    gtk_packer_add_defaults(packer, make_framed(GTK_WIDGET(slider), "Subsong selection"),
			    GTK_SIDE_TOP, GTK_ANCHOR_CENTER, GTK_FILL_X);
  }

  // Show dialog box
  gtk_widget_show_all(GTK_WIDGET(infobox));
  if(p == plr.p) { // Remember widget, so we could destroy it later
    plr.infodlg = infobox;
  } else // Delete temporary player
    delete p;
}

/***** Main player (!! threaded !!) *****/

static void update_infobox(void)
{
  ostrstream infotext;

  // Recreate info string
  infotext << "Order: " << plr.p->getorder() << " / " << plr.p->getorders() <<
    endl << "Pattern: " << plr.p->getpattern() << " / " << plr.p->getpatterns() <<
    endl << "Row: " << plr.p->getrow() << endl << "Speed: " <<
    plr.p->getspeed() << endl << "Timer: " << plr.p->getrefresh() << "Hz" << ends;

  GDK_THREADS_ENTER();
  gtk_label_set_text(plr.infobox, infotext.str());
  infotext.freeze(0); // Unfreeze infotext
  GDK_THREADS_LEAVE();
}

// Define sampsize macro (only usable inside play_loop()!)
#define sampsize (1 * (bit16 ? 2 : 1) * (stereo ? 2 : 1))

static void *play_loop(void *filename)
{
  CEmuopl opl(cfg.freq, cfg.bit16, cfg.stereo);
  long toadd = 0, i, towrite;
  char *sndbuf, *sndbufpos;
  bool playing = true, bit16 = cfg.bit16, stereo = cfg.stereo;
  unsigned long freq = cfg.freq;

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
    plr.subsong = 0;
  }

  // Allocate audio buffer
  sndbuf = (char *)malloc(SNDBUFSIZE * sampsize);

  // Set XMMS main window information
  adplug_ip.set_info(plr.songtitle, plr.songlength, freq * sampsize * 8,
		     freq, stereo ? 2 : 1);

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
        toadd += freq;
	playing = plr.p->update();
        plr.time_ms += 1000 / plr.p->getrefresh();
      }
      i = MIN(towrite,(long)(toadd / plr.p->getrefresh() + 4) & ~3);
      opl.update((short *)sndbufpos, i);
      sndbufpos += i * sampsize; towrite -= i;
      toadd -= (long)(plr.p->getrefresh() * i);
    }

    // write sound buffer and update vis
    adplug_ip.add_vis_pcm(adplug_ip.output->written_time(),
			  bit16 ? FORMAT_16 : FORMAT_8,
			  stereo ? 2 : 1, SNDBUFSIZE * sampsize, sndbuf);
    while(adplug_ip.output->buffer_free() < SNDBUFSIZE * sampsize) xmms_usleep(10000);
    adplug_ip.output->write_audio(sndbuf, SNDBUFSIZE * sampsize);

    // update infobox, if necessary
    if(plr.infobox && plr.playing) update_infobox();
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

  // On new song, re-open "Song info" dialog, if open
  if(plr.infobox && strcmp(filename, plr.filename))
    gtk_widget_destroy(GTK_WIDGET(plr.infodlg));

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

/***** Configuration file handling *****/

#define CFG_VERSION "AdPlugXMMS1"

static void adplug_init(void)
{
  ConfigFile *f = xmms_cfg_open_default_file();

  // Read configuration
  xmms_cfg_read_boolean(f, CFG_VERSION, "16bit", (gboolean *)&cfg.bit16);
  xmms_cfg_read_boolean(f, CFG_VERSION, "Stereo", (gboolean *)&cfg.stereo);
  xmms_cfg_read_int(f, CFG_VERSION, "Frequency", (gint *)&cfg.freq);
  xmms_cfg_read_boolean(f, CFG_VERSION, "Endless", (gboolean *)&cfg.endless);
  xmms_cfg_read_boolean(f, CFG_VERSION, "QuickDetect", (gboolean *)&cfg.quickdetect);

  xmms_cfg_free(f);
}

static void adplug_quit(void)
{
  ConfigFile *f = xmms_cfg_open_default_file();

  // Write configuration
  xmms_cfg_write_boolean(f, CFG_VERSION, "16bit", cfg.bit16);
  xmms_cfg_write_boolean(f, CFG_VERSION, "Stereo", cfg.stereo);
  xmms_cfg_write_int(f, CFG_VERSION, "Frequency", cfg.freq);
  xmms_cfg_write_boolean(f, CFG_VERSION, "Endless", cfg.endless);
  xmms_cfg_write_boolean(f, CFG_VERSION, "QuickDetect", cfg.quickdetect);

  xmms_cfg_write_default_file(f);
  xmms_cfg_free(f);
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
