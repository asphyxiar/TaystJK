// g_stats.c - End-of-match TFFA/FFA Impact stats summary

#include "g_local.h"
#include "g_stats.h"

#define STATS_MAX_PLAYERS 32

typedef struct {
	char	name[36];
	int		clientNum;
	int		kills;
	int		deaths;
	int		killDiff;
	float	kdRatio;
	int		dmgGiven;
	int		dmgTaken;
	int		dmgDiff;
	float	dmgRatio;
	int		dmgPerDeath;
	int		impact;
	int		eloChange;
	int		teamKills;
	int		teamDamage;
	int		timeSec;
} matchStat_t;

static void StripColorCodes(const char *in, char *out, int outSize) {
	int i = 0;
	while (*in && i < outSize - 1) {
		if (*in == '^' && *(in + 1)) {
			in += 2;
			continue;
		}
		out[i++] = *in++;
	}
	out[i] = '\0';
}

static void SendToAll(const char *msg) {
	trap->SendServerCommand(-1, va("print \"%s\\n\"", msg));
	G_LogPrintf("%s\n", msg);
}

static void SendToClient(int clientNum, const char *msg) {
	trap->SendServerCommand(clientNum, va("print \"%s\\n\"", msg));
	G_LogPrintf("%s\n", msg);
}

