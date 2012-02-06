/*
 *
 *  Copyright (C) 2005 Kemal Ilgar Eroglu <kieroglu@math.washington.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */


#define TIMEOUT_TIME    2000 /* Countdown update period in milliseconds */
#define PBAR_THICKNESS  10

#define BORDER 8
#define WIDGET_SPACING 2

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include <gtk/gtk.h>

#include <libxfce4util/libxfce4util.h>
#include <libxfcegui4/xfce_iconbutton.h>
#include <libxfce4panel/xfce-panel-plugin.h>
#include <libxfcegui4/dialogs.h>

#include "xfcetimer.h"

static void create_plugin_control (XfcePanelPlugin *plugin);
XFCE_PANEL_PLUGIN_REGISTER_EXTERNAL(create_plugin_control);

void make_menu(plugin_data *pd);

/**
 * This is the timeout function that
 * repeats the alarm.
**/
static gboolean repeat_alarm (gpointer data){

  plugin_data *pd=(plugin_data *)data;

  /* Don't repeat anymore */
  if(pd->rem_repetitions == 0){

    if(pd->timeout_command)
        g_free(pd->timeout_command);
    pd->timeout_command=NULL;
    pd->alarm_repeating=FALSE;
    make_menu(pd);

    return FALSE;


  }

  g_spawn_command_line_async (pd->timeout_command,NULL);
  pd->rem_repetitions = pd->rem_repetitions -1 ;
  return TRUE;
}


/**
 * This is the update function that updates the
 * tooltip, pbar and keeps track of elapsed time
**/
static gboolean update_function (gpointer data){

  plugin_data *pd=(plugin_data *)data;
  gint elapsed_sec,remaining;
  gchar *tiptext = NULL;
  GtkWidget *dialog;

  elapsed_sec=(gint)g_timer_elapsed(pd->timer,NULL);

  /*g_fprintf(stderr,"\nElapsed %d seconds of %d",elapsed_sec,pd-
                    >timeout_period_in_sec);*/

  /* If countdown is not over, update tooltip */
  if(elapsed_sec < pd->timeout_period_in_sec){

     remaining=pd->timeout_period_in_sec-elapsed_sec;

     if(remaining>=3600)
       tiptext = g_strdup_printf(_("%dh %dm %ds left"),remaining/3600, (remaining%3600)/60,
            remaining%60);
     else if (remaining>=60)
       tiptext = g_strdup_printf(_("%dm %ds left"),remaining/60, remaining%60);
     else
       tiptext = g_strdup_printf(_("%ds left"),remaining);
       
     if(pd->is_paused)
       g_strlcat(tiptext,_(" (Paused)"),64);

     gtk_progress_bar_set_fraction  (GTK_PROGRESS_BAR(pd->pbar),
                    1.0-((gdouble)elapsed_sec)/pd->timeout_period_in_sec);

     gtk_tooltips_set_tip(pd->tip,GTK_WIDGET(pd->base),tiptext,NULL);

     g_free(tiptext);

     return TRUE;

  }

  /* Countdown is over, stop timer and free resources */

  /*g_fprintf(stderr,"\nTimer command is ==> %s...",pd->timeout_command);*/

  if( (strlen(pd->timeout_command)==0) || !pd->nowin_if_alarm ) {
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pd->pbar),1);
    dialog = gtk_message_dialog_new     (NULL,
                                    GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_WARNING,
                                    GTK_BUTTONS_CLOSE,
                                    _("Beeep! :) \nTime is up!"));

    g_signal_connect_swapped (dialog, "response",
                           G_CALLBACK (gtk_widget_destroy),
                           dialog);
    gtk_widget_show(dialog);
  }

  if(strlen(pd->timeout_command)>0) {

    g_spawn_command_line_async (pd->timeout_command,NULL);

    if(pd->repeat_alarm){
        pd->alarm_repeating=TRUE;
        pd->rem_repetitions=pd->repetitions;
        if(pd->repeat_timeout!=0)
            g_source_remove(pd->repeat_timeout);
        pd->repeat_timeout = g_timeout_add(pd->repeat_interval*1000,repeat_alarm,pd);
    }
    else
    {
        if(pd->timeout_command)
            g_free(pd->timeout_command);
        pd->timeout_command=NULL;
    }
  }

  if(pd->timer)
     g_timer_destroy(pd->timer);
  pd->timer=NULL;
  gtk_tooltips_disable(pd->tip);

  pd->timeout=0;

  pd->timer_on=FALSE;

  /* reset pbar */
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pd->pbar),0);

  make_menu(pd);

  /* This function won't be called again */
  return FALSE;

}

/**
 * This is the callback function called when a timer
 * is selected in the popup menu
**/

static void timer_selected (GtkWidget* menuitem, GdkEventButton* event, gpointer data){

  plugin_data *pd=(plugin_data *)data;
  gint row_count;

  row_count=0;

  /* Find the index of the menuitem selected, save it in pd->selected. Not very
     elegant, though */
  while (GTK_MENU_ITEM(menuitem)!=g_array_index(pd->menuarray,GtkMenuItem*,row_count) )
     row_count++;

  pd->selected=row_count;

  /*g_fprintf(stderr,"\n Selecten menuitem is %d",row_count);*/

}

/**
 * This is the callback function called when the
 * start/stop item is selected in the popup menu
**/

static void start_stop_selected (GtkWidget* menuitem, GdkEventButton* event, gpointer
                                        data){

  plugin_data *pd=(plugin_data *)data;
  GtkTreeIter iter;
  gboolean valid;
  GSList *group=NULL;
  gchar *timerinfo,*tout_command;
  gchar temp[8];
  gint row_count,cur_h,cur_m,cur_s,time;
  gint timeout_period;
  gboolean is_cd;
  GTimeVal timeval;
  struct tm *current;

  /* If counting down, we stop the timer and free the resources */
  if(pd->timer_on){

    /*g_fprintf(stderr,"\nTimer is running, shutting down...");*/
    if(pd->timer)
       g_timer_destroy(pd->timer);
    if(pd->timeout)
       g_source_remove(pd->timeout);
    if(pd->timeout_command)
       g_free(pd->timeout_command);

    pd->timer=NULL;
    pd->timeout_command=NULL;
    pd->timeout=0;
    pd->timer_on=FALSE;
    pd->is_paused=FALSE;

    /* Disable tooltips, reset pbar */
    gtk_tooltips_set_tip(pd->tip,GTK_WIDGET(pd->base),"",NULL);
    gtk_tooltips_disable(pd->tip);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pd->pbar),0);

    /* update menu*/
    make_menu(pd);

    return;

  }

  /* If we're here then the timer is off, so we start it */

  /*g_fprintf(stderr,"\nStarting timer...");*/

  valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL(pd->list), &iter);
  row_count=0;

  /* Empty timer list-> Nothing to do. pd->selected=0, though. */
  if(!valid)
    return;

  /* Search the list item with the  right index */
  while (valid && row_count!=pd->selected){
      valid = gtk_tree_model_iter_next (GTK_TREE_MODEL(pd->list), &iter);
      row_count++;
  }

  gtk_tree_model_get    (GTK_TREE_MODEL(pd->list), &iter, 2, &timerinfo, 3,
            &tout_command, 4, &is_cd ,5, &time, -1);

  /* This will not be freed until the timeout is destroyed */
  pd->timeout_command=tout_command;

  /* If it's a 24h type alarm, we find the difference with current time
     Here 'time' is in minutes */
  if(!is_cd) {

     g_get_current_time(&timeval);
     current = localtime((time_t *)&timeval.tv_sec);
     strftime(temp,7,"%H",current);
     cur_h=atoi(temp);
     /*g_fprintf(stderr,"\n Current time: %d : ",cur_h);*/
     strftime(temp,7,"%M",current);
     cur_m=atoi(temp);
     /*g_fprintf(stderr,"%d : ",cur_m);*/
     strftime(temp,7,"%S",current);
     cur_s=atoi(temp);
     /*g_fprintf(stderr,"%d \n",cur_s);*/

     timeout_period=time*60 - ((60*cur_h + cur_m)*60 + cur_s);

     if(timeout_period <0)
        timeout_period+= 24*60*60;
        
     pd->is_countdown = FALSE;

  }
  /* Else 'time' already gives the countdown period in seconds */
  else {
  
	  pd->is_countdown = TRUE;
      timeout_period=time;

  }
  pd->timeout_period_in_sec=timeout_period;

  /* start the timer */
  pd->timer=g_timer_new();
  pd->timer_on=TRUE;

  /* update stuff */
  make_menu(pd);

  gtk_tooltips_set_tip(pd->tip, GTK_WIDGET(pd->base), timerinfo, NULL);
  gtk_tooltips_enable(pd->tip);
  g_free(timerinfo);

  g_timer_start(pd->timer);
  pd->timeout = g_timeout_add(UPDATE_INTERVAL, update_function,pd);


}

