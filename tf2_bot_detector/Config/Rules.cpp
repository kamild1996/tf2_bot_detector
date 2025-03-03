#include "Rules.h"
#include "Networking/SteamAPI.h"
#include "Util/JSONUtils.h"
#include "GameData/IPlayer.h"
#include "Log.h"
#include "PlayerListJSON.h"
#include "Settings.h"

#include <mh/text/case_insensitive_string.hpp>
#include <mh/text/string_insertion.hpp>
#include <mh/text/stringops.hpp>
#include <mh/utility.hpp>
#include <nlohmann/json.hpp>

#include <iomanip>
#include <regex>
#include <stdexcept>
#include <string>

using namespace std::string_literals;
using namespace std::string_view_literals;
using namespace tf2_bot_detector;

namespace tf2_bot_detector
{
	void to_json(nlohmann::json& j, const TextMatchMode& d)
	{
		switch (d)
		{
		case TextMatchMode::Equal:       j = "equal"; break;
		case TextMatchMode::Contains:    j = "contains"; break;
		case TextMatchMode::StartsWith:  j = "starts_with"; break;
		case TextMatchMode::EndsWith:    j = "ends_with"; break;
		case TextMatchMode::Regex:       j = "regex"; break;
		case TextMatchMode::Word:        j = "word"; break;

		default:
			throw std::runtime_error("Unknown TextMatchMode value "s << +std::underlying_type_t<TextMatchMode>(d));
		}
	}

	void to_json(nlohmann::json& j, const TriggerMatchMode& d)
	{
		switch (d)
		{
		case TriggerMatchMode::MatchAll:  j = "match_all"; break;
		case TriggerMatchMode::MatchAny:  j = "match_any"; break;

		default:
			throw std::runtime_error("Unknown TriggerMatchMode value "s << +std::underlying_type_t<TriggerMatchMode>(d));
		}
	}

	void to_json(nlohmann::json& j, const TextMatch& d)
	{
		j =
		{
			{ "mode", d.m_Mode },
			{ "case_sensitive", d.m_CaseSensitive },
			{ "patterns", d.m_Patterns },
		};
	}

	void to_json(nlohmann::json& j, const ModerationRule::Triggers& d)
	{
		size_t count = 0;

		if (d.m_ChatMsgTextMatch)
		{
			j["chatmsg_text_match"] = *d.m_ChatMsgTextMatch;
			count++;
		}
		if (d.m_UsernameTextMatch)
		{
			j["username_text_match"] = *d.m_UsernameTextMatch;
			count++;
		}
		if (d.m_AvatarMatches.size() > 0)
		{
			j["avatar_match"] = d.m_AvatarMatches;
			count++;
		}
		if (d.m_PersonanameTextMatch)
		{
			j["personaname_text_match"] = *d.m_PersonanameTextMatch;
			count++;
		}

		if (count > 1)
			j["mode"] = d.m_Mode;
	}

	void to_json(nlohmann::json& j, const ModerationRule::Actions& d)
	{
		if (!d.m_Mark.empty())
			j["mark"] = d.m_Mark;
		if (!d.m_TransientMark.empty())
			j["transient_mark"] = d.m_TransientMark;
		if (!d.m_Unmark.empty())
			j["unmark"] = d.m_Unmark;
	}

	void to_json(nlohmann::json& j, const ModerationRule& d)
	{
		j =
		{
			{ "description", d.m_Description },
			{ "triggers", d.m_Triggers },
			{ "actions", d.m_Actions },
		};
	}

