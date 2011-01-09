/*
** Copyright (C) 2011 Dirk-Jan C. Binnema <djcb@djcbsoftware.nl>
**
** This program is free software; you can redistribute it and/or modify it
** under the terms of the GNU General Public License as published by the
** Free Software Foundation; either version 3, or (at your option) any
** later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation,
** Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
**
*/

#include "mu-msg-view.h"
#include "mu-msg-body-view.h"
#include "mu-msg-attach-view.h"
#include "mu-msg.h"

/* 'private'/'protected' functions */
static void mu_msg_view_class_init (MuMsgViewClass *klass);
static void mu_msg_view_init       (MuMsgView *obj);
static void mu_msg_view_finalize   (GObject *obj);

/* list my signals  */
enum {
	/* MY_SIGNAL_1, */
	/* MY_SIGNAL_2, */
	LAST_SIGNAL
};

struct _MuMsgViewPrivate {
	GtkWidget *_body;
	GtkWidget *_attach, *_attacharea;
};
#define MU_MSG_VIEW_GET_PRIVATE(o)      (G_TYPE_INSTANCE_GET_PRIVATE((o), \
                                         MU_TYPE_MSG_VIEW, \
                                         MuMsgViewPrivate))
/* globals */
static GtkVBoxClass *parent_class = NULL;

/* uncomment the following if you have defined any signals */
/* static guint signals[LAST_SIGNAL] = {0}; */

G_DEFINE_TYPE (MuMsgView, mu_msg_view, GTK_TYPE_VBOX);

static void
mu_msg_view_class_init (MuMsgViewClass *klass)
{
	GObjectClass *gobject_class;
	gobject_class = (GObjectClass*) klass;

	parent_class            = g_type_class_peek_parent (klass);
	gobject_class->finalize = mu_msg_view_finalize;

	g_type_class_add_private (gobject_class, sizeof(MuMsgViewPrivate));

	/* signal definitions go here, e.g.: */
/* 	signals[MY_SIGNAL_1] = */
/* 		g_signal_new ("my_signal_1",....); */
/* 	signals[MY_SIGNAL_2] = */
/* 		g_signal_new ("my_signal_2",....); */
/* 	etc. */
}

static void
on_attach_activated (MuMsgView *self, guint partnum, MuMsg *msg)
{
	char *tmpdir;
	
	tmpdir = mu_util_create_tmpdir ();
	if (!tmpdir)
		return;
	
	mu_msg_mime_part_save (msg, partnum, tmpdir, FALSE, TRUE);
	g_free (tmpdir);
}


static void
mu_msg_view_init (MuMsgView *obj)
{
	GtkWidget *scrolledwin;
	
	obj->_priv = MU_MSG_VIEW_GET_PRIVATE(obj);
	
	obj->_priv->_body = mu_msg_body_view_new ();
	scrolledwin = gtk_scrolled_window_new (NULL, NULL);
	gtk_container_add (GTK_CONTAINER(scrolledwin),
			   obj->_priv->_body);
	gtk_box_pack_start (GTK_BOX(obj), scrolledwin,
			    TRUE, TRUE, 2);

	obj->_priv->_attacharea = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(obj->_priv->_attacharea),
					GTK_POLICY_NEVER,
					GTK_POLICY_AUTOMATIC);
	obj->_priv->_attach = mu_msg_attach_view_new ();
	gtk_container_add (GTK_CONTAINER(obj->_priv->_attacharea), obj->_priv->_attach);	

	g_signal_connect (obj->_priv->_attach, "attach-activated",
			  G_CALLBACK(on_attach_activated),
			  obj);
}

static void
mu_msg_view_finalize (GObject *obj)
{
/* 	free/unref instance resources here */
	G_OBJECT_CLASS(parent_class)->finalize (obj);
}

GtkWidget*
mu_msg_view_new (void)
{
	return GTK_WIDGET(g_object_new(MU_TYPE_MSG_VIEW, NULL));
}


static void
add_attachment_area_maybe (MuMsgView *self, MuMsg *msg)
{
	gint attach_num;
	GList *children, *cur;
	gboolean has_area;
	
	has_area = FALSE;
	cur = children = gtk_container_get_children (GTK_CONTAINER(self));
	while (cur) {
		if (cur->data == self->_priv->_attacharea) {
			has_area = TRUE;
			break;
		}
		cur = g_list_next (cur);
	}
	g_list_free (children);
	
	attach_num = 0;
	if (msg)
		attach_num = mu_msg_attach_view_set_message
			(MU_MSG_ATTACH_VIEW(self->_priv->_attach), msg);
	
	if (attach_num < 1 && has_area) {
		g_object_ref (self->_priv->_attacharea);
		gtk_container_remove (GTK_CONTAINER(self),
				      self->_priv->_attacharea);
	} else if (attach_num >= 1 && !has_area) {
		gtk_box_pack_start (GTK_BOX(self), self->_priv->_attacharea,
				    FALSE, FALSE, 0);
		gtk_widget_show_all (self->_priv->_attacharea);
	}
}


void
mu_msg_view_set_message (MuMsgView *self, MuMsg *msg)
{
	const char *data;
	
	g_return_if_fail (MU_IS_MSG_VIEW(self));
	
	data = msg ? mu_msg_get_body_html (msg) : "";
	if (data) 
		mu_msg_body_view_set_html (MU_MSG_BODY_VIEW(self->_priv->_body),
					   data);
	else
		mu_msg_body_view_set_text (MU_MSG_BODY_VIEW(self->_priv->_body),
					   mu_msg_get_body_text (msg));
	
	add_attachment_area_maybe (self, msg);
}