static void pause_resume_selected (GtkWidget* menuitem, GdkEventButton* event, gpointer
                                        data){

  plugin_data *pd=(plugin_data *)data;


  /* If paused, we resume */
  if(pd->is_paused){
	
	g_timer_continue(pd->timer);
	pd->is_paused=FALSE;
	
  /* If we're here then the timer is runnig, so we pause */	
  } else {
  
  pd->is_paused=TRUE;
  g_timer_stop(pd->timer);
  
  }
  
  make_menu(pd);

}

/**
 * Callback when "Stop the alarm" is selected in the popup menu
**/

static void stop_repeating_alarm (GtkWidget* menuitem, GdkEventButton* event, gpointer
                                        data){
   plugin_data *pd=(plugin_data *)data;

   g_source_remove(pd->repeat_timeout);

   pd->alarm_repeating=FALSE;

   if(pd->timeout_command){
    g_free(pd->timeout_command);
    pd->timeout_command=NULL;
   }

   make_menu(pd);

}

/**
 * Callback when clicking on pbar. Pops the menu up/down
**/
static void pbar_clicked (GtkWidget *pbar, GdkEventButton *event, gpointer data){

    plugin_data *pd=(plugin_data *)data;

    /*g_fprintf(stderr,"\nReceived click on button %d",event->button);*/


    if(!pd->menu){
      g_fprintf(stderr,"\nNo menu\n");
      return;
    }

    if(event->button==1)
      gtk_menu_popup (GTK_MENU(pd->menu),NULL,NULL,NULL,NULL,event->button,event->time);
    else
      gtk_menu_popdown(GTK_MENU(pd->menu));

}

/**
 * This function generates the popup menu
**/
void make_menu(plugin_data *pd){

  GtkTreeIter iter;
  gboolean valid;
  GSList *group=NULL;
  GtkWidget *menuitem;
  gchar *timername,*timerinfo;
  gchar *itemtext = NULL;
  gint row_count;


  /* Destroy the existing one */
  if(pd->menu)
    gtk_widget_destroy(pd->menu);

  if(pd->menuarray)
    g_array_free(pd->menuarray,TRUE);

  pd->menu=gtk_menu_new();
  pd->menuarray=g_array_new(FALSE,TRUE,sizeof(menuitem));

  /* If the alarm is paused, the only option is to resume or stop */
  if (pd->is_paused) {
      menuitem=gtk_menu_item_new_with_label(_("Resume timer"));

      gtk_menu_shell_append (GTK_MENU_SHELL(pd->menu),menuitem);
      g_signal_connect  (G_OBJECT(menuitem),"button_press_event",
                G_CALLBACK(pause_resume_selected),pd);
      gtk_widget_show(menuitem);
      menuitem=gtk_menu_item_new_with_label(_("Stop timer"));

      gtk_menu_shell_append (GTK_MENU_SHELL(pd->menu),menuitem);
      g_signal_connect  (G_OBJECT(menuitem),"button_press_event",
                G_CALLBACK(start_stop_selected),pd);
      gtk_widget_show(menuitem);      
      gtk_widget_show(pd->menu);
      return;	  
  }

  valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL(pd->list), &iter);
  row_count=0;

  while (valid){

      /* Run through the list, read name and timer period info */

      /*g_fprintf(stderr,"\nMaking menuitem %d while selected is %d",row_count,pd->
                                selected);*/
      gtk_tree_model_get(GTK_TREE_MODEL(pd->list),&iter,1,&timername,2,&timerinfo,-1);
      itemtext = g_strdup_printf("%s (%s)",timername,timerinfo);
      menuitem=gtk_radio_menu_item_new_with_label(group,itemtext);
      gtk_widget_show(menuitem);
      g_free(timername);
      g_free(timerinfo);
      group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (menuitem));
      g_signal_connect  (G_OBJECT(menuitem),"button_press_event",
            G_CALLBACK(timer_selected),pd);
      /* The selected timer is always active */
      if(row_count==pd->selected)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),TRUE);
      /* others are disabled when timer or alarm is already running */
      else if(pd->timer_on || pd->alarm_repeating)
        gtk_widget_set_sensitive(GTK_WIDGET(menuitem),FALSE);

      gtk_menu_shell_append(GTK_MENU_SHELL(pd->menu),menuitem);

      /*g_fprintf(stderr,"\nAdding menuitem with label : %s",itemtext);*/
      g_free(itemtext);

      /* We add the address of menuitem to the array */
      g_array_append_val(pd->menuarray,menuitem);

      valid = gtk_tree_model_iter_next (GTK_TREE_MODEL(pd->list), &iter);
      row_count++;
  }

  /* Horizontal line (empty item) */
  menuitem=gtk_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(pd->menu),menuitem);
  gtk_widget_show(menuitem);

  /* Pause menu item */
  if(pd->timer_on && !pd->is_paused && pd->is_countdown) {
    menuitem=gtk_menu_item_new_with_label(_("Pause timer"));

    gtk_menu_shell_append   (GTK_MENU_SHELL(pd->menu),menuitem);
    g_signal_connect    (G_OBJECT(menuitem),"button_press_event",
            G_CALLBACK(pause_resume_selected),pd);
    gtk_widget_show(menuitem);

    gtk_widget_show(pd->menu);
  }
 
  /* Start/stop menu item */

  if(!pd->alarm_repeating){
      if(pd->timer_on)
        menuitem=gtk_menu_item_new_with_label(_("Stop timer"));
      else
        menuitem=gtk_menu_item_new_with_label(_("Start timer"));

      gtk_menu_shell_append (GTK_MENU_SHELL(pd->menu),menuitem);
      g_signal_connect  (G_OBJECT(menuitem),"button_press_event",
                G_CALLBACK(start_stop_selected),pd);
      gtk_widget_show(menuitem);
  }

  /* Stop repeating alarm if so */
  if(pd->alarm_repeating) {
    menuitem=gtk_menu_item_new_with_label(_("Stop the alarm"));

    gtk_menu_shell_append   (GTK_MENU_SHELL(pd->menu),menuitem);
    g_signal_connect    (G_OBJECT(menuitem),"button_press_event",
            G_CALLBACK(stop_repeating_alarm),pd);
    gtk_widget_show(menuitem);

    gtk_widget_show(pd->menu);
  }  

}