	void from_json(const nlohmann::json& j, TriggerMatchMode& d)
	{
		const std::string_view& str = j.get<std::string_view>();
		if (str == "match_all"sv)
			d = TriggerMatchMode::MatchAll;
		else if (str == "match_any"sv)
			d = TriggerMatchMode::MatchAny;
		else
			throw std::runtime_error("Invalid value for TriggerMatchMode "s << std::quoted(str));
	}
	void from_json(const nlohmann::json& j, TextMatchMode& mode)
	{
		const std::string_view str = j;

		if (str == "equal"sv)
			mode = TextMatchMode::Equal;
		else if (str == "contains"sv)
			mode = TextMatchMode::Contains;
		else if (str == "starts_with"sv)
			mode = TextMatchMode::StartsWith;
		else if (str == "ends_with"sv)
			mode = TextMatchMode::EndsWith;
		else if (str == "regex"sv)
			mode = TextMatchMode::Regex;
		else if (str == "word"sv)
			mode = TextMatchMode::Word;
		else
			throw std::runtime_error("Unable to parse TextMatchMode from "s << std::quoted(str));
	}

	void to_json(nlohmann::json& j, const AvatarMatch& d)
	{
		j =
		{
			{ "avatar_hash", d.m_AvatarHash },
		};
	}
	void from_json(const nlohmann::json& j, AvatarMatch& d)
	{
		d.m_AvatarHash = mh::tolower(j.at("avatar_hash").get<std::string_view>());
	}

	void from_json(const nlohmann::json& j, TextMatch& d)
	{
		d.m_Mode = j.at("mode");
		d.m_Patterns = j.at("patterns").get<std::vector<std::string>>();
		try_get_to_defaulted(j, d.m_CaseSensitive, "case_sensitive", false);
	}

	void from_json(const nlohmann::json& j, ModerationRule::Triggers& d)
	{
		if (auto found = j.find("mode"); found != j.end())
			d.m_Mode = *found;

		if (auto found = j.find("chatmsg_text_match"); found != j.end())
			d.m_ChatMsgTextMatch.emplace(TextMatch(*found));
		if (auto found = j.find("username_text_match"); found != j.end())
			d.m_UsernameTextMatch.emplace(TextMatch(*found));
		if (auto found = j.find("personaname_text_match"); found != j.end())
			d.m_PersonanameTextMatch.emplace(TextMatch(*found));
		if (auto found = j.find("avatar_match"); found != j.end())
		{
			if (found->is_array())
				found->get_to(d.m_AvatarMatches);
			else if (found->is_object())
				d.m_AvatarMatches = { mh::copy(*found) };
			else
			{
				throw std::runtime_error(mh::format(
					"{}: Expected avatar_match to be an array or object, instead it was a {}",
					MH_SOURCE_LOCATION_CURRENT(), mh::enum_fmt(found->type())));
			}
		}
	}

	void from_json(const nlohmann::json& j, ModerationRule::Actions& d)
	{
		try_get_to_defaulted(j, d.m_Mark, "mark");
		try_get_to_defaulted(j, d.m_TransientMark, "transient_mark");
		try_get_to_defaulted(j, d.m_Unmark, "unmark");
	}

	void from_json(const nlohmann::json& j, ModerationRule& d)
	{
		d.m_Description = j.at("description");
		d.m_Triggers = j.at("triggers");
		d.m_Actions = j.at("actions");
	}
}

ModerationRules::ModerationRules(const Settings& settings) :
	m_CFGGroup(settings)
{
	// Immediately load and resave to normalize any formatting
	LoadFiles();
}

bool ModerationRules::LoadFiles()
{
	m_CFGGroup.LoadFiles();
	return true;
}

bool ModerationRules::SaveFile() const
{
	m_CFGGroup.SaveFiles();
	return true;
}

mh::generator<const ModerationRule&> tf2_bot_detector::ModerationRules::GetRules() const
{
	if (auto list = m_CFGGroup.m_OfficialList.try_get())
	{
		for (const auto& rule : list->m_Rules)
			co_yield rule;
	}

	if (m_CFGGroup.m_UserList)
	{
		for (const auto& rule : m_CFGGroup.m_UserList->m_Rules)
			co_yield rule;
	}

	if (auto list = m_CFGGroup.m_ThirdPartyLists.try_get())
	{
		for (const auto& rule : *list)
			co_yield rule;
	}
}

