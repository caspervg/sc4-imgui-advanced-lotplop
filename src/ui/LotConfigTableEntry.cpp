#include "LotConfigTableEntry.h"
#include "../vendor/imgui/imgui.h"
#include <cstring>

namespace LotConfigTable {

    bool LessForColumn(const LotConfigEntry& a, const LotConfigEntry& b,
                       int column_index, bool ascending) {
        int dir = ascending ? 1 : -1;
        switch (column_index) {
            case 1: { // ID
                if (a.id != b.id) return ascending ? (a.id < b.id) : (a.id > b.id);
                return false;
            }
            case 2: { // Name (case-insensitive)
                int cmp = _stricmp(a.name.c_str(), b.name.c_str());
                if (cmp != 0) return ascending ? (cmp < 0) : (cmp > 0);
                return false;
            }
            case 3: { // Size: width then depth
                if (a.sizeX != b.sizeX) return ascending ? (a.sizeX < b.sizeX) : (a.sizeX > b.sizeX);
                if (a.sizeZ != b.sizeZ) return ascending ? (a.sizeZ < b.sizeZ) : (a.sizeZ > b.sizeZ);
                return false;
            }
            default:
                return false;
        }
    }

    std::vector<int> BuildSortedIndex(const std::vector<LotConfigEntry>& entries,
                                      const ImGuiTableSortSpecs* sort_specs) {
        const int n = static_cast<int>(entries.size());
        std::vector<int> idx;
        idx.reserve(n);
        for (int i = 0; i < n; ++i) idx.push_back(i);

        if (!sort_specs || sort_specs->SpecsCount == 0) {
            return idx; // no sorting, identity order
        }

        // Copy specs so we can iterate from lowest to highest priority
        std::vector<ImGuiTableColumnSortSpecs> specs(sort_specs->Specs, sort_specs->Specs + sort_specs->SpecsCount);
        for (int si = static_cast<int>(specs.size()) - 1; si >= 0; --si) {
            const ImGuiTableColumnSortSpecs& s = specs[si];
            const bool ascending = (s.SortDirection == ImGuiSortDirection_Ascending);
            auto less = [&](int a, int b) {
                return LessForColumn(entries[static_cast<size_t>(a)], entries[static_cast<size_t>(b)],
                                     s.ColumnIndex, ascending);
            };
            std::stable_sort(idx.begin(), idx.end(), less);
        }

        return idx;
    }
}