/**
 * Callback to the OK button in the Add window
**/
static void ok_add(GtkButton *button, gpointer data){

  alarm_data *adata = (alarm_data *)data;
  GtkTreeIter iter;
  gint t1,t2,t3,t;
  gchar *timeinfo = NULL;

  /* Add item to the list */
  gtk_list_store_append(adata->pd->list,&iter);

  gtk_list_store_set(GTK_LIST_STORE(adata->pd->list),&iter,
            0,adata->pd->count,
            1,gtk_entry_get_text(GTK_ENTRY(adata->name)),
            3,gtk_entry_get_text(GTK_ENTRY(adata->command)),
            4,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(adata->
                                    rb1)),-1);
  /* Item count goes up by one */
  adata->pd->count=adata->pd->count+1;

  /* If the h-m-s format was chosen, convert time to seconds,
     save the choice into the list */
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(adata->rb1))){

    t1=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(adata->timeh));
    t2=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(adata->timem));
    t3=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(adata->times));
    t=t1*3600+t2*60+t3;

    gtk_list_store_set(GTK_LIST_STORE(adata->pd->list),&iter,5,t,-1);
    if(t1>0)
       timeinfo = g_strdup_printf(_("%dh %dm %ds"),t1,t2,t3);
    else if(t2>0)
       timeinfo = g_strdup_printf(_("%dm %ds"),t2,t3);
    else
       timeinfo = g_strdup_printf(_("%ds"),t3);

    gtk_list_store_set(GTK_LIST_STORE(adata->pd->list),&iter,2,timeinfo,-1);
  }
  else{ /* The 24h format. Save time in minutes */

    t1=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(adata->time_h));
    t2=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(adata->time_m));
    t=t1*60+t2;
    gtk_list_store_set(GTK_LIST_STORE(adata->pd->list),&iter,5,t,-1);
    timeinfo = g_strdup_printf(_("At %02d:%02d"),t1,t2);
    gtk_list_store_set(GTK_LIST_STORE(adata->pd->list),&iter,2,timeinfo,-1);

  }

  /* Update menu */
  make_menu(adata->pd);

  /* Free resources */
  gtk_widget_destroy(GTK_WIDGET(adata->window));

  g_free(adata);
  g_free(timeinfo);
}



/**
 * Callback for cancelling Add and Edit. Just closes the window :).
**/
static void cancel_add_edit(GtkButton *button, gpointer data){

  alarm_data *adata=(alarm_data *)data;

  gtk_widget_destroy(GTK_WIDGET(adata->window));

  g_free(adata);

}


/**
 * Callback for OK button on Edit window. See ok_add for comments.
**/
static void ok_edit(GtkButton *button, gpointer data){

  alarm_data *adata = (alarm_data *)data;
  GtkTreeIter iter;
  gint t1,t2,t3,t;
  gchar *timeinfo = NULL;

  GtkTreeSelection *select;
  GtkTreeModel *model;

  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (adata->pd->tree));

  if (gtk_tree_selection_get_selected (select, &model, &iter))
  {
     gtk_list_store_set(GTK_LIST_STORE(adata->pd->list),&iter,
            1,gtk_entry_get_text(GTK_ENTRY(adata->name)),
            3,gtk_entry_get_text(GTK_ENTRY(adata->command)),
            4,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(adata->
                                    rb1)),-1);
     if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(adata->rb1))){

        t1=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(adata->timeh));
        t2=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(adata->timem));
        t3=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(adata->times));
        t=t1*3600+t2*60+t3;
        gtk_list_store_set(GTK_LIST_STORE(adata->pd->list),&iter,5,t,-1);
        if(t1>0)
           timeinfo = g_strdup_printf(_("%dh %dm %ds"),t1,t2,t3);
        else if(t2>0)
           timeinfo = g_strdup_printf(_("%dm %ds"),t2,t3);
        else
           timeinfo = g_strdup_printf(_("%ds"),t3);

        gtk_list_store_set(GTK_LIST_STORE(adata->pd->list),&iter,2,timeinfo,-1);
      }
      else{

        t1=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(adata->time_h));
        t2=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(adata->time_m));
        t=t1*60+t2;
        gtk_list_store_set(GTK_LIST_STORE(adata->pd->list),&iter,5,t,-1);
        timeinfo = g_strdup_printf(_("At %02d:%02d"),t1,t2);
        gtk_list_store_set(GTK_LIST_STORE(adata->pd->list),&iter,2,timeinfo,-1);

      }

  }

  make_menu(adata->pd);

  gtk_widget_destroy(GTK_WIDGET(adata->window));

  g_free(adata);
  g_free(timeinfo);
}

/**
 * Callback when first radio button in dialog has been selected.
 */
static void alarmdialog_countdown_toggled (GtkButton *button, gpointer data)
{
  //plugin_data *pd = (plugin_data *)data;
  alarm_data *adata = (alarm_data *) data;
  gboolean active;

  active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
  gtk_widget_set_sensitive(GTK_WIDGET(adata->timeh), active);
  gtk_widget_set_sensitive(GTK_WIDGET(adata->timem), active);
  gtk_widget_set_sensitive(GTK_WIDGET(adata->times), active);
}

/**
 * Callback when second radio button in dialog has been selected.
 */
static void alarmdialog_alarmtime_toggled (GtkButton *button, gpointer data)
{
  alarm_data *adata = (alarm_data *) data;
  gboolean active;

  active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
  gtk_widget_set_sensitive(GTK_WIDGET(adata->time_h), active);
  gtk_widget_set_sensitive(GTK_WIDGET(adata->time_m), active);
}

