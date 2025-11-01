#include "AdvancedLotPlopUI.h"

#include <algorithm>
#include <unordered_set>

#include "LotConfigEntry.h"
#include "LotConfigTableEntry.h"
#include "../utils/Config.h"
#include "../vendor/imgui/imgui.h"
#include "utils/Logger.h"

AdvancedLotPlopUI::AdvancedLotPlopUI()
{
	searchBuffer[0] = '\0';
	LoadPersistedState();
}

void AdvancedLotPlopUI::SetCallbacks(const AdvancedLotPlopUICallbacks& cb)
{
	callbacks = cb;
}

void AdvancedLotPlopUI::SetCity(cISC4City* city)
{
	pCity = city;
}

void AdvancedLotPlopUI::SetLotEntries(const std::vector<LotConfigEntry>* entries)
{
	lotEntries = entries;
	// Rebuild MRU list with fresh copies from new entries (drop stale ones)
	if (lotEntries)
	{
		std::vector<LotConfigEntry> refreshed;
		refreshed.reserve(mruOrdered.size());
		for (const auto& old : mruOrdered)
		{
			auto it = std::find_if(lotEntries->begin(), lotEntries->end(), [&old](const LotConfigEntry& e){ return e.id == old.id; });
			if (it != lotEntries->end()) refreshed.push_back(*it);
		}
		mruOrdered.swap(refreshed);
	}
	else
	{
		mruOrdered.clear();
	}
}

bool* AdvancedLotPlopUI::GetShowWindowPtr()
{
	return &showWindow;
}

uint32_t AdvancedLotPlopUI::GetSelectedLotIID() const { return selectedLotIID; }

void AdvancedLotPlopUI::SetSelectedLotIID(uint32_t iid)
{
	selectedLotIID = iid;
	SavePersistedState();
}

uint8_t AdvancedLotPlopUI::GetFilterZoneType() const { return filterZoneType; }
uint8_t AdvancedLotPlopUI::GetFilterWealthType() const { return filterWealthType; }
uint32_t AdvancedLotPlopUI::GetMinSizeX() const { return minSizeX; }
uint32_t AdvancedLotPlopUI::GetMaxSizeX() const { return maxSizeX; }
uint32_t AdvancedLotPlopUI::GetMinSizeZ() const { return minSizeZ; }
uint32_t AdvancedLotPlopUI::GetMaxSizeZ() const { return maxSizeZ; }
const char* AdvancedLotPlopUI::GetSearchBuffer() const { return searchBuffer; }

void AdvancedLotPlopUI::SetFilters(uint8_t zone, uint8_t wealth, uint32_t minX, uint32_t maxX, uint32_t minZ,
                                   uint32_t maxZ, const char* search)
{
	filterZoneType = zone;
	filterWealthType = wealth;
	minSizeX = minX;
	maxSizeX = maxX;
	minSizeZ = minZ;
	maxSizeZ = maxZ;
	if (search)
	{
		strncpy_s(searchBuffer, search, sizeof(searchBuffer) - 1);
		searchBuffer[sizeof(searchBuffer) - 1] = '\0';
	}
	MarkListDirty();
	SavePersistedState();
}

void AdvancedLotPlopUI::ShowLoadingWindow(bool show)
{
	showLoadingWindow = show;
}

void AdvancedLotPlopUI::SetLoadingProgress(const char* stage, int current, int total)
{
	if (stage)
	{
		strncpy_s(loadingStage, stage, sizeof(loadingStage) - 1);
		loadingStage[sizeof(loadingStage) - 1] = '\0';
	}
	loadingCurrent = current;
	loadingTotal = total;
}

void AdvancedLotPlopUI::RenderLoadingWindow()
{
	if (!showLoadingWindow) return;

	ImGui::SetNextWindowSize(ImVec2(400, 150), ImGuiCond_Always);
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
	if (ImGui::Begin("Building lot cache", nullptr, flags))
	{
		ImGui::TextWrapped("Building lot cache, please wait...");
		ImGui::Spacing();

		if (loadingTotal > 0)
		{
			float progress = (float)loadingCurrent / (float)loadingTotal;
			ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
			ImGui::Text("%s (%d / %d)", loadingStage, loadingCurrent, loadingTotal);
		}
		else
		{
			ImGui::TextWrapped("%s", loadingStage);
		}
	}
	ImGui::End();
}

