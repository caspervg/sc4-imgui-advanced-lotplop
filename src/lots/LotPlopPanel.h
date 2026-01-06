#pragma once

#include "public/ImGuiPanel.h"

class ImGuiPanelHost;

class LotPlopPanel final : public ImGuiPanel {
public:
    explicit LotPlopPanel(ImGuiPanelHost* host);
    void OnUpdate() override;
    void OnRender() override;
    void OnShutdown() override;
    void OnUnregister() override;

private:
    ImGuiPanelHost* host_;
};