/**
 * Callback to the Add button in options window.
 * Creates the Add window.
**/
static void add_edit_clicked (GtkButton *buttonn, gpointer data){

  plugin_data *pd = (plugin_data *)data;

  GtkWindow *parent_window;
  GtkWindow *window;
  GtkLabel *label;
  GtkEntry *name,*command;
  GtkSpinButton *timeh,*timem,*times,*time_h,*time_m;
  GtkRadioButton *rb1,*rb2;
  GtkWidget *hbox,*vbox,*button;
  alarm_data *adata=g_new(alarm_data,1);
  gchar *nc; gboolean is_cd; gint time;
  GtkTreeIter iter;
  GtkTreeSelection *select;
  GtkTreeModel *model;

  window = (GtkWindow *)gtk_window_new(GTK_WINDOW_TOPLEVEL);

  adata->window=window;
  adata->pd=pd;

  gtk_window_set_modal(GTK_WINDOW(window),TRUE);
  parent_window = (GtkWindow *) gtk_widget_get_toplevel(GTK_WIDGET(buttonn));
  if (gtk_widget_is_toplevel(GTK_WIDGET(parent_window)))
      gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(parent_window));

  vbox=gtk_vbox_new(FALSE, BORDER);
  gtk_container_add(GTK_CONTAINER(window),vbox);
  gtk_container_set_border_width(GTK_CONTAINER(window), BORDER);

  /***********/
  hbox=gtk_hbox_new(FALSE, BORDER);
  gtk_box_pack_start(GTK_BOX(vbox),hbox,FALSE,FALSE, WIDGET_SPACING);

  label = (GtkLabel *)gtk_label_new (_("Name:"));
  name = (GtkEntry *) gtk_entry_new_with_max_length(1023);
  adata->name=name;

  gtk_box_pack_start(GTK_BOX(hbox),GTK_WIDGET(label),FALSE,FALSE,0);
  gtk_box_pack_start(GTK_BOX(hbox),GTK_WIDGET(name),TRUE,TRUE,0);

  /**********/
  rb1=(GtkRadioButton *)gtk_radio_button_new_with_label(NULL,_("Enter the countdown time"));
  g_signal_connect(G_OBJECT(rb1), "toggled", G_CALLBACK(alarmdialog_countdown_toggled), adata);
  rb2=(GtkRadioButton *)gtk_radio_button_new_with_label(gtk_radio_button_get_group
                    (rb1),_("Enter the time of alarm (24h format)"));
  g_signal_connect(G_OBJECT(rb2), "toggled", G_CALLBACK(alarmdialog_alarmtime_toggled), adata);
  adata->rb1=rb1;

  gtk_box_pack_start(GTK_BOX(vbox),GTK_WIDGET(rb1),TRUE,TRUE,0);

  hbox=gtk_hbox_new(FALSE,0);
  gtk_box_pack_start(GTK_BOX(vbox),GTK_WIDGET(hbox),TRUE,TRUE,0);

  timeh = (GtkSpinButton *)gtk_spin_button_new_with_range(0,23,1);
  gtk_box_pack_start(GTK_BOX(hbox),GTK_WIDGET(timeh),FALSE,FALSE, WIDGET_SPACING);
  adata->timeh=timeh;
  label = (GtkLabel *)gtk_label_new (_("h  "));
  gtk_box_pack_start(GTK_BOX(hbox),GTK_WIDGET(label),FALSE,FALSE, WIDGET_SPACING);
  timem = (GtkSpinButton *)gtk_spin_button_new_with_range(0,59,1);
  gtk_box_pack_start(GTK_BOX(hbox),GTK_WIDGET(timem),FALSE,FALSE,WIDGET_SPACING);
  adata->timem=timem;
  label = (GtkLabel *)gtk_label_new (_("m  "));
  gtk_box_pack_start(GTK_BOX(hbox),GTK_WIDGET(label),FALSE,FALSE,WIDGET_SPACING);
  times = (GtkSpinButton *)gtk_spin_button_new_with_range(0,59,1);
  gtk_box_pack_start(GTK_BOX(hbox),GTK_WIDGET(times),FALSE,FALSE,WIDGET_SPACING);
  adata->times=times;
  label = (GtkLabel *)gtk_label_new (_("s  "));
  gtk_box_pack_start(GTK_BOX(hbox),GTK_WIDGET(label),FALSE,FALSE,WIDGET_SPACING);

  label = (GtkLabel *)gtk_label_new (_("or"));
  gtk_box_pack_start(GTK_BOX(vbox),GTK_WIDGET(label),TRUE,TRUE,BORDER);


  gtk_box_pack_start(GTK_BOX(vbox),GTK_WIDGET(rb2),TRUE,TRUE,0);

  hbox=gtk_hbox_new(FALSE,WIDGET_SPACING);
  gtk_box_pack_start(GTK_BOX(vbox),GTK_WIDGET(hbox),TRUE,TRUE,0);

  time_h = (GtkSpinButton *)gtk_spin_button_new_with_range(0,23,1);
  gtk_box_pack_start(GTK_BOX(hbox),GTK_WIDGET(time_h),FALSE,FALSE,0);
  adata->time_h=time_h;
  label = (GtkLabel *)gtk_label_new (":");
  gtk_box_pack_start(GTK_BOX(hbox),GTK_WIDGET(label),FALSE,FALSE,WIDGET_SPACING);
  time_m = (GtkSpinButton *)gtk_spin_button_new_with_range(0,59,1);
  gtk_box_pack_start(GTK_BOX(hbox),GTK_WIDGET(time_m),FALSE,FALSE,WIDGET_SPACING);
  adata->time_m=time_m;

  /****************/

  hbox=gtk_hbox_new(FALSE, BORDER);
  gtk_box_pack_start(GTK_BOX(vbox),GTK_WIDGET(hbox),TRUE,TRUE,WIDGET_SPACING);

  label = (GtkLabel *)gtk_label_new (_("Command to run:"));
  gtk_box_pack_start(GTK_BOX(hbox),GTK_WIDGET(label),FALSE,FALSE,WIDGET_SPACING);
  command = (GtkEntry *)gtk_entry_new_with_max_length(1023);
  adata->command=command;
  gtk_box_pack_start(GTK_BOX(hbox),GTK_WIDGET(command),TRUE,TRUE,WIDGET_SPACING);

  /****************/

  //hbox=gtk_hbox_new(TRUE,0);
  hbox = gtk_hbutton_box_new();
  gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_END);
  gtk_button_box_set_spacing(GTK_BUTTON_BOX(hbox), BORDER);
  gtk_box_pack_start(GTK_BOX(vbox),hbox,TRUE,TRUE,WIDGET_SPACING);
  //gtk_container_set_border_width(GTK_CONTAINER(hbox), BORDER);

  button=gtk_button_new_from_stock(GTK_STOCK_CANCEL);
  gtk_box_pack_start(GTK_BOX(hbox),button,TRUE,TRUE,0);
  g_signal_connect(G_OBJECT(button),"clicked",G_CALLBACK(cancel_add_edit),adata);

  button=gtk_button_new_from_stock(GTK_STOCK_OK);
  gtk_box_pack_start(GTK_BOX(hbox),button,TRUE,TRUE,0);
  if(GTK_WIDGET(buttonn)==pd->buttonadd)
     g_signal_connect(G_OBJECT(button),"clicked",G_CALLBACK(ok_add),adata);
  else
     g_signal_connect(G_OBJECT(button),"clicked",G_CALLBACK(ok_edit),adata);



  /* If this is the add window, we're done */
  if(GTK_WIDGET(buttonn)==pd->buttonadd) {
    gtk_window_set_title(window,_("Add new alarm"));
    gtk_widget_show_all(GTK_WIDGET(window));
    alarmdialog_alarmtime_toggled(GTK_BUTTON(rb2), adata);
    return;
  }

  /* Else fill the values in the boxes with the current choices */
  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (pd->tree));
  /*gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);*/

  if (gtk_tree_selection_get_selected (select, &model, &iter)){
      gtk_tree_model_get(model,&iter,1,&nc,-1);
      gtk_entry_set_text(GTK_ENTRY(name),nc);
      g_free(nc);

      gtk_tree_model_get(model,&iter,3,&nc,-1);
      gtk_entry_set_text(GTK_ENTRY(command),nc);
      g_free(nc);

      gtk_tree_model_get(model,&iter,4,&is_cd,-1);
      gtk_tree_model_get(model,&iter,5,&time,-1);

      if(is_cd){

         gtk_spin_button_set_value(GTK_SPIN_BUTTON(timeh),time/3600);
         gtk_spin_button_set_value(GTK_SPIN_BUTTON(timem),(time%3600)/60);
         gtk_spin_button_set_value(GTK_SPIN_BUTTON(times),time%60);
         alarmdialog_alarmtime_toggled(GTK_BUTTON(rb2), adata);
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rb1),TRUE);
      }
      else{

         gtk_spin_button_set_value(GTK_SPIN_BUTTON(time_h),time/60);
         gtk_spin_button_set_value(GTK_SPIN_BUTTON(time_m),time%60);
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rb2),TRUE); // active by default
      }

  }

  gtk_window_set_title(window,_("Edit alarm"));
  gtk_widget_show_all(GTK_WIDGET(window));

}


