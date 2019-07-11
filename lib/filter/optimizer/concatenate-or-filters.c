#include "filter/optimizer/concatenate-or-filters.h"
#include "filter/filter-expr-parser.h"
#include <stdio.h>


static FilterExprNode* _compile_standalone_filter(gchar *config_snippet)
{
  GlobalConfig *cfg = cfg_new_snippet();
  CfgLexer *lexer = cfg_lexer_new_buffer(cfg, config_snippet, strlen(config_snippet));
  FilterExprNode *tmp;
  g_assert(cfg_run_parser(cfg, lexer, &filter_expr_parser, (gpointer *) &tmp, NULL));

  return tmp;
}

static FilterExprNode*
_concatenate(FilterExprNode *current, FilterExprNode *parent, FilterExprNode *left, FilterExprNode *right)
{
  GString *new_filter = g_string_new("");

  g_string_printf(new_filter, "%smatch(\"%s|%s\" value('PROGRAM'));", (left->comp ? "not " : ""), left->pattern, right->pattern);

  FilterExprNode *new_opt = _compile_standalone_filter(new_filter->str);
  filter_expr_replace_child(parent, current, new_opt);
  return new_opt;
}




static gboolean
_can_we_concatenate(FilterExprNode *current, FilterExprNode *left, FilterExprNode *right)
{
  // Is it an OR filter
  if (strcmp(current->type, "OR") != 0)
    return FALSE;

  if (left->comp != right->comp)
    return FALSE;

  if (strcmp(left->type, right->type) !=0 )
    return FALSE;

  //Glob is not trivial to convert to regex, nor scope
  if (strcmp(left->type, "glob") == 0)
    return FALSE;

  if (strcmp(left->template, right->template) !=0 )
    return FALSE;

  return TRUE;
}



static gpointer
_concatenate_or_filters_init(FilterExprNode *root)
{
  GList **stack = g_new0(GList*, 1);

  return stack;
}

static void
_concatenate_or_filters_deinit(gpointer cookie)
{
  GList **stack = (GList**)cookie;
  g_free(stack);
}

static void
_concatenate_or_filters_cb(FilterExprNode *current, FilterExprNode *parent, GPtrArray *childs, gpointer cookie)
{
  GList **stack = (GList**)cookie;

  FilterExprNode *left = NULL;
  FilterExprNode *right = NULL;
  if (strcmp(current->type, "OR") == 0)
    {
      left = (FilterExprNode *)g_list_last(*stack)->data;
      right = (FilterExprNode *)g_list_last(*stack)->prev->data;
    }

  if (_can_we_concatenate(current, left, right))
    {
      *stack = g_list_append(*stack, _concatenate(current, parent, left, right));
    }
  else
    {
      *stack = g_list_append(*stack, current);
    }
}

FilterExprOptimizer concatenate_or_filters = {
  .name = "concatenate-or-filters",
  .init = _concatenate_or_filters_init,
  .deinit = _concatenate_or_filters_deinit,
  .cb = _concatenate_or_filters_cb
};
