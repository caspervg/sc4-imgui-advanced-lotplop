#pragma once
#include <vector>
#include "LotConfigEntry.h"

// Forward declare ImGui types to avoid forcing all includes here
struct ImGuiTableSortSpecs;
struct ImGuiTableColumnSortSpecs;

// Table-facing helpers for displaying/sorting LotConfigEntry rows without
// mutating the underlying storage. The name mirrors the intention that these
// are specifically for the UI table.
namespace LotConfigTable {
    // Build an index vector [0..N-1] sorted according to ImGui's current table
    // sort specs. When no specs are provided, returns a straight 0..N-1 order.
    // The function is stable across multiple columns, applying lower-priority
    // specs first and higher-priority last using stable_sort.
    std::vector<int> BuildSortedIndex(const std::vector<LotConfigEntry>& entries,
                                      const ImGuiTableSortSpecs* sort_specs);

    // Compare two entries for a specific column used in the UI table.
    // Column indices:
    // 1 = ID, 2 = Name, 3 = Size (X then Z). Column 0 (Icon) is intentionally
    // not sortable.
    bool LessForColumn(const LotConfigEntry& a, const LotConfigEntry& b,
                       int column_index, bool ascending);
}
