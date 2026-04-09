#pragma once

#include "HardcoverPlugin.h"
#include "activities/Activity.h"

class HardcoverSyncActivity final : public Activity {
 public:
  explicit HardcoverSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("HardcoverSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
  void render(RenderLock&&) override;

 private:
  enum State { SYNCING, DONE };
  State state = SYNCING;
  HardcoverPlugin::SyncResult result = HardcoverPlugin::SyncResult::OK;
  void doSync();
};
