// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "athena/activity/public/activity_manager.h"

#include <vector>

#include "base/macros.h"

namespace athena {

class ActivityManagerImpl : public ActivityManager {
 public:
  ActivityManagerImpl();
  virtual ~ActivityManagerImpl();

  int num_activities() const { return activities_.size(); }

  // ActivityManager:
  virtual void AddActivity(Activity* activity) OVERRIDE;
  virtual void RemoveActivity(Activity* activity) OVERRIDE;
  virtual void UpdateActivity(Activity* activity) OVERRIDE;

 private:
  std::vector<Activity*> activities_;

  DISALLOW_COPY_AND_ASSIGN(ActivityManagerImpl);
};

}  // namespace athena
