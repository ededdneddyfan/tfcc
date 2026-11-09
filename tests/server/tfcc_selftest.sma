/*
 * tfcc_selftest - drives a TFC server with fake clients so the regression
 * matrix exercises real gameplay code (spawns, every class, weapons,
 * grenades, deaths, flag touches) instead of just loading the map.
 *
 * Only active when the cvar tfcc_selftest is 1 (set on the command line by
 * tests/server/run_matrix.sh via +localinfo). Prints machine-readable lines:
 *   TFCC_SELFTEST start map=<map> bots=<n>
 *   TFCC_SELFTEST tick t=<sec> alive=<n> deaths=<n>
 *   TFCC_SELFTEST done map=<map> deaths=<n>
 */
#include <amxmodx>
#include <fakemeta>

#define MAX_BOTS 12

new g_bots[MAX_BOTS];
new g_nbots;
new g_deaths;
new g_tick;
new Float:g_yaw[33];
new g_cvEnable, g_cvBots, g_cvSeconds;

new const g_classes[][] = { "scout", "sniper", "soldier", "demoman", "medic", "hwguy", "pyro", "spy", "engineer" };

public plugin_init()
{
	register_plugin("TFCC selftest", "0.1", "TFCC");
	g_cvEnable = register_cvar("tfcc_selftest", "0");
	g_cvBots = register_cvar("tfcc_selftest_bots", "10");
	g_cvSeconds = register_cvar("tfcc_selftest_seconds", "45");
	register_event("DeathMsg", "ev_death", "a");
}

public plugin_cfg()
{
	// Enabled by the cvar, or by "+localinfo tfcc_selftest 1" on the hlds
	// command line (cvars set there are lost before this plugin registers).
	new li[8];
	get_localinfo("tfcc_selftest", li, charsmax(li));
	if (get_pcvar_num(g_cvEnable) || str_to_num(li))
		set_task(3.0, "spawn_bots");
}

public spawn_bots()
{
	new map[32];
	get_mapname(map, charsmax(map));
	new want = clamp(get_pcvar_num(g_cvBots), 1, MAX_BOTS);
	for (new i = 0; i < want; i++)
	{
		new name[32];
		formatex(name, charsmax(name), "tfcc_bot%d", i + 1);
		new id = engfunc(EngFunc_CreateFakeClient, name);
		if (!id)
			break;
		new reject[128];
		dllfunc(DLLFunc_ClientConnect, id, name, "127.0.0.1", reject);
		dllfunc(DLLFunc_ClientPutInServer, id);
		set_pev(id, pev_flags, pev(id, pev_flags) | FL_FAKECLIENT);
		g_bots[g_nbots++] = id;
		g_yaw[id] = float(random_num(0, 359));
		set_task(1.0 + 0.2 * i, "join_team", id);
	}
	server_print("TFCC_SELFTEST start map=%s bots=%d", map, g_nbots);
	set_task(5.0, "report", 0, "", 0, "b");
	set_task(get_pcvar_float(g_cvSeconds), "finish");
}

public join_team(id)
{
	if (!is_user_connected(id))
		return;
	engclient_cmd(id, "jointeam", (id % 2) ? "1" : "2");
	engclient_cmd(id, g_classes[id % sizeof g_classes]);
}

public server_frame()
{
	if (!g_nbots)
		return;
	new Float:ms = 1000.0 / 100.0; // 100 fps bots
	for (new i = 0; i < g_nbots; i++)
	{
		new id = g_bots[i];
		if (!is_user_connected(id))
			continue;
		if (random_num(0, 50) == 0)
			g_yaw[id] = float(random_num(0, 359));
		new Float:ang[3];
		ang[0] = float(random_num(-30, 30));
		ang[1] = g_yaw[id];
		new buttons = IN_FORWARD;
		if (random_num(0, 3) == 0) buttons |= IN_ATTACK;
		if (random_num(0, 20) == 0) buttons |= IN_JUMP;
		if (random_num(0, 30) == 0) buttons |= IN_DUCK;
		if (random_num(0, 400) == 0) engclient_cmd(id, "primeone");
		if (random_num(0, 400) == 0) engclient_cmd(id, "primetwo");
		if (random_num(0, 600) == 0) engclient_cmd(id, "dropitems");
		if (random_num(0, 300) == 0)
		{
			new slot[8];
			formatex(slot, charsmax(slot), "slot%d", random_num(1, 4));
			engclient_cmd(id, slot);
		}
		if (!is_user_alive(id) && random_num(0, 10) == 0) buttons = IN_ATTACK; // respawn
		engfunc(EngFunc_RunPlayerMove, id, ang, 400.0, 0.0, 0.0, buttons, 0, floatround(ms));
	}
}

public ev_death()
{
	g_deaths++;
}

public report()
{
	g_tick += 5;
	new alive = 0;
	for (new i = 0; i < g_nbots; i++)
		if (is_user_alive(g_bots[i])) alive++;
	server_print("TFCC_SELFTEST tick t=%d alive=%d deaths=%d", g_tick, alive, g_deaths);
}

public finish()
{
	new map[32];
	get_mapname(map, charsmax(map));
	server_print("TFCC_SELFTEST done map=%s deaths=%d", map, g_deaths);
	server_cmd("meta list;amxx plugins;amxx modules");
	set_task(2.0, "do_quit");
}

public do_quit()
{
	server_cmd("quit");
}