void AdvancedLotPlopUI::Render()
{
	// Always render loading window if needed
	RenderLoadingWindow();
	if (!showWindow) return;

	ImGui::SetNextWindowSize(ImVec2(700, 600), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Advanced LotPlop", &showWindow))
	{
		if (ImGui::BeginTabBar("LotViewTabs", ImGuiTabBarFlags_Reorderable))
		{
			if (ImGui::BeginTabItem("All"))
			{
				currentViewMode = LotViewMode::All;
				RenderFilters();
				ImGui::Separator();
				RenderLotList();
				ImGui::Separator();
				RenderDetails();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Recent"))
			{
				currentViewMode = LotViewMode::Recent;
				RenderRecentLotList();
				ImGui::Separator();
				RenderDetails();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

void AdvancedLotPlopUI::RenderFilters()
{
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));

	if (ImGui::BeginTable("FilterTable", 4, ImGuiTableFlags_SizingStretchSame))
	{
		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		const char* zoneTypes[] = {
			"Any zone", "Residential (R)", "Commercial (C)", "Industrial (I)", "Agriculture", "Plopped", "None", "Other"
		};
		int currentZone = (filterZoneType == 0xFF) ? 0 : (filterZoneType + 1);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::Combo("Zone", &currentZone, zoneTypes, IM_ARRAYSIZE(zoneTypes)))
		{
			filterZoneType = (currentZone == 0) ? 0xFF : static_cast<uint8_t>(currentZone - 1);
			MarkListDirty();
			SavePersistedState();
			if (callbacks.OnRefreshList) callbacks.OnRefreshList();
		}

		ImGui::TableSetColumnIndex(1);
		const char* wealthTypes[] = {"Any wealth", "Low ($)", "Medium ($$)", "High ($$$)"};
		int currentWealth = (filterWealthType == 0xFF) ? 0 : (filterWealthType + 1);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::Combo("Wealth", &currentWealth, wealthTypes, IM_ARRAYSIZE(wealthTypes)))
		{
			filterWealthType = (currentWealth == 0) ? 0xFF : (currentWealth - 1);
			MarkListDirty();
			SavePersistedState();
			if (callbacks.OnRefreshList) callbacks.OnRefreshList();
		}

		// Favorites-only checkbox inline with Zone/Wealth
		ImGui::TableSetColumnIndex(2);
		if (ImGui::Checkbox("Favorites only", &favoritesOnly))
		{
			MarkListDirty();
			SavePersistedState();
			if (callbacks.OnRefreshList) callbacks.OnRefreshList();
		}

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::SetNextItemWidth(-FLT_MIN);
		bool widthChanged = ImGui::SliderInt("Width", (int*)&minSizeX, 1, 16, "Width: %d",
		                                     ImGuiSliderFlags_AlwaysClamp);

		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-FLT_MIN);
		widthChanged |= ImGui::SliderInt("##MaxWidth", (int*)&maxSizeX, 1, 16, "to %d", ImGuiSliderFlags_AlwaysClamp);

		ImGui::TableSetColumnIndex(2);
		ImGui::SetNextItemWidth(-FLT_MIN);
		bool depthChanged = ImGui::SliderInt("Depth", (int*)&minSizeZ, 1, 16, "Depth: %d",
		                                     ImGuiSliderFlags_AlwaysClamp);

		ImGui::TableSetColumnIndex(3);
		ImGui::SetNextItemWidth(-FLT_MIN);
		depthChanged |= ImGui::SliderInt("##MaxDepth", (int*)&maxSizeZ, 1, 16, "to %d", ImGuiSliderFlags_AlwaysClamp);

		if (widthChanged || depthChanged)
		{
			MarkListDirty();
			SavePersistedState();
			if (callbacks.OnRefreshList) callbacks.OnRefreshList();
		}

		ImGui::EndTable();
	}

	ImGui::PopStyleVar(2);

	if (ImGui::CollapsingHeader("Occupant Groups"))
	{
		RenderOccupantGroupFilter();
	}

	if (ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer)))
	{
		MarkListDirty();
		SavePersistedState();
		if (callbacks.OnRefreshList) callbacks.OnRefreshList();
	}

	if (ImGui::Button("Clear filters"))
	{
		filterZoneType = 0xFF;
		filterWealthType = 0xFF;
		minSizeX = 1;
		maxSizeX = 16;
		minSizeZ = 1;
		maxSizeZ = 16;
		searchBuffer[0] = '\0';
		selectedOccupantGroups.clear();
		favoritesOnly = false; // reset favoritesOnly when clearing filters
		MarkListDirty();
		SavePersistedState();
		if (callbacks.OnRefreshList) callbacks.OnRefreshList();
	}
}

