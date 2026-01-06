#pragma once

#include "public/ImGuiPanel.h"

class ImGuiPanelHost;

class PropPainterPanel final : public ImGuiPanel {
public:
    explicit PropPainterPanel(ImGuiPanelHost* host);
    void OnUpdate() override;
    void OnRender() override;
    void OnShutdown() override;
    void OnUnregister() override;

private:
    ImGuiPanelHost* host_;
};
