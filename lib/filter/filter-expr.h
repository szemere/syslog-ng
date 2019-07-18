/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#ifndef FILTER_H_INCLUDED
#define FILTER_H_INCLUDED

#include "syslog-ng.h"
#include "logpipe.h"
#include "stats/stats-registry.h"

struct _GlobalConfig;
typedef struct _FilterExprNode FilterExprNode;

typedef void (*FilterExprNodeTraversalCallbackFunction)(FilterExprNode *current, FilterExprNode *parent,
                                                        GPtrArray *childs, gpointer cookie);

struct _FilterExprNode
{
  guint32 ref_cnt;
  guint32 comp:1,   /* this not is negated */
          modify:1; /* this filter changes the log message */
  gchar *type;
  gchar *pattern;
  gchar *template;
  gboolean (*init)(FilterExprNode *self, GlobalConfig *cfg);
  gboolean (*eval)(FilterExprNode *self, LogMessage **msg, gint num_msg);
  void (*traversal)(FilterExprNode *self, FilterExprNode *parent, FilterExprNodeTraversalCallbackFunction func,
                    gpointer cookie);
  void (*replace_child)(FilterExprNode *self, FilterExprNode *old, FilterExprNode *new);
  void (*free_fn)(FilterExprNode *self);
  StatsCounterItem *matched;
  StatsCounterItem *not_matched;
};


static inline void
filter_expr_replace_child(FilterExprNode *self, FilterExprNode *old, FilterExprNode *new)
{
  g_assert(self->replace_child);
  if (self->replace_child)
    self->replace_child(self, old, new);
}

static inline void
filter_expr_traversal(FilterExprNode *self, FilterExprNode *parent, FilterExprNodeTraversalCallbackFunction func,
                      gpointer cookie)
{
  if (self->traversal)
    self->traversal(self, parent, func, cookie);
  else
    func(self, parent, NULL, cookie); // If it do not have a traversal function, than it is a "simple_expr" == leaf element
}

static inline gboolean
filter_expr_init(FilterExprNode *self, GlobalConfig *cfg)
{
  if (self->init)
    return self->init(self, cfg);

  return TRUE;
}

gboolean filter_expr_eval(FilterExprNode *self, LogMessage *msg);
gboolean filter_expr_eval_with_context(FilterExprNode *self, LogMessage **msgs, gint num_msg);
gboolean filter_expr_eval_root(FilterExprNode *self, LogMessage **msg, const LogPathOptions *path_options);
gboolean filter_expr_eval_root_with_context(FilterExprNode *self, LogMessage **msgs, gint num_msg,
                                            const LogPathOptions *path_options);
void filter_expr_node_init_instance(FilterExprNode *self);
FilterExprNode *filter_expr_ref(FilterExprNode *self);
void filter_expr_unref(FilterExprNode *self);

#endif
