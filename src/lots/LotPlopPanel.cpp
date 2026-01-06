#include "LotPlopPanel.h"

#include "ui/ImGuiPanelHost.h"

LotPlopPanel::LotPlopPanel(ImGuiPanelHost* host)
    : host_(host) {
}

void LotPlopPanel::OnUpdate() {
    if (host_) {
        host_->UpdateOncePerFrame();
    }
}

void LotPlopPanel::OnRender() {
    if (host_) {
        host_->RenderLotPlopUI();
    }
}

void LotPlopPanel::OnShutdown() {
    delete this;
}

void LotPlopPanel::OnUnregister() {
    delete this;
}
