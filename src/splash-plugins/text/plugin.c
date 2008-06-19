/* text.c - boot splash plugin
 *
 * Copyright (C) 2008 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by: Ray Strode <rstrode@redhat.com>
 */
#include "config.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <values.h>
#include <wchar.h>

#include "ply-answer.h"
#include "ply-boot-splash-plugin.h"
#include "ply-buffer.h"
#include "ply-event-loop.h"
#include "ply-list.h"
#include "ply-logger.h"
#include "ply-frame-buffer.h"
#include "ply-image.h"
#include "ply-utils.h"
#include "ply-window.h"

#include <linux/kd.h>

#define CLEAR_LINE_SEQUENCE "\033[2K\r\n"
#define CLEAR_SCREEN_SEQUENCE "\033[2J"
#define HIDE_CURSOR_SEQUENCE "\033[?25l"
#define SHOW_CURSOR_SEQUENCE "\033[?25h"
#define BACKSPACE "\b\033[0K"

struct _ply_boot_splash_plugin
{
  ply_event_loop_t *loop;

  ply_answer_t *pending_password_answer;

  uint32_t keyboard_input_is_hidden : 1;
};

ply_boot_splash_plugin_t *
create_plugin (void)
{
  ply_boot_splash_plugin_t *plugin;

  ply_trace ("creating plugin");

  plugin = calloc (1, sizeof (ply_boot_splash_plugin_t));

  return plugin;
}

void
destroy_plugin (ply_boot_splash_plugin_t *plugin)
{
  ply_trace ("destroying plugin");

  if (plugin == NULL)
    return;

  free (plugin);
}

static void
detach_from_event_loop (ply_boot_splash_plugin_t *plugin)
{
  plugin->loop = NULL;

  ply_trace ("detaching from event loop");
}

void
on_keyboard_input (ply_boot_splash_plugin_t *plugin,
                   const char               *keyboard_input,
                   size_t                    character_size)
{
  if (plugin->keyboard_input_is_hidden)
    write (STDOUT_FILENO, "•", strlen ("•"));
  else
    write (STDOUT_FILENO, keyboard_input, character_size);
}

void
on_backspace (ply_boot_splash_plugin_t *plugin)
{
  write (STDOUT_FILENO, BACKSPACE, strlen (BACKSPACE));
}

void
on_enter (ply_boot_splash_plugin_t *plugin,
          const char               *line)
{
  if (plugin->pending_password_answer != NULL)
    {
      ply_answer_with_string (plugin->pending_password_answer, line);
      plugin->keyboard_input_is_hidden = false;
      plugin->pending_password_answer = NULL;
      write (STDOUT_FILENO, CLEAR_LINE_SEQUENCE, strlen (CLEAR_LINE_SEQUENCE));
    }
}

static void
clear_screen (ply_boot_splash_plugin_t *plugin)
{
  write (STDOUT_FILENO, CLEAR_SCREEN_SEQUENCE, strlen (CLEAR_SCREEN_SEQUENCE));
}

static void
hide_cursor (ply_boot_splash_plugin_t *plugin)
{
  write (STDOUT_FILENO, HIDE_CURSOR_SEQUENCE, strlen (HIDE_CURSOR_SEQUENCE));
}

static void
show_cursor (ply_boot_splash_plugin_t *plugin)
{
  write (STDOUT_FILENO, SHOW_CURSOR_SEQUENCE, strlen (SHOW_CURSOR_SEQUENCE));
}

bool
show_splash_screen (ply_boot_splash_plugin_t *plugin,
                    ply_event_loop_t         *loop,
                    ply_window_t             *window,
                    ply_buffer_t             *boot_buffer)
{
  assert (plugin != NULL);

  ply_window_set_keyboard_input_handler (window,
                                         (ply_window_keyboard_input_handler_t)
                                         on_keyboard_input, plugin);
  ply_window_set_backspace_handler (window,
                                    (ply_window_backspace_handler_t)
                                    on_backspace, plugin);
  ply_window_set_enter_handler (window,
                                (ply_window_enter_handler_t)
                                on_enter, plugin);

  plugin->loop = loop;
  ply_event_loop_watch_for_exit (loop, (ply_event_loop_exit_handler_t)
                                 detach_from_event_loop,
                                 plugin);

  clear_screen (plugin);
  hide_cursor (plugin);

  return true;
}

void
update_status (ply_boot_splash_plugin_t *plugin,
               const char               *status)
{
  assert (plugin != NULL);

  ply_trace ("status update");
  write (1, ".", 1);
}

void
hide_splash_screen (ply_boot_splash_plugin_t *plugin,
                    ply_event_loop_t         *loop,
                    ply_window_t             *window)
{
  assert (plugin != NULL);

  ply_trace ("hiding splash screen");

  ply_window_set_keyboard_input_handler (window, NULL, NULL);
  ply_window_set_backspace_handler (window, NULL, NULL);
  ply_window_set_enter_handler (window, NULL, NULL);

  if (plugin->loop != NULL)
    {
      ply_event_loop_stop_watching_for_exit (plugin->loop,
                                             (ply_event_loop_exit_handler_t)
                                             detach_from_event_loop,
                                             plugin);
      detach_from_event_loop (plugin);
    }

  clear_screen (plugin);
  show_cursor (plugin);
}

void
ask_for_password (ply_boot_splash_plugin_t *plugin,
                  ply_answer_t             *answer)
{
  plugin->pending_password_answer = answer;

  write (STDOUT_FILENO, "\nPassword: ", strlen ("\nPassword: "));
  plugin->keyboard_input_is_hidden = true;
}

ply_boot_splash_plugin_interface_t *
ply_boot_splash_plugin_get_interface (void)
{
  static ply_boot_splash_plugin_interface_t plugin_interface =
    {
      .create_plugin = create_plugin,
      .destroy_plugin = destroy_plugin,
      .show_splash_screen = show_splash_screen,
      .update_status = update_status,
      .hide_splash_screen = hide_splash_screen,
      .ask_for_password = ask_for_password,
    };

  return &plugin_interface;
}

/* vim: set ts=4 sw=4 expandtab autoindent cindent cino={.5s,(0: */
