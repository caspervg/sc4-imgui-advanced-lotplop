#pragma once

class ImGuiPanelHost {
public:
    virtual ~ImGuiPanelHost() = default;
    virtual void UpdateOncePerFrame() = 0;
    virtual void RenderLotPlopUI() = 0;
    virtual void RenderPropPainterUI() = 0;
};