void AdvancedLotPlopUI::RenderOccupantGroupFilter()
{
	const auto& names = Config::GetOccupantGroupNames();

	// Build a set for quick lookup and change tracking
	std::unordered_set<uint32_t> selectedSet(selectedOccupantGroups.begin(), selectedOccupantGroups.end());
	bool anyChanged = false;

	// Helpers: trim and split prefix before ':'
	auto trim = [](const std::string& s) -> std::string
	{
		size_t a = s.find_first_not_of(" \t\r\n");
		size_t b = s.find_last_not_of(" \t\r\n");
		if (a == std::string::npos) return std::string();
		return s.substr(a, b - a + 1);
	};
	auto extract_group = [&](const std::string& full)-> std::pair<std::string, std::string>
	{
		size_t pos = full.find(':');
		if (pos == std::string::npos)
		{
			return {"Other", trim(full)}; // No prefix -> Other
		}
		std::string left = trim(full.substr(0, pos));
		std::string right = trim(full.substr(pos + 1));
		if (left.empty()) left = "Other";
		return {left, right.empty() ? trim(full) : right};
	};

	// Build grouping: GroupLabel -> vector of (id, full_label, short_label)
	struct Item
	{
		uint32_t id;
		std::string full;
		std::string short_name;
	};
	std::unordered_map<std::string, std::vector<Item>> groups;
	groups.reserve(names.size());
	for (const auto& kv : names)
	{
		uint32_t id = kv.first;
		const std::string& display = kv.second;
		auto pr = extract_group(display);
		groups[pr.first].push_back(Item{id, display, pr.second});
	}

	// Prepare group ordering by the minimum OG id inside each group
	struct GroupRow
	{
		std::string label;
		uint32_t min_id;
	};
	std::vector<GroupRow> groupRows;
	groupRows.reserve(groups.size());
	for (auto& gkv : groups)
	{
		auto& vec = gkv.second;
		std::sort(vec.begin(), vec.end(), [](const Item& a, const Item& b) { return a.id < b.id; });
		uint32_t min_id = vec.empty() ? 0xFFFFFFFFu : vec.front().id;
		groupRows.push_back(GroupRow{gkv.first, min_id});
	}
	std::sort(groupRows.begin(), groupRows.end(), [](const GroupRow& a, const GroupRow& b)
	{
		return a.min_id < b.min_id;
	});

	// Render groups collapsed by default
	for (const auto& row : groupRows)
	{
		auto& vec = groups[row.label];
		ImGuiTreeNodeFlags flags = 0; // collapsed by default
		if (ImGui::TreeNodeEx(row.label.c_str(), flags))
		{
			for (const auto& it : vec)
			{
				bool checked = selectedSet.count(it.id) != 0;
				char buf[256];
				const std::string& base = it.short_name.empty() ? it.full : it.short_name;
				snprintf(buf, sizeof(buf), "%s (0x%08X)", base.c_str(), it.id);
				if (ImGui::Checkbox(buf, &checked))
				{
					anyChanged = true;
					if (checked) selectedSet.insert(it.id);
					else selectedSet.erase(it.id);
				}
			}
			ImGui::TreePop();
		}
	}

	if (anyChanged)
	{
		selectedOccupantGroups.assign(selectedSet.begin(), selectedSet.end());
		MarkListDirty();
		SavePersistedState();
		if (callbacks.OnRefreshList) callbacks.OnRefreshList();
	}
	if (ImGui::Button("Clear Group Selection"))
	{
		selectedOccupantGroups.clear();
		MarkListDirty();
		SavePersistedState();
		if (callbacks.OnRefreshList) callbacks.OnRefreshList();
	}
}

