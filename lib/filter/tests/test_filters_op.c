/*
 * Copyright (c) 2018 Balabit
 * Copyright (c) 2018 Kokan
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
#include "filter/filter-op.h"
#include "filter/filter-expr.h"
#include "apphook.h"

#include <criterion/criterion.h>


typedef struct _FilterParams
{
  const gchar *config_snippet;
  const gboolean expected_result;
} FilterParams;

ParameterizedTestParameters(filter, test_or_evaluation)
{
  static FilterParams test_data_list[] =
  {
    {.config_snippet = "     facility(2) or     facility(2)", .expected_result = TRUE },
    {.config_snippet = "     facility(2) or not facility(2)", .expected_result = TRUE },
    {.config_snippet = "not  facility(2) or     facility(2)", .expected_result = TRUE },
    {.config_snippet = "not  facility(2) or not facility(2)", .expected_result = FALSE },

    {.config_snippet = "not (    facility(2) or     facility(2))", .expected_result = FALSE},
    {.config_snippet = "not (    facility(2) or not facility(2))", .expected_result = FALSE},
    {.config_snippet = "not (not facility(2) or     facility(2))", .expected_result = FALSE},
    {.config_snippet = "not (    facility(2) or facility(2))", .expected_result = FALSE},
    {.config_snippet = "not (facility(2) or facility(2))", .expected_result = FALSE},
    {.config_snippet = "not (facility(2) or facility(2))", .expected_result = FALSE},
    {.config_snippet =, .expected_result = },
    {.config_snippet =, .expected_result = },
    {.config_snippet =, .expected_result = },
    {.config_snippet =, .expected_result = },
    {.config_snippet =, .expected_result = },
  };

  return cr_make_param_array(FilterParams, test_data_list, G_N_ELEMNTS(test_data_list));
}

ParametrizedTest(FilterParams *params, filter, test_or_evaluation)
{
  // cfg_parser_parse
  // filter->eval == params->expected_result
}


static void
setup(void)
{
  app_startup();
  configuration = cfg_new_snippet();
}

static void
teardown(void)
{
  app_shutdown();
  cfg_free(configuration);
}

TestSuite(filter_op, .init = setup, .fini = teardown);