/**
 * Calllback for the remove button in the options
**/
static void remove_clicked(GtkButton *button, gpointer data){

  plugin_data *pd = (plugin_data *)data;

  GtkTreeIter iter,iter_remove;
  GtkTreeSelection *select;
  GtkTreePath *path;
  GtkTreeModel *model;
  gboolean valid;
  gint row_count;

  /* Get the selected row */
  select=gtk_tree_view_get_selection(GTK_TREE_VIEW(pd->tree));

  valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL(pd->list), &iter);

  row_count=0;

  while (valid){

     /* Re-index the other rows */

     /* The selected one is removed, the corresponding menuitem array is also
        updated */
     if(gtk_tree_selection_iter_is_selected(select,&iter)){
       /*g_fprintf(stderr,"\n Removing node %d ...\n", row_count);*/
       iter_remove=iter; /* Mark to be deleted */
       g_array_remove_index(pd->menuarray,row_count);
       if(pd->selected==row_count) /* Update the index of the selected item */
          pd->selected=0; /* If the selected is deleted, new selected one is the first
                one. The first radiomenuitem gets activated anyway */
       else if(pd->selected > row_count)
          pd->selected=pd->selected-1;  /* Those coming after are shifted one behind */
     }
     else{
       /* Save new index on the remaning ones */
       gtk_list_store_set (pd->list, &iter, 0,row_count, -1);
       row_count++;
    }

    valid = gtk_tree_model_iter_next (GTK_TREE_MODEL(pd->list), &iter);

  }

  /* Remove the marked one */
  gtk_list_store_remove (pd->list, &iter_remove);

  /* Update item count and menu */
  pd->count=row_count;

  make_menu(pd);

}

/**
 * Moves an alarm one row up in the list
**/
static void up_clicked(GtkButton *button, gpointer data){

  plugin_data *pd = (plugin_data *)data;

  GtkTreeIter iter,iter_prev;
  GtkTreeSelection *select;
  gboolean valid;
  gint row_count;

  /* Get the selected row */
  select=gtk_tree_view_get_selection(GTK_TREE_VIEW(pd->tree));

  valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL(pd->list), &iter_prev);
  
  /* First item can't be moved up */
  if(!valid || gtk_tree_selection_iter_is_selected(select,&iter_prev))
	return;

  iter = iter_prev;
  valid = gtk_tree_model_iter_next (GTK_TREE_MODEL(pd->list), &iter);
 
  row_count=1;

  while (valid){


     /* The selected one is swapped with the previous one */
     if(gtk_tree_selection_iter_is_selected(select,&iter)){

	   gtk_list_store_swap(pd->list, &iter, &iter_prev);
       if(pd->selected == row_count) /* Update the index of the selected item */
          pd->selected = row_count-1;          
       else if(pd->selected == row_count-1)
          pd->selected = row_count;
       break;
     }

	 iter_prev = iter;
     valid = gtk_tree_model_iter_next (GTK_TREE_MODEL(pd->list), &iter);    
     row_count++;   

  }

  make_menu(pd);

}

/**
 * Moves an alarm one row down in the list
**/
static void down_clicked(GtkButton *button, gpointer data){

  plugin_data *pd = (plugin_data *)data;

  GtkTreeIter iter,iter_next;
  GtkTreeSelection *select;
  gboolean valid;
  gint row_count;

  /* Get the selected row */
  select=gtk_tree_view_get_selection(GTK_TREE_VIEW(pd->tree));

  valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL(pd->list), &iter);

  if(!valid)
	return;

  iter_next = iter;
  valid = gtk_tree_model_iter_next (GTK_TREE_MODEL(pd->list), &iter_next);
 
  row_count=0;

  while (valid){


     /* The selected one is swapped with the next one */
     if(gtk_tree_selection_iter_is_selected(select,&iter)){

	   gtk_list_store_swap(pd->list, &iter, &iter_next);
       if(pd->selected == row_count) /* Update the index of the selected item */
          pd->selected = row_count+1;          
       else if(pd->selected == row_count+1)
          pd->selected = row_count - 1;
       break;
     }

	 iter = iter_next;
     valid = gtk_tree_model_iter_next (GTK_TREE_MODEL(pd->list), &iter_next);    
     row_count++;   

  }

  make_menu(pd);

}

/**
 * Adds the progressbar, taking into account the orientation.
 * pd->pbar is not destroyed, just reparented (saves fraction setting code etc.).
**/
static void add_pbar(XfcePanelPlugin *plugin, plugin_data *pd){

  gtk_widget_hide(GTK_WIDGET(plugin));

  xfce_textdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");

  /* Always true except at initialization */
  if(pd->box){
    g_object_ref(G_OBJECT(pd->pbar));
    gtk_container_remove(GTK_CONTAINER(pd->box),pd->pbar);
    gtk_widget_destroy(pd->box);
  }

  /* vertical bar */
  if(xfce_panel_plugin_get_orientation(plugin)==GTK_ORIENTATION_HORIZONTAL){
    pd->box=gtk_hbox_new(TRUE,0);
#ifdef HAVE_XFCE48    
    gtk_container_add(GTK_CONTAINER(plugin),pd->box);
#else    
    gtk_container_add(GTK_CONTAINER(pd->eventbox),pd->box);
#endif
    gtk_progress_bar_set_orientation    (GTK_PROGRESS_BAR(pd->
                    pbar),GTK_PROGRESS_BOTTOM_TO_TOP);
    gtk_widget_set_size_request(GTK_WIDGET(pd->pbar),PBAR_THICKNESS,0);
    gtk_box_pack_start(GTK_BOX(pd->box),gtk_vseparator_new(),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(pd->box),pd->pbar,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(pd->box),gtk_vseparator_new(),FALSE,FALSE,0);

  }
  else{ /* horizontal bar */
    pd->box=gtk_vbox_new(TRUE,0);
#ifdef HAVE_XFCE48    
    gtk_container_add(GTK_CONTAINER(plugin),pd->box);
#else    
    gtk_container_add(GTK_CONTAINER(plugin),pd->eventbox);
#endif
    gtk_progress_bar_set_orientation    (GTK_PROGRESS_BAR(pd->
                    pbar),GTK_PROGRESS_LEFT_TO_RIGHT);
    gtk_widget_set_size_request(GTK_WIDGET(pd->pbar),0,PBAR_THICKNESS);
    gtk_box_pack_start(GTK_BOX(pd->box),gtk_hseparator_new(),FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(pd->box),pd->pbar,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(pd->box),gtk_hseparator_new(),FALSE,FALSE,0);

  }
  
#ifdef HAVE_XFCE48
  gtk_widget_show_all(GTK_WIDGET(plugin)); 
#else
  gtk_widget_show_all(GTK_WIDGET(pd->eventbox));
#endif
}