void AdvancedLotPlopUI::RenderLotList()
{
	size_t count = lotEntries ? lotEntries->size() : 0;
	ImGui::Text("Lot Configurations (%zu found)", count);

	if (ImGui::BeginTable("LotTable", 5,
	                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
	                      ImGuiTableFlags_Sortable,
	                      ImVec2(0.0f, 48.0f * 8)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Fav", ImGuiTableColumnFlags_WidthFixed, 30);
		ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 56.0f);
		ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 80);
		ImGui::TableSetupColumn(
			"Name",
			ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort |
			ImGuiTableColumnFlags_PreferSortAscending);
		ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 60);
		ImGui::TableHeadersRow();

		// Determine sort order
		static std::vector<int> indices; // indices into lotEntries
		indices.clear();
		if (lotEntries)
		{
			// Build filtered index first honoring favoritesOnly
			std::vector<int> filtered;
			filtered.reserve(lotEntries->size());
			for (size_t i = 0; i < lotEntries->size(); ++i)
			{
				const auto& e = (*lotEntries)[i];
				if (favoritesOnly && !IsFavorite(e.id)) continue;
				filtered.push_back((int)i);
			}
			// Sort the filtered indices using existing helper by creating temp vector of entries
			std::vector<LotConfigEntry> temp;
			temp.reserve(filtered.size());
			for (int idx : filtered) temp.push_back((*lotEntries)[(size_t)idx]);
			std::vector<int> sortOrder = LotConfigTable::BuildSortedIndex(temp, favoritesSet, ImGui::TableGetSortSpecs());
			indices.clear();
			indices.reserve(sortOrder.size());
			for (int si : sortOrder) indices.push_back(filtered[(size_t)si]);
			if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) sort_specs->SpecsDirty = false;
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(indices.size()));
			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					int idx = indices.empty() ? row : indices[(size_t)row];
					const auto& entry = (*lotEntries)[(size_t)idx];
					ImGui::TableNextRow();

					// Ensure unique ImGui ID scope per row to avoid ID collisions when labels repeat
					ImGui::PushID(idx);

					// Fav column
					ImGui::TableSetColumnIndex(0);
					bool fav = IsFavorite(entry.id);
					if (ImGui::SmallButton(fav ? "Y" : "N")) { ToggleFavorite(entry.id); }

					// Icon column
					ImGui::TableSetColumnIndex(1);
					RenderIconForEntry(entry);

					// ID column + selection behavior spanning the row
					ImGui::TableSetColumnIndex(2);
					bool isSelected = (entry.id == selectedLotIID);
					char label[32];
					snprintf(label, sizeof(label), "0x%08X", entry.id);
					if (ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_SpanAllColumns))
					{
						selectedLotIID = entry.id;
						SavePersistedState();
					}
					// Double-click anywhere on the row to immediately plop
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						selectedLotIID = entry.id;
						SavePersistedState();
						if (callbacks.OnPlop) callbacks.OnPlop(entry.id);
					}

					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%s", entry.name.c_str());
					if (!entry.description.empty() && ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", entry.description.c_str());
					}

					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%ux%u", entry.sizeX, entry.sizeZ);

					ImGui::PopID();
				}
			}
		}

		ImGui::EndTable();
	}
}

