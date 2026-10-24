/*
 *MIT License
 *
 *Copyright (c) 2023 Azerothcore
 *
 *Permission is hereby granted, free of charge, to any person obtaining a copy
 *of this software and associated documentation files (the "Software"), to deal
 *in the Software without restriction, including without limitation the rights
 *to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *copies of the Software, and to permit persons to whom the Software is
 *furnished to do so, subject to the following conditions:
 *
 *The above copyright notice and this permission notice shall be included in all
 *copies or substantial portions of the Software.
 *
 *THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *SOFTWARE.
 */

#include "Config.h"
#include "Spell.h"
#include "AnticheatMgr.h"
#include "Object.h"
#include "AccountMgr.h"
#include "Chat.h"
#include "Player.h"
#include "Timer.h"
#include "GameTime.h"
#include "WorldSessionMgr.h"

Seconds resetTime = 0s;
Seconds lastIterationPlayer = GameTime::GetUptime() + 30s; //TODO: change 30 secs static to a configurable option

class AnticheatPlayerScript : public PlayerScript
{
public:
    AnticheatPlayerScript() : PlayerScript("AnticheatPlayerScript", { PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_SPELL_CAST }) { }

    void OnPlayerLogout(Player* player) override
    {
        sAnticheatMgr->HandlePlayerLogout(player);
    }

    void OnPlayerLogin(Player* player) override
    {
        sAnticheatMgr->HandlePlayerLogin(player);

        if (sConfigMgr->GetOption<bool>("Anticheat.LoginMessage", true))
            ChatHandler(player->GetSession()).PSendSysMessage("This server is running an Anticheat Module.");
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        if (sConfigMgr->GetOption<bool>("Anticheat.OpAckOrderHack", true) && sConfigMgr->GetOption<bool>("Anticheat.Enabled", true))
            sAnticheatMgr->AckUpdate(player, diff);
    }

    // 例外使用白名单内技能导致的误判，遍历配置白名单，命中则打上“刚使用过位移技能”标记
    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        if (!sConfigMgr->GetOption<bool>("Anticheat.Enabled", true))
            return;

        if (!player || !spell || !spell->GetSpellInfo())
            return;

        uint32 spellId = spell->GetSpellInfo()->Id;
        ObjectGuid key = player->GetGUID();

        // 直接访问 public 成员变量
        for (uint32 exemptId : sAnticheatMgr->m_exemptCastSpellIds)
        {
            if (spellId == exemptId)
            {
                sAnticheatMgr->m_Players[key].SetJustUsedMovementSpell(true);
                break;
            }
        }
    }
};

class AnticheatWorldScript : public WorldScript
{
public:
    AnticheatWorldScript() : WorldScript("AnticheatWorldScript", { WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_AFTER_CONFIG_LOAD }) { }

    void OnUpdate(uint32 /* diff */) override // unusued parameter
    {
        if (GameTime::GetGameTime() > resetTime)
        {
            LOG_INFO("module", "Anticheat: Resetting daily report states.");
            sAnticheatMgr->ResetDailyReportStates();
            UpdateReportResetTime();
            LOG_INFO("module", "Anticheat: Next daily report reset: {}", Acore::Time::TimeToHumanReadable(resetTime));
        }

        if (GameTime::GetUptime() > lastIterationPlayer)
        {
            lastIterationPlayer = GameTime::GetUptime() + Seconds(sConfigMgr->GetOption<uint32>("Anticheat.SaveReportsTime", 60));

            LOG_INFO("module", "Saving reports for {} players.", sWorldSessionMgr->GetPlayerCount());

            WorldSessionMgr::SessionMap const& sessionMap = sWorldSessionMgr->GetAllSessions();
            for (WorldSessionMgr::SessionMap::const_iterator itr = sessionMap.begin(); itr != sessionMap.end(); ++itr)
                if (Player* plr = itr->second->GetPlayer())
                    sAnticheatMgr->SavePlayerData(plr);
        }
    }

    void OnAfterConfigLoad(bool /* reload */) override // unusued parameter
    {
        sAnticheatMgr->LoadConfig(); //  执行新加的配置初始化
        LOG_INFO("module", "AnticheatModule Loaded.");
    }

    void UpdateReportResetTime()
    {
        resetTime = Seconds(Acore::Time::GetNextTimeWithDayAndHour(-1, 6));
    }
};

class AnticheatMovementHandlerScript : public MovementHandlerScript
{
public:
    AnticheatMovementHandlerScript() : MovementHandlerScript("AnticheatMovementHandlerScript", { MOVEMENTHOOK_ON_PLAYER_MOVE }) { }

    void OnPlayerMove(Player* player, MovementInfo mi, uint32 opcode) override
    {
        if (!player->GetSession()->IsGMAccount() || sConfigMgr->GetOption<bool>("Anticheat.EnabledOnGmAccounts", false))
            sAnticheatMgr->StartHackDetection(player, mi, opcode);
    }
};

void startAnticheatScripts()
{
    new AnticheatWorldScript();
    new AnticheatPlayerScript();
    new AnticheatMovementHandlerScript();
}
