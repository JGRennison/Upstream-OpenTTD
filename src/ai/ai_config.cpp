/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ai_config.cpp Implementation of AIConfig. */

#include "../stdafx.h"
#include "../company_base.h"
#include "../settings_type.h"
#include "../string_func.h"
#include "ai.hpp"
#include "ai_config.hpp"
#include "ai_info.hpp"

#include "../safeguards.h"

/* static */ AIConfig *AIConfig::GetConfig(CompanyID company, ScriptSettingSource source)
{
	assert(company < MAX_COMPANIES);

	if (_game_mode == GM_MENU) source = SSS_FORCE_NEWGAME;

	if (source == SSS_DEFAULT) {
		Company *c = Company::GetIfValid(company);
		if (c != nullptr && c->ai_config != nullptr) return c->ai_config.get();
	}

	auto &config = (source == SSS_FORCE_NEWGAME) ? _settings_newgame.script_config.ai[company] : _settings_game.script_config.ai[company];
	if (config == nullptr) config = std::make_unique<AIConfig>();

	return config.get();
}

class AIInfo *AIConfig::GetInfo() const
{
	return static_cast<class AIInfo *>(ScriptConfig::GetInfo());
}

ScriptInfo *AIConfig::FindInfo(const std::string &name, int version, bool force_exact_match)
{
	return static_cast<ScriptInfo *>(AI::FindInfo(name, version, force_exact_match));
}

bool AIConfig::ResetInfo(bool force_exact_match)
{
	this->info = (ScriptInfo *)AI::FindInfo(this->name, force_exact_match ? this->version : -1, force_exact_match);
	return this->info != nullptr;
}