void AdvancedLotPlopUI::RenderIconForEntry(const LotConfigEntry& entry)
{
	if (!entry.iconSRV || entry.iconType == LotConfigEntry::IconType::None)
	{
		// No icon available - show placeholder
		ImGui::Dummy(ImVec2(44, 44));
		return;
	}

	switch (entry.iconType)
	{
	case LotConfigEntry::IconType::PNG:
		{
			// Lot PNG icons are 176x44 made of four 44x44 states; show the second 44x44 (enabled) [pixels 44..88]
			float u1 = (entry.iconWidth > 0) ? (44.0f / (float)entry.iconWidth) : 0.0f;
			float v1 = 0.0f;
			float u2 = (entry.iconWidth > 0) ? (88.0f / (float)entry.iconWidth) : 0.0f;
			float v2 = (entry.iconHeight > 0) ? (44.0f / (float)entry.iconHeight) : 0.0f;
			ImGui::Image((ImTextureID)entry.iconSRV, ImVec2(44, 44), ImVec2(u1, v1), ImVec2(u2, v2));
		}
		break;

	case LotConfigEntry::IconType::S3D:
		{
			// S3D thumbnail - display full square texture
			// Center it in the 44x44 space if thumbnail is smaller
			float displaySize = 44.0f;
			ImVec2 cursorPos = ImGui::GetCursorPos();

			if (entry.iconWidth < 44) {
				float offset = (44.0f - entry.iconWidth) / 2.0f;
				ImGui::SetCursorPos(ImVec2(cursorPos.x + offset, cursorPos.y + offset));
				displaySize = (float)entry.iconWidth;
			}

			ImGui::Image((ImTextureID)entry.iconSRV, ImVec2(displaySize, displaySize));

			// Reset cursor if we offset it
			if (entry.iconWidth < 44) {
				ImGui::SetCursorPos(ImVec2(cursorPos.x, cursorPos.y + 44.0f));
			}
		}
		break;

	default:
		// Unknown type - show placeholder
		ImGui::Dummy(ImVec2(44, 44));
		break;
	}
}

void AdvancedLotPlopUI::RenderDetails()
{
	if (selectedLotIID == 0)
	{
		ImGui::Text("No lot selected");
		return;
	}

	if (!lotEntries) return;

	auto it = std::find_if(lotEntries->begin(), lotEntries->end(),
	                       [this](const LotConfigEntry& e) { return e.id == selectedLotIID; });

	if (it != lotEntries->end())
	{
		ImGui::Text("Selected Lot: %s", it->name.c_str());
		ImGui::Text("ID: 0x%08X", it->id);
		ImGui::Text("Size: %ux%u", it->sizeX, it->sizeZ);

		if (!it->description.empty())
		{
			ImGui::Separator();
			ImGui::TextWrapped("%s", it->description.c_str());
		}

		ImGui::Spacing();
		if (ImGui::Button("Plop"))
		{
			if (callbacks.OnPlop) callbacks.OnPlop(selectedLotIID);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Activates the game's built-in plop tool.\nClick in the city to place the building.");
		}
	}
}

void AdvancedLotPlopUI::LoadPersistedState()
{
	const auto& st = Config::GetUIState();
	filterZoneType = st.zoneFilter;
	filterWealthType = st.wealthFilter;
	minSizeX = st.minSizeX;
	maxSizeX = st.maxSizeX;
	minSizeZ = st.minSizeZ;
	maxSizeZ = st.maxSizeZ;
	strncpy_s(searchBuffer, st.search.c_str(), sizeof(searchBuffer) - 1);
	searchBuffer[sizeof(searchBuffer) - 1] = '\0';
	selectedOccupantGroups = st.selectedGroups;
	selectedLotIID = st.selectedLotID;
	favoritesOnly = st.favoritesOnly;

	favoritesSet.clear();
	favoritesOrdered.clear();
	for (auto id : st.favorites)
	{
		favoritesSet.insert(id);
		favoritesOrdered.push_back(id);
	}
	// MRU not loaded/persisted anymore; start empty for this session
	mruOrdered.clear();
	MarkListDirty();
}

