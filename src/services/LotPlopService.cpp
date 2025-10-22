/*
 * This file is part of sc4-imgui-advanced-lotplop, a DLL Plugin for
 * SimCity 4 that offers some extra terrain utilities.
 *
 * Copyright (C) 2025 Casper Van Gheluwe
 *
 * sc4-imgui-advanced-lotplop is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * sc4-imgui-advanced-lotplop is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with sc4-imgui-advanced-lotplop.
 * If not, see <http://www.gnu.org/licenses/>.
 */
#include "LotPlopService.h"
#include "../utils/Logger.h"
#include "cISC4View3DWin.h"
#include "cIGZCommandServer.h"
#include "cIGZCommandParameterSet.h"
#include "cIGZVariant.h"
#include "cRZBaseVariant.h"
#include "GZServPtrs.h"

LotPlopService::LotPlopService(cISC4View3DWin* view3D)
    : pView3D(view3D) {
}

void LotPlopService::TriggerLotPlop(uint32_t lotID) {
    if (!pView3D) {
        LOG_WARN("Cannot trigger lot plop: View3D is null");
        return;
    }

    cIGZCommandServerPtr pCmdServer;
    if (!pCmdServer) {
        LOG_ERROR("Cannot trigger lot plop: Command server not available");
        return;
    }

    cIGZCommandParameterSet* pCmd1 = nullptr;
    cIGZCommandParameterSet* pCmd2 = nullptr;

    if (!pCmdServer->CreateCommandParameterSet(&pCmd1) || !pCmd1 ||
        !pCmdServer->CreateCommandParameterSet(&pCmd2) || !pCmd2) {
        if (pCmd1) pCmd1->Release();
        if (pCmd2) pCmd2->Release();
        LOG_ERROR("Cannot trigger lot plop: Failed to create command parameter sets");
        return;
    }

    // Create a fake variant in pCmd1, this will get clobbered in AppendParameter anyway
    cRZBaseVariant dummyVariant;
    dummyVariant.SetValUint32(0);
    pCmd1->AppendParameter(dummyVariant);

    // Get the game's internal variant and patch it directly
    cIGZVariant* storedParam = pCmd1->GetParameter(0);
    if (storedParam) {
        uint32_t* ptr = reinterpret_cast<uint32_t*>(storedParam);
        uint16_t* shortPtr = reinterpret_cast<uint16_t*>(storedParam);

        ptr[1] = lotID;       // Data at offset +0x04
        shortPtr[7] = 0x0006; // Type=6 (Uint32) at offset +0x0E
    }

    pView3D->ProcessCommand(0xec3e82f8, *pCmd1, *pCmd2);

    pCmd1->Release();
    pCmd2->Release();

    LOG_INFO("Triggered lot plop for lot ID: 0x{:08X}", lotID);
}
