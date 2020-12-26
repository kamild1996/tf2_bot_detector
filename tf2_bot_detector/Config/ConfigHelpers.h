#pragma once
#include "Log.h"
#include "Settings.h"

#include <mh/coroutine/task.hpp>
#include <mh/coroutine/thread.hpp>
#include <mh/future.hpp>
#include <mh/text/format.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cassert>
#include <filesystem>
#include <future>
#include <optional>
#include <vector>

namespace tf2_bot_detector
{
	enum class ConfigFileType
	{
		User,
		Official,
		ThirdParty,

		COUNT,
	};

	struct ConfigFilePaths
	{
		std::filesystem::path m_User;
		std::filesystem::path m_Official;
		std::vector<std::filesystem::path> m_Others;
	};
	ConfigFilePaths GetConfigFilePaths(const std::string_view& basename);

	struct ConfigSchemaInfo
	{
		explicit ConfigSchemaInfo(std::nullptr_t) {}
		ConfigSchemaInfo(const std::string_view& schema);
		ConfigSchemaInfo(std::string type, unsigned version, std::string branch = "master");

		std::string m_Branch;
		std::string m_Type;
		unsigned m_Version{};
	};

	void to_json(nlohmann::json& j, const ConfigSchemaInfo& d);
	void from_json(const nlohmann::json& j, ConfigSchemaInfo& d);
	template<typename CharT, typename Traits>
	std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, const ConfigSchemaInfo& info)
	{
		return os << "https://raw.githubusercontent.com/PazerOP/tf2_bot_detector/"
			<< info.m_Branch << "/schemas/v"
			<< info.m_Version << '/'
			<< info.m_Type << ".schema.json";
	}

	struct ConfigFileInfo
	{
		std::vector<std::string> m_Authors;
		std::string m_Title;
		std::string m_Description;
		std::string m_UpdateURL;
	};

	void to_json(nlohmann::json& j, const ConfigFileInfo& d);
	void from_json(const nlohmann::json& j, ConfigFileInfo& d);

	class ConfigFileBase
	{
	public:
		virtual ~ConfigFileBase() = default;

		bool LoadFile(const std::filesystem::path& filename, const HTTPClient* client = nullptr);
		bool SaveFile(const std::filesystem::path& filename) const;

		virtual void ValidateSchema(const ConfigSchemaInfo& schema) const = 0 {}
		virtual void Deserialize(const nlohmann::json& json) = 0 {}
		virtual void Serialize(nlohmann::json& json) const = 0;

		std::optional<ConfigSchemaInfo> m_Schema;
		std::string m_FileName; // Name of the file this was loaded from

	private:
		bool LoadFileInternal(const std::filesystem::path& filename, const HTTPClient* client);
	};

	class SharedConfigFileBase : public ConfigFileBase
	{
	public:
		void Deserialize(const nlohmann::json& json) override = 0;
		void Serialize(nlohmann::json& json) const override = 0;

		const std::string& GetName() const;
		ConfigFileInfo GetFileInfo() const;

	private:
		friend class ConfigFileBase;

		std::optional<ConfigFileInfo> m_FileInfo;
	};

	template<typename T, typename = std::enable_if_t<std::is_base_of_v<ConfigFileBase, T>>>
	T LoadConfigFile(const std::filesystem::path& filename, bool allowAutoupdate, const Settings& settings)
	{
		const HTTPClient* client = allowAutoupdate ? settings.GetHTTPClient() : nullptr;
		if (allowAutoupdate && !client)
			Log("Disallowing auto-update of {} because internet connectivity is disabled or unset in settings", filename);

		// Not going to be doing any async loading
		if (T file; file.LoadFile(filename, client))
			return file;

		return T{};
	}

	template<typename T, typename = std::enable_if_t<std::is_base_of_v<ConfigFileBase, T>>>
	mh::task<T> LoadConfigFileAsync(std::filesystem::path filename, bool allowAutoupdate, const Settings& settings)
	{
		if constexpr (std::is_base_of_v<SharedConfigFileBase, T>)
		{
			if (allowAutoupdate)
			{
				co_await mh::co_create_thread();
				co_return LoadConfigFile<T>(filename, true, settings);
			}
		}

		assert(!allowAutoupdate);
		co_return LoadConfigFile<T>(filename, allowAutoupdate, settings);
	}

	template<typename T, typename TOthers = typename T::collection_type>
	class ConfigFileGroupBase
	{
	public:
		using collection_type = TOthers;

		ConfigFileGroupBase(const Settings& settings) : m_Settings(&settings) {}
		virtual ~ConfigFileGroupBase() = default;

		virtual void CombineEntries(collection_type& collection, const T& file) const = 0;
		virtual std::string GetBaseFileName() const = 0;

		void LoadFiles()
		{
			using namespace std::string_literals;

			const auto paths = GetConfigFilePaths(GetBaseFileName());

			if (!IsOfficial() && !paths.m_User.empty())
				m_UserList = LoadConfigFile<T>(paths.m_User, false, *m_Settings);

			if (!paths.m_Official.empty())
				m_OfficialList = LoadConfigFileAsync<T>(paths.m_Official, !IsOfficial(), *m_Settings);
			else
				m_OfficialList = mh::make_ready_task<T>();

			m_ThirdPartyLists = LoadThirdPartyListsAsync(paths);
		}

		void SaveFiles() const
		{
			const T* defaultMutableList = GetDefaultMutableList();
			const T* localList = GetLocalList();
			if (localList)
				localList->SaveFile(mh::format("cfg/{}.json", GetBaseFileName()));

			if (defaultMutableList && defaultMutableList != localList)
			{
				const std::filesystem::path filename = mh::format("cfg/{}.official.json", GetBaseFileName());

				if (!IsOfficial())
					throw std::runtime_error(mh::format("Attempted to save non-official data to {}", filename));

				defaultMutableList->SaveFile(filename);
			}
		}

		bool IsOfficial() const { return m_Settings->GetLocalSteamID().IsPazer(); }

		T& GetDefaultMutableList()
		{
			if (IsOfficial())
				return const_cast<T&>(m_OfficialList.get()); // forgive me for i have sinned

			return GetLocalList();
		}
		const T* GetDefaultMutableList() const
		{
			if (IsOfficial())
			{
				if (auto list = m_OfficialList.try_get())
					return list;
			}

			return GetLocalList();
		}

		T& GetLocalList()
		{
			if (!m_UserList)
				m_UserList.emplace();

			return *m_UserList;
		}
		const T* GetLocalList() const
		{
			if (!m_UserList)
				return nullptr;

			return &*m_UserList;
		}

		size_t size() const
		{
			size_t retVal = 0;

			if (auto list = m_OfficialList.try_get())
				retVal += list->size();
			if (m_UserList)
				retVal += m_UserList->size();
			if (auto list = m_ThirdPartyLists.try_get())
				retVal += list->size();

			return retVal;
		}

		const Settings* m_Settings = nullptr;
		mh::task<T> m_OfficialList;
		std::optional<T> m_UserList;
		mh::task<collection_type> m_ThirdPartyLists;

	private:
		mh::task<collection_type> LoadThirdPartyListsAsync(ConfigFilePaths paths)
		{
			co_await mh::co_create_thread();

			collection_type collection;

			for (const auto& file : paths.m_Others)
			{
				try
				{
					auto parsedFile = LoadConfigFile<T>(file, true, *m_Settings);
					CombineEntries(collection, parsedFile);
				}
				catch (const std::exception& e)
				{
					LogException(MH_SOURCE_LOCATION_CURRENT(), e, "Exception when loading {}", file);
				}
			}

			co_return collection;
		}
	};
}