/**
 * Callback for orientation change of panel. Just calls add_pbar.
**/
static void orient_change(XfcePanelPlugin *plugin, GtkOrientation orient, plugin_data *pd){

  add_pbar(plugin,pd);
}

/**
 * Loads the list from a keyfile, then saves them in a list_store
**/
static void load_settings(plugin_data *pd)
{

  gchar groupname[8];
  const gchar *timerstring;
  gint groupnum,time;
  gboolean is_cd;
  GtkTreeIter iter;
  XfceRc *rc;

  //if ((file=xfce_panel_plugin_lookup_rc_file (pd->base)) != NULL) {


      if (g_file_test(pd->configfile,G_FILE_TEST_EXISTS)) {

          rc = xfce_rc_simple_open (pd->configfile, TRUE);


          if (rc != NULL)
          {


          groupnum=0;
          g_sprintf(groupname,"G0");


          while(xfce_rc_has_group(rc,groupname)){

                 xfce_rc_set_group(rc,groupname);

             /*g_fprintf(stderr,"\nLoading item %d\n",groupnum);*/
             gtk_list_store_append(pd->list,&iter);

             timerstring=(gchar *)xfce_rc_read_entry(rc,"timername","No name");
             gtk_list_store_set(pd->list,&iter,0,groupnum,1,timerstring,-1);
             /* g_free(timerstring); */ /* Entries read are not freed ! */

             timerstring=(gchar *)xfce_rc_read_entry(rc,"timercommand","");
             gtk_list_store_set(pd->list,&iter,3,timerstring,-1);
             /*g_fprintf(stderr,"\nLoaded timer command ==> %s... with length %d",
                            timerstring,strlen(timerstring));*/
             /*g_free(timerstring);*/

             timerstring=(gchar *)xfce_rc_read_entry(rc,"timerinfo","");
             gtk_list_store_set(pd->list,&iter,2,timerstring,-1);

             /*g_free(timerstring);*/

             is_cd=xfce_rc_read_bool_entry(rc,"is_countdown",TRUE);
             time=xfce_rc_read_int_entry(rc,"time",0);

             gtk_list_store_set(pd->list,&iter,4,is_cd,5,time,-1);

             groupnum++;
             g_snprintf(groupname,5,"G%d",groupnum);

          } /* end of while loop */

          pd->count=groupnum;


          /* Read other options */
          if(xfce_rc_has_group(rc,"others")){
            xfce_rc_set_group(rc,"others");
            pd->nowin_if_alarm= xfce_rc_read_bool_entry
                (rc,"nowin_if_alarm",FALSE);
            pd->repeat_alarm= xfce_rc_read_bool_entry
                (rc,"repeat_alarm",FALSE);
            pd->repetitions= xfce_rc_read_int_entry
                (rc,"repetitions",1);
            pd->repeat_interval= xfce_rc_read_int_entry
                (rc,"repeat_interval",10);

          }

          add_pbar(pd->base,pd);

             xfce_rc_close(rc);
          }

//   }
  }

}


/**
 * Saves the list to a keyfile, backup a permanent copy
**/
static void save_settings(XfcePanelPlugin *plugin, plugin_data *pd){

  gchar *timername,*timercommand,*timerinfo;
  gint time;
  gboolean is_cd;
  gchar settings[1024];
  gchar settingsbak[1024];
  gchar line[1024];
  gchar groupname[8];
  GtkTreeIter iter;
  gboolean valid;
  gint row_count;
  gsize size;

  FILE *conffile;

  XfceRc *rc;
  gchar *file, *contents=NULL;

  if (!(file = xfce_panel_plugin_save_location (plugin, TRUE)))
    return;

  // We do this to start a fresh config file, otherwise if the old config file is longer,
  // the tail will not get truncated. See
  // http://bugzilla.xfce.org/show_bug.cgi?id=2647
  // for a related bug report.
  conffile = fopen(file,"w");
  if (conffile)
    fclose(conffile);

  rc = xfce_rc_simple_open (file, FALSE);

  if (!rc)
    return;



  /*g_fprintf(stderr,"\n Running write\n");*/

  valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL(pd->list), &iter);

  row_count=0;

  while (valid){

      gtk_tree_model_get (GTK_TREE_MODEL(pd->list), &iter,
            1, &timername,
            2, &timerinfo,
            3, &timercommand,
            4, &is_cd,
            5, &time, -1);

      g_snprintf(groupname,7,"G%d",row_count);
      xfce_rc_set_group(rc,groupname);

      xfce_rc_write_entry(rc,"timername",timername);

      xfce_rc_write_int_entry(rc,"time",time);

      xfce_rc_write_entry(rc,"timercommand",timercommand);

      xfce_rc_write_entry(rc,"timerinfo",timerinfo);

      xfce_rc_write_bool_entry(rc,"is_countdown",is_cd);

      g_free(timername);
      g_free(timercommand);
      g_free(timerinfo);

      row_count ++;
      valid = gtk_tree_model_iter_next (GTK_TREE_MODEL(pd->list), &iter);
  }


  /* save the other options */
  xfce_rc_set_group(rc,"others");

  xfce_rc_write_bool_entry(rc,"nowin_if_alarm",pd->nowin_if_alarm);
  xfce_rc_write_bool_entry(rc,"repeat_alarm",pd->repeat_alarm);
  xfce_rc_write_int_entry(rc,"repetitions",pd->repetitions);
  xfce_rc_write_int_entry(rc,"repeat_interval",pd->repeat_interval);

  xfce_rc_close(rc);

  conffile = fopen (pd->configfile,"w");
  /* We backup a permanent copy, which we'll use to load settings */
  if (conffile && g_file_get_contents(file,&contents,NULL,NULL)){
    fputs(contents,conffile);
    fclose(conffile);
  }

  g_free(file);
  if(contents)
    g_free(contents);

}


/**
 * Activates the Edit and Remove buttons when an item in the list is selected
**/

static void tree_selected (GtkTreeSelection *select, gpointer data){

  plugin_data *pd=(plugin_data *)data;

  gtk_widget_set_sensitive(pd->buttonedit,TRUE);
  gtk_widget_set_sensitive(pd->buttonremove,TRUE);
  gtk_widget_set_sensitive(pd->buttonup,TRUE);
  gtk_widget_set_sensitive(pd->buttondown,TRUE);

}