void ModerationRules::RuleFile::ValidateSchema(const ConfigSchemaInfo& schema) const
{
	if (schema.m_Type != "rules")
		throw std::runtime_error("Schema is not a rules list");
	if (schema.m_Version != RULES_SCHEMA_VERSION)
		throw std::runtime_error("Schema must be version 3");
}

void ModerationRules::RuleFile::Deserialize(const nlohmann::json& json)
{
	SharedConfigFileBase::Deserialize(json);

	m_Rules = json.at("rules").get<RuleList_t>();
}

void ModerationRules::RuleFile::Serialize(nlohmann::json& json) const
{
	SharedConfigFileBase::Serialize(json);

	if (!m_Schema || m_Schema->m_Type != "rules" || m_Schema->m_Version != RULES_SCHEMA_VERSION)
		json["$schema"] = ConfigSchemaInfo("rules", RULES_SCHEMA_VERSION);

	json["rules"] = m_Rules;
}

void ModerationRules::ConfigFileGroup::CombineEntries(RuleList_t& list, const RuleFile& file) const
{
	list.insert(list.end(), file.m_Rules.begin(), file.m_Rules.end());
}

bool TextMatch::Match(const std::string_view& text) const try
{
	switch (m_Mode)
	{
	case TextMatchMode::Equal:
	{
		return std::any_of(m_Patterns.begin(), m_Patterns.end(), [&](const std::string_view& pattern)
			{
				if (m_CaseSensitive)
					return text == pattern;
				else
					return mh::case_insensitive_compare(text, pattern);
			});
	}
	case TextMatchMode::Contains:
	{
		return std::any_of(m_Patterns.begin(), m_Patterns.end(), [&](const std::string_view& pattern)
			{
				if (m_CaseSensitive)
					return text.find(pattern) != text.npos;
				else
					return mh::case_insensitive_view(text).find(mh::case_insensitive_view(pattern)) != text.npos;
			});
		break;
	}
	case TextMatchMode::StartsWith:
	{
		return std::any_of(m_Patterns.begin(), m_Patterns.end(), [&](const std::string_view& pattern)
			{
				if (m_CaseSensitive)
					return text.starts_with(pattern);
				else
					return mh::case_insensitive_view(text).starts_with(mh::case_insensitive_view(pattern));
			});
	}
	case TextMatchMode::EndsWith:
	{
		return std::any_of(m_Patterns.begin(), m_Patterns.end(), [&](const std::string_view& pattern)
			{
				if (m_CaseSensitive)
					return text.ends_with(pattern);
				else
					return mh::case_insensitive_view(text).ends_with(mh::case_insensitive_view(pattern));
			});
	}
	case TextMatchMode::Regex:
	{
		return std::any_of(m_Patterns.begin(), m_Patterns.end(), [&](const std::string_view& pattern)
			{
				try
				{
					std::regex_constants::syntax_option_type options{};
					if (!m_CaseSensitive)
						options = std::regex_constants::icase;

					std::regex r(pattern.begin(), pattern.end(), options);
					return std::regex_match(text.begin(), text.end(), r);
				}
				catch (const std::regex_error&)
				{
					LogException("Regex error when trying to match {} against pattern {}", std::quoted(text), std::quoted(pattern));
					return false;
				}
			});
	}
	case TextMatchMode::Word:
	{
		static const std::regex s_WordRegex(R"regex((\w+))regex", std::regex::optimize);

		const auto end = std::regex_iterator<std::string_view::const_iterator>{};
		for (auto it = std::regex_iterator<std::string_view::const_iterator>(text.begin(), text.end(), s_WordRegex);
			it != end; ++it)
		{
			const std::string_view itStr(&*it->operator[](0).first, it->operator[](0).length());
			const auto anyMatches = std::any_of(m_Patterns.begin(), m_Patterns.end(), [&](const std::string_view& pattern)
				{
					if (m_CaseSensitive)
						return itStr == pattern;
					else
						return mh::case_insensitive_compare(itStr, pattern);
				});

			if (anyMatches)
				return true;
		}

		return false;
	}
	}

	throw std::runtime_error(mh::format("{}: Unknown value {}", MH_SOURCE_LOCATION_CURRENT(), mh::enum_fmt(m_Mode)));
}
catch (...)
{
	LogException("Error when trying to match against {}", std::quoted(text));
	throw;
}