void G_PrintMatchStats(void) {
	matchStat_t stats[STATS_MAX_PLAYERS];
	int			numStats = 0;
	int			i, j;
	char		line[256];
	char		timeBuf[16];
	matchStat_t tmp;

	for (i = 0; i < sv_maxclients.integer && numStats < STATS_MAX_PLAYERS; i++) {
		gclient_t	*cl = &level.clients[i];
		matchStat_t *s;
		int			timePlayed;

		if (cl->pers.connected != CON_CONNECTED) continue;
		if (cl->sess.sessionTeam == TEAM_SPECTATOR) continue;

		s = &stats[numStats++];
		s->clientNum = i;
		StripColorCodes(cl->pers.netname, s->name, sizeof(s->name));

		s->kills      = cl->pers.stats.kills;
		s->deaths     = cl->ps.persistant[PERS_KILLED];
		s->killDiff   = s->kills - s->deaths;
		s->kdRatio    = (s->deaths > 0) ? (float)s->kills / (float)s->deaths : (float)s->kills;
		s->dmgGiven   = cl->pers.stats.damageGiven;
		s->dmgTaken   = cl->pers.stats.damageTaken;
		s->dmgDiff    = s->dmgGiven - s->dmgTaken;
		s->dmgRatio   = (s->dmgTaken > 0) ? (float)s->dmgGiven / (float)s->dmgTaken : (float)s->dmgGiven;
		s->dmgPerDeath = (s->deaths > 0) ? s->dmgGiven / s->deaths : s->dmgGiven;
		s->teamKills  = cl->pers.stats.teamKills;
		s->teamDamage = cl->pers.stats.teamDamageGiven;

		s->impact = s->dmgDiff + s->killDiff * 100 - (s->teamDamage + s->teamKills * 500);

		s->eloChange = s->impact / 25;
		if (s->eloChange > 60)  s->eloChange = 60;
		if (s->eloChange < -20) s->eloChange = -20;

		timePlayed = (level.time - cl->pers.enterTime) / 1000;
		if (timePlayed < 0) timePlayed = 0;
		s->timeSec = timePlayed;
	}

	G_LogPrintf("G_PrintMatchStats: numStats=%d\n", numStats);
	if (numStats == 0) return;

	// Sort by impact descending
	for (i = 0; i < numStats - 1; i++) {
		for (j = 0; j < numStats - 1 - i; j++) {
			if (stats[j].impact < stats[j + 1].impact) {
				tmp = stats[j];
				stats[j] = stats[j + 1];
				stats[j + 1] = tmp;
			}
		}
	}

	// --- PERSONAL: compact key/value per player (~45 chars wide) ---
	for (i = 0; i < numStats; i++) {
		matchStat_t *s = &stats[i];
		Com_sprintf(timeBuf, sizeof(timeBuf), "%d:%02d", s->timeSec / 60, s->timeSec % 60);

		SendToClient(s->clientNum, "=============================================");
		SendToClient(s->clientNum, "           YOUR MATCH STATS");
		SendToClient(s->clientNum, "=============================================");
		Com_sprintf(line, sizeof(line), " %-22s      T: %s", s->name, timeBuf);
		SendToClient(s->clientNum, line);
		SendToClient(s->clientNum, "---------------------------------------------");
		Com_sprintf(line, sizeof(line), " K/D:  %d/%d   Diff: %+d   Ratio: %.2f",
			s->kills, s->deaths, s->killDiff, s->kdRatio);
		SendToClient(s->clientNum, line);
		Com_sprintf(line, sizeof(line), " Dmg:  %d   Rcvd: %d   Diff: %+d",
			s->dmgGiven, s->dmgTaken, s->dmgDiff);
		SendToClient(s->clientNum, line);
		Com_sprintf(line, sizeof(line), " DmgR: %.2f   D/D: %d",
			s->dmgRatio, s->dmgPerDeath);
		SendToClient(s->clientNum, line);
		if (s->teamKills > 0 || s->teamDamage > 0) {
			Com_sprintf(line, sizeof(line), " TK: %d   TDmg: %d   (penalty!)",
				s->teamKills, s->teamDamage);
			SendToClient(s->clientNum, line);
		}
		Com_sprintf(line, sizeof(line), " Impact: %+d", s->impact);
		SendToClient(s->clientNum, line);
		SendToClient(s->clientNum, "=============================================");
	}

	// --- GLOBAL: compact table (~55 chars wide) ---
	SendToAll("=======================================================");
	SendToAll("                  MATCH RESULTS");
	SendToAll("=======================================================");
	SendToAll(" # Name               K/D    Dmg   Rcvd    Imp");
	SendToAll("-------------------------------------------------------");
	for (i = 0; i < numStats; i++) {
		matchStat_t *s = &stats[i];
		char kd[16];
		Com_sprintf(kd, sizeof(kd), "%d/%d", s->kills, s->deaths);
		Com_sprintf(line, sizeof(line), "%2d %-18s %-6s %5d  %5d  %+5d",
			i + 1, s->name, kd,
			s->dmgGiven, s->dmgTaken, s->impact);
		SendToAll(line);
	}
	SendToAll("=======================================================");

	// --- HIGHLIGHTS (unchanged) ---
	{
		int mvpIdx      = 0;
		int mostDmgIdx  = 0;
		int mostFragIdx = 0;
		int bestKDIdx   = 0;
		int bestDmgRIdx = 0;
		int bestDDIdx   = 0;
		int tankIdx     = 0;
		int worstIdx    = numStats - 1;
		int cleanIdx    = -1;
		int teamWarnIdx = -1;

		for (i = 1; i < numStats; i++) {
			if (stats[i].impact      > stats[mvpIdx].impact)       mvpIdx      = i;
			if (stats[i].dmgGiven    > stats[mostDmgIdx].dmgGiven)  mostDmgIdx  = i;
			if (stats[i].killDiff    > stats[mostFragIdx].killDiff)  mostFragIdx = i;
			if (stats[i].kdRatio     > stats[bestKDIdx].kdRatio)    bestKDIdx   = i;
			if (stats[i].dmgRatio    > stats[bestDmgRIdx].dmgRatio)  bestDmgRIdx = i;
			if (stats[i].dmgPerDeath > stats[bestDDIdx].dmgPerDeath) bestDDIdx   = i;
			if (stats[i].dmgTaken    > stats[tankIdx].dmgTaken)     tankIdx     = i;
			if (stats[i].impact      < stats[worstIdx].impact)      worstIdx    = i;

			if (stats[i].teamKills == 0 && stats[i].teamDamage == 0) {
				if (cleanIdx < 0 || stats[i].dmgGiven > stats[cleanIdx].dmgGiven)
					cleanIdx = i;
			}
			if (stats[i].teamKills > 0 || stats[i].teamDamage > 0) {
				if (teamWarnIdx < 0 || (stats[i].teamKills * 500 + stats[i].teamDamage) >
				    (stats[teamWarnIdx].teamKills * 500 + stats[teamWarnIdx].teamDamage))
					teamWarnIdx = i;
			}
		}
		if (stats[0].teamKills == 0 && stats[0].teamDamage == 0) {
			if (cleanIdx < 0 || stats[0].dmgGiven > stats[cleanIdx].dmgGiven)
				cleanIdx = 0;
		}
		if (stats[0].teamKills > 0 || stats[0].teamDamage > 0) {
			if (teamWarnIdx < 0 || (stats[0].teamKills * 500 + stats[0].teamDamage) >
			    (stats[teamWarnIdx].teamKills * 500 + stats[teamWarnIdx].teamDamage))
				teamWarnIdx = 0;
		}

		SendToAll("=============================================================");
		SendToAll("                         HIGHLIGHTS");
		SendToAll("=============================================================");
		SendToAll("   Award | Player           |            Value | Note        ");
		SendToAll("-------------------------------------------------------------");

		Com_sprintf(line, sizeof(line), "%8s | %-16s | %16d | %-12s", "MVP",      stats[mvpIdx].name,      stats[mvpIdx].impact,        "impact");   SendToAll(line);
		Com_sprintf(line, sizeof(line), "%8s | %-16s | %16d | %-12s", "Damage",   stats[mostDmgIdx].name,  stats[mostDmgIdx].dmgGiven,   "dealt");    SendToAll(line);
		Com_sprintf(line, sizeof(line), "%8s | %-16s | %+16d | %-12s", "Frags",   stats[mostFragIdx].name, stats[mostFragIdx].killDiff,   "diff");     SendToAll(line);
		Com_sprintf(line, sizeof(line), "%8s | %-16s | %4d/%-3d [%.2f] | %-12s",
			"K/D", stats[bestKDIdx].name,
			stats[bestKDIdx].kills, stats[bestKDIdx].deaths, stats[bestKDIdx].kdRatio, "ratio");
		SendToAll(line);
		Com_sprintf(line, sizeof(line), "%8s | %-16s | %6d/%-5d [%.2f] | %-12s",
			"DmgRatio", stats[bestDmgRIdx].name,
			stats[bestDmgRIdx].dmgGiven, stats[bestDmgRIdx].dmgTaken, stats[bestDmgRIdx].dmgRatio, "ratio");
		SendToAll(line);
		Com_sprintf(line, sizeof(line), "%8s | %-16s | %16d | %-12s", "D/D",      stats[bestDDIdx].name,   stats[bestDDIdx].dmgPerDeath,  "per death"); SendToAll(line);
		if (cleanIdx >= 0) {
			Com_sprintf(line, sizeof(line), "%8s | %-16s | %16d | %-12s", "Clean", stats[cleanIdx].name, stats[cleanIdx].dmgGiven, "0 TK, 0 TDmg");
			SendToAll(line);
		}
		Com_sprintf(line, sizeof(line), "%8s | %-16s | %16d | %-12s", "Tank",     stats[tankIdx].name,     stats[tankIdx].dmgTaken,      "taken");    SendToAll(line);
		Com_sprintf(line, sizeof(line), "%8s | %-16s | %+16d | %-12s", "Punished", stats[worstIdx].name,   stats[worstIdx].impact,        "impact");   SendToAll(line);
		if (teamWarnIdx >= 0) {
			Com_sprintf(line, sizeof(line), "%8s | %-16s | %d TK, %d TDmg | %-12s",
				"TeamWarn", stats[teamWarnIdx].name,
				stats[teamWarnIdx].teamKills, stats[teamWarnIdx].teamDamage, "penalty");
			SendToAll(line);
		}

		SendToAll("=============================================================");
		SendToAll("Match Stats Summary - open console for full table.");
	}
}