void AdvancedLotPlopUI::SavePersistedState()
{
	Config::UIState st;
	st.zoneFilter = filterZoneType;
	st.wealthFilter = filterWealthType;
	st.minSizeX = minSizeX;
	st.maxSizeX = maxSizeX;
	st.minSizeZ = minSizeZ;
	st.maxSizeZ = maxSizeZ;
	st.search = searchBuffer;
	st.selectedGroups = selectedOccupantGroups;
	st.selectedLotID = selectedLotIID;
	st.favorites.assign(favoritesOrdered.begin(), favoritesOrdered.end());
	st.favoritesOnly = favoritesOnly;
	Config::SaveUIState(st);
}

void AdvancedLotPlopUI::MarkListDirty() { listDirty = true; }

void AdvancedLotPlopUI::ToggleFavorite(uint32_t lotID)
{
	if (favoritesSet.count(lotID))
	{
		favoritesSet.erase(lotID);
		favoritesOrdered.erase(std::remove(favoritesOrdered.begin(), favoritesOrdered.end(), lotID), favoritesOrdered.end());
	}
	else
	{
		favoritesSet.insert(lotID);
		favoritesOrdered.push_back(lotID);
	}
	SavePersistedState();
}

void AdvancedLotPlopUI::RegisterPlop(uint32_t lotID)
{
	if (!lotEntries) return;
	auto srcIt = std::find_if(lotEntries->begin(), lotEntries->end(), [lotID](const LotConfigEntry& e){ return e.id == lotID; });
	if (srcIt == lotEntries->end())
	{
		LOG_DEBUG("Register plop skipped, lot 0x{:x} not found in entries", lotID);
		return;
	}
	auto existing = std::find_if(mruOrdered.begin(), mruOrdered.end(), [lotID](const LotConfigEntry& e){ return e.id == lotID; });
	if (existing != mruOrdered.end()) mruOrdered.erase(existing);
	mruOrdered.insert(mruOrdered.begin(), *srcIt);
	if (mruOrdered.size() > kMaxMRU) mruOrdered.resize(kMaxMRU);
	LOG_DEBUG("Register plop 0x{:x}", lotID);
}

void AdvancedLotPlopUI::RenderRecentLotList()
{
	ImGui::Text("Recent Plops (%zu)", mruOrdered.size());
	if (ImGui::BeginTable("RecentLotTable", 5,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
		ImVec2(0.0f, 48.0f * 8)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Fav", ImGuiTableColumnFlags_WidthFixed, 30);
		ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 56.0f);
		ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 80);
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 60);
		ImGui::TableHeadersRow();

		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(mruOrdered.size()));
		while (clipper.Step())
		{
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				const LotConfigEntry& entry = mruOrdered[static_cast<size_t>(row)];
				ImGui::TableNextRow();
				ImGui::PushID(entry.id);
				// Fav column
				ImGui::TableSetColumnIndex(0);
				bool fav = IsFavorite(entry.id);
				if (ImGui::SmallButton(fav ? "Y" : "N")) { ToggleFavorite(entry.id); }
				// Icon column
				ImGui::TableSetColumnIndex(1);
				RenderIconForEntry(entry);
				// ID/select column
				ImGui::TableSetColumnIndex(2);
				bool isSelected = (entry.id == selectedLotIID);
				char label[32];
				snprintf(label, sizeof(label), "0x%08X", entry.id);
				if (ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_SpanAllColumns))
				{
					selectedLotIID = entry.id;
					SavePersistedState();
				}
				// Double-click anywhere on the recent row to plop
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					selectedLotIID = entry.id;
					SavePersistedState();
					if (callbacks.OnPlop) callbacks.OnPlop(entry.id);
				}
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%s", entry.name.c_str());
				if (!entry.description.empty() && ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", entry.description.c_str());
				}
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%ux%u", entry.sizeX, entry.sizeZ);
				ImGui::PopID();
			}
		}
		ImGui::EndTable();
	}
}
