// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SIGNIN_USER_MANAGER_UI_H_
#define CHROME_BROWSER_UI_WEBUI_SIGNIN_USER_MANAGER_UI_H_

#include "content/public/browser/web_ui_controller.h"

class UserManagerScreenHandler;

namespace base {
class DictionaryValue;
}
namespace content {
class WebUIDataSource;
}

// A WebUI dialog to display available users.
class UserManagerUI : public content::WebUIController {
 public:
  explicit UserManagerUI(content::WebUI* web_ui);
  ~UserManagerUI() override;

 private:
  content::WebUIDataSource* CreateUIDataSource(
      const base::DictionaryValue& localized_strings);
  void GetLocalizedStrings(base::DictionaryValue* localized_strings);

  UserManagerScreenHandler* user_manager_screen_handler_;

  DISALLOW_COPY_AND_ASSIGN(UserManagerUI);
};

#endif  // CHROME_BROWSER_UI_WEBUI_SIGNIN_USER_MANAGER_UI_H_