/******************/
/* plugin_free    */
/******************/

static void
plugin_free (XfcePanelPlugin *plugin, plugin_data *pd)
{

  /* remove timeouts */
  if (pd->timeout!=0) g_source_remove(pd->timeout);
  if (pd->repeat_timeout!=0) g_source_remove(pd->repeat_timeout);

  /*save_settings(pd);*/

  if(pd->timer)
    g_timer_destroy(pd->timer);

  if(pd->timeout_command)
    g_free(pd->timeout_command);

  if(pd->configfile)
    g_free(pd->configfile);

  gtk_object_destroy(GTK_OBJECT(pd->tip));

  if(pd->menuarray)
    g_array_free(pd->menuarray,TRUE);

  /* destroy all widgets */
#ifndef HAVE_XFCE48
	gtk_widget_destroy(GTK_WIDGET(pd->eventbox));
#endif  

  /* destroy the tooltips */
  /*gtk_object_destroy(GTK_OBJECT(pd->tip));*/

  /* free the plugin data structure */
  g_free(pd);

  gtk_main_quit();
}

/***************************/
/* options dialog response */
/***************************/
static void
options_dialog_response (GtkWidget *dlg, int reponse, plugin_data *pd)
{
    gtk_widget_destroy (dlg);
    xfce_panel_plugin_unblock_menu (pd->base);
    save_settings(pd->base,pd);
}

/********************************/
/* nowin_if_alarm toggle callback */
/********************************/
static void toggle_nowin_if_alarm(GtkToggleButton *button, gpointer data){

  plugin_data *pd=(plugin_data *)data;

  pd->nowin_if_alarm = gtk_toggle_button_get_active(button);

}

/***************************************/
/* toggle_repeat_alarm toggle callback */
/***************************************/
static void toggle_repeat_alarm(GtkToggleButton *button, gpointer data){

  plugin_data *pd=(plugin_data *)data;

  pd->repeat_alarm = gtk_toggle_button_get_active(button);
  gtk_widget_set_sensitive(GTK_WIDGET(pd->spin_interval), pd->repeat_alarm);
  gtk_widget_set_sensitive(GTK_WIDGET(pd->spin_repeat), pd->repeat_alarm);

  gtk_widget_set_sensitive(pd->repeat_alarm_box,pd->repeat_alarm);

}

/***************************************/
/* Spinbutton 1 (#of alarm repetitions */
/* value change callback               */
/***************************************/
static void spin1_changed(GtkSpinButton *button, gpointer data){

  plugin_data *pd=(plugin_data *)data;

  pd->repetitions = gtk_spin_button_get_value_as_int(button);

}

/***************************************/
/* Spinbutton 1 (alarm repetition      */
/* interval) value change callback     */
/***************************************/
static void spin2_changed(GtkSpinButton *button, gpointer data){

  plugin_data *pd=(plugin_data *)data;

  pd->repeat_interval = gtk_spin_button_get_value_as_int(button);

}

/******************/
/* options dialog */
/******************/
static void plugin_create_options (XfcePanelPlugin *plugin,plugin_data *pd) {

  GtkWidget *vbox=gtk_vbox_new(FALSE,0); /*outermost box */
  GtkWidget *hbox=gtk_hbox_new(FALSE,0); /* holds the treeview and buttons */
  GtkWidget *buttonbox,*button,*sw,*tree,*spinbutton;
  GtkTreeSelection *select;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkTreeIter iter;

  GtkWidget *dlg, *header;


  xfce_panel_plugin_block_menu (plugin);

  dlg = gtk_dialog_new_with_buttons (_("Xfce 4 Timer Plugin"),
              GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (plugin))),
              GTK_DIALOG_DESTROY_WITH_PARENT |
              GTK_DIALOG_NO_SEPARATOR,
              GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
              NULL);

  g_signal_connect (dlg, "response", G_CALLBACK (options_dialog_response),
                    pd);

  gtk_container_set_border_width (GTK_CONTAINER(dlg), BORDER);

  header = xfce_create_header (NULL, _("Xfce4 Timer Options"));
  gtk_widget_set_size_request (GTK_BIN (header)->child, 200, 32);
  //gtk_container_set_border_width (GTK_CONTAINER (header), 6);
  gtk_widget_show (header);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), header,
                      FALSE, TRUE, 0);

  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), vbox,
                      TRUE, TRUE, WIDGET_SPACING);


  hbox = gtk_hbox_new(FALSE, BORDER);
  gtk_box_pack_start(GTK_BOX(vbox),hbox,TRUE,TRUE,BORDER);

  sw = gtk_scrolled_window_new (NULL, NULL);

  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
                                           GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                 GTK_POLICY_AUTOMATIC,
                 GTK_POLICY_AUTOMATIC);

  gtk_box_pack_start(GTK_BOX(hbox),sw,TRUE,TRUE,0);

  tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(pd->list));
  pd->tree=tree;
  gtk_tree_view_set_rules_hint  (GTK_TREE_VIEW (tree), TRUE);
  gtk_tree_selection_set_mode   (gtk_tree_view_get_selection (GTK_TREE_VIEW (tree)),
                 GTK_SELECTION_SINGLE);

  renderer = gtk_cell_renderer_text_new ();

  column = gtk_tree_view_column_new_with_attributes (_("Timer name"), renderer,
                            "text", 1, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);


  column = gtk_tree_view_column_new_with_attributes (_("Countdown period /\nAlarm time"),
                            renderer, "text", 2, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);

  column = gtk_tree_view_column_new_with_attributes (_("Alarm command"), renderer,
                            "text", 3, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);


  if(tree)
     gtk_container_add(GTK_CONTAINER(sw),tree);
  else
     g_fprintf(stderr,"\n pd->tree is NULL\n");

  gtk_widget_set_size_request(GTK_WIDGET(sw),350,250);

  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (pd->tree));
  gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
  g_signal_connect  (G_OBJECT (select), "changed",
            G_CALLBACK(tree_selected), pd);


  buttonbox=gtk_vbutton_box_new();
  gtk_button_box_set_layout(GTK_BUTTON_BOX(buttonbox),GTK_BUTTONBOX_START);
  gtk_button_box_set_spacing(GTK_BUTTON_BOX(buttonbox),WIDGET_SPACING<<1);
  gtk_box_pack_start(GTK_BOX(hbox),buttonbox,FALSE,FALSE,0);

  button = gtk_button_new_from_stock (GTK_STOCK_ADD);
  pd->buttonadd=button;
  gtk_box_pack_start(GTK_BOX (buttonbox), button, FALSE, FALSE,WIDGET_SPACING<<1);
  gtk_widget_set_sensitive(button,TRUE);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK(add_edit_clicked), pd);


  button = gtk_button_new_from_stock (GTK_STOCK_EDIT);
  pd->buttonedit=button;
  gtk_box_pack_start(GTK_BOX (buttonbox), button, FALSE, FALSE,WIDGET_SPACING<<1);
  gtk_widget_set_sensitive(button,FALSE);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK(add_edit_clicked), pd);

  button = gtk_button_new_from_stock (GTK_STOCK_REMOVE);
  pd->buttonremove=button;
  gtk_box_pack_start(GTK_BOX (buttonbox), button, FALSE, FALSE,WIDGET_SPACING);
  gtk_widget_set_sensitive(button,FALSE);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK(remove_clicked), pd);

  button = gtk_button_new_from_stock (GTK_STOCK_GO_UP);
  pd->buttonup=button;
  gtk_box_pack_start(GTK_BOX (buttonbox), button, FALSE, FALSE,WIDGET_SPACING);
  gtk_widget_set_sensitive(button,FALSE);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK(up_clicked), pd);
  
  button = gtk_button_new_from_stock (GTK_STOCK_GO_DOWN);
  pd->buttondown=button;
  gtk_box_pack_start(GTK_BOX (buttonbox), button, FALSE, FALSE,WIDGET_SPACING);
  gtk_widget_set_sensitive(button,FALSE);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK(down_clicked), pd);
  
  gtk_box_pack_start(GTK_BOX(vbox),gtk_hseparator_new(),FALSE,FALSE,BORDER);

  button=gtk_check_button_new_with_label(_("Don't display a warning  if an alarm command is set"));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),pd->nowin_if_alarm);
  g_signal_connect(G_OBJECT(button),"toggled",G_CALLBACK(toggle_nowin_if_alarm),pd);
  gtk_box_pack_start(GTK_BOX(vbox),button,FALSE,FALSE,WIDGET_SPACING);



  button=gtk_check_button_new_with_label(_("Repeat the alarm command:"));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),pd->repeat_alarm);
  g_signal_connect(G_OBJECT(button),"toggled",G_CALLBACK(toggle_repeat_alarm),pd);
  gtk_box_pack_start(GTK_BOX(vbox),button,FALSE,FALSE,WIDGET_SPACING);

  hbox = gtk_hbox_new (FALSE,0);
  pd->repeat_alarm_box=hbox;
  gtk_box_pack_start(GTK_BOX(hbox),gtk_label_new(_("Number of repetitions")),FALSE,FALSE,0);
  spinbutton=gtk_spin_button_new_with_range(1,50,1);
  pd->spin_repeat=spinbutton;
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinbutton), pd->repetitions);
  g_signal_connect(G_OBJECT(spinbutton),"value-changed", G_CALLBACK(spin1_changed), pd);
  gtk_box_pack_start(GTK_BOX(hbox),spinbutton,FALSE,FALSE,0);
  gtk_box_pack_start(GTK_BOX(hbox),gtk_label_new(_("  Time interval (sec.)")),FALSE,FALSE,0);
  spinbutton=gtk_spin_button_new_with_range(1,600,1);
  pd->spin_interval=spinbutton;
  gtk_box_pack_start(GTK_BOX(hbox),spinbutton,FALSE,FALSE,0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinbutton), pd->repeat_interval);
  g_signal_connect(G_OBJECT(spinbutton),"value-changed", G_CALLBACK(spin2_changed), pd);

  gtk_box_pack_start(GTK_BOX(vbox),hbox,FALSE,FALSE,WIDGET_SPACING);
  gtk_widget_set_sensitive(hbox,pd->repeat_alarm);



  gtk_widget_show_all(GTK_WIDGET(dlg));

}


