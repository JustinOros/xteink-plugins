#pragma once

#include "GitHubSyncPlugin.h"
#include "activities/Activity.h"

class GitHubSyncActivity final : public Activity {
 public:
  explicit GitHubSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("GitHubSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
  void render(RenderLock&&) override;

 private:
  enum State { SYNCING, DONE };
  State state = SYNCING;
  GitHubSyncPlugin::SyncResult result = GitHubSyncPlugin::SyncResult::OK;
  void doSync();
};
