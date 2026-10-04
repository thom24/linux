// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB

#include "act.h"
#include "en/tc_priv.h"

static int
tc_act_parse_skbedit_prio(struct mlx5e_tc_act_parse_state *parse_state,
			  const struct flow_action_entry *act,
			  struct mlx5e_priv *priv,
			  struct mlx5_flow_attr *attr)
{
	/* The eswitch does not apply skb->priority to flowtable flows. */
	if (mlx5e_is_ft_flow(parse_state->flow))
		return 0;

	NL_SET_ERR_MSG_MOD(parse_state->extack, "skbedit priority is not supported");
	return -EOPNOTSUPP;
}

struct mlx5e_tc_act mlx5e_tc_act_skbedit_prio = {
	.parse_action = tc_act_parse_skbedit_prio,
};
