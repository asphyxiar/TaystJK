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
	trap->SendServerCommand(-1, va("print \"%s\n\"", msg));
	G_LogPrintf("%s\n", msg);
}

static void SendToClient(int clientNum, const char *msg) {
	trap->SendServerCommand(clientNum, va("print \"%s\n\"", msg));
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

		SendToClient(s->clientNum, "^2=============================================");
		SendToClient(s->clientNum, "^2           YOUR MATCH STATS");
		SendToClient(s->clientNum, "^2=============================================");
		Com_sprintf(line, sizeof(line), " ^3%-22s      ^5T: ^7%s", s->name, timeBuf);
		SendToClient(s->clientNum, line);
		SendToClient(s->clientNum, "^2---------------------------------------------");
		Com_sprintf(line, sizeof(line), " ^5K/D:  ^7%d/%d   ^5Diff: %s%+d^7   ^5Ratio: ^7%.2f",
			s->kills, s->deaths, s->killDiff >= 0 ? "^2" : "^1", s->killDiff, s->kdRatio);
		SendToClient(s->clientNum, line);
		Com_sprintf(line, sizeof(line), " ^5Dmg:  ^2%d   ^5Rcvd: ^1%d   ^5Diff: %s%+d^7",
			s->dmgGiven, s->dmgTaken, s->dmgDiff >= 0 ? "^2" : "^1", s->dmgDiff);
		SendToClient(s->clientNum, line);
		Com_sprintf(line, sizeof(line), " ^5DmgR: ^7%.2f   ^5D/D: ^7%d",
			s->dmgRatio, s->dmgPerDeath);
		SendToClient(s->clientNum, line);
		if (s->teamKills > 0 || s->teamDamage > 0) {
			Com_sprintf(line, sizeof(line), " ^1TK: %d   TDmg: %d   ^3(penalty!)",
				s->teamKills, s->teamDamage);
			SendToClient(s->clientNum, line);
		}
		Com_sprintf(line, sizeof(line), " ^5Impact: %s%+d^7", s->impact >= 0 ? "^2" : "^1", s->impact);
		SendToClient(s->clientNum, line);
		SendToClient(s->clientNum, "^2=============================================");
	}

	// --- GLOBAL: compact table (~55 chars wide) ---
	SendToAll("^2=======================================================");
	SendToAll("^2                  MATCH RESULTS");
	SendToAll("^2=======================================================");
	SendToAll("^5 # Name               K/D    Dmg   Rcvd    Imp");
	SendToAll("^2-------------------------------------------------------");
	for (i = 0; i < numStats; i++) {
		matchStat_t *s = &stats[i];
		char kd[16];
		char impBuf[16];
		Com_sprintf(kd, sizeof(kd), "%d/%d", s->kills, s->deaths);
		Com_sprintf(impBuf, sizeof(impBuf), "%s%+d^7", s->impact >= 0 ? "^2" : "^1", s->impact);
		Com_sprintf(line, sizeof(line), "^3%2d ^7%-18s %-6s %5d  %5d  %s",
			i + 1, s->name, kd,
			s->dmgGiven, s->dmgTaken, impBuf);
		SendToAll(line);
	}
	SendToAll("^2=======================================================");

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

		SendToAll("^2=============================================================");
		SendToAll("^2                      HIGHLIGHTS");
		SendToAll("^2=============================================================");
		SendToAll("^5   Award | Player           |            Value | Note        ");
		SendToAll("^2-------------------------------------------------------------");

		{
			char vc[48]; // colored + right-aligned value: "^X%16d^7" = 16 display chars

			// MVP
			Com_sprintf(vc, sizeof(vc), "%s%+16d^7", stats[mvpIdx].impact >= 0 ? "^2" : "^1", stats[mvpIdx].impact);
			Com_sprintf(line, sizeof(line), "^3%8s ^7| %-16s | %s | ^5%-12s", "MVP",      stats[mvpIdx].name,     vc, "impact");     SendToAll(line);

			// Damage
			Com_sprintf(vc, sizeof(vc), "^2%16d^7", stats[mostDmgIdx].dmgGiven);
			Com_sprintf(line, sizeof(line), "^3%8s ^7| %-16s | %s | ^5%-12s", "Damage",   stats[mostDmgIdx].name, vc, "dealt");      SendToAll(line);

			// Frags
			Com_sprintf(vc, sizeof(vc), "%s%+16d^7", stats[mostFragIdx].killDiff >= 0 ? "^2" : "^1", stats[mostFragIdx].killDiff);
			Com_sprintf(line, sizeof(line), "^3%8s ^7| %-16s | %s | ^5%-12s", "Frags",    stats[mostFragIdx].name, vc, "diff");      SendToAll(line);

			// K/D
			Com_sprintf(vc, sizeof(vc), "^7%4d/%-3d ^3[^2%.2f^3]^7",
				stats[bestKDIdx].kills, stats[bestKDIdx].deaths, stats[bestKDIdx].kdRatio);
			Com_sprintf(line, sizeof(line), "^3%8s ^7| %-16s | %s | ^5%-12s", "K/D",      stats[bestKDIdx].name,  vc, "ratio");      SendToAll(line);

			// DmgRatio
			Com_sprintf(vc, sizeof(vc), "^7%6d/%-5d ^3[^2%.2f^3]^7",
				stats[bestDmgRIdx].dmgGiven, stats[bestDmgRIdx].dmgTaken, stats[bestDmgRIdx].dmgRatio);
			Com_sprintf(line, sizeof(line), "^3%8s ^7| %-16s | %s | ^5%-12s", "DmgRatio", stats[bestDmgRIdx].name, vc, "ratio");     SendToAll(line);

			// D/D
			Com_sprintf(vc, sizeof(vc), "^2%16d^7", stats[bestDDIdx].dmgPerDeath);
			Com_sprintf(line, sizeof(line), "^3%8s ^7| %-16s | %s | ^5%-12s", "D/D",      stats[bestDDIdx].name,  vc, "per death");  SendToAll(line);

			// Clean
			if (cleanIdx >= 0) {
				Com_sprintf(vc, sizeof(vc), "^2%16d^7", stats[cleanIdx].dmgGiven);
				Com_sprintf(line, sizeof(line), "^3%8s ^7| %-16s | %s | ^5%-12s", "Clean", stats[cleanIdx].name, vc, "0 TK, 0 TDmg");
				SendToAll(line);
			}

			// Tank
			Com_sprintf(vc, sizeof(vc), "^1%16d^7", stats[tankIdx].dmgTaken);
			Com_sprintf(line, sizeof(line), "^3%8s ^7| %-16s | %s | ^5%-12s", "Tank",     stats[tankIdx].name,    vc, "taken");      SendToAll(line);

			// Punished
			Com_sprintf(vc, sizeof(vc), "%s%+16d^7", stats[worstIdx].impact >= 0 ? "^2" : "^1", stats[worstIdx].impact);
			Com_sprintf(line, sizeof(line), "^3%8s ^7| %-16s | %s | ^5%-12s", "Punished", stats[worstIdx].name,   vc, "impact");     SendToAll(line);

			// TeamWarn
			if (teamWarnIdx >= 0) {
				Com_sprintf(vc, sizeof(vc), "^1%d TK, %d TDmg^7", stats[teamWarnIdx].teamKills, stats[teamWarnIdx].teamDamage);
				Com_sprintf(line, sizeof(line), "^3%8s ^7| %-16s | %s | ^5%-12s", "TeamWarn", stats[teamWarnIdx].name, vc, "penalty");
				SendToAll(line);
			}
		}

		SendToAll("^2=============================================================");
		SendToAll("^7Match Stats Summary - open console for full table.");
	}
}
