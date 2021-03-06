/* Copyright (c) 2017 Kewin Rausch
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Empower Agent simulator wrapper around the eNB agent.
 */

#include <inttypes.h>

#include "emsim.h"

#define LOG_WRAP(x, ...) 	LOG_TRACE(x, ##__VA_ARGS__)

/******************************************************************************
 * Globals used all around the simulator:                                     *
 ******************************************************************************/

u32 sim_UE_rep_trigger = 0;
u32 sim_UE_rep_mod     = 0;

u32 sim_cell_stats_trigger = 0;
u32 sim_cell_stat_mod      = 0;

/******************************************************************************
 * Callback implementation.                                                   *
 ******************************************************************************/

int wrap_init()
{
	/*
	 * Add here your custom interaction mechanism with the LTE stack.
	 */

	return 0;
}

int wrap_release()
{
	/*
	 * Add here your custom interaction mechanism with the LTE stack.
	 */

	return 0;
}

int wrap_cell_stats(
	EmageMsg * request, EmageMsg ** reply, unsigned int trigger_id)
{
	if(!request ||
		!request->te ||
		!request->te->mcell_stats ||
		!request->te->mcell_stats->req) {

		LOG_WRAP("Malformed Cell statistic request received!\n");
		return 0;
	}

	LOG_WRAP("Cell statistic request %"PRIu64" received.\n",
		request->te->mcell_stats->req->cell_stats_types);

	switch(request->te->mcell_stats->req->cell_stats_types) {
	case CELL_STATS_TYPES__CELLSTT_PRB_UTILIZATION:
		LOG_WRAP("PRB report on trigger %d.\n", trigger_id);
		sim_cell_stat_mod      = request->head->t_id;
		sim_cell_stats_trigger = trigger_id;

		/* Force the first report between eNB and the controller. */
		sim_phy_dirty = 1;
		break;
	}

	return 0;
}

int wrap_rrc_meas(
	EmageMsg * request, EmageMsg ** reply, unsigned int trigger_id)
{
	int i;
	int j;
	int f = -1; /* First measurement slot which is free. */
	int u = -1;
	int m = -1;

	LOG_WRAP("RRC Measurement request received.\n");

	if(!request ||
		!request->te ||
		!request->te->mrrc_meas ||
		!request->te->mrrc_meas->req) {

		LOG_WRAP("Malformed RRC measurement request message!\n");
		return 0;
	}

	/* Pick the right UE and the right measurement indexes. */
	for(i = 0; i < UE_MAX; i++) {
		if(sim_ues[i].rnti == request->te->mrrc_meas->req->rnti) {
			u = i;

			for(j = 0; j < UE_RRCM_MAX; j++) {
				/* Find the first empty slot, just in case. */
				if(f < 0 && sim_ues[i].meas[j].id ==
					UE_RRCM_INVALID) {

					f = j;
				}

				if(sim_ues[i].meas[j].earfcn ==
					request->te->mrrc_meas->req->m_obj->
						measobj_eutra->carrier_freq) {

					m = j;
					break;
				}
			}

			break;
		}
	}

	/* RNTI not found! */
	if(u == -1) {
		LOG_WRAP("RNTI %x not found.\n",
			request->te->mrrc_meas->req->rnti);

		/* Build up a negative response and let the agent send it. */
		msg_RRC_meas_fail(sim_ID, request->head->t_id, reply);
		return 0;
	}

	/* Measurement profile found! */
	if(m != -1) {
		/* Can't delete measurement id 1; it's the default one. */
		if(request->te->action == EVENT_ACTION__EA_DEL) {
			LOG_WRAP("UE measurement for %x (%d) deleted.\n",
				sim_ues[i].rnti, sim_ues[i].meas[f].earfcn);

			/* Free this slot. */
			sim_ues[i].meas[m].id      = 0;
			/* Disable it. */
			sim_ues[i].meas[m].trigger = 0;
			sim_ues[i].meas[m].mod_id  = 0;
		}
		/* UE measurement is there, just attach a trigger. */
		else {
			/* Trigger is now enabled! */
			sim_ues[i].meas[m].trigger= trigger_id;
			sim_ues[i].meas[m].mod_id = request->head->t_id;

			/* Force the first report. */
			sim_ues[i].meas[m].dirty = 1;
		}
	}
	/* Measurement profile not found; we need to allocate one. */
	else {
		/* No more measurements slot available! */
		if(f < 0) {
			LOG_WRAP("No more measurement slots available for UE "
				"%x\n",
				sim_ues[i].rnti);

			return msg_RRC_meas_fail(
				sim_ID, request->head->t_id, reply);
		}

		/* Assign the measurement id to this slot. */
		sim_ues[i].meas[f].id = request->te->mrrc_meas->req->measid;
		/* Ok, enable this measurement to report. */
		sim_ues[i].meas[f].trigger= trigger_id;
		sim_ues[i].meas[m].mod_id = request->head->t_id;

		if(request->te->mrrc_meas->req->m_obj &&
			request->te->mrrc_meas->req->m_obj->measobj_eutra) {

			sim_ues[i].meas[f].earfcn =
				request->te->mrrc_meas->req->m_obj->
					measobj_eutra->carrier_freq;
		}

		sim_ues[i].meas[f].rs.rsrp = PHY_RSRP_LOWER;
		sim_ues[i].meas[f].rs.rsrq = PHY_RSRQ_LOWER;

		LOG_WRAP("New measurement requested for UE %x (%d).\n",
			sim_ues[i].rnti, sim_ues[i].meas[f].earfcn);
	}

	return 0;
}