bool ModerationRule::Match(const IPlayer& player) const
{
	return Match(player, std::string_view{});
}

namespace
{
	enum class MatchResult
	{
		Unset,
		Match,
		NoMatch,
	};

	__declspec(noinline) constexpr MatchResult operator&&(MatchResult lhs, MatchResult rhs)
	{
		if (lhs == MatchResult::Match && rhs == MatchResult::Match)
			return MatchResult::Match;
		else if (lhs == MatchResult::NoMatch || rhs == MatchResult::NoMatch)
			return MatchResult::NoMatch;
		else if (lhs == MatchResult::Unset)
			return rhs;
		else
			return lhs;
	}

	__declspec(noinline) constexpr MatchResult operator||(MatchResult lhs, MatchResult rhs)
	{
		if (lhs == MatchResult::Match || rhs == MatchResult::Match)
			return MatchResult::Match;
		else if (lhs != MatchResult::Unset)
			return lhs;
		else
			return rhs;
	}

	__declspec(noinline) constexpr bool operator!(MatchResult r)
	{
		return !(r == MatchResult::Match);
	}

	static_assert((MatchResult::Unset && MatchResult::Unset) == MatchResult::Unset);
	static_assert((MatchResult::Unset && MatchResult::Match) == MatchResult::Match);
	static_assert((MatchResult::Unset && MatchResult::NoMatch) == MatchResult::NoMatch);

	static_assert((MatchResult::Match && MatchResult::Unset) == MatchResult::Match);
	static_assert((MatchResult::Match && MatchResult::Match) == MatchResult::Match);
	static_assert((MatchResult::Match && MatchResult::NoMatch) == MatchResult::NoMatch);

	static_assert((MatchResult::NoMatch && MatchResult::Unset) == MatchResult::NoMatch);
	static_assert((MatchResult::NoMatch && MatchResult::Match) == MatchResult::NoMatch);
	static_assert((MatchResult::NoMatch && MatchResult::NoMatch) == MatchResult::NoMatch);

	static_assert((MatchResult::Unset || MatchResult::Unset) == MatchResult::Unset);
	static_assert((MatchResult::Unset || MatchResult::Match) == MatchResult::Match);
	static_assert((MatchResult::Unset || MatchResult::NoMatch) == MatchResult::NoMatch);

	static_assert((MatchResult::Match || MatchResult::Unset) == MatchResult::Match);
	static_assert((MatchResult::Match || MatchResult::Match) == MatchResult::Match);
	static_assert((MatchResult::Match || MatchResult::NoMatch) == MatchResult::Match);

	static_assert((MatchResult::NoMatch || MatchResult::Unset) == MatchResult::NoMatch);
	static_assert((MatchResult::NoMatch || MatchResult::Match) == MatchResult::Match);
	static_assert((MatchResult::NoMatch || MatchResult::NoMatch) == MatchResult::NoMatch);
}

template<typename... TFuncs>
static constexpr bool MatchRules(TriggerMatchMode mode, TFuncs&&... funcs)
{
	if (mode == TriggerMatchMode::MatchAll)
		return !!(... && funcs());
	else if (mode == TriggerMatchMode::MatchAny)
		return !!(... || funcs());
	else
	{
		LogError(MH_SOURCE_LOCATION_CURRENT(), "Unexpected mode {}", mh::enum_fmt(mode));
		return false;
	}
}

