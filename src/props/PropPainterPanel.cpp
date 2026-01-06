#include "PropPainterPanel.h"

#include "ui/ImGuiPanelHost.h"

PropPainterPanel::PropPainterPanel(ImGuiPanelHost* host)
    : host_(host) {
}

void PropPainterPanel::OnUpdate() {
    if (host_) {
        host_->UpdateOncePerFrame();
    }
}

void PropPainterPanel::OnRender() {
    if (host_) {
        host_->RenderPropPainterUI();
    }
}

void PropPainterPanel::OnShutdown() {
    delete this;
}

void PropPainterPanel::OnUnregister() {
    delete this;
}