int wrap_rrc_conf(
	EmageMsg * request, EmageMsg ** reply, unsigned int trigger_id)
{
#if 0
	int i;
	int u = -1;

	unsigned short rnti = 0;

	if(request->event_types_case != EMAGE_MSG__EVENT_TYPES_TE) {
		LOG_WRAP("UE RRC configuration not of Trigger type!\n");
		return 0;
	}

	if(request->te->events_case !=
		TRIGGER_EVENT__EVENTS_M_UE_RRC_MEAS_CONF) {

		LOG_WRAP("Not an UE RRC configuration message!\n");
		return 0;
	}

	/* Perform the operation on this RNTI. */
	rnti = (unsigned short)request->te->mue_rrc_meas_conf->req->rnti;

	LOG_WRAP("UE RRC configuration recv, rnti=%x.\n", rnti);


	for(i = 0; i < UE_MAX; i++) {
		if (UEs[i].rnti == rnti) {
			u = i;
			break;
		}
	}

	if(u < 0) {
		return 0;
	}

	if(msg_create_UE_RRC_mconf(bid, u, reply)) {
		reply = 0;
		return 0;
	}
#endif

	LOG_WRAP("UE RRC configuration is missing right now...\n");

	return 0;
}

int wrap_handover(EmageMsg * request, EmageMsg ** reply)
{
	CtrlHandover * hor = 0;

	if(!request->se ||
		!request->se->mctrl_cmds ||
		!request->se->mctrl_cmds->req ||
		!request->se->mctrl_cmds->req->ctrl_ho) {

		LOG_WRAP("Malformed HO command received\n!");
		return 0;
	}

	hor = request->se->mctrl_cmds->req->ctrl_ho;

	/* Hand this UE over and remove it form the managed UEs. */
	if(x2_hand_over(hor->rnti, hor->t_enb_id)) {
		return -1;
	}

	ue_rem(hor->rnti);

	LOG_WRAP("Hand-over of RNTI %d to eNB %d performed as requested.\n",
		hor->rnti, hor->t_enb_id);

	return 0;
}

int wrap_UEs_ID_report(
	EmageMsg * request, EmageMsg ** reply, unsigned int trigger_id)
{
	/*
	 * Add here your custom interaction mechanism with the LTE stack.
	 */
	EmageMsg * msg = 0;

	sim_UE_rep_trigger = trigger_id;
	sim_UE_rep_mod     = request->head->t_id;

	if(msg_UE_report(sim_ID, sim_UE_rep_mod, &msg) == 0) {
		em_send(sim_ID, msg);
	}

	LOG_WRAP("UEs report %u, transaction %u, triggered by controller.\n",
		sim_UE_rep_trigger, sim_UE_rep_mod);

	return 0;
}

int wrap_cell_report(EmageMsg * request, EmageMsg ** reply)
{
	ENBCells * ec = 0;

	LOG_WRAP("Cell Report requested by controller.\n");

	if(request->event_types_case == EMAGE_MSG__EVENT_TYPES_SE) {
		ec = request->se->menb_cells;
	} else if (request->event_types_case == EMAGE_MSG__EVENT_TYPES_SCHE) {
		ec = request->sche->menb_cells;
	} else {
		LOG_WRAP("Malformed Cell Report received.\n");
		return 0;
	}

	switch(ec->req->enb_info_types) {
	case E_NB_CELLS_INFO_TYPES__ENB_CELLS_INFO:
		msg_cell_report(sim_ID, request->head->t_id, reply);
		break;
	case E_NB_CELLS_INFO_TYPES__ENB_RAN_SHARING_INFO:
		msg_RAN_report(sim_ID, request->head->t_id, reply);
		break;
	default:
		LOG_WRAP("Cell Report %d not supported.\n",
			ec->req->enb_info_types);
		msg_cell_report_fail(sim_ID, request->head->t_id, reply);
		break;
	}

	return 0;
}

/* Operations offered by this technology abstraction module. */
struct em_agent_ops sim_ops = {
	.init                   = wrap_init,
	.release                = wrap_release,
	.UEs_ID_report          = wrap_UEs_ID_report,
	.cell_statistics_report = wrap_cell_stats,
	.RRC_measurements       = wrap_rrc_meas,
	.RRC_meas_conf          = wrap_rrc_conf,
	.handover_request       = wrap_handover,
	.eNB_cells_report       = wrap_cell_report,
};