static constexpr void RunTests()
{
	const auto match = [] { return MatchResult::Match; };
	const auto noMatch = [] { return MatchResult::NoMatch; };
	const auto unset = [] { return MatchResult::Unset; };

	//////////////
	// MatchAll //
	//////////////
	static_assert(MatchRules(TriggerMatchMode::MatchAll, match, match, match));
	static_assert(!MatchRules(TriggerMatchMode::MatchAll, match, match, noMatch));
	static_assert(MatchRules(TriggerMatchMode::MatchAll, match, match, unset));

	static_assert(!MatchRules(TriggerMatchMode::MatchAll, match, noMatch, match));
	static_assert(!MatchRules(TriggerMatchMode::MatchAll, match, noMatch, noMatch));
	static_assert(!MatchRules(TriggerMatchMode::MatchAll, match, noMatch, unset));

	static_assert(MatchRules(TriggerMatchMode::MatchAll, match, unset, match));
	static_assert(!MatchRules(TriggerMatchMode::MatchAll, match, unset, noMatch));
	static_assert(MatchRules(TriggerMatchMode::MatchAll, match, unset, unset));

	static_assert(!MatchRules(TriggerMatchMode::MatchAll, noMatch, match, match));
	static_assert(!MatchRules(TriggerMatchMode::MatchAll, noMatch, match, noMatch));
	static_assert(!MatchRules(TriggerMatchMode::MatchAll, noMatch, match, unset));

	static_assert(!MatchRules(TriggerMatchMode::MatchAll, noMatch, noMatch, match));
	static_assert(!MatchRules(TriggerMatchMode::MatchAll, noMatch, noMatch, noMatch));
	static_assert(!MatchRules(TriggerMatchMode::MatchAll, noMatch, noMatch, unset));

	static_assert(!MatchRules(TriggerMatchMode::MatchAll, noMatch, unset, match));
	static_assert(!MatchRules(TriggerMatchMode::MatchAll, noMatch, unset, noMatch));
	static_assert(!MatchRules(TriggerMatchMode::MatchAll, noMatch, unset, unset));

	static_assert(MatchRules(TriggerMatchMode::MatchAll, unset, match, match));
	static_assert(!MatchRules(TriggerMatchMode::MatchAll, unset, match, noMatch));
	static_assert(MatchRules(TriggerMatchMode::MatchAll, unset, match, unset));

	static_assert(!MatchRules(TriggerMatchMode::MatchAll, unset, noMatch, match));
	static_assert(!MatchRules(TriggerMatchMode::MatchAll, unset, noMatch, noMatch));
	static_assert(!MatchRules(TriggerMatchMode::MatchAll, unset, noMatch, unset));

	static_assert(MatchRules(TriggerMatchMode::MatchAll, unset, unset, match));
	static_assert(!MatchRules(TriggerMatchMode::MatchAll, unset, unset, noMatch));
	static_assert(!MatchRules(TriggerMatchMode::MatchAll, unset, unset, unset));

	//////////////
	// MatchAny //
	//////////////
	static_assert(MatchRules(TriggerMatchMode::MatchAny, match, match, match));
	static_assert(MatchRules(TriggerMatchMode::MatchAny, match, match, noMatch));
	static_assert(MatchRules(TriggerMatchMode::MatchAny, match, match, unset));

	static_assert(MatchRules(TriggerMatchMode::MatchAny, match, noMatch, match));
	static_assert(MatchRules(TriggerMatchMode::MatchAny, match, noMatch, noMatch));
	static_assert(MatchRules(TriggerMatchMode::MatchAny, match, noMatch, unset));

	static_assert(MatchRules(TriggerMatchMode::MatchAny, match, unset, match));
	static_assert(MatchRules(TriggerMatchMode::MatchAny, match, unset, noMatch));
	static_assert(MatchRules(TriggerMatchMode::MatchAny, match, unset, unset));

	static_assert(MatchRules(TriggerMatchMode::MatchAny, noMatch, match, match));
	static_assert(MatchRules(TriggerMatchMode::MatchAny, noMatch, match, noMatch));
	static_assert(MatchRules(TriggerMatchMode::MatchAny, noMatch, match, unset));

	static_assert(MatchRules(TriggerMatchMode::MatchAny, noMatch, noMatch, match));
	static_assert(!MatchRules(TriggerMatchMode::MatchAny, noMatch, noMatch, noMatch));
	static_assert(!MatchRules(TriggerMatchMode::MatchAny, noMatch, noMatch, unset));

	static_assert(MatchRules(TriggerMatchMode::MatchAny, noMatch, unset, match));
	static_assert(!MatchRules(TriggerMatchMode::MatchAny, noMatch, unset, noMatch));
	static_assert(!MatchRules(TriggerMatchMode::MatchAny, noMatch, unset, unset));

	static_assert(MatchRules(TriggerMatchMode::MatchAny, unset, match, match));
	static_assert(MatchRules(TriggerMatchMode::MatchAny, unset, match, noMatch));
	static_assert(MatchRules(TriggerMatchMode::MatchAny, unset, match, unset));

	static_assert(MatchRules(TriggerMatchMode::MatchAny, unset, noMatch, match));
	static_assert(!MatchRules(TriggerMatchMode::MatchAny, unset, noMatch, noMatch));
	static_assert(!MatchRules(TriggerMatchMode::MatchAny, unset, noMatch, unset));

	static_assert(MatchRules(TriggerMatchMode::MatchAny, unset, unset, match));
	static_assert(!MatchRules(TriggerMatchMode::MatchAny, unset, unset, noMatch));
	static_assert(!MatchRules(TriggerMatchMode::MatchAny, unset, unset, unset));
}