/**
 * create_sample_control
 *
 * Create a new instance of the plugin.
 *
 * @control : #Control parent container
 *
 * Returns %TRUE on success, %FALSE on failure.
 **/
static void
create_plugin_control (XfcePanelPlugin *plugin)
{

  GtkWidget *base,*menu,*socket,*menuitem,*box,*pbar2;
  GtkTooltips *tooltip;
  char command[1024];
  gchar *filename,*pathname;



  plugin_data *pd=g_new(plugin_data,1);

  pd->base=plugin;
  pd->count=0;
  pd->pbar=gtk_progress_bar_new();
  pd->list=gtk_list_store_new(6,
         G_TYPE_INT,     /* Column 0: Index */
         G_TYPE_STRING,  /* Column 1: Name */
         G_TYPE_STRING,  /* Column 2: Timer period/alarm time */
         G_TYPE_STRING,  /* Command to run */
         G_TYPE_BOOLEAN, /* TRUE= Is countdown, i.e. h-m-s format.
                    FALSE= 24h format */
         G_TYPE_INT);    /* Timer period in seconds if countdown.
                    Alarm time in minutes if 24h format is used,
                    (i.e. 60 x Hr + Min) */
#ifndef HAVE_XFCE48
  pd->eventbox = gtk_event_box_new();
#endif  
  pd->box=NULL;
  pd->timer_on=FALSE;
  pd->timeout=0;
  pd->buttonadd=NULL;
  pd->buttonedit=NULL;
  pd->buttonremove=NULL;
  pd->menu=NULL;
  pd->menuarray=NULL;
  pd->selected=0;
  pd->tip=gtk_tooltips_new();
  pd->timeout_command=NULL;
  pd->timer=NULL;
  pd->nowin_if_alarm=FALSE;
  pd->repeat_alarm=FALSE;
  pd->is_paused=FALSE;
  pd->is_countdown=TRUE;
  pd->repeat_alarm_box=NULL;
  pd->repetitions=1;
  pd->rem_repetitions=1;
  pd->repeat_interval=10;
  pd->alarm_repeating=FALSE;
  pd->repeat_timeout=0;

  gtk_tooltips_set_tip(pd->tip, GTK_WIDGET(plugin), "", NULL);
  gtk_tooltips_disable(pd->tip);


  g_object_ref(pd->list);

  filename = xfce_panel_plugin_save_location (pd->base,TRUE);
  pathname = g_path_get_dirname (filename);
  pd->configfile = g_strconcat (pathname,"/XfceTimer.rc",NULL);
  g_free(filename);
  g_free(pathname);

  load_settings(pd);

  make_menu(pd);
                      
  gtk_progress_bar_set_bar_style    (GTK_PROGRESS_BAR(pd->pbar),
                    GTK_PROGRESS_CONTINUOUS);
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pd->pbar),0);

  add_pbar(pd->base,pd);

  /* Trying to get a thin box, but no way */
  gtk_widget_set_size_request(GTK_WIDGET(plugin),10,10);
  xfce_panel_plugin_set_expand(plugin,FALSE);

#ifdef HAVE_XFCE48
  g_signal_connect  (G_OBJECT(plugin), "button_press_event",
            G_CALLBACK(pbar_clicked), pd);      
#else
  gtk_container_add(GTK_CONTAINER(plugin),pd->eventbox);
  g_signal_connect  (G_OBJECT(pd->eventbox), "button_press_event",
            G_CALLBACK(pbar_clicked), pd);
#endif  

  gtk_widget_show_all(GTK_WIDGET(plugin));

  g_signal_connect (plugin, "free-data",
                      G_CALLBACK (plugin_free), pd);

  g_signal_connect (plugin, "save",
                      G_CALLBACK (save_settings), pd);

  g_signal_connect (plugin, "orientation-changed",
                      G_CALLBACK (orient_change), pd);

  xfce_panel_plugin_menu_show_configure (plugin);
  g_signal_connect (plugin, "configure-plugin",
                      G_CALLBACK (plugin_create_options), pd);
}