bool ModerationRule::Match(const IPlayer& player, const std::string_view& chatMsg) const
{
	const auto usernameMatch = [&]()
	{
		if (!m_Triggers.m_UsernameTextMatch)
			return MatchResult::Unset;

		const auto name = player.GetNameUnsafe();
		if (name.empty())
			return MatchResult::NoMatch;

		if (!m_Triggers.m_UsernameTextMatch->Match(name))
			return MatchResult::NoMatch;

		return MatchResult::Match;
	};

	const auto personanameMatch = [&]()
	{
		if (!m_Triggers.m_PersonanameTextMatch)
			return MatchResult::Unset;

		const auto& summary = player.GetPlayerSummary();
		if (!summary)
			return MatchResult::NoMatch;

		if (!m_Triggers.m_PersonanameTextMatch->Match(summary->m_Nickname))
			return MatchResult::NoMatch;

		return MatchResult::Match;
	};

	const auto chatMsgMatch = [&]()
	{
		if (!m_Triggers.m_ChatMsgTextMatch)
			return MatchResult::Unset;

		if (chatMsg.empty())
			return MatchResult::NoMatch;

		if (!m_Triggers.m_ChatMsgTextMatch->Match(chatMsg))
			return MatchResult::NoMatch;

		return MatchResult::Match;
	};

	const auto avatarMatch = [&]()
	{
		if (m_Triggers.m_AvatarMatches.empty())
			return MatchResult::Unset;

		const auto& summary = player.GetPlayerSummary();
		if (!summary)
			return MatchResult::NoMatch;

		for (const auto& m : m_Triggers.m_AvatarMatches)
		{
			if (m.Match(summary->m_AvatarHash))
				return MatchResult::Match;
		}

		return MatchResult::NoMatch;
	};


	return MatchRules(m_Triggers.m_Mode, usernameMatch, chatMsgMatch, avatarMatch, personanameMatch);
}

bool AvatarMatch::Match(const std::string_view& avatarHash) const
{
	return m_AvatarHash == avatarHash;
}